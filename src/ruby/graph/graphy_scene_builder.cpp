// ============================================================================
// graphy_scene_builder.cpp — native Swordigo document -> Graphy Graph
//
// See graphy_scene_builder.h for the design rationale. In one line: this turns
// the binary a user clicked on into the node graph the canvas paints, in
// process, with no Python and no intermediate JSON.
//
// Pipeline
// --------
//   .scene / .scl bytes
//        │  av::scene_load_bytes()    (scene)
//        │  av::scl_load_templates()  (scl)
//        ▼
//   SceneData / template list
//        │  plan_unit()  — create nodes and pins, note every edge as an id pair
//        ▼
//   UnitPlan[]  (pins exist, no positions yet)
//        │  compute_node_geometry()  — the canvas's own layout authority
//        ▼
//   measured sizes -> place() -> comment frames -> wire()
//        ▼
//   Graph
//
// Why edges are planned rather than wired immediately: reference resolution is
// object-scoped and needs every component of a unit to exist before a target id
// can be looked up, while a pin's id is only known once the pin is created.
// Keeping "intent" and "resolution" in separate passes makes the two orderings
// independent — which is what lets a test call this on a real asset and assert
// on the resulting wires.
// ============================================================================

#include "graphy_scene_builder.h"

#include "tools/filerift.h"
#include "tools/lua_api_table.h"
#include "tools/scene_loader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include <QStringList>

namespace ruby::graph {

namespace {

using av::SceneComponent;
using av::SceneComponentField;
using av::SceneData;
using av::SceneObject;

// ── String helpers ───────────────────────────────────────────────────────────

bool ends_with(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::string num(float v, int decimals = 1) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, static_cast<double>(v));
    return buf;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

QString qs(const std::string& s) {
    return QString::fromUtf8(s.c_str(), static_cast<int>(s.size()));
}

// ── Raw Component-wrapper fields ─────────────────────────────────────────────
// `SceneComponent::raw_data` is the whole Component message (ClassName,
// Identifier, Label, ParentComponentIdentifier, then the payload at >= 50).
// `av::scene_component_fields()` only walks the *payload*, so the wrapper's own
// fields are read here. That is where a component's identity lives, and
// reference resolution is impossible without it.

int64_t wrapper_varint_field(const SceneComponent& c, uint32_t field_number) {
    try {
        proto::Reader reader(c.raw_data);
        proto::Field f;
        while (reader.read_field(f)) {
            if (f.field_number == field_number && f.wire_type == proto::WIRE_VARINT)
                return static_cast<int64_t>(f.varint_val);
        }
    } catch (...) {}
    return 0;
}

int64_t component_identifier(const SceneComponent& c) {
    return wrapper_varint_field(c, 2);
}

int64_t component_parent_identifier(const SceneComponent& c) {
    return wrapper_varint_field(c, 4);
}

std::string component_label(const SceneComponent& c) {
    try {
        proto::Reader reader(c.raw_data);
        proto::Field f;
        while (reader.read_field(f)) {
            if (f.field_number == 3 && f.wire_type == proto::WIRE_LEN)
                return f.bytes_val;
        }
    } catch (...) {}
    return {};
}

// ── Payload field classification ─────────────────────────────────────────────
// The rule `(Id|Ids)$` is what the binary-derived schema census produced for all
// 129 cross-reference fields across 54 component classes. It stays a rule
// instead of a hand-written 129-entry table because a table drifts the moment a
// new component is understood, and the failure mode (a missing wire) is silent.
// The exceptions all run one way — `SoundId`, `ModelBindingId`,
// `TextureMappingId` and friends index name tables rather than components — and
// resolution simply finds no target component and draws nothing.

bool is_reference_field(const std::string& name) {
    return ends_with(name, "Id") || ends_with(name, "Ids");
}

/// These names target a `ShapeComponent` and use the Byte pin colour; every
/// other reference is a plain `Int`. Derived from the census's own grouping
/// (`RoamAreaId`, `AttackAreaId`, `TriggerShapeId`, `ElevationShapeId`,
/// `BoundsShapeId`, `PolygonId`, `GroundPolygonId`).
bool is_shape_reference(const std::string& name) {
    return ends_with(name, "ShapeId") || ends_with(name, "ShapeIds") ||
           ends_with(name, "AreaId")  || ends_with(name, "AreaIds")  ||
           ends_with(name, "PolygonId");
}

PinType reference_pin_type(const std::string& name) {
    return is_shape_reference(name) ? PinType::Byte : PinType::Int;
}

/// LEN string fields that name another OBJECT in the same document.
///
/// Derived from the schema census: the `*Identifier` family (`ObjectIdentifier`
/// on scene groups, `ItemIdentifier`, `TargetBoneIdentifier`, ...) plus the
/// portal's `SpawnPointName`, which is the same idea under a different
/// spelling — the schema's own name, not a guess. `Identifier` alone is the
/// component's *own* identity, and `DestinationSceneName` names a scene rather
/// than an object, so both are excluded here.
///
/// The rule is deliberately permissive because resolution is not: a name only
/// becomes a wire when an object with that exact identifier exists in the same
/// document, so a bone or item id that happens to share the spelling draws
/// nothing.
bool is_object_name_field(const std::string& name) {
    if (name == "Identifier") return false;
    if (name == "SpawnPointName") return true;
    return ends_with(name, "Identifier");
}

/// Human label for a name reference: `SpawnPointName` -> "SpawnPoint",
/// `TargetBoneIdentifier` -> "TargetBone".
std::string name_reference_label(const std::string& field_name) {
    for (const char* suffix : {"Identifier", "Name"}) {
        const size_t n = std::strlen(suffix);
        if (field_name.size() > n && ends_with(field_name, suffix))
            return field_name.substr(0, field_name.size() - n);
    }
    return field_name;
}

const SceneComponentField* find_field(const std::vector<SceneComponentField>& fields,
                                      const char* name) {
    for (const auto& f : fields) {
        if (f.name == name) return &f;
    }
    return nullptr;
}

bool is_string_field(const SceneComponentField& f) {
    return !f.is_message && f.wire_type == proto::WIRE_LEN && !f.bytes_value.empty();
}

bool is_numeric_field(const SceneComponentField& f) {
    return f.wire_type == proto::WIRE_I32 || f.wire_type == proto::WIRE_I64;
}

float numeric_field_value(const SceneComponentField& f) {
    if (f.wire_type == proto::WIRE_I32) return f.float_value;
    if (f.wire_type == proto::WIRE_I64) return static_cast<float>(f.double_value);
    return static_cast<float>(f.varint_value);
}

/// Category label for the palette, matching the JSON converters' vocabulary so
/// existing themes and filters keep working.
///
/// Node titles use the binary-derived schema's names (`ModelComponent`,
/// `CharControllerComponent`), so the trailing `Component` is stripped before
/// matching — otherwise every entry in the table below would need a duplicate
/// spelled out.
std::string category_for_component(const std::string& class_name) {
    std::string base = class_name;
    if (base.size() > std::strlen("Component") && ends_with(base, "Component"))
        base.erase(base.size() - std::strlen("Component"));

    static const std::pair<const char*, const char*> kTable[] = {
        {"Model",               "Rendering"},
        {"Background",          "Environment"},
        {"Light",               "Lighting"},
        {"SimpleGlow",          "Lighting"},
        {"Shadow",              "Lighting"},
        {"Particle",            "FX / Particles"},
        {"ParticleEmitter",     "FX / Particles"},
        {"FireEmitter",         "FX / Particles"},
        {"ParticleObject",      "FX / Particles"},
        {"Sprite",              "Rendering"},
        {"CollisionShape",      "Collision"},
        {"UtilityShape",        "Collision"},
        {"Shape",               "Collision"},
        {"Damage",              "Collision"},
        {"Health",              "Collision"},
        {"PhysicsObject",       "Physics"},
        {"PhysicsPlatform",     "Physics"},
        {"GroundPolygon",       "Terrain & Polygon"},
        {"GroundMesh",          "Terrain & Mesh"},
        {"GroundMeshGenerator", "Terrain Generator"},
        {"WaterMesh",           "Terrain & Mesh"},
        {"TextureMapping",      "Materials & UV"},
        {"TransformController", "Transform"},
        {"ModelTransformController", "Transform"},
        {"OrbitController",     "Transform"},
        {"DoorController",      "Gameplay / Doors"},
        {"KeyframeAnimation",   "Animation"},
        {"BlendAnimation",      "Animation"},
        {"AnimationController", "Animation"},
        {"CharAnimController",  "Animation"},
        {"SoundEffect",         "Audio"},
        {"Portal",              "Gameplay / Portals"},
        {"PortalEffect",        "Gameplay / Portals"},
        {"SpawnPoint",          "Level / Spawns"},
        {"Properties",          "Properties"},
        {"Program",             "Scripting"},
        {"EntityInfo",          "Gameplay / Entities"},
        {"EntityAction",        "Gameplay / Entities"},
        {"CollectableItem",     "Gameplay / Items"},
        {"ItemDrop",            "Gameplay / Items"},
        {"BreakableObject",     "Gameplay / Props"},
    };
    for (const auto& [cls, category] : kTable) {
        if (base == cls) return category;
    }
    if (base.find("Controller") != std::string::npos) return "Controllers";
    if (base.find("Monster") != std::string::npos)    return "Gameplay / Monsters";
    if (base.find("Entity") != std::string::npos)     return "Gameplay / Entities";
    if (base.find("Magic") != std::string::npos)      return "Gameplay / Magic";
    if (base.find("Swing") != std::string::npos)      return "Gameplay / Combat";
    if (base.find("Attack") != std::string::npos)     return "Gameplay / Combat";
    if (base.find("Skill") != std::string::npos)      return "Gameplay / Combat";
    return "Components";
}

/// Comment-frame tint by object role — the same signal the JSON converters used,
/// so a scene still reads the same way at a glance.
QColor frame_color_for(const std::string& identifier, const std::string& template_name,
                       const std::vector<std::string>& classes) {
    auto has = [&classes](const char* needle) {
        for (const auto& c : classes) {
            if (c.find(needle) != std::string::npos) return true;
        }
        return false;
    };
    auto ident_has = [&identifier](const char* needle) {
        return identifier.find(needle) != std::string::npos;
    };

    if (has("Background") || ident_has("background"))            return QColor(30, 58, 95, 140);
    if (has("Portal") || ident_has("portal"))                    return QColor(45, 30, 95, 140);
    if (has("SpawnPoint") || ident_has("spawn"))                 return QColor(30, 95, 56, 140);
    if (has("Light") || has("SimpleGlow") || ident_has("torch")) return QColor(90, 67, 28, 140);
    if (has("CollisionShape") || has("Program"))                 return QColor(95, 30, 30, 140);
    if (has("GroundMesh") || has("GroundPolygon") || has("GroundMeshGenerator"))
        return QColor(30, 77, 53, 140);
    if (has("Entity") || template_name.find("npc") != std::string::npos ||
        ident_has("npc") || ident_has("elder"))                  return QColor(95, 30, 75, 140);
    return QColor(40, 53, 66, 140);
}

// ── Lua: the objects a script reaches for ────────────────────────────────────
// Only `Scene.Find("name")` is treated as an object reference. The name is
// cross-checked against the extracted Lua API table, so a call the shipped
// engine never publishes cannot invent an edge.

bool scene_find_is_a_real_api_function() {
    for (const auto& fn : rbsrc::kGameLuaApi) {
        if (fn.lua_namespace == "Scene" && fn.name == "Find") return true;
    }
    return false;
}

const std::regex& scene_find_regex() {
    static const std::regex re(R"(Scene\s*[.:]\s*Find\s*\(\s*["']([^"'\r\n]{1,128})["'])");
    return re;
}

void collect_lua_sources(const std::vector<SceneComponentField>& fields,
                         std::vector<std::string>& out) {
    for (const auto& f : fields) {
        if (!f.is_message) continue;
        if (f.class_name != "Program" && f.name != "Program") continue;
        const std::string src = av::scene_program_source(f.bytes_value);
        if (!src.empty()) out.push_back(src);
    }
}

// ── Plan structures ──────────────────────────────────────────────────────────

struct CompPlan {
    int         node_id    = 0;
    int         out_pin    = 0;   // the wireable identity of this component
    int64_t     identifier = 0;
    std::string class_name;
    bool        is_script  = false;

    std::vector<std::pair<int64_t, int>>     ref_targets;   // target component id -> pin
    std::vector<std::pair<std::string, int>> name_targets;  // named object -> pin
    std::vector<std::pair<std::string, int>> find_targets;  // Scene.Find name -> pin

    float w = 190.0f;
    float h = 80.0f;
};

struct UnitPlan {
    int         node_id   = 0;
    int         flow_pin  = 0;   // "Components" delegate output
    int         ref_pin   = 0;   // "Entity Ref" / "Template Ref" object output
    int         owner_pin = 0;   // "Owner Scene" / "Library" delegate input
    std::string identifier;
    std::string template_name;
    std::vector<CompPlan> comps;
    float w = 190.0f;
    float h = 80.0f;
    float comp_w = 0.0f;
    float comps_h = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
};

// ── The planner ──────────────────────────────────────────────────────────────

class Planner {
public:
    Planner(const SceneGraphOptions& opts, const GraphBuildStyle& style)
        : m_opts(opts), m_style(style),
          m_measure(style.measure ? style.measure : default_text_measure()) {}

    std::shared_ptr<Graph> graph() const { return m_graph; }

    void begin(const QString& title, const QString& subtitle, const QString& category,
               const QString& child_label, int child_count) {
        m_graph = std::make_shared<Graph>();
        m_root = m_graph->create_node(title, category, 0.0f, 0.0f);
        m_root->set_subtitle(subtitle);
        m_root->set_flags(NodeFlags::Event);

        const QString count_name = child_label.endsWith('s')
            ? child_label.left(child_label.size() - 1) + QStringLiteral(" Count")
            : child_label + QStringLiteral(" Count");
        m_root->add_input(m_graph->next_pin_id(), count_name, PinType::Int,
                          QString::number(child_count));

        m_flow_pin = m_graph->next_pin_id();
        m_root->add_output(m_flow_pin, child_label, PinType::Delegate);
    }

    void root_input(const QString& name, PinType type, const QString& value) {
        m_root->add_input(m_graph->next_pin_id(), name, type, value);
    }
    void root_output(const QString& name, PinType type) {
        m_root->add_output(m_graph->next_pin_id(), name, type);
    }

    // ── Plan one SceneObject / template ──────────────────────────────────────
    UnitPlan plan_unit(const SceneObject& obj, bool is_template) {
        UnitPlan unit;
        unit.identifier    = obj.name;
        unit.template_name = obj.template_name;

        // Prefer the object's own components, but fall back to the resolved
        // (template-inherited) set so a pure template instance is not an empty
        // card. The subtitle says which one you are looking at.
        const std::vector<SceneComponent>* comps = &obj.components;
        bool inherited = false;
        if (comps->empty() && !obj.resolved_components.empty()) {
            comps = &obj.resolved_components;
            inherited = true;
        }

        const QString title = is_template
            ? QStringLiteral("Template: ") + qs(unit.identifier)
            : QStringLiteral("Entity: ") + qs(unit.identifier);
        auto node = m_graph->create_node(
            title, is_template ? QStringLiteral("Level / Archetype")
                               : QStringLiteral("Level / Entity"),
            0.0f, 0.0f);
        node->set_flags(NodeFlags::Event);
        unit.node_id = node->id();

        QStringList subtitle;
        if (is_template) {
            subtitle << QStringLiteral("Scaling: ") + qs(num(obj.template_scaling, 2));
        } else {
            if (!unit.template_name.empty())
                subtitle << QStringLiteral("Tpl: ") + qs(unit.template_name);
            subtitle << QStringLiteral("Z: ") + qs(num(obj.pos_z, 2));
        }
        subtitle << QStringLiteral("%1 components").arg(static_cast<int>(comps->size()));
        if (inherited)  subtitle << QStringLiteral("inherited");
        if (obj.hidden) subtitle << QStringLiteral("hidden");
        node->set_subtitle(subtitle.join(QStringLiteral(" \u00b7 ")));

        if (is_template) {
            node->add_input(m_graph->next_pin_id(), "Scaling", PinType::Float,
                            qs(num(obj.template_scaling, 2)));
        } else {
            node->add_input(m_graph->next_pin_id(), "Position", PinType::Vector3,
                            qs(num(obj.pos_x)) + ", " + qs(num(obj.pos_y)) + ", " +
                            qs(num(obj.pos_z, 2)));
            node->add_input(m_graph->next_pin_id(), "Rotation / Scale", PinType::Rotator,
                            qs(num(obj.rot_y, 3)) + " rad \u00b7 x" + qs(num(obj.scale_x, 2)));
            if (obj.hidden) {
                node->add_input(m_graph->next_pin_id(), "Hidden", PinType::Boolean,
                                QStringLiteral("true"));
            }
        }

        unit.owner_pin = m_graph->next_pin_id();
        node->add_input(unit.owner_pin, is_template ? "Library" : "Owner Scene",
                        PinType::Delegate);

        unit.flow_pin = m_graph->next_pin_id();
        node->add_output(unit.flow_pin, "Components", PinType::Delegate);

        unit.ref_pin = m_graph->next_pin_id();
        node->add_output(unit.ref_pin, is_template ? "Template Ref" : "Entity Ref",
                         PinType::Object);

        for (const auto& comp : *comps) unit.comps.push_back(plan_component(comp));

        // The object's own OnLoad program is a node too, so Lua has a home in
        // the graph instead of being invisible.
        if (m_opts.script_nodes) {
            const std::string src = av::scene_program_source(obj.onload);
            if (!src.empty()) {
                unit.comps.push_back(plan_script_node(src, QStringLiteral("OnLoad"),
                                                      QStringLiteral("SceneObject.OnLoad"), 0));
            }
        }

        // `ParentComponentIdentifier` is a component-level field, not a payload
        // one, so it is wired here rather than in plan_component().
        for (size_t i = 0; i < comps->size() && i < unit.comps.size(); ++i) {
            const int64_t parent = component_parent_identifier((*comps)[i]);
            if (parent == 0) continue;
            auto child = m_graph->find_node(unit.comps[i].node_id);
            if (!child) continue;
            const int pin = m_graph->next_pin_id();
            child->add_input(pin, QStringLiteral("Parent Component"), PinType::Int,
                             QString::number(parent));
            unit.comps[i].ref_targets.emplace_back(parent, pin);
        }

        measure(unit);
        return unit;
    }

    // ── Layout ───────────────────────────────────────────────────────────────
    void place(std::vector<UnitPlan>& units) {
        if (!m_root) return;

        // The root must be measured BEFORE it is used as a column offset: it is
        // created with the default card size and only `measure_unit()` retunes a
        // unit, so an unmeasured root would place the entity column inside
        // itself whenever the root's real width exceeds its default.
        {
            const NodeGeometry geo = compute_node_geometry(*m_root, m_style.layout, m_measure);
            m_root->set_size(geo.card.w, geo.card.h);
        }

        const float entity_x = m_style.origin_x + m_root->width() + m_style.root_gap;
        float entity_col_w = 0.0f;
        for (const auto& u : units) entity_col_w = std::max(entity_col_w, u.w);
        const float comp_x = entity_x + entity_col_w + m_style.component_gap;

        float y = m_style.origin_y;
        for (auto& unit : units) {
            if (auto node = m_graph->find_node(unit.node_id)) node->set_pos(entity_x, y);
            unit.x = entity_x;
            unit.y = y;

            float cy = y;
            for (const auto& comp : unit.comps) {
                if (auto cn = m_graph->find_node(comp.node_id)) cn->set_pos(comp_x, cy);
                cy += comp.h + m_style.node_gap;
            }

            const float block_h = std::max(unit.h, unit.comps_h);
            if (m_opts.comments) {
                const float fx = entity_x - m_style.frame_pad;
                const float fy = y - m_style.frame_title_h;
                const float fw = (comp_x + unit.comp_w + m_style.frame_pad) - fx;
                const float fh = block_h + m_style.frame_title_h + m_style.frame_pad;

                std::vector<std::string> classes;
                classes.reserve(unit.comps.size());
                for (const auto& c : unit.comps) classes.push_back(c.class_name);

                QString frame_title = QStringLiteral("Object: ") + qs(unit.identifier);
                if (!unit.template_name.empty())
                    frame_title += QStringLiteral(" [") + qs(unit.template_name) + QStringLiteral("]");
                m_graph->create_comment(frame_title, fx, fy, fw, fh)
                    .color = frame_color_for(unit.identifier, unit.template_name, classes);
            }

            y += block_h + m_style.block_gap;
        }

        if (auto root = m_graph->find_node(m_root->id()))
            root->set_pos(m_style.origin_x, m_style.origin_y);
    }

    // ── Resolution ───────────────────────────────────────────────────────────
    void wire(const std::vector<UnitPlan>& units,
              const std::unordered_map<std::string, std::pair<int, int>>& names) {
        for (const auto& unit : units) {
            if (m_opts.containment_wires) {
                m_graph->connect(m_flow_pin, unit.owner_pin);
                for (const auto& comp : unit.comps) {
                    const int owner = owner_pin_of(comp.node_id);
                    if (owner != 0) m_graph->connect(unit.flow_pin, owner);
                }
            }

            // Component references are object-scoped: a target id resolves only
            // against the other components of the SAME object. `hiro.scl`
            // reuses `Identifier 101` for three different components in three
            // different objects, so a file-global map would wire them wrongly.
            if (m_opts.reference_wires) {
                std::unordered_map<int64_t, int> out_pins;
                for (const auto& comp : unit.comps) {
                    if (comp.identifier != 0 && !comp.is_script)
                        out_pins[comp.identifier] = comp.out_pin;
                }
                for (const auto& comp : unit.comps) {
                    for (const auto& [target, to_pin] : comp.ref_targets) {
                        const auto it = out_pins.find(target);
                        if (it == out_pins.end()) continue;
                        m_graph->connect(it->second, to_pin);
                    }
                }
            }

            if (m_opts.name_wires) {
                for (const auto& comp : unit.comps) {
                    for (const auto& [name, to_pin] : comp.name_targets) {
                        const auto it = names.find(name);
                        if (it == names.end()) continue;
                        m_graph->connect(it->second.second, to_pin);
                    }
                }
            }

            if (m_opts.lua_find_wires) {
                for (const auto& comp : unit.comps) {
                    for (const auto& [name, to_pin] : comp.find_targets) {
                        const auto it = names.find(name);
                        if (it == names.end()) continue;
                        m_graph->connect(it->second.second, to_pin);
                    }
                }
            }

        }
    }

private:
    int owner_pin_of(int node_id) const {
        const auto it = m_owner_pins.find(node_id);
        return it == m_owner_pins.end() ? 0 : it->second;
    }

    CompPlan plan_component(const SceneComponent& comp) {
        CompPlan plan;
        plan.identifier = component_identifier(comp);
        plan.class_name = av::scene_component_class_name(comp);
        if (plan.class_name.empty()) plan.class_name = comp.type_name;
        if (plan.class_name.empty()) plan.class_name = "Component";

        const std::string label = component_label(comp);
        const auto fields = av::scene_component_fields(comp);

        // A ProgramComponent is a script with a wrapper: graph it as one.
        const SceneComponentField* program = find_field(fields, "Program");
        if (m_opts.script_nodes && program && program->is_message) {
            const std::string src = av::scene_program_source(program->bytes_value);
            if (!src.empty()) {
                QString subtitle = QStringLiteral("ID: %1 \u00b7 ProgramComponent")
                                       .arg(plan.identifier);
                if (const auto* trigger = find_field(fields, "Trigger")) {
                    if (trigger->wire_type == proto::WIRE_VARINT && trigger->varint_value != 0) {
                        subtitle += QStringLiteral(" \u00b7 Trigger %1")
                                        .arg(static_cast<qulonglong>(trigger->varint_value));
                    }
                }
                return plan_script_node(src, QStringLiteral("Program"), subtitle,
                                        plan.identifier);
            }
        }

        QString subtitle = QStringLiteral("ID: %1").arg(plan.identifier);
        if (!label.empty()) subtitle += QStringLiteral(" \u00b7 ") + qs(label);

        auto node = m_graph->create_node(qs(plan.class_name),
                                         qs(category_for_component(plan.class_name)),
                                         0.0f, 0.0f);
        node->set_subtitle(subtitle);
        plan.node_id = node->id();

        const int owner = m_graph->next_pin_id();
        node->add_input(owner, "Owner Entity", PinType::Delegate);
        m_owner_pins[plan.node_id] = owner;

        plan.out_pin = m_graph->next_pin_id();
        node->add_output(plan.out_pin, QStringLiteral("ID [%1]").arg(plan.identifier),
                         PinType::Int);

        if (m_opts.payload_pins) {
            if (const auto* f = find_field(fields, "Name"); f && is_string_field(*f)) {
                node->add_input(m_graph->next_pin_id(), QStringLiteral("Asset: ") + qs(f->bytes_value),
                                PinType::String, qs(f->bytes_value));
            }
            if (const auto* f = find_field(fields, "TextureName"); f && is_string_field(*f)) {
                node->add_input(m_graph->next_pin_id(), QStringLiteral("Tex: ") + qs(f->bytes_value),
                                PinType::String, qs(f->bytes_value));
            }
            if (const auto* f = find_field(fields, "Intensity"); f && is_numeric_field(*f)) {
                node->add_input(m_graph->next_pin_id(), QStringLiteral("Intensity"),
                                PinType::Float, qs(num(numeric_field_value(*f), 2)));
            }
            // A destination points elsewhere, so it is an output, not an input.
            if (const auto* f = find_field(fields, "DestinationSceneName");
                f && is_string_field(*f)) {
                node->add_output(m_graph->next_pin_id(),
                                 QStringLiteral("Target: ") + qs(f->bytes_value),
                                 PinType::String);
                subtitle += QStringLiteral(" \u2192 ") + qs(f->bytes_value);
            }
        }

        // Reference pins are created unconditionally: a `ModelId` is part of what
        // the file says, and hiding it would hide the document's own content.
        // The options only decide whether the pins get RESOLVED into wires.
        for (const auto& f : fields) {
            if (!is_reference_field(f.name)) continue;
            if (f.wire_type != proto::WIRE_VARINT) continue;
            const int64_t target = static_cast<int64_t>(f.varint_value);
            if (target == 0) continue;
            // A field may repeat with different targets (`hiro.scl` has three
            // `SwingComponentId` values), and each one is a real edge — so the
            // pin name carries the target id.
            const int pin = m_graph->next_pin_id();
            node->add_input(pin, qs(f.name) + QStringLiteral(" [%1]").arg(target),
                            reference_pin_type(f.name), QString::number(target));
            plan.ref_targets.emplace_back(target, pin);
        }

        for (const auto& f : fields) {
            if (!is_string_field(f)) continue;
            if (!is_object_name_field(f.name)) continue;
            const std::string target = f.bytes_value;
            if (target.empty()) continue;
            const int pin = m_graph->next_pin_id();
            node->add_input(pin,
                            qs(name_reference_label(f.name)) + QStringLiteral(": ") + qs(target),
                            PinType::Object, qs(target));
            plan.name_targets.emplace_back(target, pin);
        }

        std::vector<std::string> lua;
        collect_lua_sources(fields, lua);
        for (const auto& src : lua) add_find_targets_to(plan, node, src);
        if (!lua.empty() && m_opts.script_nodes)
            subtitle += QStringLiteral(" \u00b7 lua");

        node->set_subtitle(subtitle);
        return plan;
    }

    CompPlan plan_script_node(const std::string& source, const QString& title,
                              const QString& subtitle, int64_t identifier) {
        CompPlan plan;
        plan.is_script  = true;
        plan.identifier = identifier;

        auto node = m_graph->create_node(title, QStringLiteral("Scripting / Lua"), 0.0f, 0.0f);
        node->set_subtitle(subtitle);
        plan.node_id = node->id();

        const int owner = m_graph->next_pin_id();
        node->add_input(owner, "Owner", PinType::Delegate);
        m_owner_pins[plan.node_id] = owner;

        int lines = 1;
        for (char c : source) {
            if (c == '\n') ++lines;
        }
        if (m_opts.payload_pins) {
            node->add_input(m_graph->next_pin_id(), "Source", PinType::Text,
                            QStringLiteral("%1 lines").arg(lines));
        }

        if (identifier != 0) {
            plan.out_pin = m_graph->next_pin_id();
            node->add_output(plan.out_pin, QStringLiteral("ID [%1]").arg(identifier),
                             PinType::Int);
        } else {
            plan.out_pin = m_graph->next_pin_id();
            node->add_output(plan.out_pin, "Chunk", PinType::Delegate);
        }

        add_find_targets_to(plan, node, source);
        return plan;
    }

    /// Scan a Lua source for `Scene.Find("name")` and surface each distinct name
    /// as an Object input pin. The pin exists whether or not a matching object
    /// exists; `wire()` is what decides whether it can be connected.
    void add_find_targets_to(CompPlan& plan, const std::shared_ptr<Node>& node,
                             const std::string& source) {
        if (!scene_find_is_a_real_api_function()) return;
        std::unordered_set<std::string> seen;
        for (std::sregex_iterator it(source.begin(), source.end(), scene_find_regex()), end;
             it != end; ++it) {
            const std::string name = (*it)[1].str();
            if (name.empty() || !seen.insert(name).second) continue;
            const int pin = m_graph->next_pin_id();
            node->add_input(pin, QStringLiteral("Find: ") + qs(name), PinType::Object, qs(name));
            plan.find_targets.emplace_back(name, pin);
        }
    }

    /// Size every node with the canvas's own layout authority, so spacing comes
    /// from measured text instead of a guessed pitch.
    void measure(UnitPlan& unit) {
        if (auto entity = m_graph->find_node(unit.node_id)) {
            const NodeGeometry geo = compute_node_geometry(*entity, m_style.layout, m_measure);
            entity->set_size(geo.card.w, geo.card.h);
            unit.w = geo.card.w;
            unit.h = geo.card.h;
        }

        float stacked = 0.0f;
        for (auto& comp : unit.comps) {
            auto node = m_graph->find_node(comp.node_id);
            if (!node) continue;
            const NodeGeometry geo = compute_node_geometry(*node, m_style.layout, m_measure);
            node->set_size(geo.card.w, geo.card.h);
            comp.w = geo.card.w;
            comp.h = geo.card.h;
            unit.comp_w = std::max(unit.comp_w, geo.card.w);
            stacked += geo.card.h + m_style.node_gap;
        }
        unit.comps_h = unit.comps.empty() ? 0.0f : stacked - m_style.node_gap;
    }

    SceneGraphOptions      m_opts;
    GraphBuildStyle        m_style;
    TextMeasure            m_measure;
    std::shared_ptr<Graph> m_graph;
    std::shared_ptr<Node>  m_root;
    int                    m_flow_pin = 0;
    std::unordered_map<int, int> m_owner_pins;
};

// ── Shared assembly ──────────────────────────────────────────────────────────

std::shared_ptr<Graph> build_units(const std::vector<SceneObject>& objects,
                                  bool is_template_set,
                                  const std::string& document_name,
                                  const SceneData* scene,
                                  const SceneGraphOptions& opts,
                                  const GraphBuildStyle& style) {
    Planner planner(opts, style);

    const QString root_title = is_template_set
        ? QStringLiteral("Library: ") + qs(document_name)
        : QStringLiteral("Scene: ") + qs(document_name);
    const QString root_subtitle = is_template_set
        ? QStringLiteral("%1 templates").arg(static_cast<int>(objects.size()))
        : QStringLiteral("%1 objects").arg(static_cast<int>(objects.size()));

    planner.begin(root_title, root_subtitle,
                  is_template_set ? QStringLiteral("Level / Archetype Library")
                                  : QStringLiteral("Level / Environment"),
                  is_template_set ? QStringLiteral("Templates") : QStringLiteral("Objects"),
                  static_cast<int>(objects.size()));

    if (scene) {
        // Two compact pins rather than one long string: a single
        // "X: 0.0 Y: 0.0 [300.0 x 150.0]" value stretches the root card to the
        // layout's maximum width, which then pushes the whole graph right.
        av::CameraBounds cb;
        float origin_x = scene->bounds_min[0];
        float origin_y = scene->bounds_min[1];
        float width    = scene->bounds_max[0] - scene->bounds_min[0];
        float height   = scene->bounds_max[1] - scene->bounds_min[1];
        if (av::scene_get_camera_bounds(*scene, cb)) {
            origin_x = cb.x;
            origin_y = cb.y;
            width    = cb.width;
            height   = cb.height;
        }
        planner.root_input(QStringLiteral("Camera Origin"), PinType::Vector3,
                           qs(num(origin_x, 1)) + ", " + qs(num(origin_y, 1)));
        planner.root_input(QStringLiteral("Camera Bounds"), PinType::Vector3,
                           qs(num(width, 1)) + " x " + qs(num(height, 1)));
        for (const auto& lib : scene->imported_library_names)
            planner.root_output(QStringLiteral("Library: ") + qs(lib), PinType::Object);
    }

    std::vector<UnitPlan> units;
    units.reserve(objects.size());
    for (const auto& obj : objects) units.push_back(planner.plan_unit(obj, is_template_set));

    // Cross-object resolution table. The first object with a given identifier
    // wins, matching the engine's name lookup for `Scene.Find`.
    std::unordered_map<std::string, std::pair<int, int>> names;
    for (const auto& unit : units) {
        if (unit.identifier.empty()) continue;
        names.emplace(unit.identifier, std::make_pair(unit.node_id, unit.ref_pin));
    }

    planner.place(units);
    planner.wire(units, names);
    return planner.graph();
}

// ── Document loading ─────────────────────────────────────────────────────────

std::string read_file_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool looks_like_markup(const std::string& bytes) {
    return bytes.rfind("## FileRift", 0) == 0 || bytes.rfind("## Filerift", 0) == 0;
}

std::string strip_banner(const std::string& markup) {
    std::string out = markup;
    for (;;) {
        if (out.rfind("## FileRift", 0) != 0 && out.rfind("## Filerift", 0) != 0) break;
        const size_t nl = out.find('\n');
        if (nl == std::string::npos) { out.clear(); break; }
        out.erase(0, nl + 1);
        const size_t first = out.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) { out.clear(); break; }
        out.erase(0, first);
    }
    return out;
}

/// Text -> binary for one FileRift type. Binary input passes through unchanged.
std::string to_binary(const std::string& bytes, const std::string& type) {
    if (!looks_like_markup(bytes)) return bytes;
    if (type.empty()) return {};
    if (!::filerift::is_supported_filetype(type)) return {};
    return ::filerift::recode_markup(strip_banner(bytes), type);
}

std::string stem_of(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = base.find_last_of('.');
    return dot == std::string::npos ? base : base.substr(0, dot);
}

} // namespace

// ════════════════════════════════════════════════════════════════════════════
//  Document classification
// ════════════════════════════════════════════════════════════════════════════

std::string graphable_type_for_path(const std::string& path) {
    // The extension is authoritative for the two documents this builder knows,
    // so classification can never depend on sniffing. `detect_filetype` is only
    // consulted for extensionless paths.
    const size_t slash = path.find_last_of("/\\");
    const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = base.find_last_of('.');
    const std::string ext = dot == std::string::npos ? std::string() : lower(base.substr(dot + 1));

    if (ext == "scene" || ext == "scn") return "scene";
    if (ext == "scl") return "scl";

    const std::string detected = ::filerift::detect_filetype(path);
    if (detected == "scene" || detected == "scl") return detected;
    return {};
}

bool is_graphable_document(const std::string& path) {
    return !graphable_type_for_path(path).empty();
}

std::string graph_unsupported_reason(const std::string& path) {
    if (is_graphable_document(path)) return {};

    const size_t slash = path.find_last_of("/\\");
    const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = base.find_last_of('.');

    if (dot == std::string::npos) {
        return "The Node Graph reads Swordigo scene (.scene) and object-library "
               "(.scl) documents, and this file has no extension to identify it.";
    }
    return "The Node Graph reads Swordigo scene (.scene) and object-library "
           "(.scl) documents, not ." + base.substr(dot + 1) + " files.\n"
           "Open a scene to see its objects, components and cross-references.";
}

// ════════════════════════════════════════════════════════════════════════════
//  Builders
// ════════════════════════════════════════════════════════════════════════════

std::shared_ptr<Graph> build_scene_graph(const std::vector<uint8_t>& scene_bytes,
                                         const std::string& document_name,
                                         const SceneGraphOptions& opts,
                                         const GraphBuildStyle& style) {
    if (scene_bytes.empty()) return std::make_shared<Graph>();

    SceneData scene;
    try {
        scene = av::scene_load_bytes(scene_bytes, document_name);
    } catch (...) {
        return std::make_shared<Graph>();
    }
    if (scene.objects.empty()) return std::make_shared<Graph>();

    return build_units(scene.objects, /*is_template_set=*/false,
                       stem_of(document_name), &scene, opts, style);
}

std::shared_ptr<Graph> build_library_graph(const std::vector<uint8_t>& scl_bytes,
                                           const std::string& document_name,
                                           const SceneGraphOptions& opts,
                                           const GraphBuildStyle& style) {
    if (scl_bytes.empty()) return std::make_shared<Graph>();

    std::vector<av::SclTemplateEntry> templates;
    try {
        templates = av::scl_load_templates(std::string(scl_bytes.begin(), scl_bytes.end()));
    } catch (...) {
        return std::make_shared<Graph>();
    }
    if (templates.empty()) return std::make_shared<Graph>();

    std::vector<SceneObject> objects;
    objects.reserve(templates.size());
    for (auto& entry : templates) {
        SceneObject obj = entry.object;
        if (obj.name.empty()) obj.name = entry.name;
        obj.template_scaling = entry.scaling;
        objects.push_back(std::move(obj));
    }

    return build_units(objects, /*is_template_set=*/true,
                       stem_of(document_name), nullptr, opts, style);
}

std::shared_ptr<Graph> build_graph_from_binary(const std::vector<uint8_t>& bytes,
                                               const std::string& path,
                                               const SceneGraphOptions& opts,
                                               const GraphBuildStyle& style) {
    const std::string type = graphable_type_for_path(path);
    if (type == "scene") return build_scene_graph(bytes, path, opts, style);
    if (type == "scl")   return build_library_graph(bytes, path, opts, style);
    return std::make_shared<Graph>();
}

std::shared_ptr<Graph> build_graph_from_markup(const std::string& markup,
                                               const std::string& path,
                                               const SceneGraphOptions& opts,
                                               const GraphBuildStyle& style) {
    const std::string type = graphable_type_for_path(path);
    if (type.empty()) return std::make_shared<Graph>();
    const std::string binary = to_binary(markup, type);
    if (binary.empty()) return std::make_shared<Graph>();
    return build_graph_from_binary(std::vector<uint8_t>(binary.begin(), binary.end()),
                                   path, opts, style);
}

std::shared_ptr<Graph> build_graph_from_file(const std::string& path,
                                             const SceneGraphOptions& opts,
                                             const GraphBuildStyle& style) {
    const std::string type = graphable_type_for_path(path);
    if (type.empty()) return std::make_shared<Graph>();
    const std::string raw = read_file_bytes(path);
    if (raw.empty()) return std::make_shared<Graph>();
    const std::string binary = to_binary(raw, type);
    if (binary.empty()) return std::make_shared<Graph>();
    return build_graph_from_binary(std::vector<uint8_t>(binary.begin(), binary.end()),
                                   path, opts, style);
}

} // namespace ruby::graph
