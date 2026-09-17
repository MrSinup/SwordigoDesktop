// ============================================================================
// graphy_scene_builder_test.cpp — .scene / .scl  ->  Graph, headless
//
// The fixtures here are REAL Swordigo protobuf, built with the same
// `proto::Writer` the engine's own formats use and fed through the same
// `av::scene_load_bytes()` / `av::scl_load_templates()` the studio uses.
// Nothing is stubbed, so a regression in the schema, the loader or the builder
// fails here rather than on screen.
//
// The behaviour worth locking down, in order of how easy it is to get wrong:
//
//   1. Reference resolution is OBJECT-SCOPED. `hiro.scl` reuses `Identifier 101`
//      for three different components in three different objects, so a
//      file-global id map wires the wrong things. The fixture deliberately has
//      two objects that both use id 101.
//   2. `KeyframeAnimationComponent.ModelId` reaches the `ModelComponent` of its
//      own object and nothing else.
//   3. `Scene.Find("elder")` becomes a wire to that object — and only because
//      the object exists.
//   4. `ParentComponentIdentifier` is a Component-wrapper field, not a payload
//      field, and still has to be wired.
//   5. The three-level containment tree (Scene -> Entity -> Component, or
//      Library -> Template -> Component) is complete.
//   6. No two node cards overlap, measured with the canvas's own geometry
//      authority and a deterministic measurer (no font database needed).
// ============================================================================

#include "graphy_layout.h"
#include "graphy_scene_builder.h"

#include "platform/protobuf_reader.h"
#include "tools/scene_loader.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

// `src/platform/data_path.cpp` declares this weak so an application can point
// asset lookups at one game instance. Every executable that links swcore must
// supply a strong definition — `asset_viewer.cpp` and `ruby_cli.cpp` both do,
// and without it the link fails on libswcore's own unresolved reference.
std::string g_instance_assets_dir = "assets";

namespace {

using ruby::graph::Graph;
using ruby::graph::GraphBuildStyle;
using ruby::graph::LayoutMetrics;
using ruby::graph::NodeGeometry;
using ruby::graph::Node;
using ruby::graph::SceneGraphOptions;
using ruby::graph::TextMeasure;

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what) {
    ++g_checks;
    std::cout << (condition ? "  ok   " : "  FAIL ") << what << "\n";
    if (!condition) ++g_failures;
}

/// A deterministic width model (~0.62 em per glyph). The test must not depend
/// on which fonts the machine happens to have, and it is deliberately a little
/// wider than reality so elision paths get exercised.
const TextMeasure& measure() {
    static const TextMeasure m = [](const QString& text, int pixel_size,
                                    bool bold, bool italic) -> float {
        (void)italic;
        float w = static_cast<float>(text.size()) * static_cast<float>(pixel_size) * 0.62f;
        if (bold) w *= 1.15f;
        return w;
    };
    return m;
}

GraphBuildStyle style() {
    GraphBuildStyle s;
    s.layout = LayoutMetrics{};
    s.measure = measure();
    return s;
}

// ── Protobuf fixture construction ────────────────────────────────────────────
// Field numbers are the shipped binary's (Component payload ids come from the
// generated schema in `filerift.cpp`).

std::string component(const std::string& class_name, int64_t identifier,
                      int payload_field, const std::string& payload,
                      int64_t parent_identifier = 0) {
    proto::Writer w;
    w.write_string_field(1, class_name);                 // ClassName
    w.write_varint_field(2, identifier);                 // Identifier
    if (parent_identifier != 0)
        w.write_varint_field(4, parent_identifier);      // ParentComponentIdentifier
    // The payload field is written even when the payload is empty: the schema
    // resolves a component's class from its payload field number, so an absent
    // field means the class can only be guessed from ClassName.
    if (payload_field != 0) w.write_bytes_field(payload_field, payload);
    return w.to_string();
}

std::string scene_object(const std::string& template_name, const std::string& name,
                         float x, float y, float depth,
                         const std::vector<std::string>& components) {
    proto::Writer w;
    if (!template_name.empty()) w.write_string_field(1, template_name);   // TemplateName
    w.write_string_field(2, name);                                        // Identifier
    for (const auto& c : components) w.write_bytes_field(3, c);           // Component[]
    proto::Writer position;
    position.write_float_field(1, x);
    position.write_float_field(2, y);
    w.write_nested_field(4, position);                                    // Position (Vector2)
    w.write_float_field(5, depth);                                        // Depth
    return w.to_string();
}

std::string scene_message(const std::vector<std::string>& objects) {
    proto::Writer w;
    for (const auto& o : objects) w.write_bytes_field(1, o);              // Object[]
    return w.to_string();
}

std::string object_library(const std::vector<std::pair<std::string, float>>& templates) {
    proto::Writer w;
    for (const auto& [object_bytes, scaling] : templates) {
        proto::Writer entry;                                  // ObjectTemplate
        entry.write_bytes_field(1, object_bytes);             // Object
        entry.write_float_field(2, scaling);                  // Scaling
        w.write_nested_field(2, entry);
    }
    return w.to_string();
}

// ── Fixture ──────────────────────────────────────────────────────────────────
// Two objects, both using component id 101 for a different component class —
// the exact pattern of `hiro.scl`'s three `Identifier 101` components.

struct Fixture {
    std::string hero_sprite;      // 101: SpriteComponent
    std::string hero_model;       // 102: ModelComponent
    std::string hero_controller;  // 103: AnimationControllerComponent -> 102
    std::string hero_keyframe;    // 104: KeyframeAnimationComponent -> 102
    std::string hero_script;      // 105: ProgramComponent (Scene.Find("elder"))
    std::string elder_sprite;     // 101: SpriteComponent (id reused!)
    std::string portal;           // 201: PortalComponent -> SpawnPointName "spawn_a"
    std::vector<std::string> objects;
};

Fixture make_fixture() {
    Fixture f;

    proto::Writer sprite;
    sprite.write_string_field(1, "hero");                    // Name
    f.hero_sprite = component("Sprite", 101, 100, sprite.to_string());

    proto::Writer model;
    model.write_string_field(1, "char_hero");                // Name
    f.hero_model = component("Model", 102, 101, model.to_string());

    proto::Writer anim;
    anim.write_varint_field(1, 102);                         // ModelId -> 102
    f.hero_controller = component("AnimationController", 103, 149, anim.to_string());

    proto::Writer keyframe;
    keyframe.write_varint_field(1, 102);                     // ModelId -> 102
    keyframe.write_string_field(2, "walk");                  // Name
    f.hero_keyframe = component("KeyframeAnimation", 104, 102, keyframe.to_string());

    proto::Writer program;
    program.write_string_field(1,
        "function onLoad(self)\n"
        "  local elder = Scene.Find(\"elder\")\n"
        "  elder:setPosition(1, 2, 3)\n"
        "end\n");
    proto::Writer program_component;
    program_component.write_varint_field(1, 1);                   // ExecuteOnce
    program_component.write_bytes_field(2, program.to_string());   // Program
    program_component.write_varint_field(4, 10);                   // Trigger
    f.hero_script = component("Program", 105, 157, program_component.to_string());

    // Second object REUSES component id 101 — for a Sprite, not a Model.
    proto::Writer elder_sprite;
    elder_sprite.write_string_field(1, "npc_elder");
    f.elder_sprite = component("Sprite", 101, 100, elder_sprite.to_string());

    proto::Writer portal;
    portal.write_string_field(1, "town_woods");              // DestinationSceneName
    portal.write_string_field(2, "spawn_a");                 // SpawnPointName
    f.portal = component("Portal", 201, 500, portal.to_string());

    f.objects = {
        scene_object("hero_template", "hero", 919.5f, 394.5f, -29.9f,
                     {f.hero_sprite, f.hero_model, f.hero_controller,
                      f.hero_keyframe, f.hero_script}),
        scene_object("npc_template", "elder", 640.0f, 300.0f, -10.0f, {f.elder_sprite}),
        scene_object("", "spawn_a", 100.0f, 50.0f, 0.0f, {}),
        scene_object("", "gate", 200.0f, 60.0f, 1.0f, {f.portal}),
    };
    return f;
}

// ── Graph queries ────────────────────────────────────────────────────────────

std::shared_ptr<Node> find_node_by_title(const Graph& g, const QString& title) {
    for (const auto& n : g.nodes()) {
        if (n && n->title() == title) return n;
    }
    return nullptr;
}

std::string title_of_node(const Graph& g, int node_id) {
    for (const auto& n : g.nodes()) {
        if (n && n->id() == node_id) return n->title().toStdString();
    }
    return {};
}

std::string pin_name(const Graph& g, int pin_id) {
    for (const auto& n : g.nodes()) {
        if (!n) continue;
        for (const auto& p : n->inputs()) {
            if (p.id == pin_id) return p.name.toStdString();
        }
        for (const auto& p : n->outputs()) {
            if (p.id == pin_id) return p.name.toStdString();
        }
    }
    return {};
}

/// Is there a wire running from (node title, pin name) to (node title, pin name)?
bool has_wire(const Graph& g, const std::string& from_title, const std::string& from_pin_name,
              const std::string& to_title, const std::string& to_pin_name) {
    for (const auto& c : g.connections()) {
        if (title_of_node(g, c.from_node) == from_title &&
            pin_name(g, c.from_pin) == from_pin_name &&
            title_of_node(g, c.to_node) == to_title &&
            pin_name(g, c.to_pin) == to_pin_name) {
            return true;
        }
    }
    return false;
}

int count_wires_between(const Graph& g, const std::string& from_title,
                        const std::string& to_title) {
    int count = 0;
    for (const auto& c : g.connections()) {
        if (title_of_node(g, c.from_node) == from_title &&
            title_of_node(g, c.to_node) == to_title) {
            ++count;
        }
    }
    return count;
}

int count_wires_into_pin_prefix(const Graph& g, const std::string& prefix) {
    int count = 0;
    for (const auto& c : g.connections()) {
        const std::string name = pin_name(g, c.to_pin);
        if (name.rfind(prefix, 0) == 0) ++count;
    }
    return count;
}

/// Overlap check built on the canvas's own geometry authority, with the same
/// measurer the build used. Names the offenders: an overlap count alone does not
/// say which layout decision went wrong.
int count_overlapping_nodes(const Graph& g) {
    struct Entry {
        QString title;
        NodeGeometry geo;
    };
    std::vector<Entry> entries;
    for (const auto& n : g.nodes()) {
        if (!n) continue;
        entries.push_back({n->title(),
                           ruby::graph::compute_node_geometry(*n, LayoutMetrics{}, measure())});
    }
    int overlaps = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        for (size_t j = i + 1; j < entries.size(); ++j) {
            if (!entries[i].geo.card.intersects(entries[j].geo.card)) continue;
            ++overlaps;
            if (overlaps <= 6) {
                std::cout << "       overlap: " << entries[i].title.toStdString()
                          << " @(" << entries[i].geo.card.x << "," << entries[i].geo.card.y
                          << " " << entries[i].geo.card.w << "x" << entries[i].geo.card.h
                          << ") vs " << entries[j].title.toStdString()
                          << " @(" << entries[j].geo.card.x << "," << entries[j].geo.card.y
                          << " " << entries[j].geo.card.w << "x" << entries[j].geo.card.h
                          << ")\n";
            }
        }
    }
    return overlaps;
}

/// Every component-to-component wire must stay inside a single object.
///
/// Containment wires are the only wires that originate at an object node, so a
/// component's owner is recoverable from its own containment input pin. Any wire
/// whose BOTH ends are owned components must then have the same owner — that is
/// the object-scoped resolution rule stated as a graph invariant, and it is what
/// catches a file-global id map.
///
/// Only the *component*-side pin names count. An object node's own containment
/// pin is `Owner Scene` / `Library`, and counting those would put the object in
/// the owner map too, so every ordinary object -> component containment wire
/// would read as a violation.
///
/// The `hiro.scl` fixtures are a unit test of this; asserting it on every real
/// document is the sweep.
int count_cross_object_component_wires(const Graph& g) {
    std::unordered_map<int, int> owner;   // component node id -> owning object node id
    for (const auto& c : g.connections()) {
        const std::string pin = pin_name(g, c.to_pin);
        if (pin != "Owner Entity" && pin != "Owner") continue;
        owner[c.to_node] = c.from_node;
    }

    int violations = 0;
    for (const auto& c : g.connections()) {
        const auto a = owner.find(c.from_node);
        const auto b = owner.find(c.to_node);
        if (a == owner.end() || b == owner.end()) continue;   // not two components
        if (a->second != b->second) {
            ++violations;
            if (violations <= 4) {
                std::cout << "       cross-object wire: " << title_of_node(g, c.from_node)
                          << " -> " << title_of_node(g, c.to_node) << "\n";
            }
        }
    }
    return violations;
}

std::shared_ptr<Graph> build_scene(const std::string& bytes, const std::string& name) {
    return ruby::graph::build_scene_graph(
        std::vector<uint8_t>(bytes.begin(), bytes.end()), name, {}, style());
}

// ── Tests ────────────────────────────────────────────────────────────────────

void test_scene_structure() {
    std::cout << "\n[scene] structure\n";
    const Fixture f = make_fixture();
    const int object_count = static_cast<int>(f.objects.size());

    auto g = build_scene(scene_message(f.objects), "town_woods");
    if (!g) { check(false, "scene graph returned non-null"); return; }

    check(find_node_by_title(*g, "Scene: town_woods") != nullptr,
          "root node is titled by the document");
    for (const char* name : {"hero", "elder", "spawn_a", "gate"}) {
        check(find_node_by_title(*g, QString("Entity: ") + name) != nullptr,
              std::string("entity node for '") + name + "'");
    }
    check(find_node_by_title(*g, "ModelComponent") != nullptr, "ModelComponent node");
    check(find_node_by_title(*g, "KeyframeAnimationComponent") != nullptr,
          "KeyframeAnimationComponent node");
    check(find_node_by_title(*g, "Program") != nullptr,
          "the ProgramComponent became a script node");
    check(static_cast<int>(g->comments().size()) == object_count,
          "one comment frame per object");

    check(count_wires_between(*g, "Scene: town_woods", "Entity: hero") == 1,
          "root -> entity containment wire");
    check(count_wires_between(*g, "Entity: hero", "ModelComponent") == 1,
          "entity -> component containment wire");
    // 5 own components + the OnLoad/Program script node.
    check(count_wires_between(*g, "Entity: hero", "Program") == 1,
          "entity -> script node containment wire");
}

void test_component_references_are_object_scoped() {
    std::cout << "\n[scene] component references are object-scoped\n";
    const Fixture f = make_fixture();

    auto g = build_scene(scene_message(f.objects), "scoped");
    if (!g) { check(false, "scene graph returned non-null"); return; }

    check(has_wire(*g, "ModelComponent", "ID [102]", "KeyframeAnimationComponent",
                   "ModelId [102]"),
          "KeyframeAnimation.ModelId reaches the ModelComponent in its own object");
    check(has_wire(*g, "ModelComponent", "ID [102]", "AnimationControllerComponent",
                   "ModelId [102]"),
          "AnimationController.ModelId reaches the same ModelComponent");

    // Nothing may reach across objects: 'elder' has no ModelComponent at all, and
    // its reused id 101 must not resolve to hero's components.
    check(count_wires_between(*g, "Entity: elder", "KeyframeAnimationComponent") == 0,
          "no wire crosses from another object into hero's components");
    check(count_wires_between(*g, "Entity: elder", "ModelComponent") == 0,
          "the reused id 101 in 'elder' does not resolve to hero's ModelComponent");
    check(has_wire(*g, "SpriteComponent", "ID [101]", "KeyframeAnimationComponent",
                   "ModelId [102]") == false,
          "hero's SpriteComponent (also id 101) is never the target of ModelId 102");
    check(count_cross_object_component_wires(*g) == 0,
          "no component-to-component wire crosses an object boundary");
}

void test_lua_find_wire() {
    std::cout << "\n[scene] Scene.Find wiring\n";
    const Fixture f = make_fixture();

    auto g = build_scene(scene_message(f.objects), "find");
    if (!g) { check(false, "scene graph returned non-null"); return; }

    const auto script = find_node_by_title(*g, "Program");
    check(script != nullptr, "ProgramComponent produced a 'Program' node");
    if (script) {
        bool has_find_pin = false;
        for (const auto& p : script->inputs()) {
            if (p.name == QStringLiteral("Find: elder")) has_find_pin = true;
        }
        check(has_find_pin, "a 'Find: elder' input pin was created");
    }
    check(has_wire(*g, "Entity: elder", "Entity Ref", "Program", "Find: elder"),
          "the wire runs from elder's Entity Ref handle into the Find pin");

    // A `Scene.Find` for an object that does not exist asks for a wire that no
    // object can supply, so the pin exists but stays unconnected.
    proto::Writer program;
    program.write_string_field(1, "function f(self)\n  Scene.Find(\"nowhere\")\nend\n");
    proto::Writer program_component;
    program_component.write_bytes_field(2, program.to_string());
    proto::Writer lonely_sprite;
    lonely_sprite.write_string_field(1, "lonely");
    const std::string lonely = scene_message({scene_object(
        "", "lonely", 0, 0, 0,
        {component("Sprite", 1, 100, lonely_sprite.to_string()),
         component("Program", 105, 157, program_component.to_string())})});

    check(count_cross_object_component_wires(*g) == 0,
          "the Lua wire is object -> component, never component -> component");

    auto g2 = build_scene(lonely, "orphan");
    if (g2) {
        const auto s = find_node_by_title(*g2, "Program");
        bool has_pin = false;
        if (s) {
            for (const auto& p : s->inputs()) {
                if (p.name == QStringLiteral("Find: nowhere")) has_pin = true;
            }
        }
        check(has_pin, "the pin is still shown for an unresolvable name");
        check(count_wires_into_pin_prefix(*g2, "Find: ") == 0,
              "but no wire is drawn to a non-existent object");
    }
}

void test_parent_component_wire() {
    std::cout << "\n[scene] ParentComponentIdentifier (Component wrapper field 4)\n";
    proto::Writer shape;
    const std::string shape_comp = component("GroundPolygon", 980, 110, shape.to_string());
    proto::Writer gen;
    gen.write_varint_field(1, 980);   // GroundPolygonId
    // The mesh names its owning polygon through the WRAPPER field, not a payload
    // field — a different code path from every other reference in the file.
    const std::string mesh_comp =
        component("GroundMeshGenerator", 981, 112, gen.to_string(), /*parent=*/980);
    const std::string bytes =
        scene_message({scene_object("", "terrain", 0, 0, 0, {shape_comp, mesh_comp})});

    auto g = build_scene(bytes, "terrain");
    if (!g) { check(false, "scene graph returned non-null"); return; }

    check(has_wire(*g, "GroundPolygonComponent", "ID [980]", "GroundMeshGeneratorComponent",
                   "Parent Component"),
          "ParentComponentIdentifier wired to the named component");
    check(has_wire(*g, "GroundPolygonComponent", "ID [980]", "GroundMeshGeneratorComponent",
                   "GroundPolygonId [980]"),
          "and the payload reference resolves through the same object scope");
    check(count_wires_between(*g, "GroundPolygonComponent",
                              "GroundMeshGeneratorComponent") == 2,
          "the two distinct edges are two wires, not one");
}

void test_payload_pins() {
    std::cout << "\n[scene] payload pins\n";
    const Fixture f = make_fixture();

    auto g = build_scene(scene_message(f.objects), "payload");
    if (!g) { check(false, "scene graph returned non-null"); return; }

    const auto model = find_node_by_title(*g, "ModelComponent");
    if (model) {
        bool asset_pin = false;
        for (const auto& p : model->inputs()) {
            if (p.name == QStringLiteral("Asset: char_hero")) asset_pin = true;
        }
        check(asset_pin, "the model's asset name is an input pin");
    }

    const auto portal = find_node_by_title(*g, "PortalComponent");
    if (portal) {
        bool dest_pin = false, spawn_pin = false;
        for (const auto& p : portal->outputs()) {
            if (p.name == QStringLiteral("Target: town_woods")) dest_pin = true;
        }
        for (const auto& p : portal->inputs()) {
            if (p.name == QStringLiteral("SpawnPoint: spawn_a")) spawn_pin = true;
        }
        check(dest_pin, "DestinationSceneName is an OUTPUT pin (it points elsewhere)");
        check(spawn_pin, "SpawnPointName became a name-reference input pin");
    }

    check(has_wire(*g, "Entity: spawn_a", "Entity Ref", "PortalComponent",
                   "SpawnPoint: spawn_a"),
          "the portal is wired to the spawn point it names");

    // The object's own OnLoad program is separate from a ProgramComponent.
    proto::Writer program;
    program.write_string_field(1, "function onLoad(self)\nend\n");
    std::string obj = scene_object("", "solo", 0, 0, 0, {});
    {
        // SceneObject.OnLoad is tag 10 of the object itself.
        proto::Writer w;
        proto::Reader r(obj);
        proto::Field field;
        while (r.read_field(field)) w.write_field(field);
        w.write_bytes_field(10, program.to_string());
        obj = w.to_string();
    }
    auto g2 = build_scene(scene_message({obj}), "onload");
    if (g2) {
        check(find_node_by_title(*g2, "OnLoad") != nullptr,
              "SceneObject.OnLoad (tag 10) becomes an OnLoad script node");
    }
}

void test_layout_has_no_overlaps() {
    std::cout << "\n[scene] layout\n";
    const Fixture f = make_fixture();

    auto g = build_scene(scene_message(f.objects), "layout");
    if (!g) { check(false, "scene graph returned non-null"); return; }

    check(count_overlapping_nodes(*g) == 0, "no two node cards overlap");

    bool all_placed = true;
    for (const auto& n : g->nodes()) {
        if (!n) continue;
        if (n->width() <= 0.0f || n->height() <= 0.0f) all_placed = false;
    }
    check(all_placed, "every node carries its measured size (frame_all depends on it)");
}

void test_library_graph() {
    std::cout << "\n[scl] ObjectLibrary\n";
    const Fixture f = make_fixture();

    // A library template is the same SceneObject message a scene uses:
    // ObjectLibrary.field 2 = ObjectTemplate { field 1 = SceneObject, field 2 = Scaling }.
    const std::string tpl_a =
        scene_object("hero_template", "hero", 0, 0, 0, {f.hero_model, f.hero_keyframe});
    const std::string tpl_b =
        scene_object("npc_template", "elder", 0, 0, 0, {f.elder_sprite});
    const std::string bytes = object_library({{tpl_a, 1.25f}, {tpl_b, 1.0f}});

    auto g = ruby::graph::build_library_graph(
        std::vector<uint8_t>(bytes.begin(), bytes.end()), "game_common.scl", {}, style());
    if (!g) { check(false, "library graph returned non-null"); return; }

    check(find_node_by_title(*g, "Library: game_common") != nullptr, "root is the library");
    check(find_node_by_title(*g, "Template: hero") != nullptr, "template node for 'hero'");
    check(find_node_by_title(*g, "Template: elder") != nullptr, "template node for 'elder'");
    check(count_wires_between(*g, "Library: game_common", "Template: hero") == 1,
          "library -> template containment wire");
    check(count_wires_between(*g, "Template: hero", "ModelComponent") == 1,
          "template -> component containment wire");
    check(has_wire(*g, "ModelComponent", "ID [102]", "KeyframeAnimationComponent",
                   "ModelId [102]"),
          "references resolve inside a template too");
    check(count_overlapping_nodes(*g) == 0, "no two node cards overlap");

    // The scaling from the ObjectTemplate wrapper reaches the template's pins.
    const auto hero = find_node_by_title(*g, "Template: hero");
    if (hero) {
        bool scaling = false;
        for (const auto& p : hero->inputs()) {
            if (p.name == QStringLiteral("Scaling") &&
                p.default_value == QStringLiteral("1.25")) {
                scaling = true;
            }
        }
        check(scaling, "the template's scaling is shown on its node");
    }
}

void test_dispatch_and_rejection() {
    std::cout << "\n[dispatch]\n";
    check(ruby::graph::graphable_type_for_path("/x/town.scene") == "scene", ".scene is graphable");
    check(ruby::graph::graphable_type_for_path("/x/a.SCL") == "scl", ".SCL is graphable (case)");
    check(ruby::graph::graphable_type_for_path("/x/model.pod").empty(), ".pod is not graphable");
    check(!ruby::graph::is_graphable_document("/x/hero.lua"), "lua is not graphable");
    check(!ruby::graph::graph_unsupported_reason("/x/hero.lua").empty(),
          "an ungraphable document explains itself");
    check(ruby::graph::graph_unsupported_reason("/x/town.scene").empty(),
          "a graphable document has nothing to explain");

    // Garbage in must yield an empty graph, never a crash.
    const std::vector<uint8_t> junk{0x00, 0x01, 0x02, 0x03, 0xff, 0xfe};
    auto g = ruby::graph::build_graph_from_binary(junk, "/x/broken.scene", {}, style());
    check(g && g->nodes().empty(), "malformed bytes yield an empty graph");

    auto g2 = ruby::graph::build_graph_from_binary({}, "/x/empty.scl", {}, style());
    check(g2 && g2->nodes().empty(), "empty bytes yield an empty graph");

    auto g3 = ruby::graph::build_graph_from_file("/definitely/not/here.scene", {}, style());
    check(g3 && g3->nodes().empty(), "a missing file yields an empty graph");
}

void test_options_disable_edges() {
    std::cout << "\n[options]\n";
    const Fixture f = make_fixture();
    const std::string bytes = scene_message(f.objects);
    const std::vector<uint8_t> raw(bytes.begin(), bytes.end());

    SceneGraphOptions minimal;
    minimal.reference_wires = false;
    minimal.name_wires      = false;
    minimal.lua_find_wires  = false;
    minimal.comments        = false;

    auto g = ruby::graph::build_scene_graph(raw, "minimal", minimal, style());
    if (!g) { check(false, "minimal graph returned non-null"); return; }
    check(g->comments().empty(), "comments can be turned off");
    check(!has_wire(*g, "ModelComponent", "ID [102]", "KeyframeAnimationComponent",
                    "ModelId [102]"),
          "reference wires can be turned off");
    check(count_wires_between(*g, "Entity: elder", "Program") == 0,
          "the Lua Find wire is gone as well");
    // The pin is still rendered: only the resolution is disabled, so the file's
    // structure is never hidden from the user.
    const auto keyframe = find_node_by_title(*g, "KeyframeAnimationComponent");
    bool ref_pin_still_there = false;
    if (keyframe) {
        for (const auto& p : keyframe->inputs()) {
            if (p.name == QStringLiteral("ModelId [102]")) ref_pin_still_there = true;
        }
    }
    check(ref_pin_still_there, "but the reference pin still shows what the file says");
}

// ── Real assets ──────────────────────────────────────────────────────────────
// Synthetic fixtures prove the rules; the shipped documents prove the rules
// survive contact with thousand-object scenes and object libraries that
// reference each other. Skipped (loudly) when the machine has no game assets.

std::vector<std::string> find_real_documents(const char* extension, size_t limit) {
    std::vector<std::string> found;
    const char* home = std::getenv("HOME");
    if (!home) return found;

    const std::vector<std::string> roots = {
        std::string(home) + "/.local/share/swordigo-desktop/assets/resources",
        std::string(home) + "/.local/share/swordigo-desktop/mods",
    };
    for (const auto& root : roots) {
        if (!fs::is_directory(root)) continue;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file()) continue;
            if (it->path().extension() != extension) continue;
            found.push_back(it->path().string());
            if (found.size() >= limit) return found;
        }
    }
    return found;
}

void test_real_assets() {
    std::cout << "\n[real] shipped documents\n";

    struct Case {
        const char* extension;
        const char* label;
    };
    const Case cases[] = {{".scene", "scene"}, {".scl", "scl"}};

    for (const auto& c : cases) {
        const auto files = find_real_documents(c.extension, 12);
        if (files.empty()) {
            std::cout << "  (skipped) no " << c.extension << " documents on this machine\n";
            continue;
        }

        int built = 0;
        int total_nodes = 0;
        int total_wires = 0;
        int overlaps = 0;
        int clean = 0;
        int leaks = 0;
        for (const auto& path : files) {
            auto g = ruby::graph::build_graph_from_file(path, {}, style());
            if (!g || g->nodes().empty()) continue;
            ++built;
            total_nodes += static_cast<int>(g->nodes().size());
            total_wires += static_cast<int>(g->connections().size());
            const int bad = count_overlapping_nodes(*g);
            const int crossed = count_cross_object_component_wires(*g);
            overlaps += bad;
            leaks += crossed;
            if (bad == 0 && crossed == 0) ++clean;
            std::cout << "  " << ((bad == 0 && crossed == 0) ? "ok   " : "WARN ")
                      << path.substr(path.find_last_of('/') + 1) << ": "
                      << g->nodes().size() << " nodes, " << g->connections().size()
                      << " wires, " << g->comments().size() << " frames"
                      << (bad ? (" (" + std::to_string(bad) + " overlapping pairs)") : "")
                      << (crossed ? (" (" + std::to_string(crossed) + " cross-object wires)") : "")
                      << "\n";
        }

        check(built > 0, std::string("at least one real ") + c.label + " document graphs");
        check(overlaps == 0,
              std::string("every real ") + c.label + " graph laid out without overlaps (" +
              std::to_string(clean) + "/" + std::to_string(built) + " clean)");
        check(leaks == 0,
              std::string("no real ") + c.label +
              " graph has a component reference escaping its object");
        std::cout << "  ·    " << c.label << " totals: " << total_nodes << " nodes, "
                  << total_wires << " wires across " << built << " files\n";
    }
}

/// `hiro.scl` is the document that forced the object-scoped rule: it reuses
/// `Identifier 101` for three different components in three different objects
/// (`CharControllerComponent`, `AnimationControllerComponent`, `ModelComponent`
/// at the time of writing). Graph it directly rather than hoping a sweep picks
/// it up.
void test_hiro_object_scope() {
    std::cout << "\n[real] hiro.scl object scoping\n";
    const auto files = find_real_documents(".scl", 60);
    std::string hiro;
    for (const auto& path : files) {
        if (path.size() >= 8 && path.compare(path.size() - 8, 8, "hiro.scl") == 0) {
            hiro = path;
            break;
        }
    }
    if (hiro.empty()) {
        std::cout << "  (skipped) no hiro.scl on this machine\n";
        return;
    }

    auto g = ruby::graph::build_graph_from_file(hiro, {}, style());
    if (!g || g->nodes().empty()) { check(false, "hiro.scl graphs"); return; }

    // Count how many distinct objects reuse one component identifier: if the
    // fixture's premise ever stops holding, this tells us the test went stale
    // rather than silently weakening.
    std::unordered_map<std::string, int> id_users;
    for (const auto& n : g->nodes()) {
        if (!n) continue;
        for (const auto& p : n->outputs()) {
            if (p.name.startsWith(QStringLiteral("ID ["))) {
                ++id_users[p.name.toStdString()];
            }
        }
    }
    int reused_ids = 0;
    for (const auto& [pin_name_str, count] : id_users) {
        if (count > 1) ++reused_ids;
    }

    std::cout << "  " << hiro.substr(hiro.find_last_of('/') + 1) << ": "
              << g->nodes().size() << " nodes, " << g->connections().size()
              << " wires, " << reused_ids << " identifiers used by more than one component\n";

    check(reused_ids > 0, "the document really does reuse component identifiers");
    check(count_cross_object_component_wires(*g) == 0,
          "no component reference in hiro.scl escapes its own object");
    check(count_overlapping_nodes(*g) == 0, "hiro.scl lays out without overlaps");
}

} // namespace

int main() {
    std::cout << "graphy_scene_builder_test\n";
    std::cout << "=========================\n";

    test_scene_structure();
    test_component_references_are_object_scoped();
    test_lua_find_wire();
    test_parent_component_wire();
    test_payload_pins();
    test_layout_has_no_overlaps();
    test_library_graph();
    test_dispatch_and_rejection();
    test_options_disable_edges();
    test_real_assets();
    test_hiro_object_scope();

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    if (g_failures) std::cout << g_failures << " FAILED\n";
    return g_failures == 0 ? 0 : 1;
}
