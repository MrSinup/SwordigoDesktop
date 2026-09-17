#pragma once
// component.h — the recovered component universe.
//
// Every Swordigo component is a `Component` protobuf message: a short ClassName
// string (field 1), an object-local Identifier (field 2), and one *or more*
// payload submessages. The payload slots are recovered from the binary's own
// `Caver::Proto::<Class>::kExtensionFieldNumber` globals (see
// tools/extract_component_schema.py, which re-derives them from any
// libswordigo.so); the per-class behaviour is in
// OpenSwordigo/arm32_13/functions/Caver/<Class>/.
//
// Multi-slot is the rule, not an exception: writing a CollisionShape emits slots
// 120 (ShapeComponent) + 121 (CollisionShapeComponent), a MonsterEntity emits 152
// (EntityComponent) + 158 (MonsterEntityComponent), every monster controller
// emits 302 (MonsterControllerComponent) + its own slot, and a MagicBolt emits
// 550 + 558 (the shared SpellComponent base). A decoder that reads only the
// ClassName's own slot silently drops half of every such component.
//
// Some classes are runtime-only and are never serialised at all (Transform,
// TransformController, ShatterComponent, TextBubbleComponent,
// RotatingBackgroundComponent, MagicParticleEmitterComponent,
// ProjectileMonsterControllerComponent) — they appear in .scl files as nothing
// but ClassName + Identifier. Those have `serialized == false` and a payload_tag
// of 0.
//
// This header is the single source of truth for "which components exist and how
// far each one is recovered". `stages` is a coverage bitmap, not documentation:
// the engine uses it to decide whether an instance can be simulated, rendered,
// or only round-tripped.

#include <cstdint>
#include <string>
#include <vector>

namespace caver {

// Recovery stages, in dependency order. A component can be Parsed without being
// Instantiated, Instantiated without being Updated, and so on.
enum class Stage : uint8_t {
    None         = 0,
    Parsed       = 1u << 0,   // schema known: fields decode/encode losslessly
    Instantiated = 1u << 1,   // wired into a runtime object with resolved refs
    Updated      = 1u << 2,   // has a simulation tick (the real behaviour)
    Rendered     = 1u << 3,   // contributes to the editor/scene preview
};

constexpr Stage operator|(Stage a, Stage b) {
    return static_cast<Stage>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline bool has(Stage set, Stage bit) {
    return (static_cast<uint8_t>(set) & static_cast<uint8_t>(bit)) != 0;
}

// One recovered component class.
struct ComponentType {
    const char* class_name;     // "ModelComponent" (schema class / Caver dir name)
    const char* short_name;     // "Model" (the ClassName string written in .scl)
    uint32_t    payload_tag;    // 810 — Component tag (field_number << 3 | 2) for this payload
    Stage       stages;         // what is actually recovered today
    bool        controller;     // drives other objects/components (monster, door, spell …)
    bool        serialized = true;  // false = runtime-only, never written to disk
};

// The full table, in payload-field order. Payload fields are NOT contiguous —
// the gaps are the game's own numbering, kept verbatim so ids never collide.
const std::vector<ComponentType>& component_types();

// Lookups (nullptr when unknown — callers must handle newer-version components).
const ComponentType* component_type_by_class(const std::string& class_name);   // "ModelComponent"
const ComponentType* component_type_by_short(const std::string& short_name);   // "Model"
// Slot lookup, by tag. Several classes share a slot (`ShapeComponent` and
// `UtilityShapeComponent` both write tag 962; `CollisionShapeComponent` writes
// 970), so this returns the class that *owns* the slot. Identify the component
// itself with component_type_by_short() on the file's ClassName string.
const ComponentType* component_type_by_tag(uint32_t payload_tag);
// The same lookup by Component field number (payload_tag >> 3).
const ComponentType* component_type_by_field(uint32_t field_number);

// Coverage summary for the recovery dashboard: how many classes exist, and how
// many reached each stage.
struct CoverageReport {
    size_t total = 0;
    size_t parsed = 0;
    size_t instantiated = 0;
    size_t updated = 0;
    size_t rendered = 0;
    size_t controllers = 0;
};
CoverageReport coverage();

// "Model" <- "ModelComponent". Exposed because the ClassName strings in shipping
// .scl files are the short form while the schema uses the long form.
std::string short_name_for_class(const std::string& class_name);

// Every class that shares `field_number`, in table order. A CollisionShape has
// tags on 120 and 121, a MonsterEntity on 152 and 158 — a writer needs all of
// them, not just the one the ClassName names.
std::vector<const ComponentType*> component_types_for_field(uint32_t field_number);

} // namespace caver
