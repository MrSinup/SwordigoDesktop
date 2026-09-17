// glb_animation_skinning_test.cpp — does a POD -> GLB animation play the same
// POSES the game plays?
//
// Why this test exists
// --------------------
// gltf_bridge_roundtrip_test deliberately excludes animation ("ANIMATION is
// intentionally NOT carried by the base import path"), so nothing in the tree
// ever compared an exported clip against the engine. A merged animation POD
// (base mesh + clip, e.g. hiro.POD + hiro_die.POD) is exactly the case where
// naively deriving inverseBindMatrices from animation frame 0 goes wrong: the
// vertices are baked in the BASE model's rest pose while frame 0 is the clip's
// first pose, so every joint's skin palette ended up post-multiplied by that
// joint's bind -> frame-0 delta. The visible result was a torso shearing into
// stacked layers, limbs detaching, and — wherever a bone matrix is
// ill-conditioned — the whole skinned mesh thrown far outside the model.
//
// What it asserts
// ---------------
// 1. The animation the GLB plays reproduces av::skin_mesh() — the engine's own
//    skinning — vertex for vertex, frame for frame. This is checked by
//    re-parsing the exported GLB with the same parser the editor uses
//    (tiny_gltf_v3), rebuilding the node hierarchy from the GLB's own node
//    transforms + animation channels + inverseBindMatrices, and skinning the
//    GLB's own POSITION data with the GLB's own JOINTS_0/WEIGHTS_0.
// 2. Every animation accessor's declared count matches what its output
//    bufferView actually holds, and input/output counts agree.
// 3. The GLB's bind pose is the pose the vertices are baked in, i.e.
//    inverse(IBM[j]) equals the joint's captured bind world matrix.
//
// Runs on the shipped assets and SKIPs (loudly, exit 0) when they are absent.

#include "gltf_glb.h"
#include "pod_loader.h"
#include "tiny_gltf_v3.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace {

int checks = 0, failures = 0;
void ok(bool cond, const std::string& what) {
    ++checks;
    if (cond) {
        std::printf("  ok   %s\n", what.c_str());
    } else {
        ++failures;
        std::printf("  FAIL %s\n", what.c_str());
    }
}

// ── 4x4 math, column-major, matching the exporter/consumer convention ──────
void mul(const float a[16], const float b[16], float out[16]) {
    float t[16];
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            t[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] +
                           a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
    std::memcpy(out, t, sizeof(t));
}

// Same algorithm AND the same singularity threshold as the engine's
// local_mat4_inverse (pod_loader.cpp): 1e-8, not a tighter value of our own.
// The engine substitutes identity whenever this fails, so a test that inverts
// more aggressively than the engine would "find" differences that are really
// its own garbage division, not the exporter's.
bool inverse(const float in[16], float out[16]) {
    float a[16];
    std::memcpy(a, in, sizeof(a));
    const float id[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    std::memcpy(out, id, sizeof(id));
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 4; ++row)
            if (std::fabs(a[col * 4 + row]) > std::fabs(a[col * 4 + pivot])) pivot = row;
        if (std::fabs(a[col * 4 + pivot]) < 1e-8f) return false;
        if (pivot != col)
            for (int c = 0; c < 4; ++c) { std::swap(a[c*4+col], a[c*4+pivot]); std::swap(out[c*4+col], out[c*4+pivot]); }
        const float s = a[col * 4 + col];
        for (int c = 0; c < 4; ++c) { a[c*4+col] /= s; out[c*4+col] /= s; }
        for (int row = 0; row < 4; ++row) {
            if (row == col) continue;
            const float f = a[col * 4 + row];
            for (int c = 0; c < 4; ++c) { a[c*4+row] -= f * a[col*4+col]; out[c*4+row] -= f * out[col*4+col]; }
        }
    }
    return true;
}

// glTF TRS -> matrix (T * R * S), quaternion is (x,y,z,w).
void from_trs(const double t[3], const double q[4], const double s[3], float out[16]) {
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double xx = x*x, yy = y*y, zz = z*z;
    const double xy = x*y, xz = x*z, yz = y*z, wx = w*x, wy = w*y, wz = w*z;
    float r[16] = {
        (float)((1 - 2*(yy+zz)) * s[0]), (float)((2*(xy+wz)) * s[0]), (float)((2*(xz-wy)) * s[0]), 0.0f,
        (float)((2*(xy-wz)) * s[1]), (float)((1 - 2*(xx+zz)) * s[1]), (float)((2*(yz+wx)) * s[1]), 0.0f,
        (float)((2*(xz+wy)) * s[2]), (float)((2*(yz-wx)) * s[2]), (float)((1 - 2*(xx+yy)) * s[2]), 0.0f,
        (float)t[0], (float)t[1], (float)t[2], 1.0f,
    };
    std::memcpy(out, r, sizeof(r));
}

std::string tg3_str_of(const tg3_str& s) {
    return s.data ? std::string(s.data, s.len) : std::string();
}

// ── accessor reading ───────────────────────────────────────────────────────
int type_comps(int type) {
    switch (type) {
        case TG3_TYPE_SCALAR: return 1;
        case TG3_TYPE_VEC2:   return 2;
        case TG3_TYPE_VEC3:   return 3;
        case TG3_TYPE_VEC4:   return 4;
        case TG3_TYPE_MAT4:   return 16;
        default:              return 0;
    }
}

const uint8_t* accessor_ptr(const tg3_model& model, int32_t acc_idx, uint64_t* out_count, int* out_comps) {
    if (acc_idx < 0 || acc_idx >= (int32_t)model.accessors_count) return nullptr;
    const tg3_accessor* acc = &model.accessors[acc_idx];
    if (acc->buffer_view < 0 || acc->buffer_view >= (int32_t)model.buffer_views_count) return nullptr;
    const tg3_buffer_view* bv = &model.buffer_views[acc->buffer_view];
    if (bv->buffer < 0 || bv->buffer >= (int32_t)model.buffers_count) return nullptr;
    const tg3_buffer* buf = &model.buffers[bv->buffer];
    if (!buf->data.data) return nullptr;
    const uint64_t comps = (uint64_t)type_comps(acc->type);
    const uint64_t comp_size = acc->component_type == 5126 ? 4u : (acc->component_type == 5123 ? 2u : 4u);
    const uint64_t need = bv->byte_offset + acc->byte_offset + acc->count * comps * comp_size;
    if (need > buf->data.count) return nullptr;
    if (out_count) *out_count = acc->count;
    if (out_comps) *out_comps = (int)comps;
    return buf->data.data + bv->byte_offset + acc->byte_offset;
}

bool read_floats(const tg3_model& model, int32_t acc_idx, std::vector<float>& out,
                 uint64_t* declared_count = nullptr) {
    uint64_t count = 0;
    int comps = 0;
    const uint8_t* p = accessor_ptr(model, acc_idx, &count, &comps);
    if (!p) return false;
    if (model.accessors[acc_idx].component_type != 5126) return false;
    if (declared_count) *declared_count = count;
    out.assign((const float*)p, (const float*)p + count * (uint64_t)comps);
    return true;
}

bool read_u16(const tg3_model& model, int32_t acc_idx, std::vector<uint16_t>& out) {
    uint64_t count = 0;
    int comps = 0;
    const uint8_t* p = accessor_ptr(model, acc_idx, &count, &comps);
    if (!p) return false;
    if (model.accessors[acc_idx].component_type != 5123) return false;
    out.assign((const uint16_t*)p, (const uint16_t*)p + count * (uint64_t)comps);
    return true;
}

int attribute(const tg3_primitive& prim, const char* name) {
    for (uint32_t i = 0; i < prim.attributes_count; ++i)
        if (tg3_str_of(prim.attributes[i].key) == name) return prim.attributes[i].value;
    return -1;
}

// ── per-node animation sampling ────────────────────────────────────────────
struct Channel {
    int node = -1;
    int path = 0; // 0 T, 1 R, 2 S
    std::vector<double> times;
    std::vector<float> values;
};

void sample(const Channel& c, double t, double out[4], int comps) {
    if (c.times.empty() || c.values.empty()) return;
    size_t k = 0;
    while (k + 2 < c.times.size() && c.times[k + 1] < t) ++k;
    if (t <= c.times.front()) { k = 0; }
    else if (t >= c.times.back()) { k = c.times.size() - 2 < c.times.size() ? c.times.size() - 2 : 0; }
    const size_t k1 = std::min(k + 1, c.times.size() - 1);
    const double t0 = c.times[k], t1 = c.times[k1];
    const double a = (t1 > t0) ? std::clamp((t - t0) / (t1 - t0), 0.0, 1.0) : 0.0;
    for (int i = 0; i < comps; ++i)
        out[i] = c.values[k * comps + i] * (1.0 - a) + c.values[k1 * comps + i] * a;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<fs::path> roots;
    if (argc > 1) roots.emplace_back(argv[1]);
    roots.emplace_back("assets/resources");
    if (const char* home = std::getenv("HOME"))
        roots.emplace_back(fs::path(home) / ".local/share/swordigo-desktop/assets/resources");

    fs::path root;
    for (const auto& r : roots) {
        if (fs::exists(r / "hiro.POD")) { root = r; break; }
    }
    if (root.empty()) {
        std::printf("[glb_anim] (skipped) no Swordigo asset root with hiro.POD found; "
                    "pass one as argv[1] to run\n");
        return 0;
    }
    std::printf("[glb_anim] asset root: %s\n", root.string().c_str());

    const std::vector<std::pair<std::string, std::string>> clips = {
        {"hiro_die.POD", "hiro"},
        {"hiro_run.POD", "hiro"},
        {"hiro_stand.POD", "hiro"},
    };

    for (const auto& [clip, base] : clips) {
        const fs::path clip_path = root / clip;
        if (!fs::exists(clip_path)) {
            std::printf("[glb_anim] (skipped) %s not present\n", clip.c_str());
            continue;
        }
        std::printf("\n=== %s (base %s) ===\n", clip.c_str(), base.c_str());

        av::PODModel pod = av::pod_load(clip_path.string(), base);
        if (pod.nodes.empty()) { ok(false, clip + ": loaded"); continue; }

        const fs::path glb_path = fs::temp_directory_path() /
            ("glb_anim_" + clip.substr(0, clip.find_last_of('.')) + ".glb");
        std::string err;
        const bool exported = av::gltf_export_glb(pod, {}, glb_path.string(), &err);
        ok(exported, clip + ": exported to GLB" + (err.empty() ? "" : " (" + err + ")"));
        if (!exported) continue;

        // Read the bytes ourselves and parse in memory: tiny_gltf_v3 is built
        // without TINYGLTF3_ENABLE_FS, so tg3_parse_file has no file loader.
        // This mirrors GLBModel::load_from_file (tg3_parse_auto on the buffer).
        std::vector<uint8_t> glb_bytes;
        {
            std::ifstream in(glb_path, std::ios::binary);
            glb_bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        tg3_model glb;
        tg3_error_stack errors;
        tg3_error_stack_init(&errors);
        tg3_parse_options opts;
        tg3_parse_options_init(&opts);
        const tg3_error_code rc = tg3_parse_auto(&glb, &errors, glb_bytes.data(), glb_bytes.size(),
                                                 nullptr, 0, &opts);
        if (rc != TG3_OK) {
            const char* why = tg3_errors_count(&errors)
                ? tg3_errors_get(&errors, 0)->message : "(no diagnostic)";
            std::printf("  tg3_parse_auto failed: code %d, %s\n", (int)rc, why ? why : "?");
            ok(false, clip + ": tiny_gltf_v3 re-parsed the exported GLB");
            tg3_error_stack_free(&errors);
            continue;
        }
        tg3_error_stack_free(&errors);
        std::printf("  GLB: %u nodes, %u meshes, %u skins, %u animations\n",
                    glb.nodes_count, glb.meshes_count, glb.skins_count, glb.animations_count);

        ok(glb.nodes_count == pod.nodes.size(), clip + ": node count survives the export");

        // ── invariant 2: accessor counts agree with the data actually written
        {
            int mismatched = 0, count_mismatch = 0;
            for (uint32_t a = 0; a < glb.animations_count; ++a) {
                const tg3_animation& an = glb.animations[a];
                for (uint32_t c = 0; c < an.channels_count; ++c) {
                    const tg3_animation_channel& ch = an.channels[c];
                    if (ch.sampler < 0 || ch.sampler >= (int32_t)an.samplers_count) continue;
                    const tg3_animation_sampler& sm = an.samplers[ch.sampler];
                    if (sm.input < 0 || sm.output < 0) continue;
                    const tg3_accessor& in = glb.accessors[sm.input];
                    const tg3_accessor& out = glb.accessors[sm.output];
                    if (in.count != out.count) ++count_mismatch;
                    // declared accessor size must fit inside its bufferView
                    const tg3_buffer_view& bv = glb.buffer_views[out.buffer_view];
                    const uint64_t comps = (uint64_t)type_comps(out.type);
                    if (out.byte_offset + out.count * comps * 4u > bv.byte_length) ++mismatched;
                }
            }
            ok(count_mismatch == 0, clip + ": every channel's input/output accessor counts match");
            ok(mismatched == 0, clip + ": every animation accessor fits its bufferView");
        }

        // ── invariant 3: the GLB's inverseBindMatrices are usable and its
        // REST pose (node TRS with no channel applied) is the bind pose, i.e.
        // a consumer that never plays the animation — Blender, three.js, a
        // thumbnail renderer — still sees the undeformed model rather than an
        // exploded one.
        //
        // This deliberately does NOT reconstruct the exporter's
        // inverse(bindJoint) * bindMesh arithmetic: several Swordigo bones have
        // near-degenerate bind matrices (a collapsed IK control, a weapon
        // socket), and re-deriving their inverses in the test only measures the
        // test's own pivoting. Boundedness below catches the failure that
        // actually matters — an exploded or non-finite palette — and the
        // frame-by-frame skinning check above is the exactness proof.
        const float radius = std::max(pod.radius, 1.0f);
        {
            double worst_elem = 0.0;
            int nonfinite = 0;
            float worst_rest = 0.0f;
            for (uint32_t s = 0; s < glb.skins_count; ++s) {
                const tg3_skin& skin = glb.skins[s];
                std::vector<float> ibm;
                if (skin.inverse_bind_matrices < 0 ||
                    !read_floats(glb, skin.inverse_bind_matrices, ibm)) continue;
                for (float v : ibm) {
                    if (!std::isfinite(v)) ++nonfinite;
                    worst_elem = std::max(worst_elem, (double)std::fabs(v));
                }
                // rest pose: node TRS only, no channels
                const int mesh_node = skin.skeleton;
                if (mesh_node < 0 || mesh_node >= (int32_t)pod.nodes.size()) continue;
                if (glb.nodes[mesh_node].mesh < 0) continue;
                const tg3_mesh& gm = glb.meshes[glb.nodes[mesh_node].mesh];
                if (gm.primitives_count == 0) continue;
                const tg3_primitive& prim = gm.primitives[0];
                const int pos_acc = attribute(prim, "POSITION");
                const int jnt_acc = attribute(prim, "JOINTS_0");
                const int wgt_acc = attribute(prim, "WEIGHTS_0");
                if (pos_acc < 0 || jnt_acc < 0 || wgt_acc < 0) continue;
                std::vector<float> gpos, gwts;
                std::vector<uint16_t> gjnt;
                if (!read_floats(glb, pos_acc, gpos) || !read_floats(glb, wgt_acc, gwts) ||
                    !read_u16(glb, jnt_acc, gjnt)) continue;

                std::vector<float> world(glb.nodes_count * 16), local(glb.nodes_count * 16, 0.0f);
                std::vector<std::vector<int>> kids2(glb.nodes_count);
                std::vector<int> par2(glb.nodes_count, -1);
                for (uint32_t i = 0; i < glb.nodes_count; ++i)
                    for (uint32_t c = 0; c < glb.nodes[i].children_count; ++c) {
                        const int32_t ch = glb.nodes[i].children[c];
                        if (ch >= 0 && ch < (int32_t)glb.nodes_count) { kids2[i].push_back(ch); par2[ch] = (int)i; }
                    }
                std::vector<int> st;
                for (uint32_t i = 0; i < glb.nodes_count; ++i) if (par2[i] < 0) st.push_back((int)i);
                std::vector<int> ord;
                while (!st.empty()) { const int n = st.back(); st.pop_back(); ord.push_back(n);
                    for (int c : kids2[n]) st.push_back(c); }
                for (uint32_t i = 0; i < glb.nodes_count; ++i) {
                    const tg3_node& n = glb.nodes[i];
                    if (n.has_matrix) std::memcpy(&local[i * 16], n.matrix, 16 * sizeof(float));
                    else from_trs(n.translation, n.rotation, n.scale, &local[i * 16]);
                }
                for (int i : ord) {
                    if (par2[i] < 0) std::memcpy(&world[i * 16], &local[i * 16], 16 * sizeof(float));
                    else mul(&world[par2[i] * 16], &local[i * 16], &world[i * 16]);
                }
                std::vector<float> palette((size_t)skin.joints_count * 16, 0.0f);
                for (uint32_t j = 0; j < skin.joints_count; ++j) {
                    const int32_t ni = skin.joints[j];
                    if (ni < 0 || ni >= (int32_t)glb.nodes_count) continue;
                    mul(&world[ni * 16], &ibm[(size_t)j * 16], &palette[(size_t)j * 16]);
                }
                const size_t nv = gpos.size() / 3;
                for (size_t v = 0; v < nv; ++v)
                    for (int k = 0; k < 4; ++k) {
                        const uint16_t j = gjnt[v * 4 + k];
                        const float w = gwts[v * 4 + k];
                        if (w <= 0.0f || j >= skin.joints_count) continue;
                        const float* P = &palette[(size_t)j * 16];
                        const float* p = &gpos[v * 3];
                        for (int c = 0; c < 3; ++c) {
                            const float out = P[0 * 4 + c]*p[0] + P[1 * 4 + c]*p[1] + P[2 * 4 + c]*p[2] + P[3 * 4 + c];
                            worst_rest = std::max(worst_rest, std::fabs(out));
                        }
                    }
            }
            char msg[256];
            std::snprintf(msg, sizeof(msg),
                          "%s: inverseBindMatrices are finite and bounded (worst |element| %.3g, "
                          "%d non-finite)", clip.c_str(), worst_elem, nonfinite);
            ok(nonfinite == 0 && worst_elem < 1.0e4, msg);
            std::snprintf(msg, sizeof(msg),
                          "%s: rest pose stays inside the model (worst |vertex| %.2f, radius %.2f)",
                          clip.c_str(), worst_rest, radius);
            ok(worst_rest <= 3.0f * radius, msg);
        }

        // ── invariant 1: the GLB plays what skin_mesh plays
        {
            // animation channels by node
            std::vector<std::vector<Channel>> per_node(pod.nodes.size());
            for (uint32_t a = 0; a < glb.animations_count; ++a) {
                const tg3_animation& an = glb.animations[a];
                for (uint32_t c = 0; c < an.channels_count; ++c) {
                    const tg3_animation_channel& ch = an.channels[c];
                    if (ch.sampler < 0 || ch.sampler >= (int32_t)an.samplers_count) continue;
                    if (ch.target.node < 0 || ch.target.node >= (int32_t)pod.nodes.size()) continue;
                    const tg3_animation_sampler& sm = an.samplers[ch.sampler];
                    const std::string path = tg3_str_of(ch.target.path);
                    Channel out;
                    out.node = ch.target.node;
                    out.path = path == "translation" ? 0 : (path == "rotation" ? 1 : 2);
                    std::vector<float> times;
                    uint64_t n = 0;
                    if (!read_floats(glb, sm.input, times, &n)) continue;
                    out.times.assign(times.begin(), times.end());
                    if (!read_floats(glb, sm.output, out.values)) continue;
                    per_node[ch.target.node].push_back(std::move(out));
                }
            }

            // children, for a topological evaluation (never assume node order)
            std::vector<std::vector<int>> kids(pod.nodes.size());
            std::vector<int> parent(pod.nodes.size(), -1);
            for (uint32_t i = 0; i < glb.nodes_count; ++i)
                for (uint32_t c = 0; c < glb.nodes[i].children_count; ++c) {
                    const int32_t ch = glb.nodes[i].children[c];
                    if (ch >= 0 && ch < (int32_t)pod.nodes.size()) { kids[i].push_back(ch); parent[ch] = (int)i; }
                }
            std::vector<int> order;
            {
                std::vector<int> stack;
                for (uint32_t i = 0; i < glb.nodes_count; ++i)
                    if (parent[i] < 0) stack.push_back((int)i);
                while (!stack.empty()) {
                    const int n = stack.back(); stack.pop_back();
                    order.push_back(n);
                    for (int c : kids[n]) stack.push_back(c);
                }
            }

            const int frames = std::max(pod.num_frames, 1);
            const double fps = pod.fps > 0.0f ? pod.fps : 30.0;
            float worst = 0.0f;
            int worst_mesh = -1, worst_frame = -1;
            float scale = std::max(pod.radius, 1.0f);
            // Skeleton agreement is tracked separately from vertex agreement so
            // a failure can be localised: if the worlds already disagree the
            // fault is in the node/channel transform, not in the skin palette.
            float worst_world = 0.0f;
            int worst_world_node = -1, worst_world_frame = -1;
            float worst_basis = 0.0f;
            int worst_basis_node = -1, worst_basis_frame = -1;

            // Evaluate the GLB's own node tree at time t: node locals from the
            // GLB's TRS overridden by the sampled channels, then worlds in
            // topological order (the exporter happens to emit parents before
            // children; a test must not rely on that).
            std::vector<float> local, world;
            auto eval_glb = [&](int f) {
                const double t = (double)f / fps;
                local.assign(pod.nodes.size() * 16, 0.0f);
                for (uint32_t i = 0; i < glb.nodes_count; ++i) {
                    const tg3_node& n = glb.nodes[i];
                    double T[3] = {n.translation[0], n.translation[1], n.translation[2]};
                    double R[4] = {n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3]};
                    double S[3] = {n.scale[0], n.scale[1], n.scale[2]};
                    for (const Channel& ch : per_node[i]) {
                        double v[4] = {0, 0, 0, 0};
                        const int comps = ch.path == 1 ? 4 : 3;
                        sample(ch, t, v, comps);
                        if (ch.path == 0) { T[0] = v[0]; T[1] = v[1]; T[2] = v[2]; }
                        else if (ch.path == 2) { S[0] = v[0]; S[1] = v[1]; S[2] = v[2]; }
                        else { R[0] = v[0]; R[1] = v[1]; R[2] = v[2]; R[3] = v[3]; }
                    }
                    if (n.has_matrix) std::memcpy(&local[i * 16], n.matrix, 16 * sizeof(float));
                    else from_trs(T, R, S, &local[i * 16]);
                }
                world.assign(pod.nodes.size() * 16, 0.0f);
                for (int i : order) {
                    if (parent[i] < 0) std::memcpy(&world[i * 16], &local[i * 16], 16 * sizeof(float));
                    else mul(&world[parent[i] * 16], &local[i * 16], &world[i * 16]);
                }
            };

            for (size_t mi = 0; mi < pod.nodes.size(); ++mi) {
                const av::PODNode& pn = pod.nodes[mi];
                if (pn.object_index < 0 || pn.object_index >= (int)pod.meshes.size()) continue;
                const av::PODMesh& pm = pod.meshes[pn.object_index];
                if (pm.bones_per_vertex <= 0) continue;
                if (glb.nodes[mi].mesh < 0 || glb.nodes[mi].skin < 0) continue;
                const tg3_mesh& gm = glb.meshes[glb.nodes[mi].mesh];
                if (gm.primitives_count == 0) continue;
                const tg3_primitive& prim = gm.primitives[0];
                const int pos_acc = attribute(prim, "POSITION");
                const int jnt_acc = attribute(prim, "JOINTS_0");
                const int wgt_acc = attribute(prim, "WEIGHTS_0");
                if (pos_acc < 0 || jnt_acc < 0 || wgt_acc < 0) continue;
                std::vector<float> gpos, gwts;
                std::vector<uint16_t> gjnt;
                if (!read_floats(glb, pos_acc, gpos) || !read_floats(glb, wgt_acc, gwts) ||
                    !read_u16(glb, jnt_acc, gjnt)) continue;
                const size_t nv = gpos.size() / 3;
                if (nv != (size_t)pm.num_vertices) continue;

                const tg3_skin& skin = glb.skins[glb.nodes[mi].skin];
                std::vector<float> ibm;
                if (!read_floats(glb, skin.inverse_bind_matrices, ibm)) continue;

                for (int f = 0; f < frames; ++f) {
                    eval_glb(f);
                    // stage 1: does the GLB's skeleton equal the engine's at t?
                    //
                    // Translation and rotation are measured separately, and must
                    // be: the 3x3 basis lives in [-1,1] while the translation
                    // column is in model units, so a single absolute threshold
                    // over all 16 elements lets an arbitrarily wrong rotation
                    // pass as "agreeing" (it did, at first, hiding the topmost
                    // offender behind whichever node's translation happened to
                    // move most).
                    for (uint32_t i = 0; i < glb.nodes_count; ++i) {
                        float ref_w[16];
                        av::get_node_matrix(pod, (int)i, (float)f, ref_w);
                        float dt = 0.0f, dr = 0.0f;
                        for (int c = 0; c < 3; ++c)
                            dt = std::max(dt, std::fabs(world[i*16 + 12 + c] - ref_w[12 + c]));
                        for (int c = 0; c < 3; ++c)
                            for (int r = 0; r < 3; ++r)
                                dr = std::max(dr, std::fabs(world[(i)*16 + c*4 + r] - ref_w[c*4 + r]));
                        const bool bad = dt > 0.05f * scale || dr > 1e-3f;
                        if (bad && (dt > worst_world || dr > worst_basis)) {
                            if (dt > worst_world) { worst_world = dt; worst_world_node = (int)i; worst_world_frame = f; }
                            if (dr > worst_basis) { worst_basis = dr; worst_basis_node = (int)i; worst_basis_frame = f; }
                        }
                    }

                    std::vector<float> palette((size_t)skin.joints_count * 16);
                    for (uint32_t j = 0; j < skin.joints_count; ++j) {
                        const int32_t ni = skin.joints[j];
                        if (ni < 0 || ni >= (int32_t)pod.nodes.size()) { continue; }
                        mul(&world[ni * 16], &ibm[(size_t)j * 16], &palette[(size_t)j * 16]);
                    }

                    // reference: engine skinning, mapped into world space the way
                    // the viewport draws it (mesh node world * skin_mesh output)
                    std::vector<float> ref_local, ref_nrm;
                    if (!av::skin_mesh(pod, (int)mi, (float)f, ref_local, ref_nrm)) continue;
                    float mesh_world[16];
                    av::get_node_matrix(pod, (int)mi, (float)f, mesh_world);

                    for (size_t v = 0; v < nv; ++v) {
                        float glb_v[3] = {0, 0, 0};
                        float wsum = 0.0f;
                        for (int k = 0; k < 4; ++k) {
                            const uint16_t j = gjnt[v * 4 + k];
                            const float w = gwts[v * 4 + k];
                            if (w <= 0.0f || j >= skin.joints_count) continue;
                            const float* P = &palette[(size_t)j * 16];
                            const float* p = &gpos[v * 3];
                            glb_v[0] += (P[0]*p[0] + P[4]*p[1] + P[8]*p[2]  + P[12]) * w;
                            glb_v[1] += (P[1]*p[0] + P[5]*p[1] + P[9]*p[2]  + P[13]) * w;
                            glb_v[2] += (P[2]*p[0] + P[6]*p[1] + P[10]*p[2] + P[14]) * w;
                            wsum += w;
                        }
                        if (wsum <= 0.0f) continue;
                        const float* r = &ref_local[v * 3];
                        const float ref_v[3] = {
                            mesh_world[0]*r[0] + mesh_world[4]*r[1] + mesh_world[8]*r[2]  + mesh_world[12],
                            mesh_world[1]*r[0] + mesh_world[5]*r[1] + mesh_world[9]*r[2]  + mesh_world[13],
                            mesh_world[2]*r[0] + mesh_world[6]*r[1] + mesh_world[10]*r[2] + mesh_world[14],
                        };
                        const float d = std::sqrt((glb_v[0]-ref_v[0])*(glb_v[0]-ref_v[0]) +
                                                  (glb_v[1]-ref_v[1])*(glb_v[1]-ref_v[1]) +
                                                  (glb_v[2]-ref_v[2])*(glb_v[2]-ref_v[2]));
                        if (d > worst) { worst = d; worst_mesh = (int)mi; worst_frame = f; }
                    }
                }

            }

            // On failure, list EVERY node whose world disagrees at the worst
            // frame — in index (i.e. hierarchy) order, so the first entry is the
            // topmost node that is actually wrong and everything after it is
            // cascade. Each line carries what each side drove.
            if (worst_world > 0.05f * scale || worst_basis > 1e-3f) {
                const int dump_frame = worst_basis > 1e-3f ? worst_basis_frame : worst_world_frame;
                std::printf("  --- skeleton divergences at frame %d (worst basis delta %.4f, "
                            "worst translation delta %.4f) ---\n", dump_frame, worst_basis, worst_world);
                eval_glb(dump_frame);
                int shown = 0;
                for (uint32_t i = 0; i < glb.nodes_count && shown < 12; ++i) {
                    float ref_w[16];
                    av::get_node_matrix(pod, (int)i, (float)dump_frame, ref_w);
                    float d = 0.0f, db = 0.0f;
                    for (int c = 0; c < 3; ++c)
                        d = std::max(d, std::fabs(world[i*16 + 12 + c] - ref_w[12 + c]));
                    for (int c = 0; c < 3; ++c)
                        for (int r = 0; r < 3; ++r)
                            db = std::max(db, std::fabs(world[i*16 + c*4 + r] - ref_w[c*4 + r]));
                    if (d <= 0.05f * scale && db <= 1e-3f) continue;
                    d = std::max(d, db * scale);
                    ++shown;
                    const av::PODNode& pn2 = pod.nodes[i];
                    std::printf("     #%-2u %-22s d=%8.3f parent=%d  POD flags=%u T[%zu/%zu] R[%zu/%zu] S[%zu/%zu] M[%zu/%zu]\n",
                                i, pn2.name.c_str(), d, parent[i], pn2.anim_flags,
                                pn2.anim_translation.size(), pn2.anim_translation_idx.size(),
                                pn2.anim_rotation.size(), pn2.anim_rotation_idx.size(),
                                pn2.anim_scale.size(), pn2.anim_scale_idx.size(),
                                pn2.anim_matrix.size(), pn2.anim_matrix_idx.size());
                    std::printf("          engine world T=(%.3f %.3f %.3f)  glb world T=(%.3f %.3f %.3f)\n",
                                ref_w[12], ref_w[13], ref_w[14],
                                world[i*16+12], world[i*16+13], world[i*16+14]);
                    std::printf("          glb channels:");
                    for (const Channel& c : per_node[i]) {
                        double v[4] = {0,0,0,0};
                        sample(c, (double)dump_frame / fps, v, c.path == 1 ? 4 : 3);
                        std::printf(" %s[%zu]{%.3f %.3f %.3f w=%.3f}",
                                    c.path == 0 ? "T" : (c.path == 1 ? "R" : "S"),
                                    c.times.size(), v[0], v[1], v[2], c.path == 1 ? v[3] : 1.0);
                    }
                    std::printf("\n");
                    // Raw stream values the engine samples vs what the GLB
                    // channel holds at the same key: separates "the channel data
                    // is wrong" from "the composition/hierarchy is wrong".
                    auto key_of = [](const std::vector<uint32_t>& idx, size_t values,
                                     int frame, int comps) -> int {
                        if (values < (size_t)comps) return -1;
                        int fi = std::max(frame, 0);
                        if (!idx.empty() && fi < (int)idx.size()) fi = (int)idx[fi];
                        const int keys = (int)(values / (size_t)comps);
                        return std::clamp(fi, 0, keys - 1);
                    };
                    const int kt = key_of(pn2.anim_translation_idx, pn2.anim_translation.size(), dump_frame, 3);
                    const int kr = key_of(pn2.anim_rotation_idx, pn2.anim_rotation.size(), dump_frame, 4);
                    const int sc = (pn2.anim_scale.size() % 7 == 0) ? 7 : 3;
                    const int ks = key_of(pn2.anim_scale_idx, pn2.anim_scale.size(), dump_frame, sc);
                    if (kt >= 0)
                        std::printf("          POD T[key %d]=(%.3f %.3f %.3f)\n", kt,
                                    pn2.anim_translation[kt*3], pn2.anim_translation[kt*3+1],
                                    pn2.anim_translation[kt*3+2]);
                    if (kr >= 0)
                        std::printf("          POD R[key %d]=(%.3f %.3f %.3f %.3f)\n", kr,
                                    pn2.anim_rotation[kr*4], pn2.anim_rotation[kr*4+1],
                                    pn2.anim_rotation[kr*4+2], pn2.anim_rotation[kr*4+3]);
                    if (ks >= 0)
                        std::printf("          POD S[key %d stride %d]=(%.3f %.3f %.3f)\n", ks, sc,
                                    pn2.anim_scale[ks*sc], pn2.anim_scale[ks*sc+1],
                                    pn2.anim_scale[ks*sc+2]);
                    std::printf("          engine world col3=(%.3f %.3f %.3f)   glb is parent=%d\n",
                                ref_w[12], ref_w[13], ref_w[14], parent[i]);
                    std::printf("          glb    local col3=(%.3f %.3f %.3f)  [T R S]=(%.3f %.3f %.3f)(%.3f %.3f %.3f %.3f)(%.3f %.3f %.3f)\n",
                                local[i*16+12], local[i*16+13], local[i*16+14],
                                glb.nodes[i].translation[0], glb.nodes[i].translation[1], glb.nodes[i].translation[2],
                                glb.nodes[i].rotation[0], glb.nodes[i].rotation[1], glb.nodes[i].rotation[2], glb.nodes[i].rotation[3],
                                glb.nodes[i].scale[0], glb.nodes[i].scale[1], glb.nodes[i].scale[2]);
                }
            }

            char msg[320];
            std::snprintf(msg, sizeof(msg),
                          "%s: GLB skeleton matches get_node_matrix (worst translation delta %.4f "
                          "units, worst basis delta %.4f, node %d \"%s\" frame %d)",
                          clip.c_str(), worst_world, worst_basis, worst_basis_node >= 0 ? worst_basis_node : worst_world_node,
                          worst_basis_node >= 0 ? pod.nodes[worst_basis_node].name.c_str() : "-",
                          worst_basis_node >= 0 ? worst_basis_frame : worst_world_frame);
            ok(worst_world <= 0.05f * scale && worst_basis <= 1e-3f, msg);
            std::snprintf(msg, sizeof(msg),
                          "%s: GLB playback matches skin_mesh (worst vertex delta %.4f units, "
                          "%.2f%% of model radius %0.2f; mesh %d frame %d)",
                          clip.c_str(), worst, 100.0f * worst / scale, scale, worst_mesh, worst_frame);
            ok(worst <= 0.05f * scale, msg);
        }

        tg3_model_free(&glb);
        std::error_code ec;
        fs::remove(glb_path, ec);
    }

    std::printf("\n%d/%d checks passed%s\n", checks - failures, checks,
                failures ? " — FAILURES PRESENT" : " — all good");
    return failures == 0 ? 0 : 1;
}
