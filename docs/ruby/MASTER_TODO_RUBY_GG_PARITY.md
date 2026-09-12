# Ruby GG — Master Parity TODO (ImGui × Web Editor, Best-of-Both)

**Purpose:** Single ordered work list that merges the two parity audits into one
execution plan. When the ImGui edition (`src/tools/asset_viewer.cpp`) and the web
editor (szkot.xyz, research in `docs/web_swordigo_editor_research.md`) both offer a
feature, we implement the **better design and fold in the other one's extras** —
never a blind copy of either.

**Inputs**
- `docs/parity_ruby_gg_vs_ruby_imgui.md` — legacy ImGui `asset_viewer.cpp` audit.
- `docs/scene_editor_parity.md` — scene-3D editing inventory (gizmo + mesh editor).
- `docs/web_swordigo_editor_research.md` — web editor (three.js) feature research.
- `docs/PHASE4_APK_SESSION_DESIGN.md` — APK import→edit→export session workflow + native v1 signer feasibility (task 4.1 spec).
- `src/ruby/TODO.md` — older TODO, superseded by this master list where they overlap.

**Status legend:** ☐ todo · 🔶 in progress · ✅ done · 🟡 partially done · ⛔
deliberately skipped (reason in the row).

---

## 0. How to work this list (the guide)

1. **Do tasks one at a time, in order.** Each task is self-contained: read its
   "Spec", implement, then run the acceptance test + full suite before moving on.
2. **Collision rule.** When a task's source says "ImGui" *and* "Web", the Spec
   already states which design wins and what extra is merged in. If you find a
   new collision mid-task, prefer the design with the **single source of truth**
   (encoded scene bytes / one undo model), then add the other side's convenience
   surface. Never implement two parallel mechanisms for the same edit.
3. **Ground truth over docs.** The three parity docs were written at different
   times; **verify against the code** before starting (the doc table in
   `docs/parity_ruby_gg_vs_ruby_imgui.md` predates several features that are now
   live — see "already done" below). Update the doc row when you finish a task.
4. **Definition of done.** (a) feature works in `ruby_gg`; (b) a regression test
   is added under `tests/` and registered in `CMakeLists.txt`; (c) `ctest
   --test-dir build-gg` is green; (d) the relevant parity doc row is flipped to
   ✅ and `src/ruby/TODO.md` updated if it mentions the item.
5. **Save/undo discipline.** Every mutating editor action must (i) snapshot
   before mutate, (ii) emit the dirty signal, (iii) be undoable via the scene
   undo stack. This is the rule that prevents the stale-cache / revert-on-save
   bug class from earlier sessions.

## 1. Already done (verified against code 2026-09 — do NOT redo)

| Area | Status | Where |
|---|---|---|
| Atomic + validated saves, smart structured save, camera preservation | ✅ | `ScriptIDEWidget::save_file`, `save_scene_doc_structured` |
| VBO/EBO viewport render migration, per-pod GPU cache | ✅ | `upload_mesh_gpu`/`draw_mesh_gpu` |
| Transform gizmo move/rotate/scale, plane handles, uniform scale, screen-space scale ratios, Ctrl-snap, ground-snap, Esc-cancel | ✅ | `ruby_gizmo.cpp` |
| Object create / duplicate / delete / move (loader + viewport wiring, Del/Ctrl+D/Alt+Up/Down) | ✅ | `scene_loader.cpp`, `Viewport3DWidget` |
| Object copy/paste cross-scene (Ctrl+C/V, model+background cache reload) | ✅ | `Viewport3DWidget::copy_scene_selection` |
| Component **add / remove / copy** in InspectorPanel | ✅ | `inspector_panel.cpp` (Copy button → static clipboard) |
| Template **palette** (templates + models, measured LocalAABB) | ✅ | `template_palette_panel.cpp`, `viewport add_template_object/add_model_object` |
| Template **inheritance UI** (retarget / override / materialize / reset, syncs to viewport + FileRift + save) | ✅ | `template_inspector_panel.cpp`, `scene_loader` template helpers |
| In-scene mesh editor ("3D projection lock") + GroundMeshStudio + "Add to Scene…" | ✅ | `viewport_3d_widget.cpp`, `ground_mesh_studio.cpp` |
| Audio viewer WAV/MP3/OGG (SDL3 + mpg123 + vorbis) | ✅ | `audio_viewer_panel.cpp` (better than both editions) |
| Scene hierarchy panel, inspector dock, lighting panel, console, asset browser quick-sync | ✅ | `src/ruby/panels/` |
| Game lights preview rig | ✅ | `ViewportLighting` |

> ✅ **Task 0.0 refreshed the parity tables** (2026-09) — the ❌/🟡 rows that
> duplicated the ✅ items above are gone, so these three docs now agree. The
> master list below is authoritative where they ever drift again.

---

## 2. The master task list (one at a time)

### Phase 0 — Ground truth

- **0.0 ✅ Refresh the parity docs.** *Done 2026-09.*
  `docs/parity_ruby_gg_vs_ruby_imgui.md` §1 (render backend), §3 (object /
  component editing), §4 (inline mesh editing), §5 (content editors), §7
  (render-effects toggle) and §9 (remaining-gaps list) now match the code, and
  `src/ruby/TODO.md` "Structured scene editor" / "Surface the ImGui-edition
  tools" reflect only the open items. `docs/web_swordigo_editor_research.md` §4
  carries a port-status banner: **rows 1, 2, 3, 4, 8 and 10 — all six "high"
  items — are now ported**; 5 (APK) and 12 (validate-on-save) are partial.
  Note the original task text said scale payload scaling and snapshot undo did
  NOT exist; both landed after it was written (§1.2 / §1.3), so the annotation
  records them as done. Also fixed a false premise in §6.1 below (the ImGui
  edition *does* have multi-select).
  **DoD met:** verified each flipped row against the code
  (`scene_paste_component`, `push_scene_snapshot_undo`, `scene_scale_object_payload`,
  `InspectorPanel` rotation presets, `TemplatePalettePanel` / `add_model_object` /
  `scene_build_local_aabb`, `TemplateInspectorPanel`, `AudioViewerPanel`,
  `src/ruby/render/{water,portal,particle}_*.cpp`). *No code.*

### Phase 1 — Correctness & undo (do first: fixes the bug class from earlier sessions)

- **1.1 ✅ Connect component paste (web editor's headline feature — was half-built).**
  **Source:** Web §5.3 + ImGui `draw_object_inspector` paste path.
  **Collision:** both editions paste components; web's fresh-ID logic is the
  better one and **already exists** as `av::scene_paste_component` +
  `scene_loader.cpp:1658`. The InspectorPanel emits `componentPasted(object_idx)`
  but **nothing connects it** — the Paste button currently does nothing.
  **Spec:** connect `InspectorPanel::componentPasted` in `ruby_main_window.cpp`
  to a handler that (1) `scene_snapshot` for undo, (2) reads the static
  `s_component_clipboard`, (3) calls `av::scene_paste_component`, (4) refreshes
  viewport + inspector + hierarchy + FileRift text, (5) marks doc dirty.
  **Files:** `ruby_main_window.cpp`, `inspector_panel.{h,cpp}`, `scene_loader.*`.
  **Acceptance:** select object A, copy a component, select object B, Paste →
  component appears with a fresh instance ID (old one untouched), undo restores.
  **Test:** extend `tests/scene_edit_acceptance_test.cpp` (or new
  `component_clipboard_test.cpp`) — copy→paste→assert `type_id` differs + round trip.

- **1.2 ✅ Scene-wide snapshot undo/redo (web editor's core design).**
  **Source:** Web §5.2 (full-file encode snapshots, 100 deep, no-op skip) + ImGui
  `snapshot_scene` (per-drag snapshots). **Collision:** ImGui = ad-hoc session
  snapshot; Web = unified full-file snapshot. **Winner: Web model**, surfaced on
  the existing per-scene `QUndoStack` (`m_scene_undo_stacks`) so Ctrl+Z keeps
  working: add one `SceneSnapshotUndoCommand` storing the encoded `SceneData`
  bytes; every mutating op (add/remove/paste component, create/duplicate/delete/
  move object, mesh-edit commit, inspector field change) pushes it; gizmo drags
  keep `TransformUndoCommand` (cheaper) but can reuse the snapshot command on
  release. ImGui's "snapshot at drag start" is preserved by calling snapshot
  before the first mutation, exactly like web's `snapshot()`.
  **Files:** `viewport_3d_widget.{h,cpp}`, `scene_loader.*` (add
  `scene_serialize`/`scene_deserialize` helpers if missing), `ruby_main_window.cpp`.
  **Acceptance:** any 5 sequential edits undo/redo in reverse order; no-op
  snapshots don't add entries; undo after mesh edit returns the polygon.
  **Test:** `tests/scene_undo_test.cpp` — script edits via loader, undo all,
  assert byte-identical scene.

- **1.3 ✅ Scale gizmo also scales payload data (web `scaleObjectData`).**
  **Source:** Web §5.4 (scales object LocalAABB + Shape rect/circle/polygon +
  CollisionShape radius on gizmo scale) + ImGui (scale ratio semantics, already
  in `ruby_gizmo.cpp`). **Collision:** merge — keep the fixed ImGuizmo-style
  ratio math, add web's payload propagation on gizmo end.
  **Spec:** new `av::scene_scale_object_payload(scene, idx, sx, sy, sr)` in
  `scene_loader.*` (port `scaleObjectData`; radius uses max(sx,sy)); call it
  from the gizmo scale-end path; scale non-model objects uniformly (web uses the
  dominant-axis ratio for models, per-axis for others — port that rule).
  **Acceptance:** scale an object with a CollisionShape; collision gizmo/overlay
  matches the visual; LocalAABB updated; undo restores both.
  **Test:** `tests/scene_scale_payload_test.cpp` — construct object with Shape +
  CollisionShape, scale ×2, assert payload floats doubled.

### Phase 2 — Editing surface (ImGui inspector × web schema-driven properties)

- **2.1 ✅ Schema-driven component field editors (web's typed property grid).**
  **Source:** Web §3.2/§4 row 3 (auto editor per protobuf field from
  `Ft.types`) + ImGui inspector fields. **Collision:** ImGui = hand-written
  inspector rows; Web = schema-generated for *every* field incl. unknown ones.
  **Winner: Web**, reusing Ruby GG's existing `filerift_schema.cpp` field table.
  **Spec:** in `InspectorPanel`, render each `SceneComponentField` with a control
  matched to the schema (float→spinbox, int→spinbox, string→line edit, Program→
  "Edit Lua" button opening the Script IDE, nested messages→group box); keep the
  current copy/remove buttons; unknown fields render as hex/raw with the field
  number shown. Wire `scene_set_component_field` + undo (1.2) per change.
  **Acceptance:** every field of a Portal/Health/Sprite component edits + round
  trips; a field added by a newer game version still edits as raw bytes.
  **Test:** `tests/component_field_editors_test.cpp` — set fields via the same
  code path, re-encode, assert equality.

- **2.2 ✅ Rotation presets + roll (ImGui convenience, small).**
  **Source:** ImGui §3 (`scene_editor_parity.md` §3 "roll presets"). Web has no
  equivalent. **Winner: ImGui** (add to InspectorPanel transform group as
  0/90/180/270 buttons for rotation, using the existing rotation spinbox path +
  undo).
  **DoD:** buttons exist, set rotation exactly, undoable. **Test:** fold into
  2.1's test or `scene_edit_acceptance_test.cpp`.

- **2.3 ✅ Template palette dock — "Add to Scene" with computed LocalAABB.**
  **Source:** ImGui `draw_object_browser` (template palette) + Web "Add to
  Scene" (auto-measure POD bounds → LocalAABB, §3.4). **Collision:** ImGui =
  browser list of templates; Web = per-model drop with measured bounds.
  **Winner: both merged** — a dock listing scene templates (from `*.scl` +
  ObjectLibrary) *and* model assets; dropping/adding a model runs
  `Ya`-equivalent bounds measurement (Ruby GG: `pod_loader` AABB) and calls
  `scene_create_object` with a Model component + LocalAABB (web `createObject`
  port). Reuse `src/ruby/database/` component catalogs.
  **Acceptance:** add a template object and a model object; model gets a correct
  LocalAABB; both undoable; identifier deduped.
  **Test:** `tests/template_sources_test.cpp` (scan + wire builders + link/
  materialize lifecycle) + `scene_undo_test.cpp` (viewport add/retarget/undo).
  **Done:** `TemplatePalettePanel` dock (templates from scene libraries +
  scanned `.scl`, models from `.pod` scan, search, Add to Scene); viewport
  `add_template_object` (linked or component-carried for non-imported .scl) +
  `add_model_object` (Model component + measured LocalAABB, deduped id);
  `av::scan_template_sources` / `scene_find_template` / `scene_make_model_component`
  / `scene_build_local_aabb`; all undoable via 1.2's snapshot stack.

- **2.4 🟡 SCL template studio + inheritance UI (best of both).**
  **Source:** ImGui `scl_studio_*` (edit a library tile, sync back) + Web
  `unlinkTemplate`/dimmed inherited components (§3.2). **Collision:** ImGui =
  editor for the library file; Web = inheritance virtualization in the object
  tree. **Winner: both** — (a) open any `.scl` as structured rows (add/remove/
  rename templates, then save back byte-exact), (b) in the scene hierarchy,
  show template-provided components dimmed with an "Inherited" tag and an
  "Unlink Template" action that materializes them into the object
  (`materializeComponents` + `unlinkTemplate` port into `scene_loader.*`).
  **Acceptance:** template edits round-trip; unlink adds components + scale
  correction (web multiplies field 7 by template scale — port that).
  **Test:** `tests/scl_studio_test.cpp`.
  **Done (part b — inheritance UI):** `TemplateInspectorPanel` dock shows the
  selected object's template: combo retarget (typed or from every known
  template), status (link/override count/missing .scl), component tree with
  `[local]` vs dimmed `[inherited]`, and actions Override Selected / Unlink &
  Materialize / Reset to Template — all through undoable viewport ops
  (`set_scene_object_template`, `scene_override_inherited_component`,
  `scene_materialize_object_template`, `scene_apply_clean_template`), which sync
  to the 3D view + FileRift text + save via the standard `sceneEdited` path.
  **Part a progress (2026-09):** the loader layer is done. `scl_add_template` /
  `scl_rename_template` / `scl_remove_template` join the existing
  `scl_load_templates` / `scl_update_template` / `scl_save_to_file`; each one
  rewrites only the entry it touches and re-emits every other field of the
  library verbatim, so an untouched `.scl` round-trips byte-exact and an edited
  one loses nothing outside the edit (rename patches only the object's Name
  field, keeping unknown fields and the template scaling). Covered by
  `tests/scl_studio_test.cpp` (registered in `CMakeLists.txt`): add/rename/
  remove, prefix-exact preservation on append, duplicate-name refusal with the
  bytes left untouched, remove-keeps-unknown-fields, and a byte-exact
  save/reload round trip.
  **Remaining (part a):** the `.scl` studio **GUI** — a dock that opens an
  ObjectLibrary as structured template rows (list/rename/add/remove + edit the
  template's scaling, Save through `scl_save_to_file`) — plus the
  scale-correction fold.

### Phase 3 — Viewport preview parity (Web's render toggles)

- **3.1 ✅ Animated water + particle preview toggles (Web's standout renderer).**
  **Source:** Web §3.3 (water vertex animator `PS`, particle engine `DS` with 5
  emitter types, gated behind "Render Effects (WIP)"). ImGui = research-only
  (parses but no surface). **Winner: Web** (animation logic is portable math).
  **Status: COMPLETE.** Native forward GL pipeline with dynamic double-sine wave
  surface displacement, scrolling UVs, front/surface color gradient, procedural
  multi-arm swirling portal vortex billboards, 5-type particle engine with fire
  torch/brazier emitters, camera-facing billboard batching, and point light torch
  flicker modulation. Wired to `[FX]` button on in-viewport toolbar and View menu.
  **Spec:** parse WaterMesh + ParticleEmitter/FireEmitter components in the
  viewport's per-scene build (data is already decoded), add a "Render Effects"
  toolbar toggle; animate water height via the existing mesh update path and
  particles as simple quads with the web emitter parameter tables (`LS`).
  Keep it off by default. **Acceptance:** water ripples and fire emitters move
  with the toggle; toggle off restores static ground render.
  **Test:** `tests/render_effects_test.cpp` (offscreen GL) — assert particle
  count > 0 after ticks with effects on.

- **3.2 ☐ Frame camera at spawn point (ImGui parity §7/§9 gap).**
  **Source:** ImGui `frame_scene_at_spawn`. Web = frame-selection only.
  **Winner: ImGui.** On scene load, if a SpawnPoint object exists, frame it;
  else frame bounds (current behavior). **Test:** extend
  `tests/scene_edit_acceptance_test.cpp`.

- **3.3 ☐ Walkable GroundPolygon overlay in the 3D viewport.**
  **Source:** ImGui parity §4 row (overlay TODO) + mesh editor's polygon overlay
  (reuse the drawing pass from `mesh_edit`). **Winner: merge** — persistent
  translucent polygon fill + outline for selected ground objects outside mesh
  edit mode (mesh-edit mode already has it). **Test:**
  `tests/scene_mesh_edit_test.cpp` extension or new overlay test.

### Phase 4 — Asset & file layer (Web-only capabilities)

- **4.1 🟡 APK session workflow — Import → Edit → Export (design: `docs/PHASE4_APK_SESSION_DESIGN.md`).**
  Web §3.1/§4 row 5, redesigned per the product ask: Ruby GG already has the
  backend (`ruby_cli apk extract/build/sign`, byte-verified) — this links it to
  the GUI as a *session*, not a read-only view.
  **Spec:** File ▸ Import APK… extracts the whole APK into
  `~/.ruby/apk-sessions/<id>/` and writes a `session.json` registry
  (`apk_origin`, id, timestamp). Project/asset roots point at the extraction;
  the user edits normally (3D scene edits, FileRift text, textures — all
  loose files in the extracted tree). File ▸ Export as APK… repacks the tree
  (shared zip writer, CRC-diff optional), writes to `apk_origin` when it still
  exists else a file dialog, signs, then asks to delete the session copy.
  **Signing (per request):** native C v1 (JAR) signer with a built-in demo
  debug key — SHA-1/SHA-256 + RSA-2048 + PKCS#7, ~600 lines, no runtime deps
  (feasible; v1-only installs on all Android for Swordigo's old targetSdk).
  Fallback per the user's call: **unsigned APK with a loud warning**; the
  `apksigner.jar` (java) path stays as an option. Zip reader/writer moves from
  `ruby_cli.cpp` to a shared `src/platform/zip_archive.{h,cpp}` so the GUI and
  CLI never drift.
  **Split:** 4.1a import/session+registry+roots · 4.1b repack/export+cleanup ·
  4.1c native v1 signer (or unsigned fallback).
  **Acceptance:** import a game APK → edit a scene → export → re-extract:
  changed entries differ, everything else is byte-identical; the APK installs
  on an emulator. **Test:** `tests/apk_roundtrip_test.cpp` +
  `tests/apk_v1_sign_test.cpp`.
  **Done (4.1a — import/session+registry+roots):** zip reader/writer moved out
  of `ruby_cli.cpp` into the shared `src/platform/zip_archive.{h,cpp}`
  (`zip::read_entries/read_entry/write_archive/extract_all`, external_attr
  preserved on repack; CLI re-plumbed over it — `ruby_cli apk extract` still
  byte-identical); new `src/tools/apk_session.{h,cpp}` (`apk::Session`,
  `import_apk` with fail-fast validation — must be a ZIP with
  AndroidManifest.xml + assets/, extraction + session.json registry under
  `~/.ruby/apk-sessions/<id>/`, `save/load/list/remove_session`;
  `$RUBY_SESSIONS_DIR` override for hermetic tests); GUI: File ▸ Import APK…
  (`ApkSessionPanel` status-bar badge in `ruby/editor/apk_session_panel.{h,cpp}`)
  points ProjectContext + Asset Browser roots at the extracted tree and feeds
  the engine-preview dock; File ▸ Export as APK… added but gated off until a
  session is active (its slot explains 4.1b is next).
  **Remaining:** 4.1b repack/export (target = origin or dialog, CRC-diff,
  cleanup ask) · 4.1c native v1 signer (or unsigned fallback).
  **Test (4.1a):** `tests/apk_session_test.cpp` — fixture APK → import →
  byte-identical entries, traversal/absolute names contained, session.json
  round trip, list/remove, fail-fast validation.

- **4.2 ☐ GLB/GLTF → POD import.** Web §3.5 (lazy `glbToPod` chunk). Ruby GG has
  partial support in `ruby_tools_workspace.cpp`. Finish: mesh+material+texture
  conversion writing an AB.POD.2.0 file via the existing POD writer, then the
  new model appears in the asset browser and "Add to Scene" works. **Test:**
  `tests/glb_to_pod_test.cpp` with a tiny generated GLB.

- **4.3 ☐ Decoded-text editor with validate-on-save for all protobuf assets.**
  **Source:** Web §3.6 ("Decode to Clipboard", validate-on-save). Ruby GG's
  Script IDE already decodes `.scene`/`.scl` etc. — add the validation gate:
  before writing, re-parse the text (FileRift `decode→encode` round trip) and
  abort with the error surfaced (ImGui `compile_scene_source` parity, but for
  every protobuf kind, not just scenes). **Test:** extend
  `tests/scene_smart_save_test.cpp` with a corrupt-text case per kind.

- **4.4 ☐ Lua API meta for the Script IDE.** Web §5.6 (full `---@meta` of the
  game API). Ruby GG's IDE has syntax highlighting but no API autocomplete.
  Add the meta text (from the web bundle or `docs/sre13` Lua surface) as an
  autocomplete/API-completion source in `ScriptIDEWidget`. **Acceptance:**
  typing `Camera.` suggests `Rumble`, `FocusAtShape`, etc. **Test:** unit test
  on the completion provider.

### Phase 5 — Content tools (ImGui-only, lower priority)

- **5.1 ☐ Texture painter + save-back.** ImGui `tex_edit_save_png/tex`.
  Extend `TextureViewerPanel` from view-only to paint (brush/color/undo), then
  re-encode `.tex`/`.png` save-back with the existing codecs. **Test:**
  `tests/texture_paint_test.cpp`.
- **5.2 ☐ Object thumbnail browser** (ImGui `obj_browser_make_thumb`): render
  each POD's first frame to a small pixmap cache for the 2.3 palette.
- **5.3 ☐ MCP console** (ImGui `draw_mcp_console`): check current MCP wiring in
  `src/ruby/`; add a console panel if missing (Ruby GG has a Raijin Lua console
  already — likely merge).
- **5.4 ☐ Scene creator / procedural generator surfacing**
  (`open_scene_creator`, `draw_procedural_generator`): verify
  `RubyToolsWorkspace` coverage; add the missing generation forms.
- **5.5 ☐ RubyMesh "Apply zones to scene"** — decode `.rbm` zones into
  ground-mesh objects (today only posts a status message).
- **5.6 ☐ GroundMeshCanvas ghost-preview + keyboard nudge** (uses
  `would_cross_at`).

### Phase 6 — Polish / perf

- **6.1 ☐ Multi-select + batch move.** **Source (corrected 2026-09, task 0.0):**
  ImGui **has** this — `ViewerState::scene_selection`, Ctrl+click toggle, union
  -bounds framing, and a multi-object clipboard (`asset_viewer.cpp` ~12223,
  ~5466; `scene_object_clipboard_multi`). Web has none, and Ruby GG is single
  -selection today, so this is a real ImGui parity gap, not a greenfield
  feature. **Winner: ImGui's model**, lifted onto the viewport: Ctrl+click
  selection set, gizmo on the centroid, batch transform undo (one snapshot),
  batch delete/duplicate. **Test:** `tests/multi_select_test.cpp`.
- **6.2 ☐ Interleave viewport vertex data + process-wide model GPU cache**
  (`src/ruby/TODO.md` viewport-perf items).
- **6.3 ☐ Background toggle in the viewport** (`scene_editor_parity.md` §1) —
  Web has "Show Background"; ImGui toggles backgrounds. Add toolbar toggle.

---

## 3. Deliberately skipped (research notes live in the docs)

| Feature | Why skipped | Doc |
|---|---|---|
| Terrain sculpting / heightmap painting | Neither edition has a working sculpt tool | `scene_editor_parity.md` §6 |
| Per-object light / water editing surfaces | Scene data is parsed; neither tool edits it | `scene_editor_parity.md` §6 |
| ETC1/PVR software decoders | Ruby GG uses native SDL_image — strictly better | `web_swordigo_editor_research.md` §8 |
| Hat/dome authoring in the inline editor | boulder supports hats; surfacing is a later nicety | `scene_editor_parity.md` §4 |
| Full Monaco editor in Ruby GG | Script IDE is native, virtualized, faster | `parity_ruby_gg_vs_ruby_imgui.md` §6 |

## 4. Suggested execution order (short version)

0.0 → 1.1 → 1.2 → 1.3 → 2.1 → 2.2 → 2.3 → 2.4 → 3.1 → 3.2 → 3.3 → 4.1 → 4.2 →
4.3 → 4.4 → 5.1 … 5.6 → 6.1 … 6.3

**Progress (2026-09):** 0.0, 1.1, 1.2, 1.3, 2.1, 2.2, 2.3, 3.1 are ✅ complete.
2.4 and 4.1 are 🟡 (2.4: inheritance UI done, `.scl` file studio open; 4.1:
import/session done, repack/export + signer open). **Next in order: 2.4a — the
`.scl` template file studio**, then 3.2 (frame camera at spawn point).

Phases 1–2 deliver the biggest user-visible parity in the fewest changes
(component paste is literally one missing `connect`), Phase 3 is the web
editor's unique value, Phase 4 is new capability neither edition fully has in a
desktop tool.