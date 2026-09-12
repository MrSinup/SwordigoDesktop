# 05 — Portal Renderer Gap (PortalComponent / PortalEffectComponent)

Deep dive on the single most-requested parity item: a **live portal renderer**
in the editor.

## 1. The data model is already identical on both sides ✅
### Native protobuf schema (`src/tools/filerift.cpp`)
```
// Component registry
Component 500 → PortalComponent            (filerift.cpp:529)
Component 507 → PortalEffectComponent      (filerift.cpp:535)

// PortalComponent fields (filerift.cpp:903-906)
1  DestinationSceneName  (string)
2  SpawnPointName        (string)
3  TapToEnter            (varint/bool)
4  TriggerShapeId        (varint → CollisionShape)

// PortalEffectComponent fields (filerift.cpp:938-941)
1  PolygonId             (varint)
2  TextureMappingId      (varint)
3  Color                 (FloatColor)
4  Speed                 (Vector3)

// Map connectivity (different concept) — MapNode_Portal (filerift.cpp:377-380)
1 DestinationName, 2 Direction, 3 PassDirection, 4 IgnoreInNodePositioning
```
### Web editor
- Same `PortalComponent` (line 21683) and `PortalEffectComponent` (21707)
  schema entries; `Portal` / `PortalHint` / `PreviousPortalLevel` fields;
  `Game.EnterPortal(scenename, portalname)` Lua hook (25127).

**Conclusion:** parity is *not* a schema problem. Native can already read/write
these components byte-exact (`scene_generator.cpp:649,705` builds them).

## 2. What renders a portal today
| # | Renderer | File | Draws where | Notes |
|---|----------|------|-------------|-------|
| 1 | 2D graph edge + badge | `map_editor.cpp:834,1027` | Map editor (2D) | World-map connectivity only |
| 2 | Runtime swirl quad | `fbo_scaler.cpp:74,199-200` | **Live game** | `g_prog_portal`, `portal_effect_2x.pvr`, SRE-driven `g_portal_*` |
| 3 | Template FX author | `scene_generator.cpp:705` | none (writes bytes) | Template `"portal"` in `game_common.scl` |

## 3. The gap
- **No editor-viewport portal renderer.** `av_renderer` has no function that
  takes a `PortalEffectComponent` (or its `Color`/`Speed`/`PolygonId`/
  `TextureMappingId`) and draws the swirl in the scene-edit / asset-view FBO.
- The runtime renderer (#2) is coupled to SRE frame data (`g_portal_world_x/y/z`,
  `g_portal_vp_matrix`, `g_portal_color_*`, `g_portal_intensity`,
  `g_portal_speed`), so it cannot be driven from static editor scene data as-is.
- Result: placing / editing a `PortalComponent` in native gives **no visual
  feedback**, unlike the web editor.

## 4. Findings for a future implementation (spec only)
1. Add an `av_renderer` entry point, e.g. `render_portal_effect(const Camera&,
   model_matrix, color[3], speed[3], float time)` — **reuse the existing
   `fbo_scaler` portal shader/texture** rather than authoring a new one.
2. Feed it from the loaded scene's `PortalEffectComponent` fields (already
   parsed by `scene_loader.cpp` / `scene_schemas.cpp`) instead of SRE globals.
3. Resolve `PolygonId` / `TextureMappingId` against the scene's polygon &
   texture-mapping tables (native already loads these for `WaterMeshComponent`
   /`GroundPolygonComponent`).
4. Draw a selectable **trigger-shape outline** from `TriggerShapeId` so the
   `PortalComponent` collision zone is visible (parity with web picking).

## 5. Explicit non-goals of this doc
- No shader is to be written now.
- No wiring of `av_renderer` to `scene_loader` now.
- This is the **gap spec**; implementation is deferred per the request.
