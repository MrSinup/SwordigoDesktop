#pragma once
// runtime.h — the live object/component runtime.
//
// `RuntimeScene` is the caver equivalent of `Caver::Scene` + its SceneObjects:
// identifiers are unique, components are wired by object-local Identifier
// references, and `update(dt)` ticks the behaviours recovered so far.
//
// It is deliberately headless: it owns no GL, no Qt, and no viewport state. The
// renderer consumes `ObjectRenderState` produced here (same split as the
// existing sp::PlayObject bridge, which this replaces rather than extends).

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "platform/protobuf_reader.h"
#include "ruby/caver/behaviour.h"
#include "ruby/caver/component.h"
#include "tools/scene_loader.h"

namespace caver {

class LibraryManager;
class ProgramHost;

// A resolved cross-component reference. `*Id` fields address another component's
// Identifier on the SAME object (LightId, ParticleEmitterId, ModelId, …).
struct ComponentRef {
    uint32_t    tag = 0;          // the field's protobuf tag in the payload
    std::string field_name;       // "LightId"
    int32_t     target = 0;       // referenced component Identifier
    bool        resolved = false; // the target exists on this object
};

// Field is a decoded payload field with its recovered schema name (empty when
// the field is newer than the schema — those are preserved, never dropped).
struct RuntimeField {
    uint32_t        tag = 0;
    proto::WireType wire = proto::WIRE_VARINT;
    std::string     name;
    uint64_t        varint_value = 0;
    double          double_value = 0.0;
    float           float_value = 0.0f;
    std::string     bytes_value;
};

struct RuntimeComponent {
    const ComponentType* type = nullptr;   // recovered metadata (may be null: newer version)
    std::string class_name;                // as written in the file ("UtilityShape")
    int32_t     identifier = 0;
    std::string label;
    int32_t     parent_identifier = 0;
    std::string raw;                       // the whole Component message, verbatim
    std::string payload;                   // the payload submessage bytes
    std::vector<RuntimeField> fields;      // decoded, schema-named
    std::vector<ComponentRef> refs;        // resolved id references

    bool is(const std::string& cls) const;             // class_name or long name match
    const RuntimeField* field(const std::string& name) const;
    float number(const std::string& name, float fallback = 0.0f) const;
};

// A Lua program embedded in a component (Program{String, Bytes}) — either the
// ProgramComponent's own program or an event handler field (OnKill, OnLoad …).
struct RuntimeProgram {
    int32_t     owner_identifier = 0;
    std::string owner_class;
    std::string field_name;
    std::string source;         // Program.String  (field 1)
    std::string bytecode;       // Program.Bytes   (field 2 — what the engine runs)
    bool        keep_active = false;   // `while true do … Program.Wait` loop
};

struct RuntimeObject {
    std::string identifier;              // unique (MakeUniqueObjectIdentifier parity)
    std::string template_name;
    float pos[3]   = {0, 0, 0};
    float base_pos[3] = {0, 0, 0};       // authored placement (never mutated)
    float rot[3]   = {0, 0, 0};
    float scale[3] = {1, 1, 1};
    float template_scaling = 1.0f;
    bool  hidden = false;
    bool  activated = false;             // Scene::ActivateObject gate
    // Physics integration state (driven by PhysicsObjectComponent).
    float vel[3] = {0, 0, 0};
    bool  grounded = true;
    bool  on_ground = true;
    int   facing = 1;                    // EntityComponent.FacingDirection sign
    // The recovered per-component behaviour state (see behaviour.h).
    BehaviourState behaviour;
    std::vector<RuntimeComponent> components;
    std::vector<RuntimeProgram>   programs;

    RuntimeComponent*       find(int32_t component_identifier);
    const RuntimeComponent* find(int32_t component_identifier) const;
    RuntimeComponent*       find_class(const std::string& class_name);
    const RuntimeComponent* find_class(const std::string& class_name) const;
};

// What the renderer needs per object after a tick. Data only — the viewport
// applies it, it never mutates the runtime.
struct ObjectRenderState {
    std::string identifier;
    float pos[3]   = {0, 0, 0};
    float rot_y    = 0.0f;
    float scale    = 1.0f;
    bool  hidden   = false;
    float anim_time = 0.0f;      // KeyframeAnimation / AnimationController clock
    float emitter_time = 0.0f;   // ParticleEmitter / FireEmitter clock
    float light_pulse = 1.0f;    // SimpleGlow / Light intensity multiplier
    float water_time = 0.0f;     // WaterMesh surface clock
    float light_radius = 0.0f;
    float light_intensity = 0.0f;
    float light_color[3] = {1, 1, 1};
    float light_offset[3] = {0, 0, 0};
    std::string anim_name;       // resolved animation clip
    int   facing = 1;
    bool  on_ground = true;
};

// One scripted event the simulation fired this session (OnKill, OnCollide …).
// The studio/console reads this to show *why* something happened.
struct SceneEvent {
    std::string event;      // "OnKill"
    std::string object;     // emitter's identifier
    std::string other;      // the other participant ("" when none)
    std::string handler;    // owning component class
    double      time = 0.0; // scene clock
};

class RuntimeScene {
public:
    // Build the runtime from a parsed scene. Template components must already be
    // resolved on each object (`resolved_components`) — that is the loader's job,
    // mirroring SceneObject::InitWithTemplate.
    bool load(const av::SceneData& scene);

    void update(float dt);

    RuntimeObject*       object(const std::string& identifier);
    const RuntimeObject* object(const std::string& identifier) const;
    const std::vector<RuntimeObject>& objects() const { return objects_; }
    // Non-const view for the program host (it starts new objects' scripts).
    std::vector<RuntimeObject>& mutable_objects() { return objects_; }

    // ── the SCL runtime ────────────────────────────────────────────────────
    // The library registry Scene.CreateObject / item drops / spell spawns resolve
    // through. Not owned: the editor owns the registry and shares it with the
    // preview so an edited .scl is visible to the very next spawn.
    void set_library_manager(LibraryManager* libraries) { libraries_ = libraries; }
    LibraryManager* libraries() const { return libraries_; }

    // Instantiate an archetype from the library registry (SceneObject::
    // InitWithTemplate parity). Returns nullptr when the template is unknown.
    RuntimeObject* spawn(const std::string& template_name, const std::string& identifier,
                         const float position[3]);

    // The object scripts address as the hero ("hero" proxy in the Lua API).
    void set_hero(const std::string& identifier) { hero_identifier_ = identifier; }
    const std::string& hero_identifier() const { return hero_identifier_; }
    const RuntimeObject* hero() const;
    RuntimeObject*       hero();

    // World grounding hook: the av:: collision layer installs this so recovered
    // physics resolves against real terrain. Without it everything floats to 0.
    void set_ground_query(std::function<float(float, float)> query) { ground_query_ = std::move(query); }
    float ground_height(float x, float z) const { return ground_query_ ? ground_query_(x, z) : 0.0f; }

    // ── scripting ──────────────────────────────────────────────────────────
    // The Lua 5.1 host driving this scene (owned by the caller). When set, the
    // keep-active programs are started on the first update and every event fires
    // into the real VM.
    void set_program_host(ProgramHost* host) { program_host_ = host; }
    ProgramHost* program_host() const { return program_host_; }

    const std::vector<SceneEvent>& events() const { return events_; }
    void record_event(const SceneEvent& event) { events_.push_back(event); }

    const std::vector<ObjectRenderState>& render_state() const { return render_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    const std::vector<RuntimeProgram>& programs() const { return programs_; }

    double clock() const { return clock_; }
    size_t component_count() const;
    // Components whose class has no recovered tick yet — the honest to-do list.
    std::vector<std::string> unrecovered_classes() const;

    void clear();

private:
    void build_object(const av::SceneObject& src, size_t index);
    void instantiate(const av::SceneObject& src, RuntimeObject& object);
    void decode_component(const av::SceneComponent& src, RuntimeObject& owner);
    void reindex();
    void collect_program(RuntimeObject& owner, const RuntimeComponent& comp,
                         const std::string& field_name, const std::string& program_bytes);
    void resolve_references(RuntimeObject& object);
    void tick(RuntimeObject& object, float dt);

    std::vector<RuntimeObject> objects_;
    std::unordered_map<std::string, size_t> by_identifier_;
    std::vector<ObjectRenderState> render_;
    std::vector<RuntimeProgram> programs_;
    std::vector<std::string> warnings_;
    std::vector<SceneEvent> events_;
    LibraryManager* libraries_ = nullptr;
    ProgramHost*    program_host_ = nullptr;
    std::string     hero_identifier_ = "hero";
    std::function<float(float, float)> ground_query_;
    double clock_ = 0.0;
};

} // namespace caver
