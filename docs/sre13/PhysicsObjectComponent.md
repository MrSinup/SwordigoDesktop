# PhysicsObjectComponent (`Caver::PhysicsObjectComponent`)

## Summary

`PhysicsObjectComponent` gives a non-entity `SceneObject` physics: it embeds
the same `PhysicsObjectState` that `EntityComponent` embeds (velocity,
gravity, on-ground flags) so crates, projectiles, doors and swinging objects
behave physically. Unlike `HeroEntityComponent`/`MonsterEntityComponent`, it
derives **directly** from `Component` (not `EntityComponent`) and re-embeds
the state at the same +0x70 offset.

Allocation size: **0x110** (`operator new(0x110)` in `Create`). Vtable at
**0x61F230**.

## Struct layout

64-bit verified from ctor (`0x37627C`), `LoadFromProtobufMessage`
(`0x376380`), `Create`.

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x00 | 0x68 | — | `Component` base | See Component.md. |
| 0x68 | 1 | `bool` | `enabled` | Ctor default **1**. Load overwrites from proto byte (msg+12). |
| 0x69 | 7 | — | (unknown) | — |
| 0x70 | 0x9C | `PhysicsObjectState` | embedded physics state | Constructed by ctor. Same class/layout as the state inside `EntityComponent`. See below for proto-loaded fields. |
| 0x10C | 1 | `bool` | (unknown) | Ctor `= 0`. |
| 0x10D | 3 | — | (end) | Size 0x110. |

PhysicsObjectState fields written by `LoadFromProtobufMessage` (offsets
relative to the component; state starts at +0x70):

| Offset | Size | Field | Notes |
|---|---|---|---|
| +0xA4 | 4 | `int` | Proto field (msg+36). |
| +0xD8 | 8 | `Vector2` | Copied from the proto's Vector2 sub-message (msg+16). **Inferred: velocity.** |
| +0xE0 | 4 | `float` | Proto float (msg+24) **× 800** — the engine scales this proto value. **Inferred: gravity or bounce constant.** |
| +0xE5 | 1 | `bool` | Proto byte (msg+40). |
| +0x104 | 4 | `float` | Proto float (msg+28); ctor default **1500.0** (bit pattern `0x44BB8000`). **Inferred: max fall/terminal velocity** (a well-known Swordigo feel constant). |
| +0x108 | 4 | `int` | Proto dword (msg+32). |

## Vtable (0x61F230)

33-slot `Component` layout; overridden: dtor, `Clone`, `Load`/`Save`,
`ShouldSave`, `Process` (+0x38), `Prepare` (+0x40), bindings
(+0x70..+0x90), `componentCategories` (+0x98),
`ImplementsInterface`/`RegisterInterfaces` (+0xA0/+0xA8),
`RequiresUpdate` (+0xB8 — overridden), `Update(float)` (+0xC8),
`HandleMessage` (+0xD0). Bounds slots stay base.

## Exported functions

### PhysicsObjectComponent::Create()
- Mangled: `_ZN5Caver22PhysicsObjectComponent6CreateEv`
- Behavior: `operator new(0x110)` + ctor. Size anchor.
- Confidence: **verified**.

### PhysicsObjectComponent::PhysicsObjectComponent()
- Mangled: `_ZN5Caver22PhysicsObjectComponentC2Ev` (0x37627C).
- Behavior: zeroes the base fields itself, sets the three vtable pointers,
  defaults `enabled` to 1, constructs the embedded `PhysicsObjectState` at
  +0x70, and defaults the +0x104 velocity limit to 1500.0.
- Confidence: **verified**.

### PhysicsObjectComponent::LoadFromProtobufMessage(Proto::Component const&)
- Mangled: `_ZN5Caver22PhysicsObjectComponent23LoadFromProtobufMessageERKNS_5Proto9ComponentE` (0x376380).
- Behavior: `Component::LoadFromProtobufMessage`, then writes the state
  fields listed above from the `PhysicsObjectComponent` proto extension.
- Confidence: **verified** (offsets); field semantics inferred.

## Open questions / unresolved offsets

- The exact `PhysicsObjectState` layout needs a dedicated pass (its own
  ctor + the `Update` that integrates velocity/gravity). The offsets above
  are only the fields the component's Load touches.
- The ×800 scaling factor (proto value → +0xE0): which constant it feeds
  (gravity? bounce?) is unconfirmed.
- +0x10C byte: not touched by Load.

## Proposed SRE hooks

- `PhysicsObjectComponent_SetVelocity(comp, x, y)` — inject velocity into the
  state (+0xD8) for projectile/knockback hacks; the physics `Update` will
  integrate it next frame.
- `PhysicsObjectComponent_SetMaxFallSpeed(comp, float)` — the +0x104
  constant; raising it enables fast-fall / low-gravity feel tweaks.
- **Safe**: prefer writing through the state's real accessors once
  `PhysicsObjectState` is documented; the embedded state is shared with
  `EntityComponent`, so raw writes must stay within the state bounds.