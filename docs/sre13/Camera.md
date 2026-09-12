# Camera (`Caver::Camera`)

## Summary

`Caver::Camera` is the engine's view/projection camera. It is a plain,
non-virtual struct (no vtable, no RTTI) of **0x1C0 (448) bytes** on both
32-bit and 64-bit builds (it contains only floats/matrices, no pointers, so
the layout is ABI-independent — the header correctly uses no `archSplit`).
A camera is *not* allocated on its own: `CameraController::CameraController()`
(0x4ECC00) does `operator new(0x1C0)` inline and stores the result into the
controller's `boost::shared_ptr<Camera>` at offset +0x58. The shared
ownership means the Camera outlives the controller if any other holder
(Scene, GameSceneController, drawing code) keeps a reference.

The active camera is reachable at `GameSceneController + 0x38` (the embedded
`CameraController`) → `CameraController + 0x58` → `Camera`. `Scene` keeps its
own `boost::shared_ptr<Camera>` at `Scene + 0x1B8` (ptr) / `0x1C0` (control),
assigned by `GameSceneController::InitWithScene`.

Every frame, `GameSceneController::Update` either feeds the hero-follow
camera positions directly into the embedded `CameraController`, or the
controller's own `Update` drives focus modes; in both paths
`Camera::EvaluateViewMatrix` is called last to rebuild `view`,
`viewProjection`, `inverseView`, and `inverseViewProjection`.

## Struct layout

All offsets identical on arm32/arm64 (no pointers in the struct).

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x00 | 0x10 | — | `_pad0` | 16 bytes zeroed at construction (Camera ctor inlined into CameraController ctor: `*(v7+0)=0; *(v7+8)=0`). No access found in decompiled functions. **Unresolved.** |
| 0x10 | 0x0C | `Vector3` | `position` | World-space camera position. Rumble path writes `position.xy` directly (CameraController::Update); SRE freecam overrides this every frame. |
| 0x1C | 0x10 | `Quaternion` | `rotation` | World-space orientation. `EvaluateViewMatrix` uses the **conjugate** quaternion to build the view matrix (verified in disasm: `v18 = *(this+0x1C)` (x), then `-y,-z,-w` fed to `Matrix4::FromRotationQuaternion`). |
| 0x2C | 0x40 | `Matrix4` | `view` | Rebuilt by `EvaluateViewMatrix` = `Rot(conj(q)) * Translate(-position)`. |
| 0x6C | 0x40 | `Matrix4` | `projection` | Set by `SetPerspectiveProjection`/`SetOrthoProjection`/`SetProjectionMatrix`. |
| 0xAC | 0x40 | `Matrix4` | `viewProjection` | `projection × view` (verified: `C_Matrix4Mul(view+16, view, out)` at EvaluateViewMatrix+0xE0, and `NormalizedScreenPointFromWorldPosition` reads it via `(const float*)this + 43` = 0xAC). |
| 0xEC | 0x01 | `bool` | `isOrtho` | Written by `SetOrthoProjection` (=1) / `SetPerspectiveProjection` (=0); ctor sets it to 1 (camera starts orthographic). Read by `SetAspectRatio` to decide whether to rebuild the projection. |
| 0xED | 0x03 | — | `_pad1` | padding (verified: next field at 0xF0). |
| 0xF0 | 0x04 | `float` | `aspect` | Written by `SetOrthoProjection` (=w/h) and `SetPerspectiveProjection` (a3); `SetAspectRatio` updates it (and re-runs perspective if `fov > 0.001`). |
| 0xF4 | 0x04 | `float` | `fov` | Vertical FOV in **radians**. 0 for ortho (SetOrthoProjection writes 0 here). Vanilla hero-follow FOV ≈ 0.7854 rad (45°); InitWithScene starts at 0.34907 (20°). |
| 0xF8 | 0x04 | `float` | `nearPlane` | Vanilla 50.0; InitWithScene uses 50.0. |
| 0xFC | 0x04 | `float` | `farPlane` | Vanilla 20000.0; InitWithScene uses 20000.0. |
| 0x100 | 0x40 | `Matrix4` | `inverseProjection` | `Matrix4::InverseEx(projection)` — written by all three projection setters. |
| 0x140 | 0x40 | `Matrix4` | `inverseView` | `Matrix4::Inverse(view)` — written by `EvaluateViewMatrix` at `this+0x2C+0x114`. |
| 0x180 | 0x40 | `Matrix4` | `inverseViewProjection` | `inverseView × inverseProjection` (= (viewProjection)⁻¹) — written by `EvaluateViewMatrix` at `this+0x2C+0x154`. |

The header's field order, sizes, and offsets are **fully verified** by
`SetOrthoProjection` (0x4EC828), `SetPerspectiveProjection` (0x4EC9EC),
`SetProjectionMatrix` (0x4EC904), `SetAspectRatio` (0x4ECA9C),
`EvaluateViewMatrix` (0x4ECAD0), and `NormalizedScreenPointFromWorldPosition`
(0x4EBC98).

## Vtable

None. `Camera` has no virtual methods; ownership is via `boost::shared_ptr`
(deleter = `boost::checked_delete<Caver::Camera>`), so no dtor symbol exists
either.

## Exported functions

All symbols are `T` (exported, defined) in v1.4.13.

### Camera::SetOrthoProjection(float, float, float, float)
- Signature: `void Camera::SetOrthoProjection(float width, float height, float near, float far)`
- Mangled: `_ZN5Caver6Camera18SetOrthoProjectionEffff` @ 0x4EC828
- Call sites: not called by exported engine code (scene setup calls perspective; editor-only path).
- Behavior: sets `isOrtho=true`, builds `Matrix4::Ortho(-w/2, h/2, -h/2, w/2, near, far)` centered on the camera position, stores it at +0x6C, computes `inverseProjection` (+0x100), writes `aspect=w/h` (+0xF0), `fov=0` (+0xF4), near/far (+0xF8/+0xFC). Note the argument order in the header (`left,right,bottom,top`) is really `width,height,near,far` per the IDA body.
- Side effects: mutates projection, inverseProjection, aspect, fov, near/far, isOrtho.
- Confidence: **verified in IDA**.

### Camera::SetPerspectiveProjection(float, float, float, float)
- Signature: `void Camera::SetPerspectiveProjection(float fovy, float aspect, float near, float far)`
- Mangled: `_ZN5Caver6Camera24SetPerspectiveProjectionEffff` @ 0x4EC9EC
- Call sites: `GameSceneController::InitWithScene` (0x425624) with (0.34907, 1.0, 50.0, 20000.0); SRE freecam (see CameraController.md). The local-alias copy at 0x60AF00 is called by `SetAspectRatio`.
- Behavior: `isOrtho=false`, `Matrix4::PerspectiveFov(fovy, aspect, near, far)` → +0x6C, inverse → +0x100, stores aspect/fov/near/far.
- Side effects: same family as SetOrthoProjection.
- Confidence: **verified in IDA**.

### Camera::SetAspectRatio(float)
- Signature: `void Camera::SetAspectRatio(float aspect)`
- Mangled: `_ZN5Caver6Camera14SetAspectRatioEf` @ 0x4ECA9C
- Call sites: host/device orientation changes (`CaverShell::UpdateViewSize` path); SRE keeps `g_sre_cam_aspect` and calls this.
- Behavior: if `!isOrtho` and `fov > 0.001`, re-runs `SetPerspectiveProjection(fov, aspect, near, far)`; always stores `aspect` at +0xF0.
- Confidence: **verified in IDA**.

### Camera::SetProjectionMatrix(Matrix4 const&)
- Signature: `void Camera::SetProjectionMatrix(Caver::Matrix4 const& mat)`
- Mangled: `_ZN5Caver6Camera19SetProjectionMatrixERKNS_7Matrix4E` @ 0x4EC904
- Call sites: none in exported code (external/editor path for custom projections).
- Behavior: copies 16 floats into +0x6C and recomputes inverseProjection (+0x100). Does **not** touch isOrtho/fov/aspect/near/far.
- Confidence: **verified in IDA**.

### Camera::EvaluateViewMatrix()
- Signature: `void Camera::EvaluateViewMatrix()`
- Mangled: `_ZN5Caver6Camera18EvaluateViewMatrixEv` @ 0x4ECAD0
- Call sites: `CameraController::Update` (last thing it does), SRE freecam hook calls it after overriding position/rotation.
- Behavior: builds `view` = `RotMatrix(conj(rotation)) · Translate(-position)` (conjugate quaternion → inverse rotation; negated position → inverse translation); `viewProjection` = `projection × view`; `inverseView` = `Inverse(view)`; `inverseViewProjection` = `inverseView × inverseProjection`.
- Side effects: rebuilds 4 matrices; called every frame.
- Confidence: **verified in IDA** (offsets cross-checked against header in the struct table above).

### View/ray helpers (const, no state mutation)
All verified in IDA; they read `viewProjection`/`inverseViewProjection` and
world→screen conversion helpers:

| Function | Address | Notes |
|---|---|---|
| `NormalizedScreenPointFromWorldPosition(Vector3 const&, float*)` | 0x4EBC98 | ×viewProjection (+0xAC), divides by w; returns Vector2 via hidden sret (x8). |
| `ScreenPointFromWorldPosition(Vector3 const&, Rectangle const&, float*)` | 0x4EBD58 | normalized → pixel space using viewport rect. |
| `ViewPointFromWorldPosition(Vector3 const&, float*)` | 0x4EBE34 | view-space transform. |
| `WorldPositionFromNormalizedScreenPoint(Vector2, float)` | 0x4EBE40 | ×inverseViewProjection. |
| `WorldPositionFromScreenPoint(Vector2, Rectangle const&, float)` | 0x4EBED8 | pixel → normalized → world. |
| `RayFromScreenPoint(Vector2 const&)` | 0x4EBF60 | builds a Ray for picking. |
| `ScreenAABBFromWorldAABB(Rectangle const&, float, Rectangle const&)` | 0x4EC0CC | world AABB → screen AABB at a z-distance. |
| `ForwardDirection() const` | 0x4EC36C | camera forward vector (from rotation). |
| `AABBOnZPlane(float)` / `AABBForZRange(float,float)` | 0x4EC3F4 / 0x4EC548 | frustum slice AABBs (used by SceneGrid culling). |
| `VisibleAreaSizeAtDistance(float)` | 0x4EC740 | world units visible at distance d (uses fov+aspect). |
| `DistanceForVisibleAreaWidth/Height(float)` | 0x4EC784 / 0x4EC7B4 | inverse of the above. |
| `MinDistanceForVisibleAreaSize(Vector2 const&)` | 0x4EC7E0 | distance so the given world size fits (used by `CameraController::FocusAtRectangle`/`FocusAtShape`). |

## Open questions / unresolved offsets

- **0x00–0x0F (`_pad0[0x10]`)**: zeroed by the inlined ctor; nothing in the
  decompiled code reads it. Could be a refcount/`intrusive_ptr` anchor or
  editor metadata — currently "unresolved, 16 bytes, no access found."
- The header comment on `SetOrthoProjection`'s params (`left,right,bottom,top`)
  is wrong: IDA shows `width,height,near,far` with the ortho rect centered on
  the camera position. The header `DL_SYMBOL_DECL` arg names should be updated
  accordingly.

## Proposed SRE hooks

- **Free camera beyond `CameraController`** — already partially done via
  `g_sre_cam_*` globals in `hooks/CameraController.c` (fov/near/far/yaw/pitch/roll
  overrides). Remaining ideas:
  - `g_sre_cam_follow_mode` int: 0 = vanilla, 1 = follow hero, 2 = locked
    (ignore GameSceneController's per-frame targetPos/currentPos writes).
  - Direct accessor `sre_camera_set_transform(pos, quat)` that writes
    `position`/`rotation` then calls `Camera_EvaluateViewMatrix` — safe
    because the matrices are the only cached state (no dirty flag to
    invalidate).
  - `g_sre_cam_proj_override` matrix hook: expose `SetProjectionMatrix` so a
    script can install a custom projection (fish-eye, ortho splitscreen) —
    safe, engine-supported.
- **What not to expose raw**: `inverseViewProjection` (+0x180) is derived
  state — never write it directly; always go through `EvaluateViewMatrix`.