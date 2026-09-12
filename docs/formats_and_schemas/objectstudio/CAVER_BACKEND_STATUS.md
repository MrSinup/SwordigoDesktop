# Caver backend — what actually landed

> Status note for `CAVER_CORE_RUNTIME_PLAN.md`. Everything below is code on disk
> under `src/ruby/caver/`, built into `libcaver.so` and linked by `ruby_gg`.
> Last edited: 2026-09-12.

## 1. The naming question, answered

`Caver` was never a product name: it appears only in the mangled symbols of
`libswordigo.so` (`_ZN5Caver…`), i.e. it is the engine's internal namespace. Names
and short phrases are not protected by copyright (copyright covers expression,
not labels); the real risk vector would be trademark, which needs use in commerce
as a source identifier — and Touch Foo never shipped "Caver" as a brand. So:

* `caver::` as an internal namespace and "Caver — Swordigo's internal engine" in
  comments: fine.
* Do not use it as a product name, do not imply affiliation/endorsement, keep
  "Swordigo"/"Touch Foo" out of any marketing, and keep the attribution line.
* Separate and larger question: the repo ships game binaries and extracted
  assets. That is a distribution question, not a naming one. Not legal advice.

## 2. One engine runner, not two

caver does **not** run `libswordigo`. Ruby already does, through the existing
path:

```
ruby_gg ──EnginePod──▶ `swordfare --pod-preview` ──loader + Dynarmic──▶ libswordigo
   ▲            │ shm pod::FrameRing (frames)          │
   └──ILiveEngine◀ stdin control msgs + TCP Lua console┘
```

* `ruby/emulator/engine_pod.{h,cpp}` + `platform/pod_ipc.{h,cpp}` — the genuine
  engine, spawned as a child, frames over shared memory, input/pause/scene-shift
  over stdin, guest Lua console over TCP.
* `caver::ILiveEngine` (`ruby/caver/live_engine.h`) — the seam. caver asks a live
  engine to attach / load a scene / step / pause, and may ask for per-object
  readback.
* `ruby/emulator/live_engine_pod.{h,cpp}` — the adapter implementing that seam
  over `EnginePod`. It adds **no** loading path: `attach()` boots the pod and
  `load_scene()` goes through the engine's own Scene Shifter. Calls from the
  preview worker thread are marshalled to the GUI thread; state is cached in
  atomics.
* `caver::Engine` (`Backend::Native` | `Backend::LiveEngine`) runs one backend on
  its own thread at a fixed 60 Hz with catch-up capped at 250 ms and publishes
  immutable snapshots. `Backend::LiveEngine` is a thin forwarder to the injected
  `ILiveEngine`; a fake "libswordigo backend" stub no longer exists anywhere.

Honest limitation, stated in code: `PodLiveEngine::read_state()` returns `false`
today and records why — the pod's control channel carries pixels, not object
transforms. Per-object readback will come from the guest Lua console
(`EnginePod::lua_console_port()`) or SRE hooks, never from a second emulator.

## 3. What is ported

| File | Contents |
|---|---|
| `component.h` / `component_registry.cpp` | All 80 component classes with their verbatim payload tags and ClassName aliases. The `Stage` bitmap is **derived** from the code that implements each stage (`behaviour_has_tick`, `visual_has_item`), so the coverage report can never overclaim. |
| `runtime.h` / `runtime.cpp` | `RuntimeScene`: unique identifiers, schema-decoded fields, resolved `*Id` edges with dangling detection, embedded programs (`String`/`Bytes` split, keep-active triage), the SCL-spawn path, hero lookup, a ground-query seam to the `av::` collision layer, scripted-event log, and the two-pass update: **SCENE_OBJECT_UPDATE → PROGRAM_UPDATE → events**. |
| `behaviour.h` / `behaviour.cpp` | The recovered per-component behaviours: physics (gravity vector + magnitude, ground/air deceleration, max speed, elasticity, grounding), Entity/EntityController (action state machine, facing, roam/patrol via `RoamAreaId`, targeting), the whole MonsterController family (shared recovered core + per-class traits: walking, charging, snapping, leaping, skelly, static, shooting, bat, bouncing, generic), Attack damage windows, Health/Damage, CollisionShape enable/friction/unsafe-ground, PressureTrigger/Touchable/Door/Elevator/Bush/Breakable/Collectable/ItemDrop, MagicBolt/Bomb/Explosion/Hookshot cast, ProjectileController, Skill/SwingableWeapon, ModelTransformController, OrbitController, Light, SimpleGlow pulse, particle emitters, WaterMesh, MonsterDeath. |
| `program_host.h` / `program_host.cpp` | The **real** Lua 5.1 VM (the host runtime filerift already embeds). Loads `Program.Bytes` via `luaL_loadbuffer` — the exact call `Caver::Program::LoadIntoState` makes — with `String` as fallback only. Keep-active programs run as coroutines resumed on the scene clock; `Program.Wait` yields; one-shot handlers run only when fired. Binds the API the corpus actually uses and records usage counts per function. |
| `visual.h` / `visual.cpp` | Components → draw items (Model/Sprite/Particle/Light/Glow/Shadow/Ground/Water/Background/Shape/Trail/Overlay) with live clock values. **Data only:** no GL, no Qt — the caller decides whether the viewport, an offline capture, or a studio diorama consumes it. The 3D viewport is untouched. |
| `library_manager.{h,cpp}` | The SCL runtime: name-keyed `ObjectLibrary` registry, bounded search paths, content-hash gated re-parse, import DAG, `refresh_changed()` for hot swap. |
| `engine.{h,cpp}` | The worker thread, the two backends, published snapshots. |

`RuntimeScene::spawn()` is `SceneObject::InitWithTemplate`: it resolves the
archetype through `LibraryManager::find_template`, instantiates its components,
resolves references, initialises behaviour, and starts its keep-active programs —
which is why `Scene.CreateObject`, item drops, death drops and spell spawns all
work through one path.

## 4. Why the Lua VM is the API layer, not "AI"

The decompilation and the corpus agree: the C++ controllers are **primitives**,
and the authored per-archetype AI is Lua stored in the data. So the port is
complete only when both halves are in place — which is why `behaviour.cpp` and
`program_host.cpp` landed together. What used to be a hardcoded `AiKind` enum in
`scene_player.cpp` is now: real scripts, running in a real Lua state, calling
real recovered primitives.

## 5. Still open (deliberately)

1. **Emitter cadence for generic `ParticleEmitter`** is `Type`-derived in the
   engine; a neutral 20 Hz default is used and marked as unverified in code.
   `FireEmitter` uses its authored `ParticleInterval`/`ParticleMaxAge`.
2. **`KeyframeAnimation` duration** is not in the component payload (it comes
   from the ANI clip), so `TimeToCompletion`/`TimeToFrame` are honest zeros until
   the ANI loader is wired in.
3. **Door/elevator travel rates** are placeholders (marked in code) pending the
   animation-frame-to-time mapping.
4. **Snapping/charging coefficient tables** are trait defaults; the specialised
   monsters are staged through the coverage matrix rather than guessed.
5. **Per-object readback from the live engine** (see §2) — the one thing the
   recovered/reference diff needs.
6. **Hero archetype instantiation** from the shipped assets, and rbsrc — the
   ObjectStudio milestones, unchanged.

## 6. Build

```
cmake --build build-cmake --target caver     # libcaver.so
cmake --build build-cmake --target ruby_gg   # links libcaver + the pod adapter
```

`caver` links `filerift` for the host Lua 5.1 runtime. If a 32-bit host ever
emits chunks for the arm64 guest, revisit the bytecode assumption
(`sizeof(size_t)==8`, little-endian, `lua_Number=double`) recorded in
`OBJECTSTUDIO_AND_RBSRC.md`.
