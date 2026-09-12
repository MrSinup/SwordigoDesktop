# Scene Component Library (SCL) Script Engine Research Notes

> Analysis of SCL script loading, `Caver::Program` / `Caver::ProgramState` coroutine execution, and protobuf message parsing.

---

## 1. Engine Scripting Pipeline Architecture

### 1.1 SCL Representation & Protobuf Schema (`Caver::Program`)
In the native C++ engine, scripts are represented by `Caver::Program` mapping to a Protobuf schema:
- **Field 1 (`String`)**: Raw Lua source code (text format, authoring only).
- **Field 2 (`Bytes`)**: Serialized binary Lua 5.1 bytecode — what the runtime executes.
- **Field 3 (`Name`)**: Optional script identifier.

`Program::LoadIntoState` checks **field 2 only**: it returns 0 when the `Bytes`
field is absent and otherwise feeds it straight to
`luaL_loadbuffer(L, code, len, "program")`. The runtime never compiles field 1.

> **Authoring rule:** an editor that rewrites only the source (field 1) has no
> runtime effect — the game keeps executing whatever bytecode was previously
> embedded. Every source edit must also regenerate field 2 (`luaL_loadbuffer` +
> `lua_dump`). `av::scene_set_program_source()` does this via the host Lua 5.1
> runtime in the `filerift` component; on a compile error it drops the stale
> bytecode rather than leaving an old chunk in place.

### 1.2 Hierarchical Script Scheduler (`Caver::ProgramState`)
- **Root State**: Master `lua_State*` initialized during `Scene` boot with built-in libraries (`Scene`, `Character`, `Item`, `System`, `Sound`, `Music`).
- **Child Threads**: Spawns coroutine threads (`lua_newthread(parent_L)`) anchored to `LUA_REGISTRYINDEX`.
- **Coroutine Update Loop**: `ProgramState::Update(float dt)` updates timers (`Wait(sec)`), decrements `dt * speedMultiplier`, and calls `lua_resume(L, 0)` when timers reach 0.

### 1.3 Event Callbacks
- **Action Triggers (`EntityActionComponent::Perform`)**: Triggered when interacting with objects. Spawns a child `ProgramState` and runs `lua_pcall` with 1 argument (`self`).
- **Collision Triggers (`CollisionShapeComponent::Perform`)**: Physics collision callback running `onCollision(self, other, flag)` (`lua_pcall` with 3 arguments).

---

## 2. Generic Schema-Independent SCL Extractor
To bypass tag-bound parsing limitations when reading raw `.scl` protobuf files, SRE implements a wire-2 payload scanner:
1. Scan protobuf buffer for wire-2 length-delimited payloads (`wire == 2`).
2. If payload starts with `\033Lua` (`0x1B 0x4C 0x75 0x61`), extract as binary bytecode.
3. If payload is ASCII text matching Lua keywords (`function`, `local`), compile directly as Lua source text.

---

## 3. Authored Model Transform (`ModelTransformControllerComponent`)

Recovered from ARM32 (`libswordigo_ida32.c`). Layout/behaviour are strict from the binary; programming synthesized where the bytes do not specify it.

### 3.1 Component Data (member offsets in `ModelTransformControllerComponent`)
| Member | Offset | Encoding |
|---|---|---|
| `ModelId` | +8 | u64 |
| `Origin` | +18 | `Vector3` protobuf object (field in message payload) |
| `RotationAxis` | +26 | `Vector3` (normalized on set) |
| `RotationAngle` | +37 | `float` (radians) |
| `RotationSpeed` | +45 | `float` (radians/sec) |

Protobuf embedding: paylarge embed field is `500`; payload is 104 bytes with fields `1=ModelId` (string), `2=Origin`, `3=RotationAxis`, `4=RotationAngle`, `5=RotationSpeed`.

### 3.2 Update Semantics
- `Update(dt)`: `rotationAngle += rotationSpeed * dt` (radians).
- Model matrix is `T(origin) · R(axis, angle) · T(−origin)` (confirmed via `PostTranslate` right-multiply and `C_Matrix4Mul` argument order `result = a·b`).
- `RotationAxis` general-axis → standard Rodrigues matrix (matches ARM32 output exactly).
- `hasRotation()` is true when `|rotationAngle|` or `|rotationSpeed|` exceeds `1e-4`.

### 3.3 Boundary (NOT reconstructed)
- Scene graph/`Transform` parenting, baked model animation, and Lua-`ModelTransformController` event callbacks are outside the verified slice.

---

## 4. Portal State: `PortalComponent`

Recovered from ARM32. Layout/behaviour:

### 4.1 State semantics (verified)
- `Activate()`: sets `active = true`; when `tapToEnter == false`, also sets `entered = true` immediately.
- `Enter()`: sets `entered = true` unconditionally.
- `Deactivate()`: clears both `active` and `entered`.
- Metadata: `DestinationSceneName` (offset 10, string), `SpawnPointName` (offset 18), `TapToEnter` (offset 24, bool), `TriggerShapeId` (offset 32).

### 4.2 Boundary (NOT reconstructed)
- Actual scene-to-scene transition / linked `SceneManager` load, `TriggerShape` collision hit-testing, and the Lua `onPortalEnter` callback are intentionally not fabricated; `Scene::setPortal` only binds the component state.

---

## 5. Reconstruction policy
- Implement only behaviour recoverable from ARM32. Where the binary gives a member/layout/rule it is reproduced exactly; anything the binary does not pin down (collision, scene switch, Lua program flow, enemy AI, cutscenes) is left as an explicit boundary rather than guessed.
