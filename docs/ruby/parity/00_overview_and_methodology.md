# Ruby Parity — Web Editor vs Native Ruby Tools (Reverse-Engineering Research)

> **Status:** Research only. Nothing in this folder is implemented yet. These
> documents catalog the gap between the open-source **SwordigoEditor web app**
> and the **native "Ruby" C++ tools** in `src/tools/` + `src/game/`, so the
> missing features (camera modifier, camera/portal renderers, portal renderer,
> etc.) can be implemented later at parity.

---

## 1. Sources analyzed

### Web editor (reference implementation)
- **URL:** `https://szkot.xyz/SwordigoEditor/index.html`
- Downloaded with `wget` (recursive, assets pulled): `index.html`,
  `assets/index-u_qz_Wej.js` (819 KB minified), `assets/index-CoXoKe-t.css`.
- The single bundle `index-u_qz_Wej.js` **is** the "main.js / backend" — a Vite
  build. It was beautified with `js-beautify` →
  `index.beautified.js` (**31,818 lines**, 1.19 MB) for readability.
- **Stack:** React + **Three.js** (`WebGLRenderer`, `PerspectiveCamera`,
  `OrthographicCamera`, `TransformControls` gizmo, `Raycaster`), full PBR
  material/shadow pipeline.

### Native Ruby tools (the codebase we are bringing to parity)
Correct working set is **`src/tools/`** (NOT `RubyMobile/`) plus `src/game/`
and one `src/platform/` file:

| Area | File(s) | Lines |
|------|---------|-------|
| Asset viewer (ImGui) | `src/tools/asset_viewer.cpp` | 14,335 |
| Modern GL renderer | `src/tools/av_renderer.cpp` / `.h` | 3,130 |
| Map editor (2D graph) | `src/tools/map_editor.cpp` / `.h` | 1,702 |
| Scene loader / schema | `src/tools/scene_loader.cpp`, `scene_schemas.cpp` | 1,938 / 1,028 |
| Protobuf schema | `src/tools/filerift.cpp` | 1,522 |
| Scene generators | `src/tools/scene_generator*.cpp` | 1,340+ |
| Object categories | `src/tools/scene_categories.h` | — |
| Camera override (runtime) | `src/game/camera_override.cpp` / `.h` | 216 / 120 |
| Procedural sky | `src/game/sky_renderer.h` | 207 |
| Portal post-fx (runtime) | `src/platform/fbo_scaler.cpp` | — |

> `RubyMobile/` and `OpenSwordigo/arm32/` were also inspected. `arm32` contains
> only the IDA decompile (`libswordigo_ida32.c`) + a source tarball — used as an
> **address/offset reference** for `camera_override.h`, not as an editor source.

## 2. Method
1. Beautify the web bundle; grep the class / component / schema vocabulary.
2. Grep the same vocabulary out of the native tools.
3. Diff the two vocabularies (`comm`) — the **component schemas are identical**
   (same Swordigo protobuf), so gaps are in **rendering & interaction**, not the
   data model.
4. For each gap, locate the native code that *would* host the feature and record
   the concrete symbols/fields so implementation is a fill-in job.

## 3. Headline finding

The web editor and native tools **share the exact same component set** (`comm`
of the two `*Component` lists is empty except grep noise like `addComponent`).
Both know `PortalComponent`, `PortalEffectComponent`, all `*MonsterController`
variants, `LightComponent`, etc.

> **Therefore the parity gap is NOT the data model. It is the editor's ability to
> _render and manipulate_ that data in 3D.** The web editor is a live Three.js
> scene with a movable camera and gizmos; the native side is (a) a fixed-orbit
> asset viewer and (b) a 2D map-graph editor. The specific missing pieces are
> covered in the per-feature files below.

## 4. Index of research files
- `01_web_editor_feature_inventory.md` — everything the web editor does.
- `02_native_ruby_tools_inventory.md` — everything the native tools do today.
- `03_camera_modifier_gap.md` — camera component / modifier parity.
- `04_camera_and_portal_renderers_gap.md` — in-scene renderers gap.
- `05_portal_renderer_gap.md` — PortalComponent / PortalEffectComponent visual.
- `06_component_schema_parity_matrix.md` — full component-by-component matrix.
- `07_missing_features_master_list.md` — prioritized backlog (no code yet).
