# Graphy — native document backend

Status: **implemented and verified** (Ruby GG, `bin/ruby_gg`).
Source: `src/ruby/graph/graphy_scene_builder.{h,cpp}`, `tests/graphy_scene_builder_test.cpp`.

## 1. What was wrong

Graphy — the Unreal-style node canvas in Ruby GG's **Node Graph** tab — could
only ever display one of two things:

* `Graph::create_demo_graph()`, a hand-authored showcase drawn from
  `platforms.scl` / `traps.scl`, hard-wired in the constructor; or
* a graph produced **outside the process** by `tools/scene_to_graph.py` /
  `tools/scl_to_graph.py` and loaded as JSON by the standalone
  `scl_graph_viewer`.

Nothing in the shipped application could turn a file the user clicked on into a
graph. `ruby_main_window.cpp:286` mounted the canvas and never handed it a
document, so the tab was decorative.

## 2. What it is now

`graphy_scene_builder` is the missing backend: it reads the same binary the
engine reads — the protobuf `.scene` / `.scl` payload, decoded by the project's
own FileRift and `av::scene_loader` — and produces a `Graph` directly, in
process, in the same C++ the canvas paints. No Python, no intermediate JSON, no
temp files.

Two documents, one shape:

| Document | Tree |
| --- | --- |
| `.scene` / `.scn` | `Scene ──▶ Entity ──▶ Component` |
| `.scl` (ObjectLibrary) | `Library ──▶ Template ──▶ Component` |

Edges, in decreasing order of how much they tell you:

1. **Containment** — Delegate pins: the root owns every object, the object owns
   every component, and the object owns its `OnLoad` script node.
2. **Component references** — a component's `*Id` / `*ShapeId` field naming
   another component **in the same object**.
3. **Cross-object references** — an object's `Entity Ref` handle wired into a
   component that names it, either through a name field
   (`PortalComponent.SpawnPointName` → the matching object) or through Lua
   (`Scene.Find("elder")` inside the component's embedded `Program`).
4. **Payload pins** — `Name`, `TextureName`, `Intensity`, `DestinationSceneName`,
   surfaced as pins with their values pre-filled. A destination is an *output*:
   it points somewhere else.

## 3. Decisions worth knowing

**Reference resolution is object-scoped, and that is not an optimisation.**
`hiro.scl` reuses `Identifier 101` for three *different* components in three
*different* objects. A file-global `id → component` map would wire them to each
other. The engine agrees: it exports
`Caver::SceneObject::ComponentWithIdentifier`.

**The reference rule is `(Id|Ids)$`, not a 129-entry table.** That is the pattern
the binary-derived schema census produced for all 129 cross-reference fields
across 54 component classes. A table drifts the moment a new component is
understood, and the failure mode is a silently missing wire. The exceptions
(`SoundId`, `ModelBindingId`, `TextureMappingId`) all run one way — they index
name tables rather than components — and resolution simply finds no target and
draws nothing. `*ShapeId` / `*AreaId` / `*PolygonId` targets are
`ShapeComponent`s and use the `Byte` pin colour; everything else is `Int`.

**Name references are filtered twice.** The `*Identifier` family plus the
portal's `SpawnPointName` (the schema's own name for the same idea) propose an
edge; it is only drawn when an object with that exact identifier exists in the
same document. That guard is what makes a permissive rule safe — a bone name or
item id that happens to share the spelling draws nothing.

**`Scene.Find` is cross-checked against the extracted API table.** The call must
exist in `rbsrc::kGameLuaApi` under namespace `Scene` before any wire is drawn,
so a typo cannot invent an edge.

**Options gate resolution, not content.** A reference pin is part of what the
file says; turning `reference_wires` off leaves the pin dangling rather than
hiding the document.

**Layout is computed here, from measured text.** Node sizes come from
`compute_node_geometry()` — the same single authority the canvas paints with —
so spacing follows real text metrics instead of a guessed pitch, and
`frame_all()` fits the result exactly. The root node is measured *before* it is
used as a column offset; skipping that placed the entity column inside the root
whenever the root's measured width exceeded its default.

## 4. Studio wiring

* `RubyMainWindow::refresh_graph_view()` builds the active document's graph and
  hands it to the canvas. It reads from the **in-memory editor buffer** when the
  user has unsaved FileRift edits, so the graph tracks what the IDE shows rather
  than the last save.
* Building runs the document through `scene_load_bytes()`, so it is not free; the
  result is cached on `(path, revision)`. Revisions are bumped only at the choke
  points that already know content changed — IDE typing, IDE tab exit, viewport
  structured-edit sync.
* The **Node Graph** tab is shown only for graphable documents and is built only
  while it is on screen. Ungraphable documents get a notice page explaining why,
  never a stale graph.
* `notice_for(kind, TabGraph)` supplies that explanation;
  `graph_unsupported_reason()` produces the text.

## 5. Verification

`tests/graphy_scene_builder_test.cpp` — 64 checks, all passing. The fixtures are
real Swordigo protobuf built with `proto::Writer`, fed through the real loader,
so a regression in the schema, the loader or the builder fails there.

Covered: object-scoped resolution with a deliberately reused `Identifier 101`;
`KeyframeAnimationComponent.ModelId` reaching its own object's `ModelComponent`
and nothing else; `Scene.Find` wiring including the unresolvable-name case;
`ParentComponentIdentifier` (a Component-wrapper field, not a payload field);
containment at all three levels; payload pins; option gating; malformed bytes;
non-graphable paths.

The suite also graphs **real shipped documents** (skipped loudly when the machine
has none), asserting two graph invariants on every one of them: no two node
cards overlap, and **no component reference escapes its own object**.

| | files | nodes | wires | overlapping node pairs | cross-object wires |
| --- | --- | --- | --- | --- | --- |
| `.scene` | 12 | 4470 | 6349 | **0** | **0** |
| `.scl` | 12 | 724 | 944 | **0** | **0** |

Largest single scene graphed: `florennum_jail_part1.scene` (612 nodes, 893
wires, 112 frames).

The object-scoping sweep is the one that earns its keep. `hiro.scl` — the file
the rule was derived from — turns out to have **seven** component identifiers
used by more than one component, not the one (`Identifier 101`) the initial
investigation found. Its graph (49 nodes, 97 wires) has zero escaping
references. A file-global id map would have wired all seven wrongly.

## 6. Not done yet

* The **Scene timeline / ghost-object scrub** work in
  `docs/03-graphy-scene-timeline-backend.md` is still a plan. `Graph` already
  carries `CommentFrame`s and subgraph nodes, which is where a timeline strip
  would live.
* RBSRC round-trip: the builder is read-only. Writing an edited graph back to
  `.scene` / `.scl` is the ObjectStudio structural path, which already has
  `scene_save` / `scl_update_template` primitives.
* The standalone `scl_graph_viewer` stays Qt-only and still loads Python JSON;
  it would need `swpod` + `filerift` to host the builder.
* `tools/scene_to_graph.py` and `tools/scl_to_graph.py` are now redundant for the
  app but remain the reference for the JSON shape, and still cover a few
  hand-maintained reference labels the C++ side derives from the schema instead.
