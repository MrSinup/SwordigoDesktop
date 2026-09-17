// rigid_skin_refine_test.cpp — the game reads ONE bone per vertex, so a smooth
// rig has to be collapsed before it is written. Which bone each vertex gets is
// therefore a real modelling decision, not a formality, and it used to be made
// by argmax(weight) AT THE BIND POSE — a choice taken from a single instant, for
// a mesh that only exists to be animated.
//
// refine_rigid_skin() scores each vertex's own influences against every pose of
// every clip and keeps the one that tracks it best. This test pins the four
// things that make that safe to ship:
//
//   1. It cannot be a no-op. On a real animated rig some vertices must change
//      bone, or the "refinement" is doing nothing.
//   2. It must not make anything worse. Worst-case and mean deviation from the
//      full smooth-skin result may only improve, never regress.
//   3. It must not touch the geometry. Only bone_indices may differ; positions,
//      indices, uvs and normals must be identical to the max-weight bake.
//   4. It must be a strict no-op without clips. Callers that convert an
//      unanimated model must get exactly the bake they got before.
//
// Deviation is measured as the distance between a vertex's rigidly-skinned
// position and its full smooth-skinned position, which is the error the engine
// incurs by reading one bone. That number is in model space, so it is
// independent of any change of basis and needs no reference implementation.
//
// Runs on real third-party GLBs and SKIPs (loudly, exit 0) when the machine has
// none, matching glb_import_skinning_coherence_test.cpp.

#include "tools/gltf_glb.h"
#include "tools/pod_loader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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

std::string fmt_d(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.5g", v);
    return buf;
}

std::vector<fs::path> candidate_assets() {
    std::vector<fs::path> out;
    if (const char* env = std::getenv("SWORDIGO_GLB_ASSETS")) {
        if (fs::exists(env)) out.emplace_back(env);
    }
    // Rigs with real animation, on the corpora this repo is developed with.
    const char* kFixed[] = {
        "smario/smashroyale.io/assets/soldier-v1/soldier.glb",
        "smario/smashroyale.io/assets/pilot.glb",
        "MEGA/statue.glb",
    };
    const char* home = std::getenv("HOME");
    for (const char* rel : kFixed) {
        if (!home) break;
        fs::path p = fs::path(home) / rel;
        if (fs::exists(p)) out.push_back(p);
    }
    return out;
}

// The bake the converter performs when there is nothing to score against:
// argmax over each vertex's influences, weight 1.0. Mirrors the fallback inside
// refine_rigid_skin, so the no-clip assertion compares like with like.
std::vector<float> max_weight_bake(const av::PODMesh& m) {
    std::vector<float> out((size_t)m.num_vertices, 0.0f);
    const int bpv = std::max(1, m.bones_per_vertex);
    for (int v = 0; v < m.num_vertices; ++v) {
        float best_w = -1.0f;
        float best_i = 0.0f;
        for (int k = 0; k < bpv; ++k) {
            const size_t idx = (size_t)v * bpv + k;
            if (idx >= m.bone_weights.size() || idx >= m.bone_indices.size()) break;
            if (m.bone_weights[idx] > best_w) {
                best_w = m.bone_weights[idx];
                best_i = m.bone_indices[idx];
            }
        }
        out[(size_t)v] = best_i;
    }
    return out;
}

void check_asset(const fs::path& glb) {
    const std::string name = glb.stem().string();
    std::printf("\n%s\n", glb.string().c_str());

    // Full weights: the influences are exactly what the refinement chooses
    // between, so it must see all of them.
    av::PODModel model;
    std::vector<av::GLTFImageBuffer> images;
    av::GLTFPBRInfo pbr;
    std::string err;
    if (!av::gltf_import_glb(glb.string(), model, images, &err, &pbr, 1.0f, false)) {
        check(false, name + ": import", err);
        return;
    }

    std::vector<std::pair<std::string, av::PODModel>> clips;
    std::string cerr;
    if (!av::gltf_import_all_clips(glb.string(), clips, &cerr, 1.0f, 0.0f, false) || clips.empty()) {
        std::printf("       (no animation clips — refinement is a no-op here, skipped)\n");
        return;
    }

    int mesh_i = -1;
    for (size_t i = 0; i < model.meshes.size(); ++i) {
        if (model.meshes[i].bones_per_vertex > 0 && !model.meshes[i].bone_indices.empty()) {
            mesh_i = (int)i;
            break;
        }
    }
    if (mesh_i < 0) {
        std::printf("       (no skinned mesh in the base — skipped)\n");
        return;
    }

    const av::PODMesh& src = model.meshes[(size_t)mesh_i];
    const std::vector<float> expected_argmax = max_weight_bake(src);
    const size_t want_n = expected_argmax.size();

    // ── 4. No clips at all must reproduce the max-weight bake exactly. ───────
    {
        av::PODModel nocopy = model;
        av::RigidSkinRefineStats st;
        std::string e;
        const bool ok = av::refine_rigid_skin(nocopy, {}, &st, &e);
        check(ok, name + ": refine with no clips succeeds", e);
        const bool same = nocopy.meshes[(size_t)mesh_i].bone_indices == expected_argmax;
        check(same && st.moved == 0,
              name + ": no clips -> max-weight bake, unchanged",
              "moved " + std::to_string(st.moved) + " of " + std::to_string((int)want_n));
    }

    // ── 1. With clips, some vertices must actually change bone. ─────────────
    av::PODModel refined = model;
    av::RigidSkinRefineStats st;
    std::string e;
    if (!av::refine_rigid_skin(refined, clips, &st, &e)) {
        check(false, name + ": refine with clips", e);
        return;
    }

    const av::PODMesh& out = refined.meshes[(size_t)mesh_i];
    check(st.pose_samples > 0, name + ": poses were scored",
          std::to_string(st.pose_samples) + " poses over " +
          std::to_string((int)clips.size()) + " clips");
    check(st.moved > 0, name + ": refinement is not a no-op",
          std::to_string(st.moved) + " of " + std::to_string(st.vertices) +
          " vertices re-bound (" +
          std::to_string(100.0 * st.moved / std::max(1, st.vertices)) + "%)");

    // ── 2. It may improve; it may not regress. ──────────────────────────────
    const double eps = 1e-6;
    check(st.worst_after <= st.worst_before + eps,
          name + ": worst-case deviation does not regress",
          fmt_d(st.worst_before) + " -> " + fmt_d(st.worst_after));
    check(st.mean_after <= st.mean_before + eps,
          name + ": mean deviation does not regress",
          fmt_d(st.mean_before) + " -> " + fmt_d(st.mean_after));

    // ── 3. Geometry untouched; only the bone list may differ. ───────────────
    check(out.positions == src.positions && out.indices == src.indices &&
              out.uvs == src.uvs && out.normals == src.normals,
          name + ": geometry is identical to the max-weight bake");
    check(out.bones_per_vertex == 1 &&
              out.bone_weights.size() == out.bone_indices.size() &&
              out.bone_indices.size() == want_n,
          name + ": output is 1 bone per vertex",
          "bpv " + std::to_string(out.bones_per_vertex));
    bool weights_one = true;
    for (float w : out.bone_weights) if (w != 1.0f) { weights_one = false; break; }
    check(weights_one, name + ": every output weight is 1.0");

    bool idx_ok = true;
    for (float i : out.bone_indices)
        if (i < 0.0f || i >= (float)refined.nodes.size()) { idx_ok = false; break; }
    check(idx_ok, name + ": every bone index is in range");

    // Determinism: the same input must give the same bake, or two runs of the
    // converter produce different files from one model.
    {
        av::PODModel again = model;
        av::RigidSkinRefineStats st2;
        av::refine_rigid_skin(again, clips, &st2, nullptr);
        check(again.meshes[(size_t)mesh_i].bone_indices == out.bone_indices &&
                  st2.moved == st.moved,
              name + ": refinement is deterministic");
    }
}

} // namespace

int main() {
    std::printf("rigid_skin_refine_test — the 1-bone bake must be chosen from how "
                "the model moves\n\n");

    const std::vector<fs::path> assets = candidate_assets();
    if (assets.empty()) {
        std::printf("SKIP: no real GLB assets found. Set SWORDIGO_GLB_ASSETS=<dir or file>\n"
                    "      to point at a directory of third-party .glb models.\n");
        return 0;
    }

    std::error_code ec;
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
            for (const fs::path& f : found) check_asset(f);
        } else if (fs::exists(a)) {
            check_asset(a);
        }
    }

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
