# CameraController (`Caver::CameraController`)

## Summary

`CameraController` owns the game's camera. It is **embedded inside
`GameSceneController` at offset +0x38** (not heap-allocated; the GSC ctor runs
`CameraController::CameraController(this+0x38)`), is ~0xA0 bytes, and holds:
the follow target (`boost::intrusive_ptr<SceneObject>`), a focus shape, the
shared `Camera`, and the smoothing/lerp state. The destructor is exported
(weak, 0x4254F4) and releases the intrusive refs and the shared Camera.

Two independent systems drive it:

1. **Hero-follow mode** (default): `GameSceneController::Update` writes
   `cc->targetPos`/`cc->currentPos` directly every frame
   (`*((float*)this+18..24)` = absolute 0x48/0x4C/0x50 and 0x58/0x5C/0x60 in
   the GSC = `cc+0x10`/`cc+0x20`), also nudging `cc->zoom`/`cc->lerpFactor`
   (`gsc+0x54`/`gsc+0x64`). Then `CameraController::Update(cc, dt)` lerps and
   calls `Camera::EvaluateViewMatrix`.
2. **Focus modes**: `FocusAtPoint`, `FocusAtRectangle`, `FocusAtShape` set
   `flags`/target/current and let `CameraController::Update` handle the
   LookAt orientation (the camera always looks at `focusPos`).

The camera sits at `targetPos` = focus/hero + `cameraOffset` (+0x04) and
*looks at* `focusPos` (+0x30); for the hero this is the classic Swordigo
"high behind the hero" framing (offset y≈1188–1544, z≈187–243 depending on
device).

## Struct layout (64-bit; the header is **wrong** in several places)

The header's field order matches, but several offsets are off — the real
layout below is derived from the ctor (0x4ECC00), `Update` (0x4ECE50),
`FocusAtPoint` (0x4ED14C), `FocusAtRectangle` (0x4ED4C8),
`FocusAtShape` (0x4ED228), `FollowObject` (0x4ED634), `StopFollowing`
(0x4ED448), `ResetFocus` (0x4ED3D4), `Rumble` (0x4ED7E0), and
`GameSceneController::InitWithScene` (0x425624).

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x00 | 0x04 | `int` | `flags` | 0 = idle, 1 = focusing (target active), 2 = "keep focus" (ResetFocus; Update clears to 0 once within 100 units of target & current). |
| 0x04 | 0x0C | `Vector3` | `cameraOffset` | Direction/offset from the focus target to the camera. Set by `InitWithScene`: `x=0, y∈{1544 (phone), 1188 (tablet)}, z∈{243,187}` (device-dependent). **The header's `_pad0[0x0c]` is this field.** Added raw to the target in `FocusAtPoint`; **normalized** and scaled by view distance in `FocusAtRectangle`/`FocusAtShape`. |
| 0x10 | 0x0C | `Vector3` | `targetPos` | Where the camera is heading (lerp destination). |
| 0x1C | 0x04 | `float` | `lerpFactor` | Per-frame interpolation fraction. Ctor 0; `FocusAtPoint` 0.975; `InitWithScene` ≈1.0 (0x3F7FFFB6); hero-follow path sets 0.985. |
| 0x20 | 0x0C | `Vector3` | `currentPos` | Camera position this frame (before Update's lerp). |
| 0x2C | 0x04 | `float` | `zoom` | Ctor 0; `FocusAtPoint` 0.9552; `InitWithScene` 0.8; hero-follow decays toward 0.8. |
| 0x30 | 0x0C | `Vector3` | `focusPos` | Look-at point. `Update` builds `LookAt(cameraPos, focusPos, up)` each frame. |
| 0x3C | 0x14 | — | `_pad1` | 20 bytes. Ctor writes a zero pattern into 0x40–0x4F; `InitWithScene` writes a double `1.0` at 0x48 (unverified purpose — possibly a shake/offset scale). **Unresolved.** |
| 0x50 | 0x08 | — | `_pad2` (dword?) | Zeroed by ctor (`*((_DWORD*)this+20)=0`) and by InitWithScene (+0x88 abs). **Unresolved.** |
| 0x58 | 0x08 | `Camera*` | `camera` | `boost::shared_ptr<Camera>` pointer part. **Header says 0x50 — wrong; the real camera ptr is at 0x58** (ctor: `boost::shared_ptr<Camera>::reset(this+0x58, new Camera)`; Update: `*((_QWORD*)this+11)`). |
| 0x60 | 0x08 | `void*` | `cameraRef` | shared_ptr control block. Header's "cameraRef" name is right, offset should be 0x60 not 0x50. |
| 0x68 | 0x08 | `SceneObject*` | `followObject` | `boost::intrusive_ptr<SceneObject>` (raw ptr; refcount lives in SceneObject+0x08). **Header says 0x60 — wrong.** |
| 0x70 | 0x08 | `ShapeComponent*` | `focusShape` | `boost::intrusive_ptr<ShapeComponent>` for `FocusAtShape` mode. **Header's `followObjectRef` at 0x68 was a misread; this slot is the focus shape at 0x70.** |
| 0x78 | 0x0C | `Vector3` | `followOffset` | Added to `followObject->Position` to get `currentPos`. **Header says 0x70 — wrong.** |
| 0x84 | 0x10 | `Rectangle` | `focusRect` | World-space rect used by `FocusAtShape`-driven framing in Update (passed as `this+0x84`). **Header's `_pad3` covers this.** |
| 0x94 | 0x04 | `float` | `rumbleTimer` | 0..1 progress of the shake. Rumble sets it indirectly via Update (`*((float*)this+37)`). |
| 0x98 | 0x04 | `float` | `rumble` | 1.0 = shake active (set by `Rumble()` at +0x98 — **header says 0x94, wrong**); counted down by dt in Update; while >0.01 the camera position is offset by `(0, (1−0.7·t)·6·sin(60·t))` directly on the Camera object. |
| 0x9C | 0x04 | — | `_pad4` | tail padding; total size 0xA0. |

32-bit layout: the header's `archSplit` values for this struct were never
validated against the arm32 binary; treat the 32-bit column as **guess**
until re-verified (pointers shrink, but Vector3/float fields stay put).

## Vtable

None — `CameraController` has no virtual functions (only the exported dtor,
which is weak and referenced via checked_delete).

## Exported functions

All `T` exports unless noted. Local aliases (0x60xxxx, IDB-only) exist for
most; the canonical exported copies are listed.

### CameraController::Update(float)
- Mangled: `_ZN5Caver16CameraController6UpdateEf` @ 0x4ECE50
- Call sites: `GameSceneController::Update` (`CameraController::Update(this+0x38, dt)` — last call before the scene's own update).
- Behavior:
  1. Lerps `currentPos` → `targetPos` by `clamp((1−lerpFactor)·dt·60, 0..1)` (writes back +0x20).
  2. If `rumble > 0.01`: decays it and applies the shake to `camera->position`.
  3. If `currentPos` isn't close to `focusPos` (0.001), builds `Matrix4::LookAt(currentPos, focusPos, up)`, converts to a quaternion, and writes the **conjugate** into `camera->rotation` (+0x1C) — the camera is re-oriented every frame.
  4. Mode dispatch: `followObject` non-null → recompute target from `obj->Position + followOffset + cameraOffset` and write `flags=1, lerpFactor=0.975, zoom=0.9552`; else if `focusShape` non-null → `FocusAtShape(this, &focusShape, &focusRect, false)`; else if `flags==2` → clear to 0 when both diffs < 100.
  5. Calls `Camera::EvaluateViewMatrix(camera)`.
- Side effects: mutates targetPos/currentPos/focusPos/zoom/lerpFactor/flags/rumble and the Camera's matrices. SRE hooks this symbol to sanitize NaN (see hooks/CameraController.c) and to apply freecam.
- Confidence: **verified in IDA**.

### CameraController::FollowObject(intrusive_ptr<SceneObject> const&, Vector3 const&)
- Mangled: `_ZN5Caver16CameraController12FollowObjectERKN5boost13intrusive_ptrINS_11SceneObjectEEERKNS_7Vector3E` @ 0x4ED634
- Behavior: releases old `followObject` (+0x68) and `focusShape` (+0x70), adopts the new object (refcount++ at `SceneObject+8`), copies `offset` into `followOffset` (+0x78).
- Confidence: **verified in IDA**.

### CameraController::StopFollowing()
- Mangled: `_ZN5Caver16CameraController13StopFollowingEv` @ 0x4ED448
- Behavior: releases both intrusive refs (+0x68, +0x70) to null.
- Confidence: **verified in IDA**.

### CameraController::FocusAtPoint(Vector3 const&, bool immediate)
- Mangled: `_ZN5Caver16CameraController12FocusAtPointERKNS_7Vector3Eb` @ 0x4ED14C
- Behavior: if `immediate`, drops both intrusive refs; then `flags=1`, `targetPos = point + cameraOffset`, `currentPos = point`, `lerpFactor=0.975`, `zoom=0.9552`.
- Confidence: **verified in IDA**.

### CameraController::FocusAtRectangle(Rectangle const&, float z, bool immediate)
- Mangled: `_ZN5Caver16CameraController16FocusAtRectangleERKNS_9RectangleEfb` @ 0x4ED4C8
- Behavior: like FocusAtPoint but positions the camera so the given world rect is fully visible: `distance = MinDistanceForVisibleAreaSize(rect.size)`; `targetPos = rectCenter + normalize(cameraOffset)·distance` (z: `distance·offset.z + z`).
- Confidence: **verified in IDA**.

### CameraController::FocusAtShape(intrusive_ptr<ShapeComponent> const&, Rectangle const&, bool)
- Mangled: `_ZN5Caver16CameraController12FocusAtShapeERKN5boost13intrusive_ptrINS_14ShapeComponentEEERKNS_9RectangleEb` @ 0x4ED228
- Behavior: world-transforms `Shape::Bounds` through `SceneObject::WorldMatrix`, frames the resulting rect like FocusAtRectangle, and **stores the shape ref into +0x70 and the rect into +0x84** so Update keeps re-framing it each frame (cutscene framing). The `bool` releases previous refs when set.
- Confidence: **verified in IDA**.

### CameraController::GotoTargetImmediately()
- Mangled: `_ZN5Caver16CameraController21GotoTargetImmediatelyEv` @ 0x4ED7BC
- Behavior: `currentPos = targetPos` (16-byte copy + z fixup at +0x38).
- Confidence: **verified in IDA**.

### CameraController::ResetFocus()
- Mangled: `_ZN5Caver16CameraController10ResetFocusEv` @ 0x4ED3D4
- Behavior: releases follow/focus refs, sets `flags=2` (keep-position-until-close mode).
- Confidence: **verified in IDA**.

### CameraController::Rumble()
- Mangled: `_ZN5Caver16CameraController6RumbleEv` @ 0x4ED7E0
- Behavior: `rumble (+0x98) = 1.0`.
- Confidence: **verified in IDA**.

### CameraController::RegisterProgramLibrary(ProgramState*)
- Mangled: `_ZN5Caver16CameraController17RegisterProgramLibraryEPNS_12ProgramStateE` @ 0x4ED7EC
- Behavior: exposes the camera controller to Lua (`cameraController` global, plus `FocusAtPoint`/`FocusAtRectangle` bindings).
- Confidence: inferred from name + call site (`InitWithScene`).

## Open questions / unresolved offsets

- 0x3C–0x4F (`_pad1`): a double `1.0` is written at 0x48 by InitWithScene; no
  reads found. Possibly a "shake intensity scale" or editor field.
- 0x50: zeroed at init; no reads found. Possibly a spare `ShapeComponent*` or
  flags dword.
- 32-bit offsets unverified — the `archSplit` values in the header for this
  struct should be re-derived from `engine/v1.4.13/armeabi-v7a/libswordigo.so`
  before trusting them.

## Proposed SRE hooks

- `g_sre_cam_*` freecam is already implemented in `hooks/CameraController.c`
  (position/fov/near/far/yaw/pitch/roll + POV mode) — extend with:
  - **`sre_camera_follow_offset(x,y,z)`**: a host call that patches
    `cc->followOffset` (+0x78) — changes the hero-follow framing without
    touching the per-frame target writes.
  - **`sre_camera_shake(amp, dur)`**: scripted shake via the existing
    `rumble`/`rumbleTimer` pair (+0x94/+0x98) instead of the fixed 6-unit
    sine — safe (Update already consumes these fields).
  - **`g_sre_cam_hero_lock`**: force `flags=0` + skip the
    GameSceneController hero-follow block so the camera stops tracking the
    hero (usable with freecam), restoring on unset.
- **Unsafe to expose raw**: `followObject`/`focusShape` are intrusive_ptr
  slots — writing a pointer there without bumping `SceneObject+8` refcount
  corrupts ownership (double free on StopFollowing/dtor). Any follow-target
  override must call `CameraController_FollowObject` (the real function) or
  replicate the refcount dance. `rumble` etc. are safe plain floats.
- The `Update` hook already sanitizes the 12 floats from `targetPos` through
  `focusPos` (+0x10..+0x3F) — keep that as the NaN firewall when adding new
  host-driven camera writes.