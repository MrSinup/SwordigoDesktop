# 03 — Camera Modifier Gap

## Definition
"Camera modifier" = the editor's ability to **change how the scene is viewed**
(projection, zoom, framing) and to **edit camera-related scene data**, matching
the web editor.

## Web editor (reference)
| Capability | Evidence (`index.beautified.js`) |
|-----------|-----------------------------------|
| Perspective camera | `PerspectiveCamera`, `isPerspectiveCamera` (line 18689) |
| **Orthographic camera** + zoom | `OrthographicCamera`, `zoom`, `left/right/top/bottom` (9901, 9934); `makeOrthographic` (2583) |
| Ortho depth math | `viewZToOrthographicDepth`, `orthographicDepthToViewZ` (6623–6626) |
| Camera-aware gizmo scaling | ortho vs perspective branch (27210) |
| Camera-relative shading | `uniform bool isOrthographic;`, `cameraPosition` (11497/11499) |
| Shadow cameras per light | `shadowCameraNear/Far` (6705, 8975, 12086) |

So the web "camera modifier" surface = **projection switch + zoom + framing +
camera-aware helpers**, wired straight into the render pipeline.

## Native today
- `av_renderer.h` `Camera` is a **single orbit camera** — yaw/pitch/distance/
  target/fov/near/far. **No orthographic path, no zoom-as-ortho, no projection
  toggle.**
- `camera_override.*` is a **runtime free-cam for the running game** (engine
  `CameraController` hooks), **not** an editor camera and **not** tied to the
  `av_renderer` viewport.
- There is **no `CameraComponent` / `CameraModifier`** in the schema on either
  side — camera is a viewer concern, so parity = matching the viewer, not adding
  a data component. (Confirmed: grep for `CameraModifier`/`CameraComponent`
  returns nothing in either codebase.)

## The gap (research conclusions — not implemented)
1. **No orthographic projection** in `av_renderer`. To reach parity, `Camera`
   needs a `bool orthographic` + `float ortho_zoom` and `begin_3d` must build an
   ortho projection matrix when set (mirror `makeOrthographic`).
2. **No projection toggle / zoom control** in the asset-viewer or map-editor UI.
3. **No camera framing helpers** (frame-selected, reset-to-fit) equivalent to
   the web editor's camera-aware behaviour.
4. **No shadow-camera near/far exposure** — native lights (`set_point_lights`,
   `set_directional_lights`) don't carry per-light shadow-camera params the web
   pipeline uses.

## Where implementation would live (for later)
- `src/tools/av_renderer.h` — extend `struct Camera` (ortho fields).
- `src/tools/av_renderer.cpp` — `begin_3d` projection-matrix branch.
- `src/tools/asset_viewer.cpp` — UI toggle + zoom slider + framing buttons.

> **Do not implement yet — this file is the spec of the gap only.**
