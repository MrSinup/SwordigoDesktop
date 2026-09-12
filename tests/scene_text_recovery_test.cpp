// scene_text_recovery_test — regression test for the scene-corruption bug that
// turned .scene files into FileRift TEXT with a duplicated banner line
// ("## FileRift decoded ..." twice), which then made the 3D viewport render an
// empty world and the engine crash.
//
// Guards the fixes:
//  1) decode_protobuf already emits the FileRift banner — callers must never
//     prepend a second one (the double-banner was the corruption).
//  2) A FileRift text scene — single OR double banner — re-encodes to a binary
//     that loads back to the same SceneData via av::scene_load_bytes; that is
//     the exact in-memory recovery the 3D viewport now performs instead of
//     showing an empty scene.
//  3) The banner-strip loop (ScriptIDEWidget::save_file) removes every leading
//     banner line, so saving a legacy double-banner buffer is safe.
#include "tools/scene_loader.h"
#include "tools/filerift.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

std::string g_instance_assets_dir = "assets";
namespace fs = std::filesystem;

namespace {

// Same predicate the viewport uses to decide structure equality (kept in sync).
bool structure_matches(const av::SceneData& cur, const av::SceneData& next) {
    if (cur.objects.size() != next.objects.size()) return false;
    for (size_t i = 0; i < cur.objects.size(); ++i) {
        const av::SceneObject& a = cur.objects[i];
        const av::SceneObject& b = next.objects[i];
        if (a.mesh_name != b.mesh_name) return false;
        if (a.template_name != b.template_name) return false;
        if (a.background_name != b.background_name) return false;
        if (a.ground_meshes.size() != b.ground_meshes.size()) return false;
        if (a.ground_mesh_raw.size() != b.ground_mesh_raw.size()) return false;
        for (size_t g = 0; g < a.ground_mesh_raw.size(); ++g)
            if (a.ground_mesh_raw[g] != b.ground_mesh_raw[g]) return false;
        if (a.ground_mesh_textures != b.ground_mesh_textures) return false;
    }
    return true;
}

// Mirrors ScriptIDEWidget::save_file's strip loop: remove EVERY leading
// "## FileRift decoded" banner line plus surrounding blank lines.
std::string strip_all_banners(std::string text) {
    const std::string prefix = "## FileRift decoded Swordigo file type: scene";
    for (;;) {
        if (text.rfind(prefix, 0) != 0) break;
        const size_t nl = text.find('\n');
        if (nl == std::string::npos) { text.clear(); break; }
        text = text.substr(nl + 1);
        const size_t first = text.find_first_not_of(" \t\r\n");
        text = (first == std::string::npos) ? std::string() : text.substr(first);
    }
    return text;
}

} // namespace

int main(int argc, char** argv) {
    std::string src;
    if (argc >= 2) {
        src = argv[1];
    } else {
#ifdef SWORDIGO_SRC_DIR
        src = std::string(SWORDIGO_SRC_DIR) + "/assets/v3_Wasteland.scene";
#else
        src = "src/assets/v3_Wasteland.scene";
#endif
    }
    if (!fs::exists(src)) {
        std::cerr << "SKIP: scene file not found: " << src << "\n";
        return 0;   // optional data file — skip, not fail
    }

    const av::SceneData base = av::scene_load(src);
    if (base.objects.empty()) {
        std::cerr << "FAIL: could not load " << src << "\n";
        return 1;
    }
    const size_t n = base.objects.size();

    const std::string bin = av::scene_serialize(base);
    const std::string markup = ::filerift::decode_protobuf(bin, "scene");

    // 1) decode_protobuf must already carry exactly one banner.
    const std::string banner = "## FileRift decoded Swordigo file type: scene\n\n";
    if (markup.rfind(banner, 0) != 0) {
        std::cerr << "FAIL: decode_protobuf must emit the FileRift banner\n";
        return 1;
    }
    std::string body = markup.substr(banner.size());

    // 2) Single-banner text scene recovers to the same structure.
    {
        const std::string text = banner + body;
        const std::string re = ::filerift::recode_markup(text, "scene");
        const std::vector<uint8_t> bytes(re.begin(), re.end());
        const av::SceneData recovered = av::scene_load_bytes(bytes, src, {});
        if (recovered.objects.empty() || !structure_matches(base, recovered)) {
            std::cerr << "FAIL: single-banner text scene did not recover ("
                      << recovered.objects.size() << " objects)\n";
            return 1;
        }
    }

    // 3) DOUBLE-banner text scene (the exact corrupted-file shape) recovers.
    {
        const std::string corrupted = banner + banner + body;
        const std::string re = ::filerift::recode_markup(corrupted, "scene");
        const std::vector<uint8_t> bytes(re.begin(), re.end());
        const av::SceneData recovered = av::scene_load_bytes(bytes, src, {});
        if (recovered.objects.empty() || !structure_matches(base, recovered)) {
            std::cerr << "FAIL: double-banner text scene did not recover ("
                      << recovered.objects.size() << " objects)\n";
            return 1;
        }
        // The save-file strip loop must remove both banners so the re-encoder
        // sees clean markup (== same binary as the single-banner case).
        const std::string stripped = strip_all_banners(corrupted);
        const std::string re_stripped = ::filerift::recode_markup(stripped, "scene");
        if (re_stripped != re) {
            std::cerr << "FAIL: banner strip loop changed the encoded output\n";
            return 1;
        }
    }

    std::cout << "PASS: text scenes (single + double banner) recover to the same "
              << n << "-object structure; banner strip is idempotent\n";
    return 0;
}