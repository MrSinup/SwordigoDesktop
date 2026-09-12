# ModelComponent (`Caver::ModelComponent`)

## Summary

`ModelComponent` renders a 3D model on its owner `SceneObject`. It is one of
the two visual components (with `SpriteComponent`) and is what the game uses
for the hero, monsters, chests, doors and any object backed by a `.pod` mesh.
It stores the model's file name, an optional per-instance color (tint), a
scaling Vector3, flags for shadows/visibility, and an embedded `FloatColor`
for emissive/glow. The model resource itself is fetched lazily from
`ModelLibrary::sharedLibrary` via `ModelForName` at render time.

It derives directly from `Component` (it zeroes the base fields itself rather
than calling a base ctor) — it does **not** derive from `EntityComponent`,
so it has no embedded `PhysicsObjectState`. Allocation size: **0x148**
(`operator new(0x148)` in `Create`). Vtable at **0x619CE8**.

## Struct layout

64-bit verified from ctor (`0x332818`), `LoadFromProtobufMessage`
(`0x3330A8`), `Create` (`0x33xxxx`).

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x00 | 0x68 | — | `Component` base | Base fields (vtable, refcount@0x08, vtables@+0x10/+0x18, label@+0x38, outlet list node@+0x50/+0x58, `object`@+0x28). See Component.md. |
| 0x68 | 24 | `std::string` | `modelName` | Model file name (proto field 1 of the ModelComponent extension). Loaded with `std::string::operator=(this+0x68, ...)`. |
| 0x80 | 12 | — | (unknown) | Zeroed in ctor (3 OWORDs cover +0x68..+0x97). |
| 0x8C | 4 | — | (unknown) | — |
| 0x90 | 4 | — | (unknown) | — |
| 0x94 | 4 | — | (unknown) | — |
| 0x98 | 4 | — | (unknown) | OWORD at +0x95..+0xA4 zeroed. |
| 0x9C | 4 | — | (unknown) | — |
| 0xA0 | 4 | `int` | (unknown) | Load writes `this+40` from proto field 5 area (`*(v5+40)`). **Inferred: shader/pass id.** |
| 0xA4 | 1 | `bool` | (unknown) | Load writes `this+0xA4` from proto field 7 (`*(v5+56)`). **Inferred: castsShadow / isOpaque.** |
| 0xA5 | 3 | — | (unknown) | — |
| 0xA8 | 16 | `FloatColor` | `emissive/glow` | 4 floats, default (1,1,1,1) (ctor `FMOV V0.4S, #1.0`). Overwritten from proto field 8 (`FloatColorFromProtobufMessage`). |
| 0xB8 | 16 | — | (unknown) | Zeroed (OWORD at 0xB8..0xC7). |
| 0xC8 | 1 | `bool` | `scalingNonZero` | Set by Load: `Vector3::IsClose(scaling, Vector3Zero, 0.001) == 0` — 1 while the scaling at +0xCC differs from zero. |
| 0xC9 | 3 | — | (unknown) | — |
| 0xCC | 12 | `Vector3` | `scaling` | x,y@+0xCC (one qword) + z@+0xD4, from the proto's Vector3 sub-message (field 2). Ctor default (0,0,0). |
| 0xD8 | 8 | `void*` | (proto view) | Load stores `*(v5+32)` directly — a **protobuf-internal pointer** (fragile view, not a copy). Unresolved. |
| 0xE0 | 8 | `void*` | (unknown) | Zeroed in ctor. |
| 0xE8 | 16 | — | (unknown) | Zeroed (OWORD at +0xE8..+0xF7). |
| 0xF8 | 8 | `void*` | (unknown) | Zeroed. |
| 0x100 | 4 | — | (unknown) | Zeroed. |
| 0x104 | 32 | — | (unknown) | Zeroed (OWORDs at +0x104..+0x123). |
| 0x124 | 8 | — | (unknown) | Zeroed (qword at +0x124). |
| 0x12C | 8 | — | (unknown) | Zeroed (qword at +0x12C). |
| 0x134 | 8 | — | (unknown) | Zeroed (qword at +0x134). |
| 0x13C | 2 | `uint16` | (unknown) | Ctor `*(_WORD*)(this+0x13C) = 0`. |
| 0x13E | 8 | `int64` | `= -1` | Ctor writes `-1` (misaligned qword at +0x13E..+0x145). **Inferred: sentinel / "no entry" id.** |
| 0x146 | 1 | `bool` | (unknown) | Ctor `= 0`. |
| 0x147 | 1 | — | (end) | Size 0x148. |

Header: none exists yet for ModelComponent (`src/sre/sre13/caver/` has no
ModelComponent.h). This doc is the primary reference.

## Vtable (0x619CE8)

Same 33-slot `Component` layout (see Component.md); overrides highlighted:

| Slot | Offset | Target | Notes |
|---|---|---|---|
| 0/1 | +0x00/+0x08 | `~ModelComponent()` | |
| 2 | +0x10 | `ModelComponent::Clone() const` | |
| 4 | +0x20 | `ModelComponent::LoadFromProtobufMessage` | 0x3330A8. |
| 5 | +0x28 | `SaveToProtobufMessage` | |
| 6 | +0x30 | `ShouldSave()` | Overridden. |
| 8 | +0x40 | `Prepare()` | Overridden. |
| 14–18 | +0x70–0x90 | `GetBindings`/`GetEnum`/`ValueFor`/`SetValue`/`PerformAction` | Overridden (bindable model properties: model name, scaling, colors). |
| 19 | +0x98 | `componentCategories()` | Overridden. |
| 20/21 | +0xA0/+0xA8 | `ImplementsInterface`/`RegisterInterfaces` | Overridden. |
| 25 | +0xC8 | `Update(float)` | Not overridden (base). |
| 27/28 | +0xD8/+0xE0 | `HasBounds()`/`localAABB()` | Overridden (the model's bounds drive scene culling). |
| 29/30 | +0xE8/+0xF0 | `minDepth`/`maxDepth` | Overridden. |

## Exported functions

### ModelComponent::Create()
- Mangled: `_ZN5Caver14ModelComponent6CreateEv`
- Behavior: `operator new(0x148)` + ctor. Size anchor.
- Confidence: **verified**.

### ModelComponent::ModelComponent()
- Mangled: `_ZN5Caver14ModelComponentC2Ev` (0x332818).
- Behavior: zeroes the `Component` base fields itself, sets the three vtable
  pointers (+0x00/+0x10/+0x18), initializes the outlet list node at
  +0x50/+0x58, defaults the emissive color to (1,1,1,1), and the +0x13E
  sentinel to -1.
- Confidence: **verified**.

### ModelComponent::LoadFromProtobufMessage(Proto::Component const&)
- Mangled: `_ZN5Caver14ModelComponent23LoadFromProtobufMessageERKNS_5Proto9ComponentE` (0x3330A8).
- Behavior: calls `Component::LoadFromProtobufMessage` (identifier/label),
  then reads the `ModelComponent` protobuf extension: modelName (+0x68),
  scaling Vector3 (+0xCC), a proto pointer stored raw at +0xD8, a dword at
  +0xA0, the shadow/opacity byte at +0xA4, and the emissive FloatColor
  (+0xA8). Also sets `scalingNonZero` (+0xC8).
- Side effects: none beyond the object itself.
- Confidence: **verified** (offsets); proto field numbers inferred.

### ModelComponent::WorldMatrix-ish rendering
The actual draw path lives in the renderer; `ModelLibrary::ModelForName`
resolves the `.pod` by name. The model's world transform comes from the
owner `SceneObject`'s position/rotation/scale (see SceneObject.md), NOT from
fields on this component.

## Open questions / unresolved offsets

- +0x80..+0x9F region: no access found in ctor/Load beyond zeroing. The proto
  `dword` at +0xA0 and byte at +0xA4 need a render-path cross-check
  (likely shader/pass selection and shadow flags).
- +0xD8 stores a raw protobuf-internal pointer — fragile; needs a call-site
  check to see how (or whether) it is dereferenced after load.
- The -1 sentinel at +0x13E: candidate for a "no model" marker consumed by
  the renderer.
- `scaling` vs `instanceScaling` on the SceneObject: both exist; the
  component scaling multiplies the per-instance scaling at render time
  (exact composition not traced).

## Proposed SRE hooks

- `ModelComponent_SetModelName(comp, const char*)` — swap an object's model
  at runtime (write +0x68 through `String_create`), the building block for
  "replace the hero with X" hacks.
- `ModelComponent_GetModelName(comp)` — read-only accessor for debug
  overlays (list what every object in a scene renders).
- Color hooks: `ModelComponent_SetTint(comp, r,g,b,a)` writing the FloatColor
  at +0xA8 via the real setters once their addresses are recovered.
- **Safe**: modelName is a normal `std::string`; read it freely, but write it
  through the load path or a String-copy helper, never raw.