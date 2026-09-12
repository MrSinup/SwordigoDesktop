# ProgramState

## Summary

`Caver::ProgramState` is the engine's per-scene/per-object **Lua execution context**.
It wraps a `lua_State*` and provides the coroutine model the game's `.lua` scripts
run under: a state is either a *root* state (owns a freshly created `lua_State` via
`luaL_newstate`) or a *child* state (owns a `lua_newthread` borrowed from its
parent's state), and children are driven from the parent's `Update` loop with
per-child wait/abort bookkeeping.

Every `Scene` embeds a root `ProgramState` at **+0x28** (see `Scene.md`), and every
`SceneObject` that carries a script embeds one at **+0x118** (see `SceneObject.md`).
The Scene registers its embedded state as the parent for per-object states and for
states created by `CreateChildState`, so the whole scene's script stack is one
thread hierarchy rooted at the scene's Lua state.

Header: `src/sre/sre13/caver/ProgramState.h`.

## Struct layout

Verified from the ARM64 v1.4.13 ctor (`0x544C68`), `Update` (`0x545480`),
`CreateChildState` (`0x545158`), `SetParentObject` (`0x545210`),
`PrepareForRemoval` (`0x5450D0`), `Wait`/`Abort`/`Resume`/`Execute`. All offsets
are for the 64-bit ABI; the 32-bit column is inferred by analogy (object is
`0x60` bytes on 64-bit, `operator new(0x60)` in `CreateChildState`).

| Offset (64-bit) | Offset (32-bit, inferred) | Size | Type | Field | Notes |
|---|---|---|---|---|---|
| 0x00 | 0x00 | 8 | `lua_State*` | `L` | Root: own `luaL_newstate()`; child: `lua_newthread(parent->L)`. |
| 0x08 | 0x04 | 8 | `ProgramState*` | `parent` | Raw pointer; only set on child states (ctor arg). `Execute` branches on it. |
| 0x10 | 0x08 | 8 | `void*` | `childrenBegin` | Intrusive list head of child states; empty ⇒ `= &this[0x10]`. |
| 0x18 | 0x0C | 8 | `void*` | `childrenEnd` | Tail pointer; empty ⇒ `= &this[0x10]`. |
| 0x20 | 0x10 | 8 | `int64` | `childCount` | Decremented in `Update` when a child is unlinked (was `this+4`). |
| 0x28 | 0x14 | 8 | `SceneObject*` | `parentObject` | `boost::intrusive_ptr<SceneObject>` (ptr at +0x28, refcount lives in object at +8). Set via `SetParentObject`, released by `PrepareForRemoval`/child removal. |
| 0x30 | 0x18 | 16 | `ProgramTable` | `registryTable` | `{lua_State*, LUA_REGISTRYINDEX (-10002)}`. Maps `lua_State*` → `ProgramState*` via `ProgramTable::SetPointerForPointerKey`; this is what `FromLuaState` looks up. |
| 0x40 | 0x20 | 16 | `ProgramTable` | `globalsTable` | `{lua_State*, LUA_GLOBALSINDEX (-10000)}`. Second table ref, also pointing at `L`. |
| 0x50 | 0x28 | 4 | `int` | `state` | 0 = running, 1 = waiting (`Wait`), 2 = aborted (`Abort`). Checked by `Update` at `this+0x50` (`this+20` as DWORD). |
| 0x54 | 0x2C | 4 | `float` | `waitTimeRemaining` | Counted down by `Update`; on expiry the coroutine is resumed via `lua_resume`. |
| 0x58 | 0x30 | 1 | `bool` | `paused` | If set, `Update` still runs but skips children unless the child has `ignoreParentPause` (byte 0x5A). |
| 0x59 | 0x31 | 1 | `bool` | `active` | Default 1 in ctor. Gates the whole update (`Update` returns early if bytes 0x59 and 0x5A are both 0). |
| 0x5A | 0x32 | 1 | `bool` | `ignoreParentPause` | Child flag: update even when the parent is paused. |
| 0x5B | 0x33 | 1 | `bool` | `finished` | Set when a coroutine finishes/errors or `Abort()` is called; parent `Update` unlinks and deletes the child when set. |
| 0x5C | 0x34 | 4 | `float` | `speedMultiplier` | Default `1.0f`. `Update` scales dt by it (`this+0x5C`), and the object's own `updateSpeedMultiplier` is folded in first when `parentObject` is set. |
| 0x60 | 0x38 | — | — | (end) | Size 0x60 (64-bit) per `operator new(0x60)` in `CreateChildState`. |

The header's guess had `parentObject` at 0x20/0x28 and a `luaRef`/`speedMultiplier`
pair at the tail; the real layout puts the intrusive child list at 0x10, the
`parentObject` intrusive_ptr at **0x28**, two `ProgramTable`s at 0x30/0x40, and the
state/wait/flags/speed block at 0x50–0x5C.

## Object graph & lifecycle

- **Root states** are embedded (Scene +0x28, SceneObject +0x118) and constructed
  with `parent == NULL` → they allocate their own `lua_State`.
- **Child states** are heap-allocated (`operator new(0x60)`) by `CreateChildState`,
  which also allocates a `boost::shared_ptr` wrapper and an intrusive list node
  (`{prev@+0, next@+8, state*@+16, shared_count*@+24}`, 0x20 bytes). The child is
  linked into the parent's list at `parent+0x10`.
- A child holds its **parent's** `shared_ptr` to itself? No — the list node holds
  the shared_count; `Update` releases it when the child's `finished` byte is set,
  then unlinks the node and deletes it. The child's own +0x28 `parentObject`
  intrusive_ptr is released at the same time.
- `PrepareForRemoval` drops the +0x28 `parentObject` ref; `SetParentObject` takes
  a `boost::intrusive_ptr<SceneObject> const&` (one QWORD + refcount bump at +8 of
  the object).

## Exported functions

### ProgramState::ProgramState(ProgramState* parent)
- Signature: `ProgramState(ProgramState *parent)` — ctor.
- Mangled: `_ZN5Caver12ProgramStateC2EPS0_` (0x544C68).
- Call sites: `CreateChildState` (via `0x60FC60`), root-state embedding.
- Behavior: zeroes the object; sets `active=1`, `state=0`, `speedMultiplier=1.0f`.
  If `parent != NULL` → `lua_newthread(parent->L)` stored in +0x00 and registered
  in the parent's `ProgramTable` (pointer-key → this). Else → `luaL_newstate()`,
  installs the panic handler, `RegisterProgramLibrary`, `ProgramMath::RegisterLibraries`.
  Sets both `ProgramTable`s (+0x30 → `{L, LUA_REGISTRYINDEX}`, +0x40 → `{L, LUA_GLOBALSINDEX}`)
  and registers `this` for its own `L` in the registry table.
- Side effects: allocates a Lua state or thread; registers C functions.
- Confidence: **verified** (IDA).

### ProgramState::FromLuaState(lua_State*)
- Signature: `static ProgramState *FromLuaState(lua_State *L)`.
- Mangled: `_ZN5Caver12ProgramState12FromLuaStateEP9lua_State` (0x545100).
- Call sites: Lua binding helpers that need the `ProgramState` for a raw `lua_State`.
- Behavior: builds a stack `ProgramTable{ L, LUA_REGISTRYINDEX }` and returns
  `ProgramTable::PointerForPointerKey(...)` — i.e. the reverse of the ctor's registration.
- Confidence: **verified**.

### ProgramState::CreateChildState()
- Signature: `boost::shared_ptr<ProgramState> CreateChildState()` (returns via hidden `shared_ptr*` out-arg).
- Mangled: `_ZN5Caver12ProgramState16CreateChildStateEv` (0x545158).
- Call sites: `Scene::FinishLoad` (runs the scene script in a child), script `CreateChildState()` calls.
- Behavior: allocates a 0x60-byte state, constructs it with `this` as parent, wraps
  it in a `shared_ptr`, links it into the parent's child list at `parent+0x10`,
  increments the parent's `childCount` (+0x20).
- Confidence: **verified**.

### ProgramState::Update(float dt)
- Signature: `void Update(float dt)`.
- Mangled: `_ZN5Caver12ProgramState6UpdateEf` (0x545480).
- Call sites: `Scene::Update` (scene root), parent-state recursion, per-object states.
- Behavior: if `active|ignoreParentPause` is clear, returns. Reads `parentObject`
  (+0x28) and folds in `SceneObject::updateSpeedMultiplier()`; scales dt by
  `speedMultiplier` (+0x5C). If `state == 1` (waiting), decrements
  `waitTimeRemaining` (+0x54) and — when it crosses zero — clears `state`, calls
  `lua_resume(L)`, and marks `finished` (+0x5B) on error. Then walks the child
  list, recursing `Update` into each child (skipping children whose parent is
  paused unless the child has `ignoreParentPause`), and **unlinks + deletes any
  child whose `finished` byte is set** (releasing the node's shared_count and the
  child's +0x28 intrusive_ptr, decrementing `childCount`).
- Side effects: mutates Lua state (resumes coroutines), may delete child states.
- Confidence: **verified**.

### ProgramState::Wait(float seconds) / Abort() / Resume(int) / Execute(int)
- Wait: `_ZN5Caver12ProgramState4WaitEf` (0x5457AC) — sets `state=1`, `waitTimeRemaining=seconds`.
- Abort: `_ZN5Caver12ProgramState5AbortEv` (0x5457BC) — sets `finished=1`, `state=2`.
- Resume: `_ZN5Caver12ProgramState6ResumeEi` (0x545624) — clears `state`, `lua_resume(L)`, sets `finished` on error.
- Execute: `_ZN5Caver12ProgramState7ExecuteEi` (0x5456E0) — child: `lua_resume(L, nargs)`; root: `lua_pcall(L, nargs, 0, 0)` then `lua_settop` on error.
- Confidence: all **verified**.

### ProgramState::LoadProgram(Program const&) / ExecuteProgram(Program const&)
- `LoadProgram` (0x5456D0): `Program::LoadIntoState(prog, L)` — compiles the program into the state.
- `ExecuteProgram` (0x545768): `LoadProgram` then `Execute(0)`.
- Confidence: **verified**.

### Stack helpers
`PushInt/PushBool/PushFloat/PushVector3/PushRectangle/PushPointer/PushValueAtStackIndex`
(0x5459B0–0x5459E4) and `Pop(int)` (`lua_settop(L, ~n)`, 0x54575C), plus typed
accessors `IsIntAtStackIndex`/`IntAtStackIndex`/`IsFloatAtStackIndex`/`FloatAtStackIndex`/
`IsBoolAtStackIndex`/`BoolAtStackIndex`/`StringAtStackIndex`/`PointerAtStackIndex`
(0x5458A0–0x5457D0) are thin Lua C-API wrappers. `RegisterFunctions(LibFunction const*)`
(0x545A74) registers a table of C functions.

## Open questions

- Exact meaning of byte 0x58 (`paused`): `Update`'s child gate reads
  `!parent->byte0x58 || child->byte0x5A` — consistent with "paused parent skips
  children unless they opt out", but nothing in the recovered functions *sets* 0x58
  or 0x5A; they are only read. Likely driven by pause/resume bindings.
- 32-bit column is entirely inferred; no 32-bit build of this engine was examined.
- `ProgramTable`'s own layout (the `{void*, int}` pair) is inferred from the two
  call sites; the type's full definition wasn't recovered.

## Proposed SRE hooks

- `g_sre_program_state` root-state pointer would let scripts inspect
  `L`, `parentObject`, and per-child state cheaply without guessing offsets.
- A `ProgramState_Update` wrapper already exists implicitly via `Scene_Update`;
  exposing a dedicated `ProgramState_ExecuteString(lua_State*, const char*)`
  (there is a `ExecuteString` at 0x54566C) would give SRE a safe REPL-style
  eval channel into the scene's Lua state.
- Safe to expose read-only: `childCount`, `state`, `waitTimeRemaining`,
  `speedMultiplier` (as an SRE-side scalar that is written back each frame —
  **needs an accessor function**, not raw field writes, because the object may be
  deleted by the parent when `finished`).
- **Do not** expose raw writes to `childrenBegin/End` or `childCount`: the parent
  `Update` loop owns the list and will walk deleted nodes if the list is corrupted.