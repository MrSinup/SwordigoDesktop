# 07 — Missing Features Master List (Backlog, NOT Implemented)

Prioritized parity backlog derived from files 01–06. **Nothing here is built.**
Each item cites the gap doc and the native file where work would land.

## P0 — Explicitly requested gaps
### 1. Camera modifier (projection + zoom + framing)
- **Gap:** file 03. Native `av_renderer::Camera` is orbit-only; no orthographic
  projection, no zoom-as-ortho, no frame-selected.
- **Would land in:** `src/tools/av_renderer.h` (`struct Camera` fields),
  `av_renderer.cpp` (`begin_3d` projection branch), `asset_viewer.cpp` (UI).
- **Reference:** web `OrthographicCamera`/`makeOrthographic`/zoom.

### 2. Camera & portal renderers (in editor viewport)
- **Gap:** file 04. No camera-frustum preview renderer; no portal draw in the
  editor FBO. Three disconnected portal renderers exist (map 2D edge, runtime
  post-fx, template author) but none render editor scene data.
- **Would land in:** `src/tools/av_renderer.cpp` (new draw entry points reusing
  `fbo_scaler` portal shader + `render_glow_sprite`).

### 3. Portal renderer (PortalEffectComponent live preview)
- **Gap:** file 05. Schema is complete (`filerift.cpp:903-941`); no editor draw.
- **Would land in:** `av_renderer` `render_portal_effect(...)` fed by
  `scene_loader` `PortalEffectComponent` fields (Color/Speed/PolygonId/
  TextureMappingId) + trigger-shape outline from `PortalComponent.TriggerShapeId`.

## P1 — Interaction parity
### 4. Transform gizmos (translate / rotate / scale)
- **Gap:** web `TransformControls` (file 01 §3, lines 26723-26726) + raycast
  picking (`Raycaster`, 18689). Native has no 3D manipulator in the viewport.
- **Would land in:** `asset_viewer.cpp` + `av_renderer.cpp` (ImGuizmo already
  vendored at `src/imgui/Guizmo/` — reuse it).

### 5. Raycast object picking in the 3D viewport
- **Gap:** select a scene object by clicking it in 3D (web `Raycaster`). Native
  selection today is list-based (asset viewer) / node-based (map editor).

## P2 — Visual preview parity for other components
### 6. Live previews for Light / Particle / Water components in a unified scene view
- **Gap:** file 06 matrix. Native renders some of these at runtime or in the
  asset viewer, but not as a single editable scene preview like the web editor.

## P3 — Nice-to-have (from web pipeline)
### 7. Per-light shadow-camera params (near/far/bias/radius/intensity)
- **Gap:** web pipeline exposes `shadowCamera*` (file 01 §1); native
  `set_point_lights`/`set_directional_lights` don't carry them.

## Non-goals / already at parity (do not re-do)
- **Component schema** — already identical (file 06). No work needed.
- **Byte-exact scene/portal authoring** — native already ahead
  (`scene_generator.cpp`).
- **Runtime free-cam / sky / portal post-fx** — native already has these in
  `src/game/` + `src/platform/`; they are *runtime*, separate from the editor
  parity work above.

## Suggested implementation order (when work begins)
1. Camera modifier (ortho + zoom + framing) — unblocks everything visual.
2. Portal effect renderer in viewport (reuse `fbo_scaler` shader).
3. Camera frustum / portal trigger-shape overlays.
4. ImGuizmo transform gizmo + raycast picking.
5. Unified light/particle/water previews.
6. Shadow-camera params.

---
*All items above are research conclusions. Implementation is deferred by request.*
