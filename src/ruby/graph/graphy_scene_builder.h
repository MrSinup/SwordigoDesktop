#pragma once
// ============================================================================
// graphy_scene_builder.h — native (pure C++) Swordigo document -> Graph builder
//
// Before this file, the node graph in Ruby GG and in `scl_graph_viewer` could
// only ever display one of two things: `Graph::create_demo_graph()` (a
// hand-authored showcase) or a JSON document produced *outside the process* by
// `tools/scene_to_graph.py` / `tools/scl_to_graph.py`. Nothing in the shipped
// application could turn a file the user clicked on into a graph.
//
// This is the missing backend. It reads the same binary the engine reads — the
// protobuf `.scene` / `.scl` payload, decoded by the project's own FileRift
// and `av::scene_loader` — and produces a `Graph` directly, in process, in the
// same C++ that the canvas paints. No Python, no intermediate JSON, no temp
// files.
//
// Two document dialects, because Swordigo ships two:
//
//   * `.scene` — one Scene: an object list (each with a transform and a list of
//     components) plus camera bounds and the names of the `.scl` libraries it
//     imports. Graphed as a three-level containment tree:
//         Scene ──▶ Entity ──▶ Component
//   * `.scl`  — an ObjectLibrary: a bag of templates that scenes instantiate.
//     Graphed with the same shape:
//         Library ──▶ Template ──▶ Component
//
// Edges are of four kinds, in decreasing order of how much they tell you:
//
//   1. Containment  — Delegate pins: the object owns the component, the scene
//      owns the object. This is the spine of the graph.
//   2. Component references — a component's `*Id` / `*ShapeId` field naming
//      another component *in the same object*. Resolution is object-scoped:
//      `hiro.scl` reuses `Identifier 101` for three different components in
//      three different objects, so a file-global id map would wire them
//      together wrongly. The `*ShapeId`/`*AreaId` family targets a
//      `ShapeComponent` and is typed `Byte`; everything else is `Int`.
//   3. Cross-object references — an object's `Entity Ref` handle wired into a
//      component that names it, either through a `*Identifier` string field
//      (`PortalComponent.SpawnPointIdentifier` -> the matching SpawnPoint) or
//      through Lua (`Scene.Find("elder")`) found in the component's embedded
//      `Program`. The Lua scan is deliberately conservative: it only draws a
//      wire when the named object actually exists in the same document.
//   4. Payload pins — the human-readable scalars that make a card worth
//      reading (`Name`, `TextureName`, `Intensity`, `DestinationSceneName`),
//      surfaced as pins with their value pre-filled.
//
// Layout is computed here rather than left to the caller, because a graph whose
// nodes overlap is worse than no graph. Node sizes come from
// `compute_node_geometry()` — the same single authority the canvas paints with —
// so spacing is derived from measured text, not from a guessed constant, and
// `frame_all()` fits the result exactly.
//
// Everything is a pure function of its inputs: nothing here reads or writes
// disk. Whoever loads the document (Ruby GG, the CLI, a test) decides how the
// bytes arrive. That is what makes the whole pipeline unit-testable against the
// real shipped assets.
// ============================================================================

#include "graphy.h"
#include "graphy_layout.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ruby::graph {

// ── What to include ──────────────────────────────────────────────────────────
//
// These gate *resolution* (which edges get drawn), not *content*: a reference
// field is part of what the file says, so its pin is always shown, and turning
// an option off leaves the pin dangling rather than hiding the document.
//
// Each flag is independently observable in a test, so a regression in one edge
// class can't hide behind the others.

struct SceneGraphOptions {
    /// `Scene ──▶ Entity ──▶ Component` containment wires.
    bool containment_wires = true;
    /// `*Id` / `*ShapeId` pins resolved to their target component (object-scoped).
    bool reference_wires   = true;
    /// Name-reference pins (`SpawnPointName`, `*Identifier`) resolved to the
    /// object they name.
    bool name_wires        = true;
    /// `Scene.Find("x")` pins resolved to the named object.
    bool lua_find_wires    = true;
    /// A node per embedded `Program` (the object's `OnLoad` and any
    /// `ProgramComponent`) so Lua has a home in the graph.
    bool script_nodes      = true;
    /// One comment frame per object / template, bounding its nodes.
    bool comments          = true;
    /// `Name` / `TextureName` / `Intensity` / `DestinationSceneName` pins.
    bool payload_pins      = true;
};

// ── How to lay it out ────────────────────────────────────────────────────────

struct GraphBuildStyle {
    /// Metrics + measurer used to size nodes while laying out. Leaving
    /// `measure` empty uses `default_text_measure()` — font metrics in the app,
    /// a width heuristic when no QGuiApplication exists (headless tests).
    LayoutMetrics layout{};
    TextMeasure   measure{};

    float root_gap      = 150.0f;  // root column -> object column
    float component_gap = 120.0f;  // object column -> component column
    float block_gap     = 80.0f;   // vertical gap between object blocks
    float node_gap      = 26.0f;   // vertical gap between stacked nodes
    float origin_x      = 60.0f;
    float origin_y      = 60.0f;

    float frame_pad     = 28.0f;   // comment frame inset around its nodes
    float frame_title_h = 44.0f;   // space reserved above the block for the title
};

// ── Builders ─────────────────────────────────────────────────────────────────
// All of them return an empty (never null) Graph when the bytes don't parse, so
// callers can hand the result straight to `GraphyCanvas::set_graph()`.

/// Build from the binary protobuf of a `.scene` document.
std::shared_ptr<Graph> build_scene_graph(const std::vector<uint8_t>& scene_bytes,
                                         const std::string& document_name,
                                         const SceneGraphOptions& opts = {},
                                         const GraphBuildStyle& style = {});

/// Build from the binary protobuf of a `.scl` ObjectLibrary.
std::shared_ptr<Graph> build_library_graph(const std::vector<uint8_t>& scl_bytes,
                                           const std::string& document_name,
                                           const SceneGraphOptions& opts = {},
                                           const GraphBuildStyle& style = {});

/// Dispatch on the document's FileRift type (`.scene` -> scene, `.scl` ->
/// library). Unsupported documents yield an empty graph.
std::shared_ptr<Graph> build_graph_from_binary(const std::vector<uint8_t>& bytes,
                                               const std::string& path,
                                               const SceneGraphOptions& opts = {},
                                               const GraphBuildStyle& style = {});

/// Same as `build_graph_from_binary`, but accepts FileRift *markup* text (with
/// or without the `## FileRift decoded Swordigo file type: X` banner) and
/// re-encodes it first. This is how the studio graphs unsaved editor text.
std::shared_ptr<Graph> build_graph_from_markup(const std::string& markup,
                                               const std::string& path,
                                               const SceneGraphOptions& opts = {},
                                               const GraphBuildStyle& style = {});

/// Read `path` and build. Binary input is used as-is; markup input is
/// re-encoded. Returns an empty graph if the file can't be read.
std::shared_ptr<Graph> build_graph_from_file(const std::string& path,
                                             const SceneGraphOptions& opts = {},
                                             const GraphBuildStyle& style = {});

// ── Document classification ──────────────────────────────────────────────────

/// Canonical FileRift type for a document ("scene" / "scl"), or an empty string
/// when this builder cannot turn the document into a graph.
std::string graphable_type_for_path(const std::string& path);

/// `true` when `build_graph_from_binary()` can do something useful with `path`.
bool is_graphable_document(const std::string& path);

/// A sentence explaining why the Node Graph tab is empty for `path`, for the
/// studio's notice page. Empty when the document *is* graphable.
std::string graph_unsupported_reason(const std::string& path);

} // namespace ruby::graph
