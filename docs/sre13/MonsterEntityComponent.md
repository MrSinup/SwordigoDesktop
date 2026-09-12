# MonsterEntityComponent (`Caver::MonsterEntityComponent`)

## Summary

`MonsterEntityComponent` gives a `SceneObject` monster AI. It derives from
`EntityComponent` (embedded `PhysicsObjectState` at +0x70) and adds two
embedded script `Program`s — the monster's behavior program and a second
program (likely the "on-death"/attack program) — plus a runtime AI state:
facing vector, current heading angle, an activation timer, and two pointers
to attached runtime objects (a flag-object and a sub-component, updated via
its virtual `Update`). Its `Update` rotates the owner toward a target angle
and marks the owner out-of-bounds when its world AABB leaves the scene bounds.

Derives from `EntityComponent` → `Component`. Allocation size: **0x1B8**
(`operator new(0x1B8)` in `Create`). Vtable at **0x61F050**.

## Struct layout

64-bit verified from ctor (`0x373FAC`), `LoadFromProtobufMessage`
(`0x37422C`), `Update` (`0x374B9C`), `Create`.

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x00 | 0x128 | — | `EntityComponent` base | See HeroEntityComponent.md (embedded PhysicsObjectState at +0x70). |
| 0x128 | 0x38 | `Program` | `behaviorProgram` | Embedded script (0x38 bytes), loaded via `Program::LoadFromProtobufMessage` from proto field 1. The monster's AI script. |
| 0x160 | 0x38 | `Program` | `program[1]` | Second embedded script (proto field 2). **Inferred: attack/on-death program.** |
| 0x198 | 4 | `float` | `angle` | Current heading angle (radians). Written by `Update` (`this+0x198`), read when rotating the owner. Ctor default 0. |
| 0x19C | 4 | `float` | `timer` | Activation timer, accumulated with `dt` in `Update` (`this+0x19C`); gates a flag-object toggle after 0.2 s. Ctor default FLT_MAX. |
| 0x1A0 | 8 | `void*` | (flag object) | Pointer whose byte at +0xB4 (180) is set to 1 once `timer > 0.2` — **inferred: an "awake/alert" flag object or second entity.** |
| 0x1A8 | 8 | `Component*` | (sub-component) | A component whose virtual `Update` (vtable+0xC8) is invoked each frame — **inferred: attack/damage component**. |
| 0x1B0 | 1 | `bool` | (unknown) | Load from proto (msg+32, field 3); ctor sets WORD at +0x1B0 to 0x0101 (bytes 1,1). |
| 0x1B1 | 1 | `bool` | (unknown) | See above (part of ctor WORD 257). |
| 0x1B2 | 1 | `bool` | (unknown) | Load from proto (msg+33); ctor sets it to 1. |
| 0x1B3 | 5 | — | (end) | Size 0x1B8. |

Runtime AI fields read by `Update` (all **verified** offsets, semantics
inferred): byte +0xE4 gates the AI update; float +0x78 (compared to 0.1)
selects between idle and chase behavior; the facing `Vector2` at +0x80/+0x84
whose angle is computed when the monster is chasing; byte +0x88 (a
"frozen"/stunned flag checked in the chase condition); and float +0x84 (the
facing y, must be > 0).

## Vtable (0x61F050)

33-slot `Component` layout; overridden: dtor, `Clone`, `Load`/`Save`,
`ShouldSave`, `Process`, `Prepare`, bindings (+0x70..+0x90),
`componentCategories`, `ImplementsInterface`/`RegisterInterfaces`,
`UpdateWhenPaused` (+0xC0 — **overridden**, unlike most components),
`Update(float)` (+0xC8), `HandleMessage` (+0xD0).

## Exported functions

### MonsterEntityComponent::Create()
- Mangled: `_ZN5Caver22MonsterEntityComponent6CreateEv`
- Behavior: `operator new(0x1B8)` + ctor. Size anchor.
- Confidence: **verified**.

### MonsterEntityComponent::Update(float dt)
- Mangled: `_ZN5Caver22MonsterEntityComponent6UpdateEf` (0x374B9C).
- Behavior (verified):
  1. If byte +0xE4 is false → skip AI.
  2. If float +0x78 < 0.1 (idle): compute the desired heading from the
     facing Vector2 (+0x80/+0x84) when chase conditions hold, else keep
     straight ahead (1.0 rad/sec default turn rate `v5`).
  3. Compare the owner's current rotation (`owner+0x90`) against the desired
     angle (+0x198); if they differ by > 0.01, rotate the owner toward it,
     writing `owner+0x90` and setting the owner's bounds-dirty flag
     (`owner+0xC4`) + `Scene::RegisterForWorldBoundsUpdate`.
  4. Calls `EntityComponent::Update` (physics).
  5. Accumulates the +0x19C timer; after 0.2 s flips byte at +0xB4 of the
     +0x1A0 flag object.
  6. Calls the sub-component's virtual `Update` (+0x1A8 → vtable+0xC8).
  7. If the monster's world AABB (owner+0xB4, a `Rectangle`) stops
     intersecting the scene's bounds rectangle (scene at owner+0x20, rect at
     scene+0x98), sets owner byte +0xF8 = 1 (out-of-bounds/cull flag).
- Side effects: rotates the owner, mutates flags, marks culling.
- Confidence: **verified** (offsets and flow from decompilation).

### MonsterEntityComponent::LoadFromProtobufMessage(Proto::Component const&)
- Mangled: `_ZN5Caver22MonsterEntityComponent23LoadFromProtobufMessageERKNS_5Proto9ComponentE` (0x37422C).
- Behavior: `EntityComponent::LoadFromProtobufMessage`, then loads the two
  `Program`s (+0x128/+0x160) and the three flag bytes (+0x1B0/+0x1B1/+0x1B2).
- Confidence: **verified**.

## Open questions / unresolved offsets

- The two programs' exact roles (behavior vs. attack/death) — confirmed by
  call sites into `ProgramState` from the monster's message handling.
- +0x78 (the <0.1 float) and +0x88 (the chase gate byte): likely "alertness"
  and "stunned" — needs the damage-handling call site.
- The +0x1A0 flag object's class (byte at +0xB4) and the +0x1A8
  sub-component's class (invoked via vtable+0xC8).
- Note: `SceneObject` +0xB4 is used as a `Rectangle` (world AABB) by the
  monster Update — this resolves part of the previously "unresolved" +0xB4
  region in SceneObject.md (world bounds, computed by UpdateBounds).

## Proposed SRE hooks

- `MonsterEntityComponent_SetFrozen(comp, bool)` — write byte +0x88 to
  freeze/thaw any monster, the building block for slow-mo and debug
  "stop the horde" hacks.
- `MonsterEntityComponent_GetBehaviorProgram(comp)` — introspect the AI
  program at +0x128 with `ProgramState` tooling.
- **Safe**: the angle/timer fields are runtime AI state — prefer exposing
  setters that route through the real `Update` (e.g. `SetHeading(comp, rad)`)
  over raw writes, and never touch the +0x1A0/+0x1A8 pointers until their
  classes are identified.