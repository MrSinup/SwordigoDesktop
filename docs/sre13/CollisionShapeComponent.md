# CollisionShapeComponent (`Caver::CollisionShapeComponent`)

## Summary

`CollisionShapeComponent` is the physics/solidity component: it gives a
`SceneObject` a collidable shape (through the intermediate `ShapeComponent`
base, which holds the geometry and draw state). The recovered instance has
three embedded script `Program`s, a pair of range floats (default −15/+15,
likely a damage/trigger range), several friction/material-ish floats and
flags, and a self-referential list hook. It is the component the game uses
for solid ground, walls, spikes and trigger volumes.

Derives from `ShapeComponent` (which derives from `Component`). Allocation
size: **0x1F8** (`operator new(0x1F8)` in `Create`). Vtable at **0x619038**.

## Struct layout

64-bit verified from ctor (`0x328DA8`), `LoadFromProtobufMessage`
(`0x32920C`), `Create`.

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x00 | 0x68 | — | `Component` base | See Component.md. |
| 0x68 | 0x18 | — | `ShapeComponent` fields | dword at +0x68, qwords at +0x70/+0x78 (shape bounds/draw state). Copied by CollisionShape::Load to +0x130/+0x138/+0x140. |
| 0x80 | 8 | `void*` | (unknown) | Not written by CollisionShape ctor directly (ShapeComponent's). |
| 0x88 | 0x38 | `Program` | `program[0]` | Embedded script (0x38 bytes, loaded via `Program::LoadFromProtobufMessage`). **Inferred: on-enter trigger.** |
| 0xC0 | 0x38 | `Program` | `program[1]` | Second embedded script. **Inferred: on-exit trigger.** |
| 0xF8 | 0x38 | `Program` | `program[2]` | Third embedded script. **Inferred: per-tick/contact.** |
| 0x130 | 4 | `int` | (shape copy) | Load copies `this+0x68` here. |
| 0x134 | 4 | — | (unknown) | — |
| 0x138 | 16 | — | (bounds copy) | Load copies qwords +0x70/+0x78 here (16 bytes). |
| 0x148 | 4 | `float` | (unknown) | Ctor `= 0.0f`. |
| 0x14C | 4 | — | (unknown) | — |
| 0x150 | 4 | `float` | (unknown) | Ctor `= 1.0f` (qword 0x3F80000000000000 at +0x148 spans {0.0, 1.0}). |
| 0x154 | 4 | — | (unknown) | — |
| 0x158 | 1 | `bool` | (unknown) | Ctor `= 0`. |
| 0x159 | 11 | — | (unknown) | Zeroed OWORDs at +0x108..+0x12F region… |
| 0x164 | 8 | — | (unknown) | Zeroed (qword). |
| 0x16C | 8 | — | (unknown) | Zeroed (qword). |
| 0x178 | 4 | `float` | `rangeMin` | Ctor default **−15.0**; Load overwrites from proto field (dword at proto msg+20). **Inferred: damage/trigger range.** |
| 0x17C | 4 | `float` | `rangeMax` | Ctor default **+15.0**; proto field (msg+24). |
| 0x180 | 4 | — | (unknown) | — |
| 0x184 | 4 | — | (unknown) | — |
| 0x188 | 4 | — | (unknown) | — |
| 0x18C | 4 | — | (unknown) | — |
| 0x190 | 4 | — | (unknown) | — |
| 0x194 | 4 | — | (unknown) | — |
| 0x198 | 4 | — | (unknown) | — |
| 0x19C | 4 | — | (unknown) | — |
| 0x1A0 | 1 | `bool` | `= 1` | Ctor default true; Load overwrites from proto byte (msg+12). **Inferred: solid/enabled.** |
| 0x1A1 | 1 | — | (unknown) | Ctor dword at +0x1A1 = 0. |
| 0x1A2 | 4 | `int` | (unknown) | Load writes `this+0x1A2` from proto byte (msg+13, 4-byte write). |
| 0x1A5 | 3 | — | (unknown) | — |
| 0x1A8 | 4 | `float` | (unknown) | Load writes `dword_2647AC[index]` — a lookup-table value. Ctor 0.0f. |
| 0x1AC | 4 | `float` | (unknown) | Load from proto (msg+32). Ctor 1.0f (qword at +0x1A8 = {0.0, 1.0}). |
| 0x1B0 | 1 | `bool` | (unknown) | Load from proto byte (msg+36). Ctor `= 0`. |
| 0x1B1 | 7 | — | (unknown) | — |
| 0x1B8 | 8 | — | (unknown) | Ctor qword `= 0`. |
| 0x1C0 | 8 | `void*` | (list node) | Ctor `= this` — self-referential hook (group/manager list). |
| 0x1C8 | 1 | `bool` | (unknown) | Ctor `= 0`. |
| 0x1C9 | 3 | — | (unknown) | — |
| 0x1CC | 8 | — | (unknown) | Zeroed (qword). |
| 0x1D4 | 8 | — | (unknown) | Zeroed (qword). |
| 0x1DC | 16 | — | (unknown) | — |
| 0x1EC | 8 | — | (unknown) | Zeroed (qword at +0x1EC). |
| 0x1F4 | 1 | `bool` | (unknown) | Ctor `= 0`. |
| 0x1F5 | 3 | — | (end) | Size 0x1F8. |

## Vtable (0x619038)

33-slot `Component` layout; overridden: dtor, `Clone` (+0x10), `Load`/`Save`
(+0x20/+0x28), `ShouldSave` (+0x30), `Prepare` (+0x40), bindings
(GetBindings..PerformAction +0x70..+0x90), `componentCategories` (+0x98,
inherited from `ShapeComponent` — the dump shows `ShapeComp` there),
`ImplementsInterface`/`RegisterInterfaces` (+0xA0/+0xA8), `HandleMessage`
(+0xD0), `HasBounds` (+0xD8), `localAABB` (+0xE0), `minDepth`/`maxDepth`
(+0xE8/+0xF0). `Update` stays base.

## Exported functions

### CollisionShapeComponent::Create()
- Mangled: `_ZN5Caver23CollisionShapeComponent6CreateEv`
- Behavior: `operator new(0x1F8)` + ctor. Size anchor.
- Confidence: **verified**.

### CollisionShapeComponent::CollisionShapeComponent()
- Mangled: `_ZN5Caver23CollisionShapeComponentC2Ev` (0x328DA8).
- Behavior: calls `ShapeComponent::ShapeComponent`, sets the three vtable
  pointers, defaults the range pair to −15/+15, `solid` to true, the
  self-referential hook at +0x1C0, and zeroes the program regions.
- Confidence: **verified**.

### CollisionShapeComponent::LoadFromProtobufMessage(Proto::Component const&)
- Mangled: `_ZN5Caver23CollisionShapeComponent23LoadFromProtobufMessageERKNS_5Proto9ComponentE` (0x32920C).
- Behavior: `ShapeComponent::LoadFromProtobufMessage`, then loads three
  `Program`s (+0x88/+0xC0/+0xF8), the range pair (+0x178/+0x17C), the
  solid/enabled byte (+0x1A0), a lookup-table float (+0x1A8), and copies the
  ShapeComponent bounds to +0x130/+0x138/+0x140.
- Confidence: **verified** (offsets); program roles inferred.

## Open questions / unresolved offsets

- The three embedded `Program`s' roles (enter/exit/tick?) — check
  `ProgramState` creation sites for this component.
- The ±15 range pair: candidate meanings are damage range, trigger distance,
  or "bounce" limits — needs a physics call-site check.
- `dword_2647AC[index]` at +0x1A8: the lookup table was not decoded; likely
  maps a proto enum (shape type / friction material) to a float.
- `ShapeComponent` (the intermediate base) deserves its own doc; its
  fields at +0x68..+0x7F and draw path were only partially traced.

## Proposed SRE hooks

- `CollisionShapeComponent_SetSolid(comp, bool)` — toggle solidity live
  (byte +0x1A0), enabling walk-through-walls debug hacks.
- `CollisionShapeComponent_SetRange(comp, min, max)` — widen/narrow the
  trigger range (+0x178/+0x17C).
- The embedded programs are full `Program` objects: SRE can feed them to
  `ProgramState` for introspection (what script runs when the player touches
  this spike?).
- **Safe**: never touch the +0x1C0 self-referential node or the ShapeComponent
  bounds copies — those are manager-owned.