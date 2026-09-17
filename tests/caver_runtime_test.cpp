// caver_runtime_test.cpp — the component decoder against real shipping data.
//
// WHY THIS EXISTS
// ---------------
// A Component message carries a *set* of payload submessage slots, not one: the
// engine writes the base class's slot alongside the derived class's. A
// CollisionShape is tags 120 (ShapeComponent) + 121 (CollisionShapeComponent), a
// MonsterEntity is 152 (EntityComponent) + 158 (MonsterEntityComponent), every
// monster controller is 302 (MonsterControllerComponent) + its own slot, and a
// MagicBolt is 550 (MagicBoltComponent) + 558 (SpellComponent, the shared base).
//
// A decoder that keys off the ClassName's own slot only reads half of every such
// component and silently drops the rest — which is exactly what this test locks
// down, using the shipped .scl files as the fixture (they are the specification).
//
// The asset directory is discovered from the same place the desktop app keeps its
// extracted assets; when it is absent the test prints SKIP and passes, so it never
// fails on a machine without the game data.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "ruby/caver/library_manager.h"
#include "ruby/caver/runtime.h"

// Required by libswcore (asset resolution global; the other tests define it too).
std::string g_instance_assets_dir = "assets";

using caver::LibraryManager;
using caver::RuntimeComponent;
using caver::RuntimeObject;
using caver::RuntimeScene;

static int failures = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++failures; }
}

static std::vector<std::string> candidate_asset_dirs() {
    std::vector<std::string> dirs;
    if (const char* env = std::getenv("SWORDIGO_ASSETS")) dirs.push_back(env);
    const char* home = std::getenv("HOME");
    if (home) {
        std::string base = std::string(home) + "/.local/share/swordigo-desktop";
        dirs.push_back(base + "/assets13/resources");
        dirs.push_back(base + "/assets/resources");
    }
    dirs.push_back("assets/resources");
    return dirs;
}

static const RuntimeComponent* find_class(const RuntimeObject& object, const std::string& short_name) {
    for (const auto& component : object.components)
        if (component.class_name == short_name) return &component;
    return nullptr;
}

int main() {
    std::string asset_dir;
    for (const auto& dir : candidate_asset_dirs()) {
        FILE* probe = std::fopen((dir + "/monsters.scl").c_str(), "rb");
        if (probe) { std::fclose(probe); asset_dir = dir; break; }
    }
    if (asset_dir.empty()) {
        std::printf("SKIP: no extracted assets found (set SWORDIGO_ASSETS to enable)\n");
        return 0;
    }

    LibraryManager libraries;
    libraries.add_search_path(asset_dir);
    int loaded = 0;
    for (const char* name : {"monsters", "hiro", "magic", "collectibles", "dimensional", "game_common"})
        if (libraries.load_by_name(name, nullptr)) ++loaded;
    check(loaded > 0, "at least one shipping .scl loaded from " + asset_dir);
    if (!loaded) return 1;

    RuntimeScene scene;
    scene.set_library_manager(&libraries);

    // Spawn every template in the corpus and total up what the decoder saw.
    size_t spawned = 0;
    size_t multi_payload = 0;
    size_t collision_pair = 0;
    size_t monster_pair = 0;
    size_t bolt_pair = 0;
    size_t named_fields = 0;
    std::vector<std::string> monster_with_facing;
    std::vector<std::string> collision_with_friction;

    for (const auto& library_name : libraries.names()) {
        const caver::Library* library = libraries.get(library_name);
        if (!library) continue;
        for (const auto& templ : library->templates) {
            float origin[3] = {0.0f, 0.0f, 0.0f};
            RuntimeObject* object = scene.spawn(templ.name, "t:" + templ.name, origin);
            if (!object) continue;
            ++spawned;

            for (const auto& component : object->components) {
                if (component.payloads.size() >= 2) ++multi_payload;
                for (const auto& field : component.fields)
                    if (!field.name.empty()) ++named_fields;

                if (component.class_name == "CollisionShape" || component.class_name == "UtilityShape") {
                    const bool has_shape = component.payload_for(120) != nullptr;
                    const bool has_collision = component.payload_for(121) != nullptr;
                    if (has_shape && has_collision) ++collision_pair;
                    if (component.field("Friction") && component.field("Friction")->payload_field == 121)
                        collision_with_friction.push_back(templ.name);
                }
                if (component.class_name == "MonsterEntity") {
                    const bool entity_slot = component.payload_for(152) != nullptr;
                    const bool monster_slot = component.payload_for(158) != nullptr;
                    if (entity_slot && monster_slot) ++monster_pair;
                    // FacingDirection lives in the *base* Entity slot. Before the
                    // multi-payload fix it was never decoded at all.
                    if (component.field("FacingDirection") &&
                        component.field("FacingDirection")->payload_field == 152)
                        monster_with_facing.push_back(templ.name);
                    check(component.field("OnKill") != nullptr,
                          "MonsterEntity has its OnKill handler: " + templ.name);
                }
                if (component.class_name == "MagicBolt" || component.class_name == "MagicBomb" ||
                    component.class_name == "MagicHookshot" || component.class_name == "FireBreath") {
                    if (component.payload_for(558) != nullptr) ++bolt_pair;
                }
            }
        }
    }

    std::printf("assets:            %s\n", asset_dir.c_str());
    std::printf("libraries:         %d loaded, %zu templates\n", loaded, libraries.template_count());
    std::printf("spawned:           %zu objects\n", spawned);
    std::printf("named fields:      %zu resolved\n", named_fields);
    std::printf("multi-payload:     %zu components carry >=2 slots\n", multi_payload);
    std::printf("shape+collision:   %zu (slots 120 and 121)\n", collision_pair);
    std::printf("entity+monster:    %zu (slots 152 and 158)\n", monster_pair);
    std::printf("spell base slot:   %zu bolts carrying 558\n", bolt_pair);

    check(spawned > 100, "the corpus produced a meaningful number of objects");
    check(multi_payload > 0, "some components carry more than one payload slot");
    check(!monster_with_facing.empty(),
          "MonsterEntity FacingDirection decoded from the base Entity slot");
    if (!monster_with_facing.empty())
        std::printf("entity slot read:  %s\n", monster_with_facing.front().c_str());
    if (!collision_with_friction.empty())
        std::printf("collision slot:    %s\n", collision_with_friction.front().c_str());

    // Every component must have been identified by ClassName: the corpus only
    // contains classes the recovered table knows.
    size_t unidentified = 0;
    for (const auto& object : scene.objects())
        for (const auto& component : object.components)
            if (!component.type) ++unidentified;
    check(unidentified == 0, "no component class left unidentified");

    if (failures) { std::printf("%d check(s) failed\n", failures); return 1; }
    std::printf("OK\n");
    return 0;
}
