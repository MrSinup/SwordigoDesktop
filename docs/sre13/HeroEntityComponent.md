# HeroEntityComponent (`Caver::HeroEntityComponent`)

## Summary

`HeroEntityComponent` is the component that marks a `SceneObject` as the
playable hero. It is a thin subclass of `EntityComponent` (which embeds the
`PhysicsObjectState` and the shared entity scripting state) plus one embedded
script `Program` at +0x148 — the hero's control/behavior script. The game's
`GameSceneController` finds the hero by this component (hero@0xD8 in the GSC)
and drives player input through it.

Derives from `EntityComponent` → `Component`. Allocation size: **0x1B8**
(`operator new(0x1B8)` in `Create`). Vtable at **0x61EB40**.

## EntityComponent base (shared with MonsterEntityComponent)

`EntityComponent` (ctor `0x36488C`, size 0x128, vtable 0x61E1D0) adds to
`Component`:

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x68 | 4 | `int` | `= 1` | Ctor sets dword at +0x68 to 1. **Inferred: entity enabled flag.** |
| 0x6C | 4 | — | (unknown) | — |
| 0x70 | 0x9C | `PhysicsObjectState` | embedded physics state | Constructed by ctor (`PhysicsObjectState::PhysicsObjectState(this+0x70)`). Own class; needs its own doc. |
| 0x10C | 2 | `uint16` | `= 0` | Ctor. |
| 0x110 | 8 | — | `= 0` | Ctor (qword). |
| 0xFC | 16 | — | default `xmmword_259BD0` | Ctor default constant (region overlaps PhysicsObjectState tail — see note). |
| 0x120 | 1 | `bool` | `= 0` | Ctor. |
| 0x121 | 7 | — | (end) | Size 0x128. |

`EntityComponent::LoadFromProtobufMessage` (0x364968) handles the shared
entity proto extension before subclasses load theirs. `EntityComponent::Update`
(0x36xxxx) advances the physics state; both Hero and Monster call it first.

## Struct layout (Hero-specific)

64-bit verified from ctor (`0x36D0D8`), `LoadFromProtobufMessage`
(`0x36D250`), `Create`.

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x00 | 0x128 | — | `EntityComponent` base | Above. |
| 0x128 | 0x20 | — | (unknown) | Zeroed (OWORDs at +0x128, +0x138). Hero-specific state before the program — **unresolved** (candidates: hero mode/state, dash flags). |
| 0x148 | 0x38 | `Program` | `controlProgram` | The hero's behavior script, loaded from proto field 1 (`Program::LoadFromProtobufMessage(this+0x148, ...)`). |
| 0x180 | 0x38 | — | (unknown) | Zeroed in ctor (OWORDs at +0x188, +0x1A0, +0x1B0…; Load writes nothing here). |
| 0x1B8 | 1 | — | (end) | Size 0x1B8. |

## Vtable (0x61EB40)

33-slot `Component` layout; overridden: dtor, `Clone`, `Load`/`Save`,
`ShouldSave`, `Process` (+0x38), `Prepare` (+0x40), bindings (+0x70..+0x90),
`componentCategories` (+0x98), `ImplementsInterface`/`RegisterInterfaces`
(+0xA0/+0xA8), `Update(float)` (+0xC8), `HandleMessage` (+0xD0). Bounds
slots stay `Component`'s.

## Exported functions

### HeroEntityComponent::Create()
- Mangled: `_ZN5Caver19HeroEntityComponent6CreateEv`
- Behavior: `operator new(0x1B8)` + ctor. Size anchor.
- Confidence: **verified**.

### HeroEntityComponent::HeroEntityComponent()
- Mangled: `_ZN5Caver19HeroEntityComponentC2Ev` (0x36D0D8).
- Behavior: calls `EntityComponent::EntityComponent`, sets the three vtable
  pointers, zeroes +0x128..+0x1B7 (including the control program region).
- Confidence: **verified**.

### HeroEntityComponent::LoadFromProtobufMessage(Proto::Component const&)
- Mangled: `_ZN5Caver19HeroEntityComponent23LoadFromProtobufMessageERKNS_5Proto9ComponentE` (0x36D250).
- Behavior: `EntityComponent::LoadFromProtobufMessage`, then loads the single
  control `Program` at +0x148.
- Confidence: **verified**.

### HeroEntityComponent::Update(float)
- The hero's per-frame update lives here/EntityComponent::Update; it was not
  extracted in this pass (the hero is driven by input + the control
  ProgramState, unlike the monster AI).
- Confidence: **inferred** (function exists in vtable; body not read).

## Open questions / unresolved offsets

- +0x128..+0x147 (32 bytes before the control program): zeroed, nothing
  loaded — candidates are hero-mode/state enums used by the control script.
- The hero is usually created by `GameSceneController::SpawnHeroAt`/
  `CreateHeroObjectAt` — the exact field writes there should be cross-checked
  against this layout (the GSC writes hero@0xD8, components at 0xE0–0xF8).
- `PhysicsObjectState` (embedded at +0x70) needs its own reverse-engineering
  pass: it holds velocity, gravity, on-ground flags, etc.

## Proposed SRE hooks

- `HeroEntityComponent_GetControlProgram(comp)` — hand the control `Program`
  at +0x148 to `ProgramState` tooling for script introspection.
- Live hero-state editing is better done through `GameSceneController`'s
  hero reference (`g_sre_hero` → `SceneObject`) and the embedded
  `PhysicsObjectState` once it is documented: teleport, velocity injection,
  gravity toggles all live there.
- **Safe**: write the control program only by replacing it via the load path
  (a saved-game edit), not by mutating +0x148 raw while a ProgramState is
  running.