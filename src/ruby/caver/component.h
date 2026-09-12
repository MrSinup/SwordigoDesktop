#pragma once
// component.h — the recovered component universe.
//
// Every Swordigo component is a `Component` protobuf message: a short ClassName
// string (field 1), an object-local Identifier (field 2), and exactly one
// payload submessage whose tag identifies the component class. The tag table is
// recovered from the generated `Component` schema (src/tools/scene_schemas.cpp),
// which was itself recovered from the binary; the per-class behaviour is in
// OpenSwordigo/arm64_12/functions/Caver/<Class>/.
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
    uint32_t    payload_field;  // 810 — the Component tag >> 3 for this payload
    Stage       stages;         // what is actually recovered today
    bool        controller;     // drives other objects/components (monster, door, spell …)
};

// The full table, in payload-field order. Payload fields are NOT contiguous —
// the gaps are the game's own numbering, kept verbatim so ids never collide.
const std::vector<ComponentType>& component_types();

// Lookups (nullptr when unknown — callers must handle newer-version components).
const ComponentType* component_type_by_class(const std::string& class_name);   // "ModelComponent"
const ComponentType* component_type_by_short(const std::string& short_name);   // "Model"
const ComponentType* component_type_by_field(uint32_t payload_field);

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

} // namespace caver
