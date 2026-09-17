// ============================================================================
// glb_import_spec_conformance_test.cpp — is our glTF *import* path spec-correct?
//
// Why this test exists
// --------------------
// glb_animation_skinning_test.cpp pins the EXPORT direction: a GLB we wrote
// must reproduce av::skin_mesh(), the engine's own skinning. It cannot tell us
// whether av::gltf_import_all_clips() reads a GLB the way the glTF 2.0 spec
// says to — that is the other half of the pipeline, and until now nothing in
// the tree checked it at all. A GLB could be perfect coming out and still be
// imported into the editor as the wrong poses.
//
// The reference here is an evaluator written straight from the spec:
// node local M = T * R * S, world built by walking the parent chain, and
// STEP / LINEAR / CUBICSPLINE sampler evaluation with shortest-path slerp.
// It shares no code with av::.
//
// The invariant compared is
//
//     |world_i(t) - world_j(t)|   for joints i, j at time t
//
// Pairwise joint distance is invariant under any global change of basis, under
// the quaternion conjugation our importer applies (x,y,z -> -x,-y,-z), and under
// handedness flips. So the two implementations can be compared WITHOUT first
// agreeing on a coordinate convention — which is the only way to compare them
// honestly, since POD and glTF disagree about axes by design.
//
// That one invariant covers every way this layer can be wrong:
//   * shear leaked into a rotation basis  -> a non-orthogonal basis changes lengths
//   * local-vs-world space mix-up         -> compounding error drifts the distances
//   * accumulated / double-applied root translation -> distances run away
//   * quaternion sign / long-way slerp    -> sampled BETWEEN keys, so it shows
//   * joint index mismatch                -> two different bones, wrong distances
//
// Runs on real third-party GLBs (assets our own exporter has never touched, so
// a shared bug cannot hide behind itself) and SKIPs loudly, exit 0, when the
// machine has none.
// ============================================================================

#include "gltf_glb.h"
#include "pod_loader.h"
#include "tiny_gltf_v3.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

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

// ── column-major 4x4 (m[col*4 + row]), the convention both sides use ────────
using Mat4 = double[16];
const Mat4 kIdent = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void mat_mul(const Mat4 a, const Mat4 b, Mat4 out) {
    Mat4 t;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            t[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] +
                           a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
    std::memcpy(out, t, sizeof(Mat4));
}

void mat_compose(const double T[3], const double q[4], const double S[3], Mat4 out) {
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double xx = x * x, yy = y * y, zz = z * z;
    const double xy = x * y, xz = x * z, yz = y * z;
    const double wx = w * x, wy = w * y, wz = w * z;
    out[0] = (1 - 2 * (yy + zz)) * S[0];
    out[1] = (2 * (xy + wz)) * S[0];
    out[2] = (2 * (xz - wy)) * S[0];
    out[3] = 0;
    out[4] = (2 * (xy - wz)) * S[1];
    out[5] = (1 - 2 * (xx + zz)) * S[1];
    out[6] = (2 * (yz + wx)) * S[1];
    out[7] = 0;
    out[8] = (2 * (xz + wy)) * S[2];
    out[9] = (2 * (yz - wx)) * S[2];
    out[10] = (1 - 2 * (xx + yy)) * S[2];
    out[11] = 0;
    out[12] = T[0];
    out[13] = T[1];
    out[14] = T[2];
    out[15] = 1;
}

// Shortest-path slerp — the spec's required rotation interpolation for LINEAR.
void quat_slerp(const double a[4], const double b_in[4], double t, double out[4]) {
    double b[4] = {b_in[0], b_in[1], b_in[2], b_in[3]};
    double d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (d < 0.0) {
        d = -d;
        for (int i = 0; i < 4; ++i) b[i] = -b[i];
    }
    if (d > 0.9995) {
        for (int i = 0; i < 4; ++i) out[i] = a[i] + t * (b[i] - a[i]);
    } else {
        const double th0 = std::acos(std::max(-1.0, std::min(1.0, d)));
        const double th = th0 * t;
        const double s0 = std::sin(th0 - th) / std::sin(th0);
        const double s1 = std::sin(th) / std::sin(th0);
        for (int i = 0; i < 4; ++i) out[i] = a[i] * s0 + b[i] * s1;
    }
    double n = 0;
    for (int i = 0; i < 4; ++i) n += out[i] * out[i];
    n = std::sqrt(n);
    if (n > 1e-12)
        for (int i = 0; i < 4; ++i) out[i] /= n;
}

std::string tg3_str_of(const tg3_str& s) {
    return s.data ? std::string(s.data, s.len) : std::string();
}

int comps_of(int type) {
    switch (type) {
        case TG3_TYPE_SCALAR: return 1;
        case TG3_TYPE_VEC2:   return 2;
        case TG3_TYPE_VEC3:   return 3;
        case TG3_TYPE_VEC4:   return 4;
        case TG3_TYPE_MAT2:   return 4;
        case TG3_TYPE_MAT3:   return 9;
        case TG3_TYPE_MAT4:   return 16;
        default:              return 1;
    }
}

bool read_accessor(const tg3_model* m, int idx, std::vector<double>& out, int* ncomp) {
    if (idx < 0 || idx >= (int)m->accessors_count) return false;
    const tg3_accessor* acc = &m->accessors[idx];
    if (acc->buffer_view < 0 || acc->buffer_view >= (int)m->buffer_views_count) return false;
    const tg3_buffer_view* bv = &m->buffer_views[acc->buffer_view];
    if (bv->buffer < 0 || bv->buffer >= (int)m->buffers_count) return false;
    const tg3_buffer* buf = &m->buffers[bv->buffer];
    if (!buf->data.data) return false;

    const int nc = comps_of(acc->type);
    *ncomp = nc;

    size_t csize = 4;
    switch (acc->component_type) {
        case TG3_COMPONENT_TYPE_BYTE:
        case TG3_COMPONENT_TYPE_UNSIGNED_BYTE: csize = 1; break;
        case TG3_COMPONENT_TYPE_SHORT:
        case TG3_COMPONENT_TYPE_UNSIGNED_SHORT: csize = 2; break;
        case TG3_COMPONENT_TYPE_FLOAT: csize = 4; break;
        case TG3_COMPONENT_TYPE_DOUBLE: csize = 8; break;
        default: return false;
    }
    const size_t stride = bv->byte_stride ? bv->byte_stride : csize * nc;
    const size_t base = bv->byte_offset + acc->byte_offset;

    out.assign((size_t)acc->count * nc, 0.0);
    for (uint64_t e = 0; e < acc->count; ++e) {
        const uint8_t* p = buf->data.data + base + e * stride;
        if (p + csize * nc > buf->data.data + buf->data.count) return false;
        for (int c = 0; c < nc; ++c) {
            const uint8_t* q = p + c * csize;
            double v = 0;
            switch (acc->component_type) {
                case TG3_COMPONENT_TYPE_FLOAT:  { float f;  std::memcpy(&f, q, 4); v = f; break; }
                case TG3_COMPONENT_TYPE_DOUBLE: { double d; std::memcpy(&d, q, 8); v = d; break; }
                case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:  v = *q; break;
                case TG3_COMPONENT_TYPE_BYTE:           v = (int8_t)*q; break;
                case TG3_COMPONENT_TYPE_UNSIGNED_SHORT: { uint16_t u; std::memcpy(&u, q, 2); v = u; break; }
                case TG3_COMPONENT_TYPE_SHORT:          { int16_t s;  std::memcpy(&s, q, 2); v = s; break; }
                default: break;
            }
            out[(size_t)e * nc + c] = v;
        }
    }
    return true;
}

// ── spec evaluator ─────────────────────────────────────────────────────────
struct Channel {
    int node = -1;
    std::string path;
    std::string interp = "LINEAR";
    std::vector<double> times;
    std::vector<double> vals;
    int ncomp = 0;
};

struct SpecClip {
    std::vector<Channel> channels;
    // channel indices per node, so evaluation is O(nodes + channels) per
    // sampled time rather than O(nodes * channels).
    std::vector<std::vector<int>> chan_of_node;
    double duration = 0.0;
    std::vector<double> base_T, base_R, base_S, base_M;
    std::vector<int> has_matrix;
    std::vector<int> parent;
    std::vector<std::string> names;
};

int find_key(const std::vector<double>& times, double t) {
    if (times.empty()) return 0;
    int k = 0;
    while (k + 1 < (int)times.size() && times[k + 1] <= t) ++k;
    return k;
}

void sample_channel(const Channel& ch, double t, double out[4], int* out_n) {
    *out_n = ch.ncomp;
    const int n = (int)ch.times.size();
    if (n == 0) return;
    if (n == 1 || t <= ch.times.front()) {
        const int off = ch.interp == "CUBICSPLINE" ? 1 : 0;
        for (int c = 0; c < ch.ncomp; ++c) out[c] = ch.vals[(size_t)off * ch.ncomp + c];
        return;
    }
    if (t >= ch.times.back()) {
        const int off = ch.interp == "CUBICSPLINE" ? (n * 3 - 2) : (n - 1);
        for (int c = 0; c < ch.ncomp; ++c) out[c] = ch.vals[(size_t)off * ch.ncomp + c];
        return;
    }
    const int k = find_key(ch.times, t);
    const double t0 = ch.times[k], t1 = ch.times[k + 1];
    const double u = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0;

    if (ch.interp == "STEP") {
        for (int c = 0; c < ch.ncomp; ++c) out[c] = ch.vals[(size_t)k * ch.ncomp + c];
        return;
    }
    if (ch.interp == "CUBICSPLINE") {
        // output holds in-tangent / value / out-tangent per key; tangents are
        // per-second and scale by the key interval (spec section 3.7.2).
        const double dt = t1 - t0;
        const int i0 = k * 3 + 1, o0 = k * 3 + 0, o1 = k * 3 + 2;
        const int i1 = (k + 1) * 3 + 1;
        for (int c = 0; c < ch.ncomp; ++c) {
            const double p0 = ch.vals[(size_t)i0 * ch.ncomp + c];
            const double m0 = ch.vals[(size_t)o0 * ch.ncomp + c] * dt;
            const double p1 = ch.vals[(size_t)i1 * ch.ncomp + c];
            const double m1 = ch.vals[(size_t)o1 * ch.ncomp + c] * dt;
            const double u2 = u * u, u3 = u2 * u;
            out[c] = (2 * u3 - 3 * u2 + 1) * p0 + (u3 - 2 * u2 + u) * m0 +
                     (-2 * u3 + 3 * u2) * p1 + (u3 - u2) * m1;
        }
        return;
    }
    if (ch.path == "rotation" && ch.ncomp == 4) {
        const double* a = &ch.vals[(size_t)k * 4];
        const double* b = &ch.vals[(size_t)(k + 1) * 4];
        quat_slerp(a, b, u, out);
        return;
    }
    for (int c = 0; c < ch.ncomp; ++c) {
        const double a = ch.vals[(size_t)k * ch.ncomp + c];
        const double b = ch.vals[(size_t)(k + 1) * ch.ncomp + c];
        out[c] = a + u * (b - a);
    }
}

void spec_world(const SpecClip& clip, double t, std::vector<Mat4>& world,
                std::vector<char>& done) {
    const int n = (int)clip.names.size();
    for (int i = 0; i < n; ++i) {
        std::memcpy(world[i], kIdent, sizeof(Mat4));
        done[i] = 0;
    }

    std::vector<Mat4> local(n);
    for (int i = 0; i < n; ++i) {
        double T[3] = {clip.base_T[i * 3 + 0], clip.base_T[i * 3 + 1], clip.base_T[i * 3 + 2]};
        double R[4] = {clip.base_R[i * 4 + 0], clip.base_R[i * 4 + 1],
                       clip.base_R[i * 4 + 2], clip.base_R[i * 4 + 3]};
        double S[3] = {clip.base_S[i * 3 + 0], clip.base_S[i * 3 + 1], clip.base_S[i * 3 + 2]};
        if (i < (int)clip.chan_of_node.size()) {
            for (int ci : clip.chan_of_node[i]) {
                const Channel& ch = clip.channels[ci];
                double v[4] = {0, 0, 0, 0};
                int nc = 0;
                sample_channel(ch, t, v, &nc);
                if (ch.path == "translation" && nc >= 3) { T[0] = v[0]; T[1] = v[1]; T[2] = v[2]; }
                else if (ch.path == "rotation" && nc >= 4) { R[0] = v[0]; R[1] = v[1]; R[2] = v[2]; R[3] = v[3]; }
                else if (ch.path == "scale" && nc >= 3) { S[0] = v[0]; S[1] = v[1]; S[2] = v[2]; }
            }
        }
        if (clip.has_matrix[i]) std::memcpy(local[i], &clip.base_M[i * 16], sizeof(Mat4));
        else                    mat_compose(T, R, S, local[i]);
    }

    for (int i = 0; i < n; ++i) {
        if (done[i]) continue;
        std::vector<int> chain;
        int cur = i, guard = 0;
        while (cur >= 0 && !done[cur] && guard++ < 4096) {
            chain.push_back(cur);
            cur = clip.parent[cur];
        }
        Mat4 acc;
        std::memcpy(acc, (cur >= 0 && done[cur]) ? world[cur] : kIdent, sizeof(Mat4));
        for (int k = (int)chain.size() - 1; k >= 0; --k) {
            mat_mul(acc, local[chain[k]], acc);
            std::memcpy(world[chain[k]], acc, sizeof(Mat4));
            done[chain[k]] = 1;
        }
    }
}

std::string norm_name(const std::string& s) {
    std::string o;
    for (char c : s)
        if (std::isalnum((unsigned char)c)) o += (char)std::tolower((unsigned char)c);
    return o;
}

double g_parse_seconds = 0.0;

// Bulk read. Not `(istreambuf_iterator, istreambuf_iterator)`: that walks the
// stream one virtual sgetc() per byte, and this fixture tree is ~1.3 GB of
// GLBs, which turned a 1-second sweep into a 30-second one.
bool read_whole_file(const fs::path& p, std::vector<uint8_t>& out) {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const std::streamoff n = in.tellg();
    if (n <= 0) return false;
    out.resize((size_t)n);
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(out.data()), n);
    return in.good() || (size_t)in.gcount() == (size_t)n;
}

struct Outcome {
    bool ran = false;
    std::string clip;      // animation name
    int joints = 0;
    long long samples = 0;
    double radius = 0.0;
    // POD stores one sample per frame — a dense grid. So the comparison has to
    // be split in two, because the two halves mean different things:
    //
    //   AT a frame  our importer evaluated the source curve exactly, so a
    //               correct importer must match the spec to float precision.
    //               This is the actual conformance assertion.
    //   BETWEEN frames POD can only lerp between two stored frames. When the
    //               source's keys sit off our grid (pilot.glb's "death" is the
    //               only clip that does), the source has a kink inside the
    //               interval that one lerp cannot follow. That difference is
    //               inherent to the format, so it is only bounded, not zero.
    double worst_at_frame = 0.0;
    double worst_between = 0.0;
    std::string at_frame_where, between_where;
    std::string note;
};

// Node hierarchy + rest pose, which every animation in a file shares.
void build_spec_base(const tg3_model& model, SpecClip& clip) {
    const int nn = (int)model.nodes_count;
    clip.names.resize(nn);
    clip.base_T.assign(nn * 3, 0);
    clip.base_R.assign(nn * 4, 0);
    clip.base_S.assign(nn * 3, 1);
    clip.base_M.assign(nn * 16, 0);
    clip.has_matrix.assign(nn, 0);
    clip.parent.assign(nn, -1);
    for (int i = 0; i < nn; ++i) {
        const tg3_node& nd = model.nodes[i];
        clip.names[i] = tg3_str_of(nd.name);
        for (int c = 0; c < 3; ++c) clip.base_T[i * 3 + c] = nd.translation[c];
        for (int c = 0; c < 4; ++c) clip.base_R[i * 4 + c] = nd.rotation[c];
        for (int c = 0; c < 3; ++c) clip.base_S[i * 3 + c] = nd.scale[c];
        std::memcpy(&clip.base_M[i * 16], nd.matrix, sizeof(Mat4));
        clip.has_matrix[i] = nd.has_matrix ? 1 : 0;
        for (uint32_t k = 0; k < nd.children_count; ++k)
            if (nd.children[k] >= 0 && nd.children[k] < nn) clip.parent[nd.children[k]] = i;
    }
}

// Resolve one animation into spec-channel form. Returns false when nothing in
// it is usable.
bool attach_anim(const tg3_model& model, const tg3_animation& anim, SpecClip& clip) {
    const int nn = (int)model.nodes_count;
    for (uint32_t ci = 0; ci < anim.channels_count; ++ci) {
        const tg3_animation_channel& ch = anim.channels[ci];
        if (ch.target.node < 0 || ch.target.node >= nn) continue;
        if (ch.sampler < 0 || ch.sampler >= (int)anim.samplers_count) continue;
        const tg3_animation_sampler& sp = anim.samplers[ch.sampler];
        Channel c;
        c.node = ch.target.node;
        c.path = tg3_str_of(ch.target.path);
        c.interp = sp.interpolation.data ? tg3_str_of(sp.interpolation) : "LINEAR";
        if (!read_accessor(&model, sp.input, c.times, &c.ncomp)) continue;
        int on = 0;
        if (!read_accessor(&model, sp.output, c.vals, &on)) continue;
        c.ncomp = on;
        if (!c.times.empty()) clip.duration = std::max(clip.duration, c.times.back());
        if (clip.chan_of_node.size() < (size_t)nn) clip.chan_of_node.resize((size_t)nn);
        clip.chan_of_node[(size_t)c.node].push_back((int)clip.channels.size());
        clip.channels.push_back(std::move(c));
    }
    return !clip.channels.empty();
}

// Evaluate one (spec clip, our imported clip) pair. The whole comparison is
// convention-free (see the file header).
Outcome compare_pair(const SpecClip& clip, const tg3_skin& skin,
                     const av::PODModel& pod, const std::string& clip_name) {
    Outcome r;
    r.clip = clip_name;
    const int nn = (int)clip.names.size();
    std::vector<std::string> pod_names;
    pod_names.reserve(pod.nodes.size());
    for (const av::PODNode& nd : pod.nodes) pod_names.push_back(nd.name);

    // av::gltf_import_glb() renames every joint that does not already look like
    // one to "Bone_<name>". Match through that rewrite so this test measures
    // transforms, not naming.
    std::map<std::string, int> pod_exact, pod_stripped;
    {
        std::map<std::string, int> seen;
        std::set<std::string> dup;
        for (int i = 0; i < (int)pod_names.size(); ++i) {
            std::string n = norm_name(pod_names[i]);
            if (n.empty()) continue;
            if (seen.count(n)) { dup.insert(n); continue; }
            seen[n] = i;
        }
        for (const std::string& d : dup) seen.erase(d);
        pod_exact = seen;
        for (const auto& kv : seen) {
            const std::string& n = kv.first;
            if (n.size() > 4 && n.compare(0, 4, "bone") == 0) {
                const std::string s = n.substr(4);
                if (!pod_stripped.count(s)) pod_stripped[s] = kv.second;
            }
        }
    }

    std::vector<std::pair<int, int>> pairs;  // (glTF joint index, POD node index)
    for (uint32_t j = 0; j < skin.joints_count; ++j) {
        const int gj = skin.joints[j];
        if (gj < 0 || gj >= nn) continue;
        const std::string key = norm_name(clip.names[gj]);
        if (key.empty()) continue;
        auto it = pod_exact.find(key);
        if (it != pod_exact.end()) { pairs.emplace_back(gj, it->second); continue; }
        auto it2 = pod_stripped.find(key);
        if (it2 != pod_stripped.end()) pairs.emplace_back(gj, it2->second);
    }
    if (pairs.size() < 3) {
        r.note = "too few joints matched (" + std::to_string(pairs.size()) + ")";
        return r;
    }
    r.joints = (int)pairs.size();

    std::vector<Mat4> world(nn);
    std::vector<char> done(nn);

    {
        spec_world(clip, 0.0, world, done);
        for (size_t a = 0; a < pairs.size(); ++a)
            for (size_t b = a + 1; b < pairs.size(); ++b) {
                const Mat4& wa = world[pairs[a].first];
                const Mat4& wb = world[pairs[b].first];
                const double dx = wa[12] - wb[12], dy = wa[13] - wb[13], dz = wa[14] - wb[14];
                r.radius = std::max(r.radius, std::sqrt(dx * dx + dy * dy + dz * dz));
            }
        if (r.radius <= 0) r.radius = 1.0;
    }

    // Sample our own grid, plus midpoints between grid frames: a long-way slerp
    // ([D]) is only visible away from the keys.
    const int nf = std::max(1, pod.num_frames);
    const double step = (pod.fps > 0) ? 1.0 / pod.fps : 1.0;
    std::vector<double> times;
    for (int f = 0; f < nf; ++f) {
        const double t = f * step;
        if (t > clip.duration + 1e-6) break;
        times.push_back(t);
        const double tm = t + 0.5 * step;
        if (tm <= clip.duration) times.push_back(tm);
    }
    if (times.empty()) times.push_back(0.0);

    // Our side: build each matched joint's world matrix ONCE per time step.
    // Deriving them inside the pair loop costs O(pairs^2) full hierarchy walks
    // (the dominant cost of this test).
    std::vector<float> pod_world(16);
    std::vector<double> pod_pos(pairs.size() * 3);
    for (double t : times) {
        const double tf = t * pod.fps;
        const bool at_frame = std::fabs(tf - std::round(tf)) < 1e-6;
        spec_world(clip, t, world, done);
        for (size_t k = 0; k < pairs.size(); ++k) {
            av::get_node_matrix(pod, pairs[k].second, (float)tf, pod_world.data());
            pod_pos[k * 3 + 0] = pod_world[12];
            pod_pos[k * 3 + 1] = pod_world[13];
            pod_pos[k * 3 + 2] = pod_world[14];
        }
        for (size_t a = 0; a < pairs.size(); ++a) {
            const double* pa = &pod_pos[a * 3];
            for (size_t b = a + 1; b < pairs.size(); ++b) {
                const double* pb = &pod_pos[b * 3];

                const Mat4& wa = world[pairs[a].first];
                const Mat4& wb = world[pairs[b].first];
                const double gx = wa[12] - wb[12], gy = wa[13] - wb[13], gz = wa[14] - wb[14];
                const double gd = std::sqrt(gx * gx + gy * gy + gz * gz);

                const double dx = pa[0] - pb[0], dy = pa[1] - pb[1], dz = pa[2] - pb[2];
                const double pd = std::sqrt(dx * dx + dy * dy + dz * dz);

                const double e = std::fabs(gd - pd);
                ++r.samples;
                double& worst = at_frame ? r.worst_at_frame : r.worst_between;
                std::string& where = at_frame ? r.at_frame_where : r.between_where;
                if (e > worst) {
                    worst = e;
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "t=%.3f %s <-> %s", t,
                                  clip.names[pairs[a].first].c_str(),
                                  clip.names[pairs[b].first].c_str());
                    where = buf;
                }
            }
        }
    }

    r.ran = true;
    return r;
}

// One file: build the spec clips, import every clip with av::, compare each
// pair by name (falling back to declaration order).
struct FileOutcome {
    bool ran = false;
    std::string note;
    std::vector<Outcome> clips;
};

FileOutcome cross_check_all(const fs::path& path, float fps) {
    FileOutcome out;

    std::vector<uint8_t> bytes;
    if (!read_whole_file(path, bytes)) { out.note = "unreadable"; return out; }
    if (bytes.size() < 20) { out.note = "too small"; return out; }

    // Cheap pre-filter: almost every GLB in a world-art tree is a static prop,
    // and a full tg3 parse of one costs real time. A GLB with neither "skins"
    // nor "animations" in its JSON chunk cannot be skinned+animated, so skip it
    // before parsing. (Substring, not a JSON parse — this only ever trades a
    // parse for a scan, and `ran` still comes from the real parse below.)
    {
        const uint8_t* p = bytes.data();
        size_t json_len = 0, json_off = 0;
        if (bytes.size() >= 20 && std::memcmp(p, "glTF", 4) == 0) {
            json_len = (size_t)p[12] | ((size_t)p[13] << 8) | ((size_t)p[14] << 16) |
                       ((size_t)p[15] << 24);
            json_off = 20;
        } else {
            json_len = bytes.size();  // .gltf: the whole file is JSON
        }
        if (json_off + json_len > bytes.size()) json_len = bytes.size() - json_off;
        const std::string_view js((const char*)p + json_off, json_len);
        if (js.find("\"skins\"") == std::string_view::npos ||
            js.find("\"animations\"") == std::string_view::npos) {
            out.note = "not skinned+animated";
            return out;
        }
    }

    tg3_model model;
    std::memset(&model, 0, sizeof(model));
    tg3_parse_options opts;
    tg3_parse_options_init(&opts);
    const std::string dir = path.parent_path().string();
    const auto t_parse0 = std::chrono::steady_clock::now();
    const tg3_error_code perr = tg3_parse_auto(&model, nullptr, bytes.data(), bytes.size(),
                                              dir.c_str(), (uint32_t)dir.size(), &opts);
    g_parse_seconds += std::chrono::duration<double>(
                           std::chrono::steady_clock::now() - t_parse0).count();
    if (perr != TG3_OK) {
        out.note = "tg3 parse failed";
        tg3_model_free(&model);
        return out;
    }
    if (model.skins_count == 0 || model.animations_count == 0) {
        out.note = "not skinned+animated";
        tg3_model_free(&model);
        return out;
    }

    std::vector<std::pair<std::string, av::PODModel>> imported;
    std::string err;
    const auto t_import0 = std::chrono::steady_clock::now();
    const bool imported_ok = av::gltf_import_all_clips(path.string(), imported, &err, 1.0f, fps);
    if (std::getenv("GLB_CONF_TIMING"))
        std::fprintf(stderr, "  [timing] gltf_import_all_clips: %.3f s\n",
                     std::chrono::duration<double>(std::chrono::steady_clock::now() - t_import0).count());
    if (!imported_ok) {
        out.note = "our importer failed: " + err;
        tg3_model_free(&model);
        return out;
    }
    if (imported.empty()) {
        out.note = "our importer produced no clips";
        tg3_model_free(&model);
        return out;
    }

    // Match imported clips to source animations by name, so clip order drifting
    // can never mask one animation being read as another. `use_names` is off
    // when names are absent or ambiguous on either side.
    std::vector<std::string> anim_names((size_t)model.animations_count);
    std::set<std::string> anim_seen;
    bool use_names = true;
    for (uint32_t a = 0; a < model.animations_count; ++a) {
        anim_names[a] = norm_name(tg3_str_of(model.animations[a].name));
        if (anim_names[a].empty() || !anim_seen.insert(anim_names[a]).second) use_names = false;
    }
    std::set<std::string> imp_seen;
    for (const auto& kv : imported) {
        const std::string n = norm_name(kv.first);
        if (n.empty() || !imp_seen.insert(n).second) use_names = false;
    }

    for (size_t i = 0; i < imported.size(); ++i) {
        size_t anim_idx = i;
        if (use_names) {
            auto it = std::find(anim_names.begin(), anim_names.end(),
                                norm_name(imported[i].first));
            if (it == anim_names.end()) continue;
            anim_idx = (size_t)std::distance(anim_names.begin(), it);
        }
        if (anim_idx >= (size_t)model.animations_count) continue;

        SpecClip clip;
        build_spec_base(model, clip);
        if (!attach_anim(model, model.animations[anim_idx], clip)) continue;

        const auto t_cmp0 = std::chrono::steady_clock::now();
        Outcome r = compare_pair(clip, model.skins[0], imported[i].second,
                                 imported[i].first);
        if (std::getenv("GLB_CONF_TIMING"))
            std::fprintf(stderr, "  [timing] compare_pair [%s]: %.3f s\n",
                         imported[i].first.c_str(),
                         std::chrono::duration<double>(std::chrono::steady_clock::now() - t_cmp0).count());
        if (r.ran) out.clips.push_back(std::move(r));
    }

    out.ran = !out.clips.empty();
    if (!out.ran && out.note.empty()) out.note = "no clip pair usable";
    tg3_model_free(&model);
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<fs::path> roots;
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) roots.emplace_back(argv[i]);
    } else {
        if (const char* env = std::getenv("SWORDIGO_GLB_FIXTURE_DIR"))
            roots.emplace_back(env);
        if (const char* home = std::getenv("HOME"))
            roots.emplace_back(fs::path(home) / "smario" / "smashroyale.io");
    }

    std::printf("glb_import_spec_conformance_test — av::gltf_import_all_clips vs the glTF 2.0 spec\n\n");

    std::vector<fs::path> files;
    for (const fs::path& root : roots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            if (ext == ".glb" || ext == ".gltf") files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());

    if (files.empty()) {
        std::printf("  (skipped) no third-party .glb/.gltf on this machine\n");
        std::printf("            set SWORDIGO_GLB_FIXTURE_DIR to a directory of them\n");
        std::printf("\n%d checks, 0 failures (skipped)\n", 0);
        return 0;
    }

    const float fps = 30.0f;
    // AT a frame: our importer read the source curve, so this must be exact.
    // Absolute, in model units — not a fraction of the rig, so a bug in a tiny
    // rig cannot hide behind its own smallness.
    const double tol_at_frame = 1e-3;
    // BETWEEN frames: POD can only lerp across its own interval, so a source
    // whose keys sit off our grid is bounded by its own motion across that
    // interval. Loose, but a wrong axis or a mis-indexed joint still trips it.
    const double tol_between_frac = 0.05;  // 5% of the rig's joint spread

    int skipped = 0, ran = 0;
    long long total_samples = 0;
    double worst_between_seen = 0.0;
    std::string worst_file, worst_clip;

    int clips_checked = 0;
    for (const fs::path& f : files) {
        const FileOutcome fo = cross_check_all(f, fps);
        const std::string rel = fs::relative(f, roots.front()).string();
        if (!fo.ran) { ++skipped; continue; }
        ++ran;
        for (const Outcome& r : fo.clips) {
            ++clips_checked;
            total_samples += r.samples;
            const double between_frac = r.worst_between / r.radius;
            if (between_frac > worst_between_seen) {
                worst_between_seen = between_frac;
                worst_file = rel;
                worst_clip = r.clip;
            }
            char what[768];
            std::snprintf(what, sizeof(what),
                          "%s [%s]: %d joints, %lld pairwise samples; at-frame error "
                          "%.6f units%s",
                          rel.c_str(), r.clip.c_str(), r.joints, r.samples,
                          r.worst_at_frame,
                          r.at_frame_where.empty() ? "" : (", worst " + r.at_frame_where).c_str());
            ok(r.worst_at_frame <= tol_at_frame, what);

            std::snprintf(what, sizeof(what),
                          "%s [%s]: sub-frame resampling stays bounded "
                          "(%.6f = %.3f%% of spread, limit %.0f%%%s)",
                          rel.c_str(), r.clip.c_str(), r.worst_between,
                          between_frac * 100.0, tol_between_frac * 100.0,
                          r.between_where.empty() ? "" : (", worst " + r.between_where).c_str());
            ok(between_frac <= tol_between_frac, what);
        }
    }

    if (ran == 0) {
        std::printf("  (skipped) %d glb/gltf files, none skinned+animated\n", (int)files.size());
    } else {
        std::printf("\n  %d skinned+animated GLB(s), %d clip(s), %lld pairwise "
                    "joint-distance samples\n", ran, clips_checked, total_samples);
        if (!worst_file.empty())
            std::printf("  worst sub-frame: %s [%s] at %.3f%% of joint spread\n",
                        worst_file.c_str(), worst_clip.c_str(), worst_between_seen * 100.0);
    }

    if (std::getenv("GLB_CONF_TIMING"))
        std::fprintf(stderr, "  [timing] tg3_parse over %zu files: %.3f s\n",
                     files.size(), g_parse_seconds);

    std::printf("\n%d checks, %d failures%s\n", checks, failures,
                ran == 0 ? " (skipped)" : "");
    return failures == 0 ? 0 : 1;
}
