// glb_import_skinning_coherence_test.cpp — does a converted model ANIMATE at the
// size it was baked at?
//
// The bug this pins down: gltf_import baked skinned vertices in their glTF skin
// space while pod_loader::skin_mesh evaluates them in the mesh node's LOCAL
// space, so the two cs differed by whatever wrapper the mesh node carried. The
// bind pose looked perfect — the pose delta is identity there, so nothing moves —
// and the model only tore itself apart once you scrubbed the timeline, because
// each joint's world-space translation was added to vertices measured in a
// smaller unit. pilot.glb rendered at 62–130× its own size; across a 54-GLB
// corpus all 39 animated clips were affected.
//
// The invariant, which needs no reference implementation:
//
//     diag(bbox(skin_mesh(model, f))) ≈ diag(bbox(mesh.positions))    for all f
//
// skin_mesh() returns vertices in the SAME space as mesh.positions, so for a
// rig that merely poses the two bounding boxes must be comparable. A ratio in
// the tens or hundreds means the skin matrices carry a scale the mesh data does
// not — i.e. the vertices and the skeleton disagree about their unit.
//
// Converting through the real pipeline (gltf_import → pod_write → pod_load) is
// deliberate: the base/clip split and the bind rebuild in pod_load's animation
// merge are part of the contract, and a test that skipped them could not have
// caught this.
//
// Runs on real third-party GLBs and SKIPs (loudly, exit 0) when the machine has
// none, matching glb_import_spec_conformance_test.cpp.

#include "tools/gltf_glb.h"
#include "tools/pod_loader.h"
#include "tools/pod_writer.h"
#include "tools/tiny_gltf_v3.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int checks = 0;
int failures = 0;

void check(bool ok, const std::string& what, const std::string& detail = "") {
    ++checks;
    if (ok) {
        std::printf("  ok   %s%s%s\n", what.c_str(),
                    detail.empty() ? "" : "  —  ", detail.c_str());
    } else {
        ++failures;
        std::printf("  FAIL %s%s%s\n", what.c_str(),
                    detail.empty() ? "" : "  —  ", detail.c_str());
    }
}

struct Box {
    float lo[3] = { 1e30f,  1e30f,  1e30f };
    float hi[3] = {-1e30f, -1e30f, -1e30f };
    void add(const float* p) {
        for (int i = 0; i < 3; ++i) {
            lo[i] = std::min(lo[i], p[i]);
            hi[i] = std::max(hi[i], p[i]);
        }
    }
    double diag() const {
        double s = 0;
        for (int i = 0; i < 3; ++i) {
            const double d = (double)hi[i] - (double)lo[i];
            s += d * d;
        }
        return std::sqrt(s);
    }
};

// A ratio this far from 1.0 is a unit mismatch, not an extreme pose. Natural
// sprint, jump, glide, and sprawled death poses extend limbs up to ~1.93x
// rest pose diagonal; unit mismatches produce 10x, 39x, 62–130x.
constexpr double kMaxRatio = 2.05;
constexpr double kMinRatio = 0.45;

// ── Spec reference ──────────────────────────────────────────────────────────
//
// The blocks above are all SELF-consistency checks: they compare the POD to
// itself, so a bundle of stale .POD files baked by an older converter passes
// every one of them and still renders wrong. That is not hypothetical —
// smario/.../soldier-v1 shipped a base model whose baked mesh sat at 56% of the
// size the glTF spec puts it at, 0.39 m low, while its bind pose skinned to
// itself perfectly (0.88 m mean error against every frame of the companion
// walk clip).
//
// So this one brings in an outside authority: evaluate the glTF itself, by the
// spec, and require our POD to land where it lands.
//
//   v_world = Σ_k w_k · (world_rest(joint_k) · IBM_k) · p
//
// where world_rest composes each node's STATIC local transform up its parent
// chain, and IBM is the file's accessor — or the identity the spec falls back
// to when a file ships none. The POD side is the baked mesh pushed through
// get_node_matrix(mesh_node, 0); for a base model with no animation stream that
// is exactly the rest world, so the two numbers are directly comparable and
// must agree in both centre and diagonal.

int ncomp_of(int t) {
    switch (t) {
        case TG3_TYPE_SCALAR: return 1;
        case TG3_TYPE_VEC2:   return 2;
        case TG3_TYPE_VEC3:   return 3;
        case TG3_TYPE_VEC4:   return 4;
        case TG3_TYPE_MAT4:   return 16;
        default: return 1;
    }
}

// Read any numeric accessor into doubles, honouring byteStride and the integer
// component types (JOINTS_0 is commonly ubyte).
bool read_accessor(const tg3_model* m, int idx, std::vector<double>& out, int* comps) {
    *comps = 1;
    if (idx < 0 || idx >= (int)m->accessors_count) return false;
    const tg3_accessor* acc = &m->accessors[idx];
    if (acc->buffer_view < 0 || acc->buffer_view >= (int)m->buffer_views_count) return false;
    const tg3_buffer_view* bv = &m->buffer_views[acc->buffer_view];
    if (bv->buffer < 0 || bv->buffer >= (int)m->buffers_count) return false;
    const tg3_buffer* buf = &m->buffers[bv->buffer];
    if (!buf->data.data) return false;
    const int nc = ncomp_of(acc->type);
    *comps = nc;
    size_t csize = 4;
    switch (acc->component_type) {
        case TG3_COMPONENT_TYPE_BYTE:
        case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:  csize = 1; break;
        case TG3_COMPONENT_TYPE_SHORT:
        case TG3_COMPONENT_TYPE_UNSIGNED_SHORT: csize = 2; break;
        case TG3_COMPONENT_TYPE_FLOAT:          csize = 4; break;
        default: return false;
    }
    const size_t stride = bv->byte_stride ? bv->byte_stride : csize * (size_t)nc;
    const size_t base = bv->byte_offset + acc->byte_offset;
    out.assign((size_t)acc->count * nc, 0.0);
    for (uint64_t e = 0; e < acc->count; ++e) {
        const uint8_t* p = buf->data.data + base + e * stride;
        if (p + csize * (size_t)nc > buf->data.data + buf->data.count) return false;
        for (int c = 0; c < nc; ++c) {
            const uint8_t* q = p + (size_t)c * csize;
            double v = 0;
            switch (acc->component_type) {
                case TG3_COMPONENT_TYPE_FLOAT: { float f; std::memcpy(&f, q, 4); v = f; break; }
                case TG3_COMPONENT_TYPE_BYTE: { int8_t i; std::memcpy(&i, q, 1); v = i; break; }
                case TG3_COMPONENT_TYPE_UNSIGNED_BYTE: { uint8_t i; std::memcpy(&i, q, 1); v = i; break; }
                case TG3_COMPONENT_TYPE_SHORT: { int16_t i; std::memcpy(&i, q, 2); v = i; break; }
                case TG3_COMPONENT_TYPE_UNSIGNED_SHORT: { uint16_t i; std::memcpy(&i, q, 2); v = i; break; }
                default: v = 0; break;
            }
            out[(size_t)e * nc + c] = v;
        }
    }
    return true;
}

void mul16(const double a[16], const double b[16], double o[16]) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            o[c * 4 + r] = a[r] * b[c * 4 + 0] + a[4 + r] * b[c * 4 + 1]
                         + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
}

void local_of(const tg3_node& n, double out[16]) {
    static const double I[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    if (n.has_matrix) { std::memcpy(out, n.matrix, sizeof(double) * 16); return; }
    const double x = n.rotation[0], y = n.rotation[1], z = n.rotation[2], w = n.rotation[3];
    double R[16] = {1 - 2*(y*y + z*z), 2*(x*y + z*w),     2*(x*z - y*w),     0,
                    2*(x*y - z*w),     1 - 2*(x*x + z*z), 2*(y*z + x*w),     0,
                    2*(x*z + y*w),     2*(y*z - x*w),     1 - 2*(x*x + y*y), 0,
                    0, 0, 0, 1};
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) R[c * 4 + r] *= n.scale[c];
    R[12] = n.translation[0];
    R[13] = n.translation[1];
    R[14] = n.translation[2];
    std::memcpy(out, R, sizeof(R));
    (void)I;
}

// Spec bind bounding box over every skinned mesh node in the file. Returns
// false when there is nothing skinned to compare (props, foliage, weapons).
bool spec_bind_box(const fs::path& glb, double centre[3], double& diag,
                   std::string& note) {
    std::ifstream f(glb, std::ios::binary | std::ios::ate);
    if (!f.is_open()) { note = "cannot read" ; return false; }
    const std::streamsize size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> file((size_t)std::max<std::streamsize>(size, 0));
    if (size > 0) f.read(reinterpret_cast<char*>(file.data()), size);

    tg3_model model;
    tg3_error_stack errors;
    std::memset(&model, 0, sizeof(model));
    tg3_error_stack_init(&errors);
    tg3_parse_options opts;
    tg3_parse_options_init(&opts);
    opts.strictness = TG3_PERMISSIVE;
    opts.images_as_is = 1;

    std::string base_dir = glb.parent_path().string();
    if (tg3_parse_auto(&model, &errors, file.data(), file.size(),
                       base_dir.c_str(), (uint32_t)base_dir.size(), &opts) != TG3_OK) {
        tg3_error_stack_free(&errors);
        tg3_model_free(&model);
        note = "glb parse failed";
        return false;
    }

    const uint32_t nn = model.nodes_count;
    std::vector<int> parent(nn, -1);
    for (uint32_t i = 0; i < nn; ++i)
        for (uint32_t c = 0; c < model.nodes[i].children_count; ++c)
            parent[model.nodes[i].children[c]] = (int)i;

    std::vector<double> local((size_t)nn * 16);
    for (uint32_t i = 0; i < nn; ++i) local_of(model.nodes[i], &local[(size_t)i * 16]);

    std::vector<double> world((size_t)nn * 16, 0.0);
    std::vector<char>   done(nn, 0);
    std::function<void(int)> rest_world = [&](int i) {
        if (i < 0 || i >= (int)nn) return;
        if (done[i]) return;
        done[i] = 1;
        if (parent[i] >= 0 && parent[i] != i) {
            rest_world(parent[i]);
            mul16(&world[(size_t)parent[i] * 16], &local[(size_t)i * 16], &world[(size_t)i * 16]);
        } else {
            std::memcpy(&world[(size_t)i * 16], &local[(size_t)i * 16], sizeof(double) * 16);
        }
    };
    for (uint32_t i = 0; i < nn; ++i) rest_world((int)i);

    static const double kIdent[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    double lo[3] = {1e30, 1e30, 1e30};
    double hi[3] = {-1e30, -1e30, -1e30};
    int counted = 0;
    bool continued_quantised = false;

    for (uint32_t si = 0; si < model.skins_count; ++si) {
        const tg3_skin& skin = model.skins[si];
        if (skin.joints_count == 0) continue;
        std::vector<double> ibm;
        int ic = 16;
        const bool has_ibm = read_accessor(&model, skin.inverse_bind_matrices, ibm, &ic) &&
                             (int)(ibm.size() / 16) >= (int)skin.joints_count;
        for (uint32_t ni = 0; ni < nn; ++ni) {
            const tg3_node& nd = model.nodes[ni];
            if (nd.skin != (int)si) continue;
            if (nd.mesh < 0 || nd.mesh >= (int)model.meshes_count) continue;
            const tg3_mesh& mesh = model.meshes[nd.mesh];
            for (uint32_t pi = 0; pi < mesh.primitives_count; ++pi) {
                const tg3_primitive& prim = mesh.primitives[pi];
                int pos_acc = -1, jnt_acc = -1, wgt_acc = -1;
                auto attr = [&](const char* want) -> int {
                    const size_t want_len = std::strlen(want);
                    if (!prim.attributes) return -1;
                    for (uint32_t ai = 0; ai < prim.attributes_count; ++ai) {
                        const tg3_str& nm = prim.attributes[ai].key;
                        if (nm.len == want_len && nm.data &&
                            std::strncmp(nm.data, want, want_len) == 0)
                            return prim.attributes[ai].value;
                    }
                    return -1;
                };
                pos_acc = attr("POSITION");
                jnt_acc = attr("JOINTS_0");
                wgt_acc = attr("WEIGHTS_0");
                if (pos_acc < 0) continue;
                // Quantised geometry (KHR_mesh_quantization, e.g. pilot.glb's
                // SNORM-short POSITION) needs the extension's dequantisation to
                // be read at all. Comparing it against this evaluator would
                // measure OUR reader, not the importer, so skip rather than
                // report a false alarm.
                if (model.accessors[pos_acc].component_type != TG3_COMPONENT_TYPE_FLOAT) {
                    note = "quantised POSITION - spec check skipped";
                    continued_quantised = true;
                    continue;
                }
                std::vector<double> pos, jnt, wgt;
                int c1 = 0, c2 = 0, c3 = 0;
                if (!read_accessor(&model, pos_acc, pos, &c1) || c1 != 3) continue;
                if (jnt_acc < 0 || wgt_acc < 0) continue;
                if (!read_accessor(&model, jnt_acc, jnt, &c2)) continue;
                if (!read_accessor(&model, wgt_acc, wgt, &c3)) continue;

                const int nverts = (int)pos.size() / 3;
                for (int v = 0; v < nverts; ++v) {
                    double p[3] = {pos[v * 3], pos[v * 3 + 1], pos[v * 3 + 2]};
                    double acc[3] = {0, 0, 0};
                    double wsum = 0;
                    for (int k = 0; k < 4; ++k) {
                        const size_t ii = (size_t)v * 4 + k;
                        if (wgt.size() <= ii || jnt.size() <= ii) break;
                        const double wk = wgt[ii];
                        if (wk <= 0.0) continue;
                        const int slot = (int)std::lround(jnt[ii]);
                        if (slot < 0 || slot >= (int)skin.joints_count) continue;
                        const int joint_node = skin.joints[slot];
                        if (joint_node < 0 || joint_node >= (int)nn) continue;
                        const double* W = &world[(size_t)joint_node * 16];
                        const double* B = has_ibm ? &ibm[(size_t)slot * 16] : kIdent;
                        double M[16];
                        mul16(W, B, M);
                        acc[0] += wk * (M[0]*p[0] + M[4]*p[1] + M[8]*p[2]  + M[12]);
                        acc[1] += wk * (M[1]*p[0] + M[5]*p[1] + M[9]*p[2]  + M[13]);
                        acc[2] += wk * (M[2]*p[0] + M[6]*p[1] + M[10]*p[2] + M[14]);
                        wsum += wk;
                    }
                    if (wsum <= 0.0) continue;
                    for (int c = 0; c < 3; ++c) {
                        lo[c] = std::min(lo[c], acc[c]);
                        hi[c] = std::max(hi[c], acc[c]);
                    }
                    ++counted;
                }
            }
        }
    }

    tg3_error_stack_free(&errors);
    tg3_model_free(&model);

    if (counted == 0) {
        if (!continued_quantised) note = "no skinned geometry";
        return false;
    }
    double s = 0;
    for (int c = 0; c < 3; ++c) {
        centre[c] = 0.5 * (lo[c] + hi[c]);
        const double d = hi[c] - lo[c];
        s += d * d;
    }
    diag = std::sqrt(s);
    note = std::to_string(counted) + " verts";
    return true;
}

// The same box, but from the POD: every skinned mesh node's baked vertices
// pushed through get_node_matrix(node, 0).
bool pod_bind_box(const av::PODModel& m, double centre[3], double& diag) {
    double lo[3] = {1e30, 1e30, 1e30};
    double hi[3] = {-1e30, -1e30, -1e30};
    bool any = false;
    for (size_t ni = 0; ni < m.nodes.size(); ++ni) {
        if (m.nodes[ni].object_index < 0 ||
            m.nodes[ni].object_index >= (int)m.meshes.size()) continue;
        const av::PODMesh& mesh = m.meshes[m.nodes[ni].object_index];
        if (mesh.bones_per_vertex <= 0 || mesh.positions.empty()) continue;
        float w[16];
        av::get_node_matrix(m, (int)ni, 0.0f, w);
        for (size_t v = 0; v + 2 < mesh.positions.size(); v += 3) {
            const double x = mesh.positions[v], y = mesh.positions[v + 1], z = mesh.positions[v + 2];
            const double p[3] = {w[0]*x + w[4]*y + w[8]*z  + w[12],
                                 w[1]*x + w[5]*y + w[9]*z  + w[13],
                                 w[2]*x + w[6]*y + w[10]*z + w[14]};
            for (int c = 0; c < 3; ++c) {
                lo[c] = std::min(lo[c], p[c]);
                hi[c] = std::max(hi[c], p[c]);
            }
            any = true;
        }
    }
    if (!any) return false;
    double s = 0;
    for (int c = 0; c < 3; ++c) {
        centre[c] = 0.5 * (lo[c] + hi[c]);
        const double d = hi[c] - lo[c];
        s += d * d;
    }
    diag = std::sqrt(s);
    return true;
}

// Where real animated GLBs tend to live on a dev box. Absent ones are skipped.
std::vector<fs::path> candidate_assets() {
    std::vector<fs::path> out;
    if (const char* env = std::getenv("SWORDIGO_GLB_ASSETS")) {
        if (fs::exists(env)) out.emplace_back(env);
    }
    const char* kFixed[] = {
        "smario/smashroyale.io/assets/pilot.glb",
        // Float-authored, ships inverseBindMatrices, and its mesh node hangs
        // off an Armature scaled x0.01 — the exact shape that made the spec
        // bind box the sharpest check in this file.
        "smario/smashroyale.io/assets/soldier-v1/soldier.glb",
        ".local/share/swordigo-desktop/assets/custom_scenes/minecraft_bee.glb",
        ".local/share/swordigo-desktop/assets/custom_scenes/statue.glb",
    };
    const char* home = std::getenv("HOME");
    for (const char* rel : kFixed) {
        if (!home) break;
        fs::path p = fs::path(home) / rel;
        if (fs::exists(p)) out.push_back(p);
    }
    return out;
}

// Convert one GLB the way `bin/ruby --glb2pod` does, into `dir`, and return the
// base POD path. Clip PODs land next to it as <stem>_<clip>.POD, which is the
// layout pod_load's animation-only-merge detection looks for.
bool convert_asset(const fs::path& glb, const fs::path& dir, std::string& base_pod,
                   std::vector<std::string>& clip_pods, std::string& err) {
    const std::string stem = glb.stem().string();
    base_pod = (dir / (stem + ".POD")).string();

    av::PODModel base;
    std::vector<av::GLTFImageBuffer> images;
    av::GLTFPBRInfo pbr;
    // rigid_skin = the CLI default (--smooth-skin opts out), and the game's own
    // skinning reads one bone per vertex, so the rigid bake is what ships.
    if (!av::gltf_import_glb(glb.string(), base, images, &err, &pbr, 1.0f, true))
        return false;
    if (!av::pod_write(base, base_pod, &err)) return false;

    std::vector<std::pair<std::string, av::PODModel>> clips;
    if (!av::gltf_import_all_clips(glb.string(), clips, &err, 1.0f, 0.0f, true))
        return false;

    for (const auto& kv : clips) {
        const std::string path = (dir / (stem + "_" + kv.first + ".POD")).string();
        std::string werr;
        if (!av::pod_write(kv.second, path, &werr)) {
            err = werr;
            return false;
        }
        clip_pods.push_back(path);
    }
    return true;
}

// One converted asset: the base's bind consistency plus every clip's coherence.
void check_asset(const fs::path& glb, const fs::path& dir) {
    std::printf("\n%s\n", glb.string().c_str());

    std::string base_pod, err;
    std::vector<std::string> clip_pods;
    if (!convert_asset(glb, dir, base_pod, clip_pods, err)) {
        check(false, "convert", err);
        return;
    }

    // 0. The base model's baked mesh must land where the glTF spec puts its
    //    bind pose. This is the OUTSIDE reference — everything else in this
    //    file only compares the POD to itself.
    {
        double spec_c[3] = {0, 0, 0}, pod_c[3] = {0, 0, 0};
        double spec_d = 0, pod_d = 0;
        std::string note;
        if (spec_bind_box(glb, spec_c, spec_d, note) && spec_d > 1e-9) {
            av::PODModel base = av::pod_load(base_pod);
            if (pod_bind_box(base, pod_c, pod_d)) {
                const double ratio = pod_d / spec_d;
                const double off = std::sqrt((pod_c[0]-spec_c[0])*(pod_c[0]-spec_c[0]) +
                                             (pod_c[1]-spec_c[1])*(pod_c[1]-spec_c[1]) +
                                             (pod_c[2]-spec_c[2])*(pod_c[2]-spec_c[2]));
                char buf[256];
                std::snprintf(buf, sizeof(buf),
                              "spec diag %.4f at (%.3f, %.3f, %.3f) vs POD diag %.4f at "
                              "(%.3f, %.3f, %.3f) — ratio %.4f, centre off %.4f (%.2f%%)",
                              spec_d, spec_c[0], spec_c[1], spec_c[2],
                              pod_d, pod_c[0], pod_c[1], pod_c[2],
                              ratio, off, 100.0 * off / spec_d);
                // The two boxes describe the same geometry, so agreement is
                // near-exact (soldier.glb: ratio 1.0000, offset 0.0000). The
                // stale-file failure this pins down measured 0.562 and 38%.
                check(ratio >= 0.90 && ratio <= 1.10 && off <= 0.10 * spec_d,
                      "base mesh lands where the glTF spec binds it", buf);
            } else {
                std::printf("       (no skinned mesh in the POD - bind box skipped)\n");
            }
        } else {
            std::printf("       (spec bind box unavailable: %s)\n", note.c_str());
        }
    }

    // 1. The base model's bind pose must skin to itself. All skin matrices are
    //    identity at rest, so ANY disagreement here means the mesh and the
    //    skeleton are in different spaces.
    {
        av::PODModel m = av::pod_load(base_pod);
        int mesh_node = -1;
        for (size_t i = 0; i < m.nodes.size(); ++i)
            if (m.nodes[i].object_index >= 0) { mesh_node = (int)i; break; }
        if (mesh_node < 0) { check(false, "base has a mesh node"); return; }

        const av::PODMesh& mesh = m.meshes[m.nodes[mesh_node].object_index];
        Box baked;
        for (int i = 0; i < mesh.num_vertices; ++i)
            baked.add(&mesh.positions[(size_t)i * 3]);

        // Not skinned at all (props, foliage, weapons): skin_mesh() declines by
        // design and there is no vertex space to get wrong.
        std::vector<float> sp, sn;
        if (!av::skin_mesh(m, mesh_node, 0.0f, sp, sn)) {
            std::printf("       (not a skinned mesh - nothing to check)\n");
            return;
        }

        double worst = 0;
        for (int i = 0; i < mesh.num_vertices; ++i) {
            const float* a = &sp[(size_t)i * 3];
            const float* b = &mesh.positions[(size_t)i * 3];
            const double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
            worst = std::max(worst, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        char buf[128];
        std::snprintf(buf, sizeof(buf), "max deviation %.4f of a %.1f diagonal",
                      worst, baked.diag());
        check(worst <= 1e-3 * std::max(1.0, baked.diag()),
              "bind pose skins to itself", buf);
    }

    if (clip_pods.empty()) {
        std::printf("       (no clips in this asset)\n");
        return;
    }

    // 2. Every clip: the animated mesh must stay the size it was baked at.
    for (const std::string& clip : clip_pods) {
        av::PODModel m = av::pod_load(clip);   // merges the base automatically
        int mesh_node = -1;
        for (size_t i = 0; i < m.nodes.size(); ++i)
            if (m.nodes[i].object_index >= 0) { mesh_node = (int)i; break; }
        if (mesh_node < 0) { check(false, "clip has a mesh node"); continue; }

        const av::PODMesh& mesh = m.meshes[m.nodes[mesh_node].object_index];
        Box baked;
        for (int i = 0; i < mesh.num_vertices; ++i)
            baked.add(&mesh.positions[(size_t)i * 3]);
        const double bd = baked.diag();
        if (bd <= 1e-9) continue;

        const int nf = std::max(m.num_frames, 1);
        std::vector<int> frames = {0, nf / 3, (2 * nf) / 3, nf - 1};
        std::sort(frames.begin(), frames.end());
        frames.erase(std::unique(frames.begin(), frames.end()), frames.end());

        double lo = 1e30, hi = 0;
        bool skinned = false;
        for (int f : frames) {
            std::vector<float> sp, sn;
            if (!av::skin_mesh(m, mesh_node, (float)f, sp, sn)) continue;
            skinned = true;
            Box b;
            for (size_t i = 0; i + 2 < sp.size(); i += 3) b.add(&sp[i]);
            const double r = b.diag() / bd;
            lo = std::min(lo, r);
            hi = std::max(hi, r);
        }
        if (!skinned) continue;

        char buf[192];
        std::snprintf(buf, sizeof(buf), "%d frames, baked diag %.1f, ratio %.3f..%.3f",
                      m.num_frames, bd, lo, hi);
        check(lo >= kMinRatio && hi <= kMaxRatio,
              fs::path(clip).filename().string() + " keeps its baked size", buf);
    }
}

} // namespace

int main() {
    std::printf("glb_import_skinning_coherence_test — GLB -> POD skinning must keep "
                "the mesh's unit\n\n");

    const std::vector<fs::path> assets = candidate_assets();
    if (assets.empty()) {
        std::printf("SKIP: no real GLB assets found. Set SWORDIGO_GLB_ASSETS=<dir or file>\n"
                    "      to point at a directory of third-party .glb models.\n");
        return 0;
    }

    fs::path tmp = fs::temp_directory_path() / "swordigo_skinning_coherence";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);

    for (const fs::path& a : assets) {
        if (fs::is_directory(a)) {
            std::vector<fs::path> found;
            for (const auto& e : fs::recursive_directory_iterator(a, ec)) {
                if (!e.is_regular_file()) continue;
                std::string ext = e.path().extension().string();
                for (char& c : ext) c = (char)std::tolower((unsigned char)c);
                if (ext == ".glb" || ext == ".gltf") found.push_back(e.path());
            }
            std::sort(found.begin(), found.end());
            for (const fs::path& f : found) check_asset(f, tmp);
        } else if (fs::exists(a)) {
            check_asset(a, tmp);
        }
    }

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
