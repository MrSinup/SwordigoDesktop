// scene_smart_save_test — regression test for the Ruby GG "smart save" path.
//
// Guards two real regressions:
//  1) Corruption: the FileRift re-encoder must throw on malformed markup so
//     save_file() aborts BEFORE opening/truncating the target file (it used to
//     truncate first, destroying scene files on encode errors).
//  2) Smart save: after av::scene_save() of an in-RAM scene, reloading the
//     written file must yield a structure-identical SceneData (same objects,
//     models, templates, backgrounds, ground-mesh raw bytes) so the viewport
//     can apply the save in place — no evict + reload, no camera reset.
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

// The exact predicate Viewport3DWidget::scene_structure_matches uses to decide
// whether a save can be applied in place (kept in sync deliberately).
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

    const std::string dst = src + ".smart-save-test.scene";
    fs::remove(dst);

    av::SceneData a = av::scene_load(src);
    if (a.objects.empty()) {
        std::cerr << "FAIL: could not load " << src << "\n";
        return 1;
    }
    const size_t n = a.objects.size();

    // Simulate a gizmo transform edit on object 0.
    a.objects[0].pos_x += 12.5f;
    a.objects[0].rot_y += 0.1f;

    std::string err;
    if (!av::scene_save(dst, a, &err)) {
        std::cerr << "FAIL: av::scene_save: " << err << "\n";
        return 1;
    }

    // Atomic writer guarantee: the file must be exactly serialize(RAM).
    {
        std::ifstream f(dst, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const std::string ser = av::scene_serialize(a);
        if (bytes != ser) {
            std::cerr << "FAIL: file != serialize(RAM) (" << bytes.size() << " vs " << ser.size() << ")\n";
            return 1;
        }
    }

    av::SceneData b = av::scene_load(dst);
    if (b.objects.empty()) {
        std::cerr << "FAIL: reload of saved copy failed\n";
        return 1;
    }
    if (!structure_matches(a, b)) {
        std::cerr << "FAIL: structure mismatch after round trip (" << n << " objects)\n";
        return 1;
    }
    if (b.objects[0].pos_x != a.objects[0].pos_x || b.objects[0].rot_y != a.objects[0].rot_y) {
        std::cerr << "FAIL: transform edit lost in round trip\n";
        return 1;
    }

    // Corruption-fix mechanism: malformed markup must throw during encode so a
    // caller can abort before touching the target file.
    bool threw = false;
    try {
        (void)::filerift::recode_markup("Object{\n  Position{\n    X : 1\n", "scene");
    } catch (...) {
        threw = true;
    }
    if (!threw) {
        std::cerr << "FAIL: malformed markup did not throw during encode\n";
        return 1;
    }

    fs::remove(dst);
    std::cout << "PASS: round trip structure-identical (" << n
              << " objects), transform preserved, encode-throw verified\n";
    return 0;
}