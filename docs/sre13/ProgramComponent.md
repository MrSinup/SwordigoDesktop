# ProgramComponent (`Caver::ProgramComponent`)

## Summary

`ProgramComponent` attaches a standalone script `Program` to a `SceneObject`
— the generic "run this script" component (cutscene triggers, environment
behaviors, one-shot logic that isn't a monster or touchable). It has three
small config fields (an enabled byte, a mode dword from a lookup table, a
second flag byte) plus the embedded `Program` at +0x78.

Allocation size: **0xC8** (`operator new(0xC8)` in `Create`). Vtable at
**0x61A930**.

## Struct layout

64-bit verified from ctor (`0x33C1F4`), `LoadFromProtobufMessage`
(`0x33C39C`), `Create`.

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x00 | 0x68 | — | `Component` base | See Component.md. |
| 0x68 | 1 | `bool` | `enabled` | Ctor `= 0`; Load from proto byte (msg+12). |
| 0x69 | 3 | — | (unknown) | — |
| 0x6C | 4 | `int` | `mode` | Ctor default **1**; Load sets `dword_264A4C[proto(msg+16)-1]` — a lookup-table value. **Inferred: execution mode / priority.** |
| 0x70 | 1 | `bool` | `runWhenPaused` | Ctor default **1**; Load from proto byte (msg+13). **Inferred: allow execution while game is paused.** |
| 0x71 | 7 | — | (unknown) | — |
| 0x78 | 0x38 | `Program` | `program` | The embedded script, loaded via `Program::LoadFromProtobufMessage` (proto field 1). |
| 0xB0 | 24 | — | (unknown) | Zeroed in ctor (OWORDs at +0xB0..+0xC7). |
| 0xC8 | 1 | — | (end) | Size 0xC8. |

## Vtable (0x61A930)

33-slot `Component` layout; overridden: dtor, `Clone`, `Load`/`Save`,
`ShouldSave`, **`Process` (+0x38 — overridden: this is the component that
actually *runs* its program each frame)**, `Prepare` (+0x40),
**`FinishLoad` (+0x48 — overridden)**, bindings (+0x70..+0x90),
`componentCategories` (+0x98), `ImplementsInterface`/`RegisterInterfaces`
(+0xA0/+0xA8), `HandleMessage` (+0xD0).

## Exported functions

### ProgramComponent::Create()
- Mangled: `_ZN5Caver16ProgramComponent6CreateEv`
- Behavior: `operator new(0xC8)` + ctor. Size anchor.
- Confidence: **verified**.

### ProgramComponent::LoadFromProtobufMessage(Proto::Component const&)
- Mangled: `_ZN5Caver16ProgramComponent23LoadFromProtobufMessageERKNS_5Proto9ComponentE` (0x33C39C).
- Behavior: `Component::LoadFromProtobufMessage`, then enabled (+0x68),
  mode (+0x6C via the lookup table), runWhenPaused (+0x70), and the
  embedded `Program` (+0x78).
- Confidence: **verified** (offsets); mode semantics inferred.

## Open questions / unresolved offsets

- `dword_264A4C` lookup table: not decoded — the mode values (run-once?
  loop? triggered?) map to execution policies in `Process`.
- How `Process` differs from the base (which is a no-op) needs the body:
  it likely feeds the program into `ProgramState`'s execution queue.

## Proposed SRE hooks

- `ProgramComponent_GetProgram(comp)` — introspect what script a trigger
  runs (already a `Program*` at +0x78 — hand to `ProgramState` tooling).
- `ProgramComponent_SetEnabled(comp, bool)` — toggle triggers without
  touching their scripts, for scene-testing harnesses.
- **Safe**: the mode dword should go through the same lookup (or the real
  setters) so `Process`'s expectations hold; raw writes risk a bad
  execution policy.