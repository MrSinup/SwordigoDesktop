# 04 — Camera & Portal Renderers Gap (in-editor viewport)

This file covers the **"camera port renderers"** parity item — i.e. the
renderers that draw camera-relative and portal visuals **inside the editor
viewport**.

## 1. Camera renderer (viewport)
### Web editor
- Every frame the Three.js `WebGLRenderer` renders the whole scene through the
  active camera (perspective or ortho), including camera-relative effects:
  gizmo scale by distance (line 27210), `cameraPosition` uniform feeding all
  standard materials (11497), ortho-aware raycasting (18689).

### Native
- `av_renderer::begin_3d(fbo, w, h, const Camera&)` renders through the fixed
  orbit camera only. There is **no camera-facing UI overlay renderer** (no
  camera frustum gizmo, no "what the game camera sees" preview) and **no ortho
  path** (see file 03).

### Gap
- No **camera frustum / bounds preview** renderer (the web editor implicitly
  shows framing via its live camera; native has no visual for the *game's*
  camera framing within a scene).
- No **billboarded camera icon** for any camera-tagged scene node.

## 2. Portal renderer (viewport) — see file 05 for the deep dive
### Web editor
- `PortalComponent` + `PortalEffectComponent` are **rendered live** as part of
  the Three.js scene (swirl quad / effect mesh), using the component's
  `Color`, `Speed`, `PolygonId`, `TextureMappingId`.

### Native — three separate, disconnected portal renderers exist:
| Renderer | Location | Context | Renders in editor viewport? |
|----------|----------|---------|------------------------------|
| Map-graph portal edge | `map_editor.cpp:834` | 2D world map | ❌ (2D links only) |
| Runtime portal post-fx quad | `fbo_scaler.cpp:74,199` (`g_prog_portal`, `portal_effect_2x.pvr`) | **running game**, SRE-driven | ❌ (game, not editor) |
| Portal FX template emitter | `scene_generator.cpp:705` | authoring bytes | ❌ (writes data, no draw) |

### Gap
- There is **no `av_renderer` path that draws a `PortalEffectComponent` in the
  asset-viewer / scene-editor viewport**. The only real-time portal draw lives
  in `fbo_scaler.cpp` and is bound to the **live game frame** (SRE writes
  `g_portal_world_*`, `g_portal_color_*`, `g_portal_speed` each frame), not to
  editor scene data.

## 3. Why this matters for parity
The web editor gives immediate visual feedback: you place a portal / adjust a
camera and *see it*. Native can author the exact bytes (`scene_generator`) and
render the effect at runtime (`fbo_scaler`), but the **editor cannot preview
either**, so the round-trip is blind.

## 4. Reusable native assets for a future editor renderer (inventory only)
- Portal quad shader + swirl texture already exist in `fbo_scaler.cpp`
  (`g_prog_portal`, uniforms `loc_portal_{mv_matrix,aspect,color,time,speed,
  alpha}`, texture `portal_effect_2x.pvr`). A future editor renderer could reuse
  this shader against editor camera matrices instead of SRE-supplied ones.
- `av_renderer` already has FBO, glow-sprite and billboard primitives
  (`render_glow_sprite`, `render_shadow_blob`) usable for a camera icon.

> **Research only — no code to be written from this file yet.**
