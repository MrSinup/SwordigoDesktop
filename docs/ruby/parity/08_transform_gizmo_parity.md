# 08 — Transform Gizmo Parity (web `TransformControls` vs vendored ImGuizmo)

> **Research only.** No code changes. This documents how the web editor's
> Three.js `TransformControls` works, how the vendored **ImGuizmo**
> (`src/imgui/Guizmo/`) already covers most of it in `asset_viewer.cpp`, and the
> precise remaining gaps to reach full parity.
>
> **Key correction to the P1 backlog (file 07, item 4):** ImGuizmo is **already
> integrated and working** in the native asset viewer. Transform gizmos are
> *mostly at parity*, not missing. The remaining work is small, enumerated below.

---

## 1. Web editor — `TransformControls` (reference)
Reverse-engineered from `assets/index.beautified.js`. It is stock Three.js
`TransformControls` + `TransformControlsGizmo` + `TransformControlsPlane`.

### 1.1 Configuration surface (line 26711)
```
camera, object, enabled=true, axis=null,
mode="translate",            // "translate" | "rotate" | "scale"
translationSnap=null, rotationSnap=null, scaleSnap=null,
space="world",               // "world" | "local"
size=1, dragging=false,
showX=true, showY=true, showZ=true
```
Setters: `setMode` (26789), `setSpace` (26804), `setTranslationSnap` (26792),
`setRotationSnap` (26795), `setScaleSnap`, `setSize`.

### 1.2 Behavior
- **Translate** (26750): offset in world or local space; per-axis masking via
  axis name (`X`, `Y`, `Z`, `XY`, `YZ`, `XZ`, `XYZ`); optional snap.
- **Rotate** (26761): supports single-axis (`X`/`Y`/`Z`), screen-space (`E`),
  and free (`XYZE`) rotation with `rotationSnap`. Full quaternion rotation.
- **Scale**: forces `space="local"` while scaling (27202, 27234).
- **Gizmo culling** (27214): hides axes nearly parallel/perpendicular to the
  eye; respects `showX/Y/Z`.
- **Events** (26686): emits `objectChange` on edit and `dragging-changed`
  on drag start/stop — the app snapshots/commits on these (27452–27456).
- **Camera-aware**: works with both perspective and **orthographic** cameras;
  gizmo scale adapts to ortho zoom vs perspective distance (27210).

### 1.3 How the app wires it (27581)
`gizmo.attach(obj); gizmo.setMode(mode); gizmo.showX/Y/Z = …`. On
`objectChange` → mark scene dirty; on `dragging-changed` → disable orbit +
snapshot for undo.

---

## 2. Vendored ImGuizmo (`src/imgui/Guizmo/src/ImGuizmo.h`)
The native equivalent, already in-tree.

### 2.1 API
```cpp
enum OPERATION { TRANSLATE, ROTATE, SCALE, SCALEU, UNIVERSAL,
                 // + per-axis bits TRANSLATE_X.., ROTATE_SCREEN, SCALE_XU.., BOUNDS };
enum MODE { LOCAL, WORLD };
bool Manipulate(view, projection, OPERATION, MODE, float* matrix,
                deltaMatrix=NULL, snap=NULL, localBounds=NULL, boundsSnap=NULL);
void SetOrthographic(bool); void SetRect(x,y,w,h); void SetDrawlist(dl);
void BeginFrame(); bool IsUsing(); bool IsOver();
void DecomposeMatrixToComponents(...); void RecomposeMatrixFromComponents(...);
void ViewManipulate(view, length, pos, size, bgColor);  // the view cube
void SetGizmoSizeClipSpace(float); void AllowAxisFlip(bool);
```

### 2.2 Concept mapping (web → ImGuizmo)
| Web `TransformControls` | ImGuizmo | Status |
|-------------------------|----------|--------|
| `mode "translate/rotate/scale"` | `OPERATION TRANSLATE/ROTATE/SCALE` | ✅ mapped |
| `space "world"/"local"` | `MODE WORLD/LOCAL` | ⚠️ hardcoded WORLD |
| `translationSnap/rotationSnap/scaleSnap` | `snap[3]` | ✅ (see below) |
| `showX/Y/Z` per-axis mask | per-axis `OPERATION` bits | ❌ not used |
| screen/free rotate (`E`,`XYZE`) | `ROTATE_SCREEN` | ⚠️ not exposed |
| perspective + ortho camera | `SetOrthographic(bool)` | ⚠️ hardcoded false |
| `objectChange` / `dragging-changed` | `Manipulate()` return + `IsUsing()` | ✅ mapped |
| view cube (orbit widget) | `ViewManipulate` | ✅ already used |

---

## 3. What native already does (parity ACHIEVED)
From `src/tools/asset_viewer.cpp`:
- **Setup** (7767–7770): `ImGuizmo::BeginFrame()`, `SetOrthographic(false)`,
  `AllowAxisFlip(false)`, `SetGizmoSizeClipSpace(0.14f)`.
- **State** (521): `scene_transform_mode` — `0` navigate, `1` move, `2` rotate,
  `3` scale (mirrors web `mode`).
- **Manipulate** (8803–8880): builds `view`/`proj` from `av::camera_*`, calls
  `ImGuizmo::Manipulate(view, proj, op, ImGuizmo::WORLD, matrix, nullptr, snap)`.
- **Snapping** (555, 8819): `scene_snap` toggle or hold **Ctrl**; step
  `scene_snap_step`; rotate uses **15°** snap — matches web snap semantics.
- **Undo**: snapshots at drag start (`if (!was_using && IsUsing()) snapshot_scene`)
  — equivalent to reacting to web `dragging-changed`.
- **Multi-select delta**: applies the active object's delta to the rest of the
  selection (web has no built-in equivalent — native is *ahead* here).
- **View cube**: `ImGuizmo::ViewManipulate(...)` + `decompose_view_to_camera`
  drives the orbit camera — parity with the web camera-aware gizmo.
- **Dirty flag**: `scene_dirty = true` + `av::scene_refresh` — mirrors
  `objectChange`.

---

## 4. Remaining gaps to full parity (spec only — do NOT implement yet)

### G1. No WORLD/LOCAL space toggle
- Native hardcodes `ImGuizmo::WORLD` (8873). Web exposes `space` and even
  auto-switches to `local` during scale.
- **Fix location:** add a `bool scene_gizmo_local` to `ViewerState`, pass
  `scene_gizmo_local ? ImGuizmo::LOCAL : ImGuizmo::WORLD`; force LOCAL for scale
  to match web (27202).

### G2. Rotation limited to single Z axis
- Decompose only recovers `rot_y` via `atan2` (8850) because the **scene format
  stores one Z rotation**. Web supports full XYZ/screen rotation.
- **Reality:** this is a *data-model* limit, not a gizmo limit. Full-parity
  rotation would need the scene schema to store a quaternion/Euler triple.
  Document as **schema-blocked**, low priority.

### G3. Orthographic gizmo not wired
- `SetOrthographic(false)` is hardcoded (7768). Depends on the **camera
  modifier** work (file 03) adding an ortho editor camera; then this should
  read the camera's projection mode.
- **Fix:** `ImGuizmo::SetOrthographic(st.camera.orthographic)` once ortho exists.

### G4. No per-axis masking / universal / bounds
- Web `showX/Y/Z`; ImGuizmo supports per-axis `OPERATION` bits, `UNIVERSAL`,
  and `BOUNDS` (bounding-box scale handles) — none exposed natively.
- **Fix:** optional UI toggles → OR the axis bits; expose `UNIVERSAL` as a 4th
  tool mode; pass `localBounds`/`boundsSnap` for box scaling.

### G5. No screen-space / free rotate ring
- Web `E`/`XYZE`; ImGuizmo `ROTATE_SCREEN` is part of `ROTATE` but not called
  out. Minor visual parity item.

### G6. `deltaMatrix` unused
- Native re-derives deltas by diffing fields. ImGuizmo's `deltaMatrix` out-param
  would give exact per-frame deltas for cleaner multi-select propagation.

---

## 5. Parity verdict
| Feature | Parity |
|---------|--------|
| Move / Rotate / Scale gizmo | ✅ done |
| Snap (grid + Ctrl + 15° rotate) | ✅ done |
| Drag-start undo snapshot | ✅ done |
| View-cube camera orbit | ✅ done |
| Multi-select delta | ✅ ahead of web |
| WORLD/LOCAL toggle | ❌ (G1) |
| Orthographic gizmo | ❌ (G3, blocked on file 03) |
| Full XYZ rotation | ⚠️ schema-blocked (G2) |
| Per-axis / universal / bounds | ❌ (G4) |
| Screen/free rotate | ⚠️ (G5) |

**Bottom line:** transform-gizmo parity is ~80% already shipped via ImGuizmo.
The meaningful, low-risk remaining items are **G1 (space toggle)** and **G4
(per-axis/universal/bounds)**; **G3** rides on the camera-modifier work; **G2**
is gated by the scene rotation schema.

## 6. Evidence index
- Web: `index.beautified.js` lines 26686, 26711, 26750, 26761, 26789–26805,
  27202–27234, 27452–27456, 27581.
- ImGuizmo: `src/imgui/Guizmo/src/ImGuizmo.h` (OPERATION/MODE enums 188–221,
  `Manipulate` 223, `SetOrthographic` 171, `ViewManipulate` 229).
- Native usage: `src/tools/asset_viewer.cpp` lines 65 (include), 521, 554–555,
  7767–7770, 8803–8885.
