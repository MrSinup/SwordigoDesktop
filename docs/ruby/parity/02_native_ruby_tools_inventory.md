# 02 — Native Ruby Tools Inventory (`src/tools/`, `src/game/`)

What the native (C++/ImGui) toolchain does **today**. This is the baseline the
web editor is measured against.

## 1. Asset Viewer — `src/tools/asset_viewer.cpp` (14,335 lines)
- ImGui-based viewer for POD/FBX/glTF models, animations, textures.
- Uses `av_renderer` for 3D. 324 `camera` references, but all route through the
  single fixed orbit `Camera` (see §3).
- Contains portal-aware code paths (`portal_count`, `Portals`) but as **data
  listing / asset enumeration**, not an interactive 3D portal placement tool.

## 2. Modern GL Renderer — `src/tools/av_renderer.cpp/.h` (3,130 lines)
Modern OpenGL 3.3 renderer with a real PBR-ish pipeline:
- **Camera** (`av_renderer.h:25`) is **orbit-only**:
  ```cpp
  struct Camera {
      float yaw = 0, pitch = 25, distance = 5;
      float target[3] = {0,0,0};
      float fov = 45, near_plane = 0.01f, far_plane = 1000.0f;
  };
  ```
  No orthographic mode, no free-fly, no per-scene camera object.
- Render-to-FBO for ImGui viewports (`begin_3d`, `resize_fbo`, `resize_fbo_hdr`).
- Point + directional lights (`set_point_lights`, `set_directional_lights`),
  glow sprites (`render_glow_sprite`, `render_point_light_glows`), contact
  shadow blobs (`render_shadow_blob`), animated water/lava sheets
  (`WaterSheetData`).
- Skinned mesh vertex layout (bones/weights).
- **No portal renderer, no transform gizmo, no camera-component driver.**

## 3. Map Editor — `src/tools/map_editor.cpp/.h` (1,702 lines)
- **2D node-graph** editor for the world map (zones → nodes → portals).
- Portals are **graph edges** between map nodes, drawn as thin 2D links
  (`map_editor.cpp:834–869`), with a portal badge icon
  (`ui_map_icon_portal.png`, line 389/1027).
- `MapTool::AddPortal` workflow: click source node → click dest node → writes a
  `Portal` protobuf field (`mk_add_msg(*nf, "Portal")`, lines 1146–1166).
- Portal here = **`MapNode_Portal`** (map connectivity), which is a *different*
  thing from the in-scene **`PortalComponent`** (see file 05).
- **No 3D preview of the portal, no PortalEffect visual.**

## 4. Scene schema / loaders
- `filerift.cpp` (1,522) — the **protobuf schema registry**. Fully defines
  `PortalComponent` (fields `DestinationSceneName`, `SpawnPointName`,
  `TapToEnter`, `TriggerShapeId`) and `PortalEffectComponent` (`PolygonId`,
  `TextureMappingId`, `Color`, `Speed`) — lines 903–941. Component IDs 500 /
  507 (lines 529, 535).
- `scene_schemas.cpp` (1,028) — field-level schema for all components.
- `scene_loader.cpp` (1,938) — reads/writes `.scene` files.
- `scene_categories.h` — UI categorization; `ObjCategory::Portals` →
  "Portals & Doors", icon `ICON_FA_DOOR_OPEN`.

## 5. Scene generators — `scene_generator*.cpp`
- Procedurally build scenes and can emit **functional portals**
  (`build_portal_object_bytes`, `scene_generator.cpp:649`) and **visual portal
  FX** via template `"portal"` (`build_portal_fx_object`, line 705).
- This proves the native side can *author* portal data byte-exact; it just
  can't *render/edit it live in 3D*.

## 6. Runtime (game) camera & fx — `src/game/`, `src/platform/`
- `camera_override.cpp/.h` — **runtime** camera hijack of the game engine
  (`CameraController` hooks, `FocusAtPoint`, presets, POV mode, smooth interp).
  This is a *gameplay/free-cam* feature, NOT an editor camera modifier.
  State globals: `g_cam_active`, `g_cam_mode`, `g_cam_off_{x,y,z}`,
  `g_cam_presets[5]`, `g_cam_pov_mode`.
- `sky_renderer.h` — procedural day/night sky (time_of_day, sun/moon/stars).
- `fbo_scaler.cpp` — a **runtime** post-fx portal quad renderer
  (`g_prog_portal`, `g_portal_tex` from `portal_effect_2x.pvr`, uniforms
  `loc_portal_*`, driven by `g_portal_*` globals the SRE writes each frame,
  lines 44–200). This renders the portal **in the running game**, not in the
  editor viewport.

## 7. What native has that the web editor does not
- Byte-exact protobuf authoring / round-trip of real `.scene` files.
- POD/FBX/glTF import + skinned animation playback.
- Procedural scene & map generation.
- Runtime engine hooks (free-cam, sky, portal post-fx) via SRE.

## 8. What native lacks (the parity backlog)
1. A **camera modifier**: switchable perspective/orthographic editor camera and
   editing of any per-scene camera object/component (files 03, 04).
2. **In-scene camera & portal renderers** inside the editor viewport (file 04).
3. A **live PortalEffectComponent renderer** in the editor (file 05).
4. **Transform gizmos** (translate/rotate/scale) with raycast picking.
