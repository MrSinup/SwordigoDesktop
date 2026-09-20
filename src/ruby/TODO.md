# Ruby GG — Editor TODOs

Companion to `docs/parity_ruby_gg_vs_ruby_imgui.md` (full parity audit vs the
ImGui edition). Ordered by value. ✅ marks what landed recently.

> **Refreshed 2026-09 (master task 0.0).** The authoritative, ordered work list
> is `docs/MASTER_TODO_RUBY_GG_PARITY.md` — this file is the short form. Where
> they disagree, the master list wins.

## Scene saving & state (done — 2026-09)
- ✅ **Atomic + validated saves** (`ScriptIDEWidget::save_file`): compute the
  re-encoded bytes BEFORE touching the target, validate with a FileRift decode
  round trip, write via `QSaveFile` (temp + rename). A malformed markup used to
  truncate the scene file to zero before throwing — corruption fixed. Error is
  surfaced in the status bar via `last_save_error()`.
- ✅ **Smart structured save** (`save_scene_doc_structured`): `av::scene_save`
  of the RAM scene, then only reload when the round trip changed the structure —
  normally the save is applied in place: no camera reset, no GPU rebuild, no
  loading flash.
- ✅ **Smart text save reload** (`onSaveFile`): decode the fresh file and
  `apply_scene_data` (in-place) when structure is unchanged; camera-preserving
  full reload otherwise.
- ✅ **Camera preservation** across same-scene reloads: `load_scene` /
  `load_scene_async` only frame the camera on genuine first loads / switches;
  the evict+load fallbacks save & restore the camera explicitly.
- ✅ **SCL scene sync**: switching from FileRift text to the 3D Viewport with
  unsaved edits auto-encodes in memory (`apply_unsaved_scene_text_to_viewport`)
  and shows the result — the file is untouched until Ctrl+S. Same hook in
  `activate_document`.
- ✅ `load_scene_async(path, in_memory_binary)` — load a re-encoded binary while
  keeping `path` as the scene identity.
- ✅ Regression test `tests/scene_smart_save_test.cpp` (ctest).

## Structured scene editor
- ✅ **Add / duplicate / delete object** — `TemplatePalettePanel` dock
  (templates from scene libraries + scanned `.scl`, models from `.pod` scan,
  search, "Add to Scene"), viewport `add_template_object` / `add_model_object`
  (model gets a measured `LocalAABB` via `scene_build_local_aabb`, identifier
  deduped), plus Ctrl+D duplicate, Del delete and Alt+Up/Down reorder.
- ✅ **Undo/redo for structured edits** — `push_scene_snapshot_undo(before,
  label)` on the per-scene `QUndoStack`; every mutating op routes through it
  (component add/remove/paste/field, object create/dup/delete/move/reorder/
  paste, visibility, template retarget/materialize/reset, mesh commit,
  ground-mesh regen). Gizmo drags keep the cheaper transform command.
- ✅ **Object inspector dock** bound to the viewport selection: name, template,
  Position/Depth/Rotation/Scaling (live per-keystroke), rotation presets
  (0/90/180/270°), hidden, mesh name, plus the component grid with schema-driven
  per-field editors (unknown fields fall back to raw hex + field number).
  FileRift text stays the raw view.
- [ ] **Multi-select + batch move** (gizmo on the selection centroid, batch
  transform as one snapshot). Note the ImGui edition has this already
  (Ctrl+click toggle, union-bounds framing) — a genuine parity gap.
- [ ] **Frame camera at spawn point** when the scene has a `SpawnPoint`
  (`is_spawn_point`) instead of always framing bounds.
- [ ] **Walkable GroundPolygon overlay** in the 3D viewport: today the polygon
  fill + outline renders only in mesh-edit mode; make it persistent for the
  selected ground object.
- [ ] Gizmo: R-key rotate-to / G/W/E/R tool hotkeys already partly planned;
  keyboard shortcuts for View/Move/Rotate/Scale (Q/W/E/R) + tooltips.
- [ ] Per-vertex Im3d line width via a geometry shader when GL ≥ 3.2
  (fallback keeps flat 2 px today).

## Ground Mesh Studio (2026-09) — lag fix + add-to-scene landed
- ✅ **Repaint-lag fix** (`GroundMeshCanvas`): root cause was self-intersecting
  polygons — a crossing polygon makes Qt's winding-fill tessellation blow up
  (~17 ms at 80 verts, ~40 ms at 160) while simple polygons stay ~1–2 ms.
  Fix: appends that would cross are rejected (flash feedback), live drags are
  clamped to the last simple position, and the fill+stroke goes through a
  cached `QPainterPath`. Bonus correctness: boulder's vertex-0 fan triangulation
  now always receives a simple polygon.
- ✅ **“Add to Scene…”** (`GroundMeshStudio::add_to_scene` →
  `RubyMainWindow::on_ground_mesh_add_to_scene`): generates the complete
  ground-mesh object via `boulder::generate_ground_mesh_object`, pastes it into
  the open scene's RAM (position/depth from the panel, identifier deduped),
  marks the doc dirty, syncs the FileRift text view, camera-preserving reload.
- ✅ Regression test `tests/ground_mesh_studio_test.cpp` (ctest, offscreen Qt).

## Viewport performance
- ✅ **VBO/EBO migration (parity §8, 2026-09)** — display-list capture
  (`glNewList/GL_COMPILE` per mesh on the main thread) is gone. `upload_mesh_gpu`
  uploads positions/normals/UVs + index buffer to GL buffers, `draw_mesh_gpu`
  draws straight from them (fixed-function client pointers over the VBOs — same
  GL_LIGHTING/texture pipeline, no visual change), and `m_pod_gpu_cache` keys
  buffers by resolved pod path so scene reloads / save-triggered reloads reuse
  them instead of recompiling every mesh. Public `gpu_self_test()` diagnostic.
- [ ] Interleave to a single per-mesh vertex attribute (one VBO, strided) to
  halve the buffer count, and promote `m_pod_gpu_cache` to a process-wide cache
  shared by all viewport widgets (per-widget today; single-widget app so
  functionally equivalent).
- [ ] Only decode textures actually reachable by the camera distance tier
  (render-tier aware), instead of every ground/background texture.

## Surface the ImGui-edition tools that are still missing
- [x] Audio/SFX preview panel — `AudioViewerPanel` (WAV/MP3/OGG via SDL3 +
  mpg123 + vorbis, streamed playback). Better than both editions.
- [x] In-scene ground-mesh editor ("3D projection lock") + animated water /
  portal / particle render-effect preview (`[FX]` toolbar toggle).
- [ ] Texture painter + save-back (`tex_edit_save_png/tex` parity).
- [ ] SCL template studio inside Ruby GG — the *inheritance* half is live
  (`TemplateInspectorPanel`: retarget / override / unlink & materialize / reset,
  `[local]` vs dimmed `[inherited]`); the `.scl` **file** studio is not
  (`scl_update_template` + `scl_save_to_file` exist with no GUI yet).
- [ ] Object thumbnail browser for the palette.
- [ ] Scene creator / procedural generator surfacing from `RubyToolsWorkspace`.
- [ ] GroundMeshStudio: render the walkable GroundPolygon overlay in the 3D
  viewport (decodes into `collision_parse` → `ground_polygons` already; needs a
  draw pass + selection), and wire RubyMesh's “Apply zones to scene” to actually
  decode `.rbm` zones into ground-mesh objects (today it only posts a status).
- [ ] GroundMeshCanvas: ghost-preview of the rejected vertex (uses the new
  public `would_cross_at`) + keyboard nudge of the selected vertex.

## Panels / host
- [ ] Undo for the FileRift IDE already exists (Qt text undo) — wire scene-wide
  structured undo on top so both surfaces share history.
- [ ] Keep the dock respawn + panel backend hardening work in `ruby_main_window`
  (per-dock toggles exist in the View menu).
