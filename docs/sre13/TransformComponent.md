# TransformComponent (`Caver::TransformComponent`)

## Summary

`TransformComponent` is a stateless pass-through component that exposes the
owner `SceneObject`'s transform (position/rotation/scale) through the
component binding system so the scene editor and scripts can manipulate it.
It has **no fields of its own** beyond an embedded identity `Matrix4`:
`LoadFromProtobufMessage` only calls `Component::LoadFromProtobufMessage`, and
`WorldMatrix` composes the owner's transform with that matrix. This is the
component the editor's transform gizmo talks to.

Derives directly from `Component`. Allocation size: **0xA8**
(`operator new(0xA8)` in `Create`). Vtable at **0x61BC38**.

## Struct layout

64-bit verified from ctor (`0x347418`), `WorldMatrix` (`0x3474D4`), `Create`.

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x00 | 0x68 | — | `Component` base | Base fields. See Component.md. |
| 0x68 | 64 | `Matrix4` | `matrix` | Embedded 4×4 matrix, default **identity** (ctor stores `Matrix4::identityMatrix`). Used by `WorldMatrix` as the second multiplicand: `Scale(owner) * matrix * Translate(owner)`. |
| 0xA8 | 4 | — | (end) | Size 0xA8. |

## Vtable (0x61BC38)

33-slot `Component` layout; nearly everything is **not** overridden — the
only non-base slots are the bindings (GetBindings/GetEnum/ValueFor/SetValue/
PerformAction at +0x70–+0x90), `ImplementsInterface`/`RegisterInterfaces`
(+0xA0/+0xA8), `Clone` (+0x10), `Load`/`Save` (+0x20/+0x28), and the dtor.
`Update`/`Process`/`Prepare`/`HandleMessage` remain base `Component`
implementations.

## Exported functions

### TransformComponent::Create()
- Mangled: `_ZN5Caver18TransformComponent6CreateEv`
- Behavior: `operator new(0xA8)` + ctor. Size anchor.
- Confidence: **verified**.

### TransformComponent::WorldMatrix(float out[16])
- Mangled: `_ZNK5Caver18TransformComponent11WorldMatrixEv` (0x3474D4).
- Behavior:
  1. `owner = *(Component+0x28)` (the owning SceneObject).
  2. Builds a diagonal scale matrix from `*(float*)(owner+0x9C)`
     (SceneObject `scale`, uniform — all three axes).
  3. `C_Matrix4Mul(scale, this+0x68 /* embedded matrix */, out)`.
  4. PreTranslates by `owner position` (x,y from `owner+0x80`, z from
     `owner+0x88`).
  So: `out = Scale(owner.scale) · embeddedMatrix · Translate(owner.position)`.
  Note it does **not** include `owner.rotation` (that lives in
  `SceneObject::WorldMatrix`); the embedded matrix can carry extra rotation.
- Side effects: none.
- Confidence: **verified** (all offsets cross-checked against
  `SceneObject::WorldMatrix` and `setPosition`).

### TransformComponent::LoadFromProtobufMessage(Proto::Component const&)
- Mangled: `_ZN5Caver18TransformComponent23LoadFromProtobufMessageERKNS_5Proto9ComponentE` (0x347494).
- Behavior: delegates entirely to `Component::LoadFromProtobufMessage` — the
  transform lives on the owner, so there is nothing to load.
- Confidence: **verified**.

## Open questions / unresolved offsets

- The embedded matrix at +0x68: settable through the binding system
  (`ValueForBindedProperty`/`SetValueForBindedProperty` at +0x80/+0x88 in the
  vtable), but the exact property ids and how the editor writes it weren't
  traced.
- `WorldMatrix` skips `owner.rotation`; either the renderer calls
  `SceneObject::WorldMatrix` for rotated objects, or the embedded matrix is
  expected to carry rotation. Unresolved.

## Proposed SRE hooks

- `TransformComponent_WorldMatrix(comp, float out[16])` — free transform
  read for any object, no reimplementation of the matrix math.
- The component is the natural target for a `g_sre_transform` global holding
  a live `(position, rotation, scale)` struct that scripts can edit — SRE can
  push it to the owner via `SceneObject::setPosition` +
  `rotation/scale` writes.
- **Safe**: no raw pointers beyond the base; always go through
  `SceneObject::setPosition`/`setLocalAABB` so bounds-dirty registration
  fires.