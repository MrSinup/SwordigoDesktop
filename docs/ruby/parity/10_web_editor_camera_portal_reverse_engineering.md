# Web Editor Camera & Portal Logic — Reverse-Engineered & Reimplemented

Source: `OpenSwordigo/resources/Ruby/szkot.xyz/SwordigoEditor/assets/main.js`
(the downloaded, beautified web editor — 34,511 lines).

## 1. The web editor is Three.js

Confirmed by the renderer internals: `isOrthographicCamera`, `matrixWorldInverse`,
`projectionMatrix`, `viewZToOrthographicDepth`, `viewZToPerspectiveDepth`, the
standard Three.js `#ifdef USE_INSTANCING` MVP chain, etc.

### Camera
- The editor simply swaps a Three.js `OrthographicCamera` / `PerspectiveCamera`
  and toggles `isOrthographic` on the material uniforms. There is no bespoke
  "CameraModifier" class — it is standard perspective/ortho projection.
- **Native equivalent (already implemented):** `Camera.orthographic` + `ortho_zoom`
  in `av_renderer.h`, `mat4_ortho()`, and the branch in `camera_get_projection()`.
  This is faithful. The "camera controller not rendering" report was NOT a camera
  bug — it was the duplicate **Frame** toolbar button causing an ImGui ID
  collision (fixed separately).

## 2. Portal data model (exact wire tags)

From the editor's schema tables:

### `PortalComponent`
| tag | field           | kind   |
|-----|-----------------|--------|
| 12  | SpawnPointName  | str    |
| 18  | TapToEnter      | scalar |
| 20  | TriggerShapeId  | str    |
| 1a  | SpriteName      | str    |

### `PortalEffectComponent`  (visual swirl)
| tag | field            | type       |
|-----|------------------|------------|
| 08  | PolygonId        | scalar     |
| 10  | TextureMappingId | scalar     |
| 22  | Speed            | Vector3    |
| 1a  | Color            | FloatColor |

### Component type codes
`PORTAL: 500`, `FIRE_EMITTER: 253`, `SPAWNPOINT: 501`, `LIGHT: 130`, …

## 3. How the editor decides "this object is a portal"

The editor flags a portal in **three** independent ways (icon logic `Pd()` +
schema): 
1. object carries a `PortalComponent`,
2. object carries a `PortalEffectComponent` (visual-only portal),
3. a Sprite/object component with **`SpecialType == 2`** (field tag 40 —
   documented as `2: Portal`).

Ruby previously only recognised (1), which is why effect-only / SpecialType
portals never rendered.

## 4. Native reimplementation (improved for our SDK)

Unlike the web editor (a Three.js data-editor that only shows portals as icons /
placeholder quads), Ruby draws a **real animated GPU swirl**. Improvements:

- **Detection parity** (`scene_loader.cpp::resolve_object_render_data`): `is_portal`
  is now set for `PortalComponent`, `PortalEffectComponent`, AND `SpecialType == 2`.
- **Robust field reads** (`asset_viewer.cpp` portal pass): the swirl reads `Color`
  and `Speed` by resolved field name, and the object is matched by `is_portal`
  plus the `PortalEffectComponent` payload tag (4058) / `type_id` — never by the
  frequently-empty raw `type_name` string (the old bug).
- **Rendering** (`av_renderer.cpp::render_portal_effect`): camera-facing additive
  billboard, rotating radial swirl animated by `Speed`, bright core + animated
  rim band, tinted by `Color`. Feeds the PostFX bloom bright-pass. Works in both
  perspective and orthographic projection.

## 5. Status
All wired, `cmake --build . --target ruby` clean (zero errors/warnings).
