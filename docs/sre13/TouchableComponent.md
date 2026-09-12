# TouchableComponent (`Caver::TouchableComponent`)

## Summary

`TouchableComponent` makes a `SceneObject` react to touch/contact: it stores
a trigger radius and an embedded script `Program` — the "onTouch" behavior
that runs when the player (or another object) touches it. It is the
component behind pickups, pressure plates and one-shot interaction objects.

Allocation size: **0xB0** (`operator new(0xB0)` in `Create`). Vtable at
**0x61BA88**.

## Struct layout

64-bit verified from ctor (`0x346880`), `LoadFromProtobufMessage`
(`0x346A04`), `Create`.

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x00 | 0x68 | — | `Component` base | See Component.md. |
| 0x68 | 4 | `float` | `radius` | Ctor default **90.0** (bit pattern `0x42B40000`); Load overwrites from proto float (msg+12). **Inferred: touch trigger radius in world units.** |
| 0x6C | 4 | — | (unknown) | — |
| 0x70 | 8 | — | (unknown) | Zeroed in ctor (OWORD at +0x70). |
| 0x78 | 0x38 | `Program` | `onTouchProgram` | Embedded script, loaded via `Program::LoadFromProtobufMessage` from proto field 1. Runs when touched. |
| 0xB0 | 1 | — | (end) | Size 0xB0. |

## Vtable (0x61BA88)

33-slot `Component` layout; overridden: dtor, `Clone`, `Load`/`Save`,
`ShouldSave`, `Prepare` (+0x40), bindings (+0x70..+0x90),
`componentCategories` (+0x98), `ImplementsInterface`/`RegisterInterfaces`
(+0xA0/+0xA8). `Update`/`Process`/`HandleMessage` stay base (the touch
detection happens in the scene's hit-testing, not per-frame).

## Exported functions

### TouchableComponent::Create()
- Mangled: `_ZN5Caver18TouchableComponent6CreateEv`
- Behavior: `operator new(0xB0)` + ctor. Size anchor.
- Confidence: **verified**.

### TouchableComponent::LoadFromProtobufMessage(Proto::Component const&)
- Mangled: `_ZN5Caver18TouchableComponent23LoadFromProtobufMessageERKNS_5Proto9ComponentE` (0x346A04).
- Behavior: `Component::LoadFromProtobufMessage`, then the radius (+0x68)
  and the onTouch `Program` (+0x78).
- Confidence: **verified** (offsets); radius semantics inferred.

## Open questions / unresolved offsets

- The 90.0 default radius: needs a call-site check (the scene's touch
  hit-test) to confirm it is a radius and not an angle/activation distance.
- Whether `HandleMessage` is used by the scene to fire the program (vs a
  direct call) is untraced.

## Proposed SRE hooks

- `TouchableComponent_SetRadius(comp, float)` — widen/shrink touch zones
  live for debugging trigger layouts.
- `TouchableComponent_GetOnTouchProgram(comp)` — hand the +0x78 program to
  `ProgramState` tooling to see what a pickup/plate does.
- **Safe**: the program is embedded and load-owned; to change behavior, edit
  the save data and reload, don't patch +0x78 in place.