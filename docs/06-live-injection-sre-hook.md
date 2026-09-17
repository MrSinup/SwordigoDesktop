# 06 — Live Injection: hot-patching bytecode into a running Object

Goal: from Graphy, take a `.rbsrc` bound to `(object, hook)` and get its freshly compiled
`(String, Bytes)` into the **running** game's live object, without a scene reload.

Read `00-…` §5.5 first: **v1 does object/program-level hot rebind, not instruction-level
transplant of a suspended coroutine.** That limitation is stated up front because it
determines what can be promised.

---

## 1. What already exists (and why this is tractable at all)

This subsystem is unusually well-positioned because SRE has already built most of the
plumbing. Symbol names below are from the repo's own symbol atlas
(`arm64_dyn_symbols.txt`, `nm_out.txt` — both populated, ~1.5 MB each; note that
`swordigo_symbols.txt`, `arm64_symbols.txt` and `lua_api_dump.txt` are **empty**, so the
atlas is the source of truth) and from the SRE sources.

### 1.1 Host ↔ guest IPC (exists, in production use)

`SwordfareGUI::update_console_backend()` (`src/platform/swordfare_gui.cpp:809-930`)
implements a **cross-thread single-slot mailbox** over guest memory. The host resolves the
guest addresses of the SRE's globals via the loader:

```cpp
// src/main.cpp:4736-4741
g_lua_console_buf_addr     = g_loader_64->get_symbol_vaddr(&g_sre_mod, "g_lua_console_buf");
g_lua_console_pending_addr = g_loader_64->get_symbol_vaddr(&g_sre_mod, "g_lua_console_pending");
// …result / status likewise, then handed to SwordfareGUI::set_console_addresses(...)
//    (src/platform/swordfare_gui.cpp:3568-3573)
```

and then reads/writes those addresses inside `m_guest_memory`. Protocol (documented in the
code comment at `swordfare_gui.cpp:812-833`):

```
host  : write buf ; set status = 0 ; set pending = 1        (submit)
guest : sees pending == 1 ; runs it ; writes result ;
        sets status = 1|2 ; clears pending = 0              (complete)
host  : sees status != 0 ; reads result ; sets status = 0   (consume)
```

The comment explains the bug that forced the design: dispatching and delivering in the same
tick let a dispatch wipe an unconsumed result, so the implementation enforces **DELIVER and
DISPATCH mutually exclusive per tick, delivery always winning**, and only submits when
`pending == 0 && status == 0`.

### 1.2 A TCP console server (exists)

`SwordfareGUI::start_tcp_server(int)` / `auto_start_tcp_server(int)` /
`tcp_server_loop()` with:

| Member | Role |
| --- | --- |
| `m_tcp_server_fd`, `m_tcp_client_fd` (atomic) | single client at a time; a second client is disconnected |
| `std::deque<TcpCmd> m_tcp_cmd_queue` | FIFO of complete Lua chunks |
| `struct TcpCmd { std::string code; int client_fd; uint64_t gen; }` | each command captures the generation of its client |
| `m_tcp_cmd_in_flight`, `m_tcp_in_flight_fd`, `m_tcp_in_flight_gen` | **result ownership** — a slow result never leaks to a newer client |
| `m_tcp_client_gen`, `m_tcp_completed_seq` | generation guard + monotonic completion counter |
| `m_tcp_done_cv` | reader thread blocks here to keep output strictly ordered |

Framing is **newline-delimited, multi-line-capable**: the reader accumulates lines until it
has a complete chunk, `.` on its own line clears the buffer, `exit`/`quit` closes. It
advertises itself as "RAIJIN Lua SDK Console (Swordfare TCP Server)" and prompts with
`raijin-sdk>`.

The Qt editor already consumes it: `src/ruby/panels/console_panel.{h,cpp}`'s `"Raijin"` tab
connects over TCP via `EnginePod::lua_console_port()` (port from `SWORDFARE_TCP_PORT`) and
evaluates Lua live inside the running 1.4.13 game.

### 1.3 A live Lua execution context (exists) — `sre13_console.c`

This is the important one. `sre13_console_start_command()` does *all* of the hard work of
running new code on the live VM:

- `lua_State *T = lua_newthread(root)` on the **live game state**; `g_sre_thread_ref =
  luaL_ref(root, LUA_REGISTRYINDEX)` keeps it alive.
- A **sandbox env** per command: a fresh table with `__index` → the game's `_G`, so reads
  fall through and writes stay isolated (merged back at command end).
- Shadows that would otherwise fault: `print` (captured), `wait`, `waitFrames`, `yield`,
  and crucially **`Program.Wait` is shadowed** because *"the engine's `Wait` binding stores
  through `FromLuaState(L)` with NO null check and a console coroutine is not in the
  ProgramTable map, so the real `Program.Wait` would fault here."*
- `sre13_setup_console_program_state(T, root)` builds a **native `CaverProgramState` shim**
  (`g_console_program_state`) so game C++ APIs called from console scripts run in a valid
  ProgramState context: `L = T`, `parent = root_ps`, `child_list[0] = child_list[1] =
  &self+16` (self-referential ring), `globals.L = T, table_index = -10002`,
  `registry.L = T, table_index = -10000`, `b89 = 1`.
- `luaL_loadstring(T, "return " + code)` then falls back to `luaL_loadstring(T, code)`; the
  chunk is bound to the env with `lua_replace(T, LUA_GLOBALSINDEX)`.
- Served once per frame via `sre13_console_frame_tick(0.0f)` — called from
  `sre_ProgramState_Execute` *and* from a `CaverShell::Update` (0x323944) hook, so it runs
  in every game state, not just while scripts happen to execute.

### 1.4 Lua error containment (exists) — `sre13_lua.c` / `sre13_recovery.c`

The engine's Lua signals errors with C++ exceptions (`luaD_throw` → `__cxa_throw`), which the
emulator's custom ELF loader cannot unwind (no registered `.eh_frame`). SRE installs a
**guest setjmp recovery stack**: every Lua entry point wraps its call in
`sre13_recovery_push(L)` + `sre_setjmp(g_sre_recovery_stack[depth].buf)`, and errors
longjmp there instead of reaching `std::terminate`. On catch it calls `sre13_capture_error`,
`sre13_restore_vm(L, saved_ci, saved_top, saved_base, saved_nCcalls, …)`, bumps
`g_sre13_lua_error_count`, and returns `2` (`LUA_ERRRUN`).

`ProgramState` layout, verified against IDA (`0x545480`) and documented in
`sre13_lua.c:29-32`:

| Field | Offset |
| --- | --- |
| `lua_State* L` | `+0x00` |
| `coroutine` (`void*`, NULL ⇒ plain thread) | `+0x08` |
| `isSuspended` (`int`) | `+0x50` |
| `sleepTime` (`float`) | `+0x54` |
| `completed` (`byte`) | `+0x5B` |

`lua_State` layout: `errorJmp` is at **`0xA8`**, not the stock `0x80`
(`LUA_ERRORJMP_OFFSET`), because the engine inserts extra fields.

### 1.5 The engine's own reload primitives (exist — this is the key discovery)

The symbol atlas contains exactly the APIs a hot rebind needs:

| Symbol | Use |
| --- | --- |
| `Caver::ProgramState::LoadProgram` | **load a `Program` into a live state** |
| `Caver::ProgramState::ExecuteProgram` | run it |
| `Caver::ProgramState::ExecuteString` | run source text |
| `Caver::ProgramState::Abort` | retire a state cleanly |
| `Caver::ProgramState::PrepareForRemoval` | deferred teardown |
| `Caver::ProgramState::Update` | the per-frame tick (SRE already wraps it) |
| `Caver::ProgramState::Resume` | coroutine resume (SRE already replaces it) |
| `Caver::ProgramState::FromLuaState` | state ↔ `lua_State*` map |
| `Caver::ProgramState::Wait` | the `Program.Wait` binding |
| `Caver::Component::NewProgramStateForProgram` | **factory**: state for a `Program` |
| `Caver::SceneObject::ComponentWithIdentifier` | **object-scoped component lookup** (matches `02-…` §5.1) |
| `Caver::SceneObject::ComponentWithInterface` | component by interface id |
| `Caver::SceneObject::{GetAllComponents,GetInstanceComponents,GetTemplateComponents}` | enumeration |
| `Caver::Scene::{GetAllObjects,ActivateObject,AddObject,GroupWithIdentifier}` | scene traversal |
| `Caver::Scene::MakeUniqueObjectIdentifier` | **the source of the `#N` suffixes** in `01-…` §6.6 |
| `Caver::ComponentCollection::{AddComponent,Purge,PurgeInactive}` | component lifecycle |
| `Caver::ComponentFactory::NewComponentWithClassName` | instantiate by `ClassName` |
| `Caver::ProgramTable::{StringForKey,SetStringForKey,PointerForKey,…}` | the engine's string/pointer table (matches the console shim's `-10002` / `-10000` pseudo-indices) |
| `Caver::Component::NewProgramStateForProgram` | see above |

That last group changes the design from "reverse-engineer how to poke the VM" to
"call the engine's own documented-in-symbols reload path". `ProgramState::LoadProgram` in
particular is the honest analogue of Unreal's
`FMovieSceneEvaluationTemplate` rebuild (`08-…` §2.1): authored data changes, and the
runtime artifact is *re-derived*, not patched in place.

---

## 2. Design

### 2.1 Transport: reuse the console, add a structured guest module

**Decision: do not invent a second transport.** Reuse the existing
`Graphy → TCP → SwordfareGUI → mailbox → sre13_console` path. Add a guest-side Lua module
`Graphy` (registered by a new `src/sre/sre13/sre13_graphy.c`) that exposes the structured
operations. Then injection is *just a console command*:

```lua
Graphy.BindObject{ object = "elder", hook = "CollisionShape.OnCollide", seq = 42 }
```

#### Why not put the bytecode in the text buffer directly

Tempting but **wrong**, and worth recording because it looks like it should work:

- `sre13_console_start_command()` uses `luaL_loadstring(T, code)`, which computes the length
  with `strlen`. A Lua 5.1 chunk **always** contains NUL bytes — the 12-byte header alone has
  six of them (`05-…` §5.1) and every 4-byte length prefix and 8-byte double is full of
  zeros. `strlen` truncates at the first one. So a binary chunk can never be delivered
  through the text path.
- Registering a *new* raw-bytes mailbox would mean a second IPC protocol, a second
  cross-thread handshake, and a second crash surface for no benefit.

#### The hex-in-text path

`Graphy` accumulates hex in a guest-side buffer and commits explicitly:

```lua
Graphy.Push("elder", "CollisionShape.OnCollide", 0, "<hex 0..N>")   -- idempotent by seq
Graphy.Push("elder", "CollisionShape.OnCollide", 1, "<hex N..2N>")
Graphy.TestLoad("elder", "CollisionShape.OnCollide")                 -- load-only, no bind
Graphy.Bind("elder", "CollisionShape.OnCollide", "<source text>")    -- commit + rebind
Graphy.Status("elder", "CollisionShape.OnCollide")                   -- last result + state
Graphy.Rollback("elder", "CollisionShape.OnCollide")                 -- restore previous
```

Properties:

- Chunked, so the `CONSOLE_BUF_SIZE` (4096) limit is irrelevant; a 1 MiB chunk is fine.
- Idempotent by `seq`, so a dropped/duplicated TCP command cannot corrupt a transfer.
- `TestLoad` is usable *before* `Bind` — this is the `05-…` §5.4 ground-truth gate, and it
  makes "does this chunk even load in the real VM?" a one-command question.
- The guest keeps the previous `(String, Bytes)` per `(object, hook)`, which is what makes
  `Rollback` possible and makes "compile, try, revert" a safe loop.

The `Graphy` module is registered on the live state exactly like `ButtonController` /
`SceneShifter` (`sre13_ensure_injected`), so it is available to *both* the console coroutine
and the game's own scripts — meaning a hot-patched hook can call `Graphy` too.

### 2.2 Guest-side binding algorithm

```
bind(object_ident, hook, chunk_bytes, source_text):
   1. L   = live game lua_State (g_sre_last_lua_state, adopted by sre13_console_set_root)
   2. scene = scene_get()                       # already used by sre13_console.c
      if (!scene) -> "no scene"
   3. obj = resolve_object(scene, object_ident)  # see below
   4. target = resolve_hook_target(obj, hook)    # (component, Program field) | (SceneObject, OnLoad)
   5. snapshot(target.program) -> previous
   6. push a guest recovery frame:
          depth = sre13_recovery_push(L)
          if (sre_setjmp(g_sre_recovery_stack[depth].buf)) { restore_vm(); return "lua error" }
   7. new_program = proto::Program{ String = source_text, Bytes = chunk_bytes }
   8. swap target.program := new_program
   9. if (target.program_state)  Caver::ProgramState::LoadProgram(target.program_state, target.program)
      else                      st = Caver::Component::NewProgramStateForProgram(target.program)
   10. if (should_restart)      Caver::ProgramState::Execute(st)   # or ExecuteProgram
   11. sre13_recovery_pop(depth)
   12. store previous as the rollback slot; record the applied sha256
   13. return "ok"
```

**`resolve_object`** — prefer the engine's own lookup, because it is already exercised by
shipping scripts:

```
preferred:  call the Lua binding Scene.Find(ident) on the live L   (observed in rlsw.scl,
            town_shop.scene; no offsets guessed, no RE needed)
fallback:   iterate Caver::Scene::GetAllObjects() and compare identifiers
            (needed for objects that Scene.Find cannot see, e.g. `#N`-suffixed duplicates —
             compare against the full identifier including the suffix, 01-… §6.6)
```

**`resolve_hook_target`** — driven by the same generated registry as the graph
(`02-…` §3), because the hook can live on any of 17 owner classes (`01-…` §3.2):

```
if hook == "SceneObject.OnLoad":   target = (obj, obj->program_onload_field)
else:
    (owner_class, field) = split(hook)                 # "CollisionShape.OnCollide"
    comp = Caver::SceneObject::ComponentWithIdentifier(obj, recorded_id)   # fast path
        ?? first component whose ClassName == owner_class                  # rebind path
    target = (comp, field_address_of(comp, field))
```

The fast path uses `ComponentWithIdentifier`, which is exactly the object-scoped lookup
`02-…` §5.1 requires — the engine agrees with our resolution model, which is a good sign.

**`field_address_of(comp, field)`** — for the 21 hook slots, the offsets come from the
protobuf extension tags (`01-…` §3.2) combined with the generated class → payload slot map
(`02-…` §3.2). Where the offset is not yet known, the operation returns
`"unsupported hook target"` and the UI shows the hook chip as read-only rather than
producing a partial write. **Never** guess an offset and write to it.

### 2.3 Threading and atomicity

Everything in §2.2 runs on the **game's** thread inside `sre13_console_frame_tick`, which is
already the case for console commands. Consequences and rules:

| Rule | Why |
| --- | --- |
| The host never calls into guest memory except through the mailbox | `m_guest_memory` is a mirror; concurrent writes would corrupt the VM |
| A bind is applied **between frames**, never mid-`Update` | `sre13_console_frame_tick` is invoked from `sre_ProgramState_Execute` (i.e. at a Lua entry point) and from the `CaverShell::Update` hook — both are quiescent points for the target object's own state, because a `ProgramState::Update` that is *currently executing* is not re-entered |
| At most one bind in flight | the mailbox is a single slot; the TCP queue already serialises per client |
| A failed bind must leave the previous `Program` in place | step 5 snapshots *before* step 8 mutates; step 8 is the only mutation and its only failure mode is a recovery longjmp, which is caught in step 6 |
| Both `String` and `Bytes` are swapped together | the engine reads `String` for error messages and `Bytes` for execution; a mismatch produces misleading errors |

**Explicit non-goal (v1):** if the hook's `ProgramState` is currently *suspended* inside
`Program.Wait` (its `coroutine` at `+0x08` is non-NULL and `isSuspended` at `+0x50` is set),
we do **not** transplant the stack. Instead `LoadProgram` re-seeds the state and execution
restarts from the beginning of the new program. This is a real semantic difference from a
live code swap and the UI must say so: the chip shows `rebind (restart)`, and a
`Graphy.Restart` option controls whether restart happens immediately or at the next natural
`Execute`. Instruction-level transplant would require replacing the whole `lua_State`, which
the engine's `ProgramTable` mapping (`-10002`/`-10000`) does not support safely.

---

## 3. Phasing

Each phase is independently useful, testable, and shippable.

| Phase | Capability | New RE required | New SRE code |
| --- | --- | --- | --- |
| **0** | Read-only: reflection (`_G` table/function dump, `05-…` §3.2), live transform/field sampling for the ghost, hook inventory, error capture | none | `sre13_graphy.c` with `Graphy.Reflect`, `Graphy.Sample`, `Graphy.Hooks` |
| **1** | `Graphy.Push` / `TestLoad` — load a compiled chunk on the live VM and report load/run result. Closes `05-…` §5.4's ground-truth loop | none | hex accumulate + `luaL_loadbuffer(L, buf, len, "@graphy")` + recovery frame |
| **2** | `Graphy.Bind` for objects that are **template-backed**: destroy + `Scene.CreateObject(template, name, parent)` + transform copy + `Scene.ActivateObject` | none (all Lua-observable) | bind path using `Scene.CreateObject` |
| **3** | `Graphy.Bind` for **any** object/hook: in-place `Program` swap + `LoadProgram` / `NewProgramStateForProgram` | hook-slot offsets per owner class (from tags + IDA for the runtime layout) | `field_address_of` table, `LoadProgram` call |
| **4** | `Graphy.Rollback`, and `Graphy.Bind` batched across several hooks in one frame | none | snapshot stack per `(object, hook)` |

Phase 3 is where the remaining risk is, and it is *bounded*: the symbol names exist
(`ProgramState::LoadProgram`), the ProgramState offsets are already verified (`+0`, `+0x08`,
`+0x50`), and the class→slot map is generated. What is missing is the runtime component
layout that maps a payload field to its `Program` proto. **Concrete RE task:** in the IDA
database `libswordigo.so.i64` (15 MB, present), open
`Caver::Component::NewProgramStateForProgram` and follow its argument back to the
`Program*` field it reads, for two different owner classes (`ProgramComponent` and
`CollisionShapeComponent`) — that yields both the generic accessor and the pattern for all
21 slots. Time-box: this is a few hours with the IDA DB already built.

---

## 4. Safety

| Concern | Mitigation |
| --- | --- |
| A hot-patched hook infinite-loops and starves SDL | The SRE already installs an instruction-count hook (`sre13_arm_timeout` / `sre13_disarm_timeout` around every guarded Lua entry) plus a per-frame time-slice budget (`sre12/sre_frame_loop.c`'s `sre_ProgramState_Update` budget poll). `Graphy.Bind` inherits both by construction, because the code runs in the engine's own `ProgramState::Update` path. |
| A hot-patched hook raises | Guest setjmp recovery stack (`§1.4`); the bind frame is popped, error captured, `Program` restored, `g_sre13_lua_error_count` incremented |
| A malformed chunk corrupts the VM | `Graphy.TestLoad` is mandatory before `Bind` in the UI flow; the chunk is loaded *before* any mutation, and a load failure aborts at step 6 with the `Program` untouched |
| A bind writes the wrong object | Resolution is by full identifier including `#N` (`01-…` §6.6) and by `ClassName` ordinal on rebind (`03-…` §9); a mismatch is an error, never a guess |
| Repeated binds leak `ProgramState`s | Reuse the existing state when present (step 9), and on replacement pair `Abort` with `PrepareForRemoval` rather than dropping the pointer |
| A bind during a scene transition | `sre13_scene.c`'s `sre_SceneLoadingView_*` / `sre_Scene_FinishLoad` hooks expose the loading state; `Graphy.Bind` refuses while `g_sre_scene_shift_active`/`pending` is set (`sre13_lua.c` already checks these) and returns a retryable error |
| Determinism of the running game is broken by a half-applied bind | The `(String, Bytes)` swap is the only mutation and it is two pointer/size assignments under the guest's single-threaded execution; there is no observable intermediate state from the game's perspective |

**Hard rule:** live injection never writes to the on-disk asset. `05-…` §6 owns disk
writes and is a separate, explicit action ("Bake to asset"). Keeping live and persisted
paths separate is what makes a bad experiment survivable.

---

## 5. Host-side surface

```
Qt Graphy (Scene backend: "Compile & Bind")
   │  CompilePipeline: rbsrc -> Lua text -> 32-bit chunk
   ▼
GraphyInjectClient  (src/ruby/graph/graphy_inject_client.{h,cpp})
   │  TCP, newline-framed, one command per line, multi-line via Graphy.Push chunks
   │  connects to EnginePod::lua_console_port()  (SWORDFARE_TCP_PORT)
   ▼
SwordfareGUI::tcp_server_loop()  ->  m_tcp_cmd_queue
   │  existing FIFO, generation-guarded, single in-flight
   ▼
update_console_backend()  ->  guest mailbox (buf / pending / status / result)
   ▼
sre13_console_frame_tick()  ->  Graphy.* on the live lua_State
```

Client contract:

- One outstanding command at a time (the mailbox is single-slot; the TCP layer already
  enforces ownership).
- Every command carries a `seq`; the reply is matched by `seq`, never by order alone.
- `Graphy.Push` replies with `ok <seq> <bytes_received>`; `Graphy.Bind` replies with
  `ok <seq> <sha256>` or `err <seq> <code> <message>`.
- A 5 s timeout with automatic retry is safe because `Push` is idempotent by `seq`.

Confirmed by the existing code that this integrates cleanly: `ConsolePanel` already connects
this way, `SwordfareGUI` already handles a second-client disconnect message
("Another client connected. Disconnecting...") — so Graphy must be the *only* TCP client
while injecting, and the UI should warn if the user has the console tab connected (or
better: route Graphy's injections through the same `ConsolePanel` connection so there is
exactly one client, which is the recommended implementation).

---

## 6. Verification

| # | Test | Requires |
| --- | --- | --- |
| C1 | `Graphy.Reflect` returns a non-empty table list; the resulting `accessors_1.4.13.json` contains `PhysicsObject.SetEnabled` | running game |
| C2 | `Graphy.Sample("elder")` returns a `position()` consistent with `Position{X:919.5,Y:394.5}` + `Depth:-29.9` → **resolves `05-…` §9.1** | running game |
| C3 | `Graphy.TestLoad` accepts the `item1` chunk re-embedded byte-for-byte as hex, and rejects a deliberately corrupted chunk with a Lua error string | running game |
| C4 | `Graphy.Bind("item1", "ProgramComponent.Program", …)` with the shipping text → a shop item is still created, exactly once | running game |
| C5 | `Graphy.Bind` with a `Program.Wait`-using hook, then `Graphy.Rollback` → original behaviour restored, no leaked `ProgramState` (assert via `ProgramState::Abort` counters / object count) | running game |
| C6 | A hook bound to `while true do end` is terminated by the SRE timeout within the budget and the game survives | running game |
| C7 | Binding a hook while a scene transition is active returns the retryable error and does not mutate | running game |
| C8 | Offline: `Graphy` module compiles and links in the arm64 SRE build with `-nostdlib` (no libc additions beyond what `sre13_console.c` already uses) | CI |

`C3` is the single highest-value test in this document: it is the only thing that proves the
32-bit dumper (`05-…` §5.2) produces something the *actual* engine accepts.

---

## 7. Differences for SRE v1.4.12

`sre12` has no `sre13_console.c`; the console is serviced from
`sre12/sre_lua.c` (the "Legacy single-shot service point", `sre13_console_tick`) and from
`sre12/sre_frame_loop.c` (which `extern`s `g_lua_console_pending` directly). The mailbox
globals have the same names (`g_lua_console_buf`, `g_lua_console_result`,
`g_lua_console_pending`, `g_lua_console_status`), so the host side is ABI-compatible, but:

- There is no per-frame console coroutine in sre12, so `wait`/`waitFrames` in an injected
  script are unavailable and long scripts block the frame.
- There is no `recovery_push/pop` setjmp stack in sre12 (it uses `sre_lua_call_safe` +
  panic hooks instead), so the error-containment story is weaker.
- `ProgramState` offsets are the same (`sre13.h` explicitly notes 1.4.13's layout is
  "identical to the 1.4.12 layout used by libsre12").

**Recommendation: ship live injection for 1.4.13 only**, and have the Scene backend grey out
hot-patch when the loaded SRE is v1.4.12 while still offering offline injection (`05-…` §6).

---

## 8. Open questions

1. **Where does the runtime component hold its `Program` proto pointer?** Answered by the
   Phase-3 RE task (§3). Until then, `Bind` is limited to Phases 1–2.
2. **Does `ProgramState::LoadProgram` reset `sleepTime`/`isSuspended`?** If it does not, a
   rebound state may restart in a suspended condition. Mitigation: set `isSuspended(+0x50) =
   0` and `sleepTime(+0x54) = 0` explicitly after `LoadProgram`, matching what
   `sre_ProgramState_Execute` expects. Verify by experiment (`C5`).
3. **Is `Scene.Find(name)` stable for `#N` identifiers?** `MakeUniqueObjectIdentifier`
   suggests the scene stores the full identifier including the suffix, and
   `Group.ObjectIdentifier` references use it verbatim, so it probably is — but the fallback
   (iterate `GetAllObjects()`) should be implemented regardless, and `C2` should assert the
   suffix form resolves.
4. **Should Graphy own the TCP connection or share `ConsolePanel`'s?** Sharing is safer
   (§5) but couples the editor's console tab to the injector. Recommendation: add a
   dedicated `GraphyInjectClient` that *acquires* the console connection through
   `ConsolePanel` when the Raijin tab is connected, and opens its own only when it is not
   — with a visible "graphy owns the console socket" indicator.
