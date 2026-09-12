# ParticleEmitterComponent (`Caver::ParticleEmitterComponent`)

## Summary

`ParticleEmitterComponent` spawns and drives a particle system attached to a
`SceneObject` (dust, magic sparks, torches). It carries six
`ComponentOutlet`s (one of them the `"parentEmitter"` link), two
resource id/target pairs (the particle system and its texture), a default
particle count of 30, and a couple of default vectors. The actual particle
runtime lives in a separate `ParticleSystem` class allocated through the
resource loader.

Allocation size: **0x1D0** (`operator new(0x1D0)` in `Create`). Vtable at
**0x622BC0**.

## Struct layout

64-bit verified from ctor (`0x396520`), `LoadFromProtobufMessage`
(`0x396F64`), `Create`.

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x00 | 0x68 | — | `Component` base | See Component.md. |
| 0x70 | 24 | `ComponentOutlet` | outlet[0] | Vtable `off_622DB0`; id@+0x78, target@+0x80. |
| 0x88 | 24 | `ComponentOutlet` | outlet[1] | Vtable `off_622DB0`; id@+0x90, target@+0x98. |
| 0xA0 | 24 | `ComponentOutlet` | outlet[2] | id@+0xA8, target@+0xB0. |
| 0xB8 | 24 | `ComponentOutlet` | outlet[3] | id@+0xC0, target@+0xC8. |
| 0xD0 | 24 | `ComponentOutlet` | outlet[4] | id@+0xD8, target@+0xE0. |
| 0xE8 | 20 | — | (unknown) | Zeroed. |
| 0xFC | 1 | `bool` | (unknown) | Ctor `= 0`. |
| 0x100 | 24 | `ComponentOutlet` | `parentEmitter` | Vtable `off_61FAB0` = `ComponentOutlet<ParticleEmitterComponent>`; bound by ctor via `BindOutlet("parentEmitter", this+0x100)`. id@+0x108, target@+0x110. |
| 0x118 | 24 | `ComponentOutlet` | outlet (anim-ish) | Vtable `off_618F38` (same as AnimationController's outlet); id@+0x120, target@+0x128. **Inferred: particle texture/animation outlet.** |
| 0x130 | 8 | — | (unknown) | Ctor `= 0`. |
| 0x138 | 1 | `bool` | (unknown) | Load from proto byte (msg+20); ctor `= 0`. |
| 0x139 | 3 | — | (unknown) | — |
| 0x13C | 4 | `int` | `particleCount` | Ctor default **30**; Load from proto (msg+24). |
| 0x140 | 12 | — | (unknown) | Ctor `= 0` (qword at +0x140). |
| 0x14C | 8 | — | (unknown) | — |
| 0x154 | 16 | — | (unknown) | Ctor OWORD default `xmmword_259F30`. |
| 0x164 | 16 | — | (unknown) | Ctor OWORD default `xmmword_25A630`. |
| 0x174 | 16 | — | (unknown) | Ctor `= 0`. |
| 0x184 | 8 | — | (unknown) | Ctor `= 0`. |
| 0x18C | 16 | — | (unknown) | — |
| 0x19C | 12 | — | (unknown) | — |
| 0x1A8 | 16 | — | (unknown) | — |
| 0x1B8 | 16 | — | (unknown) | — |
| 0x1C8 | 4 | — | (unknown) | — |
| 0x1CC | 1 | `bool` | (unknown) | Ctor `= 0`; Load sets byte +0x161 (353) from proto (msg+28) — **inferred: "emitting" flag**. |
| 0x1CD | 3 | — | (end) | Size 0x1D0. |

Load's resource pair pattern (same as AnimationController): when proto id
changes, `*((_QWORD*)this+34)` (+0x110) and `*((_QWORD*)this+37)` (+0x128)
are released (refcount at `[2]` decremented, freed at 0) after storing the
new id at +0x108/+0x120.

## Vtable (0x622BC0)

33-slot `Component` layout; overridden: dtor, `Clone`, `Load`/`Save`,
`ShouldSave`, `Prepare` (+0x40), bindings (+0x70..+0x90),
`componentCategories` — **not** overridden (base `Component`), per the dump,
`ImplementsInterface`/`RegisterInterfaces` (+0xA0/+0xA8),
`ShouldBeUpdatedInEditor` (+0xB0 — overridden), `RequiresUpdate` (+0xB8),
`Update(float)` (+0xC8).

## Exported functions

### ParticleEmitterComponent::Create()
- Mangled: `_ZN5Caver24ParticleEmitterComponent6CreateEv`
- Behavior: `operator new(0x1D0)` + ctor. Size anchor.
- Confidence: **verified**.

### ParticleEmitterComponent::ParticleEmitterComponent()
- Mangled: `_ZN5Caver24ParticleEmitterComponentC2Ev` (0x396520).
- Behavior: zeroes the base fields, sets the three vtable pointers,
  initializes the six outlets (five with `off_622DB0`, the parentEmitter
  outlet with `off_61FAB0`, one with `off_618F38`), defaults particleCount
  to 30 and the two vector defaults, and binds the `"parentEmitter"` outlet.
- Confidence: **verified**.

### ParticleEmitterComponent::LoadFromProtobufMessage(Proto::Component const&)
- Mangled: `_ZN5Caver24ParticleEmitterComponent23LoadFromProtobufMessageERKNS_5Proto9ComponentE` (0x396F64).
- Behavior: `Component::LoadFromProtobufMessage`, then updates the two
  resource outlets (id + released target swap), the +0x138/+0x161 flags,
  and particleCount (+0x13C).
- Side effects: releases the previous particle-system/texture targets.
- Confidence: **verified** (offsets); flag semantics inferred.

## Open questions / unresolved offsets

- The `ParticleSystem` class itself (allocated elsewhere) is undocumented;
  this component only holds its id/target pairs.
- The two ctor default vectors (`xmmword_259F30`, `xmmword_25A630`) were not
  decoded (likely a velocity range and a color/gravity default).
- Which outlets beyond `parentEmitter` map to which BindOutlet names is
  unknown (ctor names were only visible for the last two binds).

## Proposed SRE hooks

- `ParticleEmitterComponent_SetEmit(comp, bool)` — toggle the inferred
  emitting flag at +0x161 for effect-testing overlays.
- `ParticleEmitterComponent_GetParticleCount(comp)` / `_SetParticleCount` —
  read/scale the +0x13C density at runtime.
- **Safe**: outlet targets must go through the release pattern in Load;
  never write +0x110/+0x128 raw.