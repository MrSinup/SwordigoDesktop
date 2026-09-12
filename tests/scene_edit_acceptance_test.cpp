// scene_edit_acceptance_test — acceptance test for the Ruby GG viewport
// scene-object editing API (ImGui asset_viewer parity):
//   copy_scene_selection / paste_scene_selection / duplicate_scene_selection /
//   delete_scene_selection / move_scene_object.
//
// Drives the REAL Viewport3DWidget (offscreen GL, Mesa swrast) against a real
// .scene file and asserts on the in-RAM scene:
//   - paste appends a copy with a FRESH identifier and a 24-unit nudge
//   - repeated pastes cascade (+24 per paste)
//   - duplicate = copy + paste (so a following Ctrl+V re-pastes the duplicate)
//   - delete removes exactly the active object and fixes the selection
//   - Alt+Up/Down reorders the object (remove + insert) and reindexes
//   - no-selection operations are safe no-ops
//
// The scene file is optional (SKIP when absent), matching the other scene
// data-driven tests.
//
// The viewport is intentionally leaked: the QOpenGLWidget destructor needs a
// live GL context, which offscreen teardown at main() exit cannot guarantee.
// Leaking is the standard pattern for offscreen GL tests — Qt reclaims the
// context during its own cleanup.

#include <QApplication>
#include <cstdio>
#include <filesystem>
#include <string>

#include "ruby/viewport/viewport_3d_widget.h"

namespace fs = std::filesystem;

// Defined by ruby_gg's main.cpp in the app; needed by libswcore here.
std::string g_instance_assets_dir = "assets";

static int g_failures = 0;

#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::printf("FAIL: %s (line %d)\n", msg, __LINE__);               \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

static bool name_in(const av::SceneData& scene, const std::string& name) {
    for (const auto& o : scene.objects)
        if (o.name == name) return true;
    return false;
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);

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
        std::printf("SKIP: scene file not found: %s\n", src.c_str());
        return 0;   // optional data file — skip, not fail
    }

    auto* viewport = new ruby::viewport::Viewport3DWidget();   // intentionally leaked
    viewport->resize(800, 600);
    viewport->show();
    app.processEvents();

    if (!viewport->load_scene(src)) {
        std::printf("FAIL: load_scene(%s) returned false\n", src.c_str());
        return 1;
    }
    app.processEvents();

    const av::SceneData& scene = viewport->scene();
    const size_t n0 = scene.objects.size();
    if (n0 < 3) {
        std::printf("SKIP: scene too small (%zu objects) for edit assertions\n", n0);
        return 0;
    }
    const av::SceneObject& orig = scene.objects[0];
    const std::string orig_name = orig.name;
    std::printf("loaded %s: %zu objects, first='%s'\n", src.c_str(), n0, orig_name.c_str());
    std::fflush(stdout);

    // ── No selection: all clipboard ops are safe no-ops ─────────────────────
    viewport->set_selected_object(-1);
    viewport->copy_scene_selection();
    CHECK(!viewport->has_scene_clipboard(), "copy with no selection must not arm the clipboard");
    viewport->paste_scene_selection();
    CHECK(viewport->scene().objects.size() == n0, "paste with no clipboard must not add objects");
    viewport->duplicate_scene_selection();
    viewport->delete_scene_selection();
    viewport->move_scene_object(-1);
    CHECK(viewport->scene().objects.size() == n0, "no-selection edits must be no-ops");

    // ── Copy + paste ────────────────────────────────────────────────────────
    viewport->set_selected_object(0);
    app.processEvents();
    viewport->copy_scene_selection();
    CHECK(viewport->has_scene_clipboard(), "copy must arm the clipboard");

    viewport->paste_scene_selection();
    app.processEvents();
    {
        const av::SceneData& s = viewport->scene();
        CHECK(s.objects.size() == n0 + 1, "paste must append exactly one object");
        const av::SceneObject& pasted = s.objects.back();
        CHECK(pasted.name != orig_name, "pasted object must get a fresh identifier");
        CHECK(name_in(s, orig_name), "original object remains after paste");
        int name_hits = 0;
        for (const auto& o : s.objects) name_hits += (o.name == pasted.name) ? 1 : 0;
        CHECK(name_hits == 1, "fresh identifier is unique across the scene");
        CHECK(pasted.pos_x == orig.pos_x + 24.0f, "first paste nudges +24 on X");
        CHECK(pasted.pos_y == orig.pos_y + 24.0f, "first paste nudges +24 on Y");
        CHECK(pasted.mesh_name == orig.mesh_name, "paste copies the mesh");
        CHECK(viewport->selected_object() == static_cast<int>(n0), "selection moves to the pasted object");
    }

    // ── Repeated paste cascades (+24 per paste) ─────────────────────────────
    viewport->paste_scene_selection();
    app.processEvents();
    {
        const av::SceneData& s = viewport->scene();
        CHECK(s.objects.size() == n0 + 2, "second paste appends another object");
        CHECK(s.objects.back().pos_x == orig.pos_x + 48.0f, "second paste nudges +48 on X");
        CHECK(s.objects.back().name != s.objects[s.objects.size() - 2].name,
              "cascaded paste still gets a unique identifier");
        CHECK(viewport->selected_object() == static_cast<int>(n0 + 1), "selection follows the newest paste");
    }

    // ── Duplicate = copy + paste (next Ctrl+V re-pastes the duplicate) ─────
    // The active object at this point is the second paste (orig+48); the
    // duplicate copies THAT and nudges +24 (copy resets the cascade), so it
    // lands at orig+72 with a fresh identifier.
    viewport->duplicate_scene_selection();
    app.processEvents();
    {
        const av::SceneData& s = viewport->scene();
        CHECK(s.objects.size() == n0 + 3, "duplicate appends one object");
        CHECK(s.objects.back().pos_x == orig.pos_x + 72.0f,
              "duplicate nudges the active object (+48) by +24 → +72");
        CHECK(s.objects.back().name != s.objects[s.objects.size() - 2].name,
              "duplicate uses a fresh identifier");
        CHECK(viewport->selected_object() == static_cast<int>(n0 + 2), "selection follows the duplicate");
    }

    // ── Move up / down (remove + insert, selection follows) ─────────────────
    {
        const size_t n1 = viewport->scene().objects.size();
        const std::string moved_name = viewport->scene().objects[n1 - 1].name;   // last object
        const std::string prev_name = viewport->scene().objects[n1 - 2].name;

        viewport->move_scene_object(-1);   // Alt+Up on the last object
        app.processEvents();
        const av::SceneData& s = viewport->scene();
        CHECK(s.objects.size() == n1, "move does not change the object count");
        CHECK(s.objects[n1 - 2].name == moved_name, "moved object lands one slot earlier");
        CHECK(s.objects[n1 - 1].name == prev_name, "displaced object shifts one slot later");
        CHECK(viewport->selected_object() == static_cast<int>(n1 - 2), "selection follows the moved object");

        viewport->move_scene_object(1);   // Alt+Down back
        app.processEvents();
        const av::SceneData& s2 = viewport->scene();
        CHECK(s2.objects[s2.objects.size() - 1].name == moved_name, "moved object returns to the end");
        CHECK(viewport->selected_object() == static_cast<int>(s2.objects.size() - 1),
              "selection follows the move-down");
    }

    // ── Delete removes exactly the active object ────────────────────────────
    {
        const size_t n1 = viewport->scene().objects.size();
        const std::string doomed_name = viewport->scene().objects[1].name;
        viewport->set_selected_object(1);
        viewport->delete_scene_selection();
        app.processEvents();
        const av::SceneData& s = viewport->scene();
        CHECK(s.objects.size() == n1 - 1, "delete removes exactly one object");
        CHECK(!name_in(s, doomed_name), "deleted object is gone from the scene");
    }

    std::fflush(stdout);
    if (g_failures == 0)
        std::printf("PASS: scene copy/paste/duplicate/delete/move (%zu objects)\n",
                    viewport->scene().objects.size());
    else
        std::printf("FAIL: %d scene-edit assertion(s) failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}