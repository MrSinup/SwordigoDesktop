# Ruby GG ↔ ImGui Asset Viewer — Scene 3D Editing Parity

Reference: `src/tools/asset_viewer.cpp` (the ImGui-era "Visual" scene editor tab),
ported surface: `src/ruby/viewport/viewport_3d_widget.cpp` (+ `ruby_gizmo.cpp`).
This document is the running inventory of every scene-3D-editing capability the
ImGui viewer has, whether Ruby GG has it, and — for the ones deliberately not
ported — why (research notes + candidate future work).

Status legend:
- **✅ ported** — Ruby GG has the feature (possibly better).
- **✅ ported this pass** — added in the in-scene-mesh-editor / gizmo pass.
- **🟡 partial** — core exists, some ImGui surface is missing.
- **🔬 research only** — deliberately not ported; notes below.

---

## 1. Camera & viewport

| Feature | ImGui viewer | Ruby GG | Notes |
|---|---|---|---|
| Orbit / pan / zoom with release inertia | ✅ | ✅ | `update_camera_dynamics`, same feel |
| Wheel zoom keeps the world point under the cursor | ✅ | ✅ | `wheelEvent` |
| RMB + WASD free-fly camera (Shift × 3.5) | ✅ | ✅ | `keyPressEvent` free-fly block |
| ViewCube (orient camera, click faces) | ✅ | ✅ | `draw_view_cube` / `hit_test_view_cube` |
| Grid on/off, wireframe on/off | ✅ | ✅ | `set_grid_visible` / `set_wireframe` |
| Frame selection (focus) | ✅ | ✅ | `focus_object` |
| Background toggle | 🟡 | 🟡 | Ruby GG always draws the scene background |

## 2. Transform gizmo (ImGuizmo → RubyGizmo)

| Feature | ImGui viewer | Ruby GG | Notes |
|---|---|---|---|
| Move / Rotate / Scale modes (W/E/R or toolbar, Tab cycles, Esc = View) | ✅ | ✅ | toolbar + Tab |
| Axis handles (line + cone/cube tip) | ✅ | ✅ | |
| **Plane handles (XY / XZ / YZ) for translate AND scale** | ✅ | ✅ this pass | scale plane quads added; tinted with the off-plane axis color (ImGuizmo convention) |
| **Uniform scale (centre ball, ImGuizmo MT_SCALE_XYZ)** | ✅ | ✅ this pass | was broken (distance-ratio degeneracy); now screen-space radial ratio |
| **Robust axis scale** | ✅ | ✅ this pass | old code inverted/collapsed at 0.01x when the drag crossed the object origin; now `1 + pixelDelta/axisScreenLen` |
| Ctrl-snap (1 unit, 15°, 0.1) | ✅ | ✅ | `set_snap` |
| Ground snapping (Ctrl+translate lands on terrain, orients Up) | ✅ | ✅ | `draw_ruby_gizmo` ray-down block |
| Gizmo stays screen-constant size | ✅ | ✅ | `gizmo_scale` |
| Rotation ring + view-plane ring (AXIS_VIEW) | ✅ | ✅ | |
| Esc cancels an in-flight drag | ✅ | ✅ | `m_gizmo_snap_*` snapshot |
| Drag lands on the QUndoStack | 🟡 | ✅ | Ruby GG pushes `TransformUndoCommand`; ImGui uses a session snapshot |

**Scale-gizmo fixes in this pass** (`ruby_gizmo.cpp`):
- Scale ratios are now computed from **screen-space pixel deltas** captured at
  grab time (axis screen directions + lengths, plane diagonals, centre in px),
  exactly like ImGuizmo. This kills three bugs at once: grabbing near the
  object origin no longer explodes the ratio (uniform start distance is floored
  at 12 px), crossing the origin mid-drag no longer inverts and collapses the
  object to 0.01×, and the negative-side-of-axis grab no longer reverses the
  drag direction.
- Added **XY/XZ/YZ plane handles** to hit-testing and drawing (two-axis scale),
  which the old gizmo never had.
- Snap now rounds `ratio − 1` (so 1.0 = no scale change is always a snap
  point), and uniform scale uses the cursor's radial distance from the gizmo
  centre — the object stays glued to the cursor.

## 3. Object editing

| Feature | ImGui viewer | Ruby GG | Notes |
|---|---|---|---|
| Click-pick objects (screen-space, touch friendly) | ✅ | ✅ | `pick_object_at` |
| Selection highlight (wireframe + rim) | ✅ | ✅ | `draw_selection_highlight` (mesh-accurate) |
| Delete object | ✅ | ✅ | Del/Backspace + context menu |
| Duplicate object | ✅ | ✅ | Ctrl+D / context menu (copy+paste, unique id, nudge) |
| Copy / paste (RAM clipboard, cross-scene safe) | ✅ | ✅ | Ctrl+C/V; reloads model/background caches |
| Move object up/down in the scene list | ✅ | ✅ | Alt+Up/Down |
| Rename / retemplate (inspector) | ✅ | ✅ | InspectorPanel |
| Numeric transform entry (inspector) | ✅ | ✅ | InspectorPanel + gizmo live-sync |
| Per-object visibility | ✅ | ✅ | hierarchy checkboxes |
| Spawn point / portal / empty object creation | ✅ | ✅ | `add_scene_object` |
| Per-object roll presets (0°/90°/180°/270°) | ✅ | 🔬 | trivial to add to the inspector; not required for parity |
| Multi-select | ❌ | ❌ | neither viewer has it |

## 4. Ground-mesh editing

| Feature | ImGui viewer | Ruby GG | Notes |
|---|---|---|---|
| Ground Mesh Studio (standalone 2D sketch tab) | ✅ | ✅ | `ruby/tools/ground_mesh_studio.cpp` (simple-polygon enforced) |
| **In-scene mesh edit ("3D projection lock")** | ✅ | ✅ this pass | see below |
| **Live re-apply while dragging** (throttled ~90 ms) | ✅ | ✅ this pass | mesh reshapes as you drag |
| **Whole-session revert (R / Ctrl+Z)** | ✅ | ✅ this pass | pre-edit scene snapshot |
| Edge-insert node (RMB edge → new vertex) | ✅ | ✅ this pass | |
| Delete vertex (RMB / Del) | ✅ | ✅ this pass | |
| Grid snap toggle | ❌ | ✅ this pass | ImGui had no snapping; Ruby GG: G toggles 25-unit snap |
| Arrow-key nudge | ✅ | ✅ this pass | |
| 2D pan / zoom inside the locked view | ✅ | ✅ this pass | MMB pan, wheel zoom |
| Templated-mesh edit modal (SCL studio templates) | ✅ | 🔬 | niche: edit a library tile in isolation, then sync back |
| Hat / dome authoring on the polygon | ✅ | 🔬 | boulder supports hats; the inline editor doesn't surface them |

### In-scene mesh editor — what was built (`viewport_3d_widget.cpp`)
Entry: toolbar **Mesh** button or **M**. Requires a selected object with
embedded ground meshes.

- **Projection lock** — entering the session snaps the camera to the polygon's
  own plane: pitch 0, yaw = −rot_y, target at the polygon centre, distance
  fitted to the outline (same math as `gm_begin_inline_edit` in the viewer).
  The original camera is restored on exit.
- **Import** — the authoritative polygon comes from the object's
  `GroundPolygonComponent` (payload 110) parsed directly from the component
  bytes with `proto::Reader` (the `SceneObject::ground_polygon_points` field
  documented by scene_loader.h is never actually populated, so parsing the
  component is the only correct source); embedded ground-mesh vertices are the
  fallback. Generator params (min/max depth, top/bottom textures by mesh field
  8/9/6) are carried through regeneration.
- **Overlay** — QPainter composited over the GL pass in `paintGL`: zoom-adaptive
  2D grid on the object plane (local X/Y axes tinted), translucent polygon
  fill, edge outline (hovered edge glows gold), vertex handles with index
  labels, a `MESH EDIT` tag, a live `SNAP 25 (G)` indicator and a hint bar.
- **Input** — LMB drags a vertex (keeps the grab offset, grid-snapped when snap
  is on), RMB on a vertex deletes it, RMB on an edge inserts a node snapped
  onto the edge and grabs it immediately (drag continues while RMB is held),
  Del removes the hovered vertex (never the object), arrows nudge, MMB pans,
  wheel zooms, G toggles snap, Esc/M/Enter commits, R or Ctrl+Z reverts the
  whole session to the pre-edit snapshot.
- **Live apply** — during a drag the mesh regenerates through the real backend
  every ~90 ms (`boulder::serialize_swdm` → `generate_ground_mesh_object` →
  `av::scene_load_bytes`, no temp files), non-ground components are preserved,
  `scene_mark_ground_mesh_dirty` flags the save path, and the object's GPU
  buffers are re-uploaded **reusing unchanged texture ids** so the live ticks
  don't re-decode from disk. The regenerated `GroundPolygon` outline is written
  back to `ground_polygon_points` (ImGui never maintained that field), so
  re-entering the editor after an apply imports the *edited* polygon and the
  collision/terrain helpers see current walkable data.
- **Lifecycle guards** — scene switches abort the session unapplied; deleting
  the edited object aborts it; changing selection commits it first.

## 5. Rendering / presentation

| Feature | ImGui viewer | Ruby GG | Notes |
|---|---|---|---|
| Ground meshes render with textures | ✅ | ✅ | per-mesh GL textures |
| Terrain / water / fire / light / shadow debug views | ✅ | 🔬 | parsed (`scene_terrain`, waters, lights, fires, shadows) but no dedicated toggle surface |
| Skeleton view | ✅ | ✅ | `m_show_skeleton` |
| Lighting rig panel (sun/fill/bounce/ambient/fog) | ✅ | ✅ | `ViewportLighting` + LightingPanel (scene-wide, not per-object) |
| Dimension-rift ghosting | ✅ | ✅ | `is_dimension_object` rendering |
| Hidden-object pick exclusion | ✅ | ✅ | |

## 6. Research notes — deliberately not ported (future work)

- **Per-object roll presets (0/90/180/270)** — a 4-button convenience in the
  ImGui inspector. Ruby GG has exact spinbox rotation; add quick-angle buttons
  to `InspectorPanel` when the inspector gets its next pass. Low effort.
- **Templated (SCL) mesh-edit modal** — edits a library tile in isolation
  (virtual 1-object scene) then syncs the modified template back to the .scl.
  Ruby GG has no SCL template browser yet; the ground-mesh pipeline this
  session ported is the foundation, but the template library UI is a separate
  feature (research: `asset_viewer.cpp` `scl_studio_*`).
- **Hats/dome authoring** — boulder supports round-hat domes on the surface;
  the inline editor imports/regenerates the base polygon only. Surfacing hats
  (add/remove/drag domes in the locked view) is a natural next step.
- **Terrain sculpting / heightmap painting** — the viewer parses terrain but
  has no sculpt tool either; out of scope for both.
- **Per-object light / water editing** — scene data is parsed; editing surfaces
  don't exist in either tool. Out of scope.
- **Multi-select** — neither viewer implements it; the clipboard already
  stores a vector so batch paste is the designed path.

---

## Test coverage

- `tests/scene_edit_acceptance_test.cpp` — copy/paste/duplicate/delete/move
  through the real viewport (offscreen GL).
- `tests/scene_mesh_edit_test.cpp` — in-scene mesh editor: injects a
  boulder-generated ground object, verifies the projection lock, synthesizes a
  real LMB vertex drag (+30 px), commits with Esc and asserts the regenerated
  `GroundPolygon` outline moved (vertex 0: 0 → ~44 world units).
- `tests/ground_mesh_studio_test.cpp` — standalone studio round trip.