# AnimationControllerComponent (`Caver::AnimationControllerComponent`)

## Summary

`AnimationControllerComponent` drives the animation of a visual
`SceneObject`. It exposes two bindable **outlets** — `animation` (at +0x68)
and `defaultAnimation` (at +0x80) — each a `ComponentOutlet<...>` that holds
`{vtable*, identifier, target}`. `LoadFromProtobufMessage` stores the outlet
identifiers and replaces the target pointers (releasing the previous target
via its refcount), then sets a play-on-load/loop flag at +0xA0.

Allocation size: **0xB0** (`operator new(0xB0)` in `Create`). Vtable at
**0x619370**.

## ComponentOutlet<T> (shared helper, 24 bytes)

First documented here; used by Portal, ParticleEmitter and AnimationController:

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| +0x00 | 8 | `void*` | `vtable` | Per-`T` outlet vtable (e.g. `off_618F38`, `off_619538`, `off_61A8E0`, `off_61FAB0`). |
| +0x08 | 4 | `int` | `identifier` | Outlet id; the "animation" / "defaultAnimation" key. |
| +0x0C | 4 | — | (unknown) | — |
| +0x10 | 8 | `void*` | `target` | The connected component/resource; set via `ConnectTo`/load. |

## Struct layout

64-bit verified from ctor (`0x32C604`), `LoadFromProtobufMessage`
(`0x32C8E4`), `Create`.

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x00 | 0x68 | — | `Component` base | See Component.md. |
| 0x68 | 24 | `ComponentOutlet` | `animation` outlet | Vtable `off_618F38`; id at +0x70, target at +0x78. Bound as outlet 0 with name `"animation"`. |
| 0x80 | 24 | `ComponentOutlet` | `defaultAnimation` outlet | Vtable `off_619538`; id at +0x88, target at +0x90. Bound as outlet 1 with name `"defaultAnimation"`. |
| 0x98 | 8 | — | (unknown) | Ctor `= 0`. |
| 0xA0 | 1 | `bool` | `playOnLoad` | Load sets from proto byte (msg+20). Ctor `= 0`. **Inferred: auto-play/loop flag.** |
| 0xA1 | 7 | — | (unknown) | — |
| 0xA8 | 8 | — | (unknown) | Ctor `= 0`. |
| 0xB0 | 1 | — | (end) | Size 0xB0. |

## Vtable (0x619370)

33-slot `Component` layout; overridden: dtor, `Clone`, `Load`/`Save`,
`ShouldSave`, `Prepare` (+0x40), bindings (+0x70..+0x90),
`componentCategories` (+0x98), `ImplementsInterface`/`RegisterInterfaces`
(+0xA0/+0xA8), `RequiresUpdate` (+0xB8), `Update(float)` (+0xC8).
`HandleMessage` stays base.

## Exported functions

### AnimationControllerComponent::Create()
- Mangled: `_ZN5Caver28AnimationControllerComponent6CreateEv`
- Behavior: `operator new(0xB0)` + ctor. Size anchor.
- Confidence: **verified**.

### AnimationControllerComponent::LoadFromProtobufMessage(Proto::Component const&)
- Mangled: `_ZN5Caver28AnimationControllerComponent23LoadFromProtobufMessageERKNS_5Proto9ComponentE` (0x32C8E4).
- Behavior: `Component::LoadFromProtobufMessage`, then for each outlet:
  store the proto id at +0x70/+0x88 and, when it changed, release the old
  target (+0x78/+0x90) and null it; finally set +0xA0 from proto.
- Side effects: releases previously-connected animation targets (refcount
  decrement may delete them).
- Confidence: **verified** (offsets); outlet roles from ctor BindOutlet names.

## Open questions / unresolved offsets

- The outlet target classes (`off_618F38` / `off_619538` vtables) were not
  dumped — likely `ComponentOutlet<AnimationControllerComponent>` variants.
- How the actual animation asset is looked up from the identifier (through
  `TextureLibrary`/`ModelLibrary`?) is not traced.
- +0x98/+0xA8: no access found.

## Proposed SRE hooks

- `AnimationControllerComponent_SetAnimation(comp, const char*)` — swap a
  living object's animation by writing the outlet id (+0x70) and letting the
  controller resolve it, or via the outlet's `ConnectTo` vtable slot.
- `AnimationControllerComponent_GetCurrentAnimation(comp)` — read the
  connected target at +0x78 for debug overlays.
- **Safe**: always route through the outlet (`target` replacement must
  release the old refcount, exactly as Load does); raw target writes leak.