# Throndigo — LIVE API AUDIT vs SRE (verified at runtime, 2026-08-17)

Method: game running under SRE (scene 23d), TCP Lua console (port 12345, `openport`),
`type(X)` / `type(X and X.fn)` probes on the live game state + engine binary string
analysis + vanilla-resource cross-check. This CORRECTS the optimistic claims in
`04_libsre_support.md` ("100% coverage") — several SwMini surfaces are missing.

## ✅ PRESENT (verified `function`/`table` in live game state)

| API | Mod usage |
|-----|-----------|
| `Game.ShowNotification / CurrentLevelName / FadeIn / FadeOut / SetCinematicMode / DefaultMusicName / TitleForItem` | hiro.scl |
| `Camera.JumpToFocus / FocusAtPoint` | hiro.scl (8×/4×) |
| `Scene.CreateObject / Find` | all .scl |
| `Vector3.New` | hiro.scl (16×) |
| `Program.Wait` | hiro/oc/async (12×/6×) |
| `ModelTransformController.SetRotationAngle` | hiro.scl (8×) |
| `Entity.GetFacingDirection / SetFacingDirection` | hiro.scl |
| `Character.AddItem` | hiro.scl |
| `CharController.SetWeaponsHidden / DropObject / PickupObject` | hiro.scl |
| `KeyframeAnimation.SetRunning / TimeToFrame / SetCurrentTime` | hiro/handle |
| `CollisionShape.DisableAll` | hiro.scl |
| `SoundLibrary.PlayEffect` | hiro.scl |
| `PhysicsObject.SetEnabled` | hiro.scl |
| `MusicPlayer.PlayMusic / FadeOut` | hiro.scl |
| `Health.SetImmunityTime` | hiro.scl |
| `ObjectLinkController.LinkToBone` | animcheck.scl (3×) |
| `broken_socket.udp` | hiro.scl (2×) |
| `OverlayController.NewButton / RemoveAll` | hiro/oc |
| `Mini.*` (HostZAxis, Malloc, MemoryAddress, ffi.call_sig) | extras addon |
| `os.time`, `string` lib | hiro.scl |

## ❌ MISSING (would crash the mod with "attempt to index global ... nil")

1. **`GameController` table** — `GameControlButtonDown(1/2)` / `GameControlButtonUp(1/2)`
   — hiro.scl calls them **~20 times** (every onTouch/onHeld/onHeldFinish). The engine
   binary only has the C++ methods (`Caver::GameSceneController::GameControlButtonDown`)
   — the Lua table is a **SwMini** surface SRE must provide. → **CRITICAL, must implement.**
2. **`math` lib is entirely absent** — `math.floor` (oc.scl ×4), `math.clamp` (hiro.scl).
   The engine's Lua opens base/os/string but NOT math/table. `math.clamp` doesn't exist
   in Lua 5.1 either — implement both. → **CRITICAL.**
3. **`table` lib is entirely absent** — `table.insert` (hiro/oc/animcheck). → **CRITICAL.**
4. **`CameraController.SetOffset` (12×), `GetOffset` (2×), `DisableOptimizations` (1×)** —
   SRE's CameraController table only has `SetPositionOffset`/`GetPositionOffset`/
   `SetUpVector`/`GetUpVector`/`SetPerspectiveProjection`. The mod's FIRST camera call
   is `SetOffset(500,100,0)`. → **CRITICAL, add aliases.**
5. **`Mega` table / `Mega.FindAll`** — hiro.scl (1×). SwMini surface, engine has nothing.
6. **`Program.NewThread` / `NewTimer`** — nil. Needed by the mod's coroutines (async.scl,
   oc.scl) AND by SRE's own z-walk shim (`Program.NewThread('sre_zwalk_loop')`).
7. **`AnimationController` table / `BlendToAnimation`** — hiro.scl (~10 calls, several
   commented out but active ones remain). Engine binary only has the C++ class.
8. **`EntityController` / `StartSwing`** — handle.scl. Engine has a standalone
   "EntityController" registration string but it is nil in the live scene state.
9. **`CollectableItem` / `ItemName`, `RequiresPickup`** — hiro.scl (6×/2×). Same as above:
   engine registration string exists, nil in live state.
10. **`GUIButton.Find`** — only in commented-out code. Skip.

## How the gaps were verified (evidence)

- Live probes via TCP console: `type(GameController)` → `nil`, `type(math)` → `nil`,
  `type(table)` → `nil`, `type(CameraController.SetOffset)` → `nil`,
  `type(Program.NewThread)` → `nil`, `type(Mega)` → `nil`,
  `type(CollectableItem)` → `nil`, `type(AnimationController)` → `nil`,
  `type(EntityController)` → `nil`.
- Engine binary (`v1.4.12/arm64-v8a/libswordigo.so`) string scan: `GameControlButtonDown`
  appears ONLY as mangled C++ (`_ZN5Caver19GameSceneController21GameControlButtonDown...`),
  never as a Lua registration; `AnimationController` appears 96× as C++ mangled names only;
  `GUIButton` only as `Caver::GUIButton` C++ class. `CollectableItem` and `EntityController`
  DO exist as standalone strings but are nil in the live scene state → SRE should force-register them.
- Vanilla `.scl` cross-check: vanilla scripts use `GameController` (13×), `AnimationController`
  (374×), `EntityController` (1091×), `CollectableItem` (48×) — so these are real vanilla
  APIs the engine is *supposed* to expose; on desktop SRE they're missing from the live state.
- Vanilla `.scl` use ZERO `math.*` / `table.*` — the engine genuinely ships without those
  libs; only the mod needs them.

## Fix plan (priority order)

1. Inject `math` (floor, ceil, clamp, random, min, max, abs, …) into the game Lua state
   (SRE `sre_mini_ensure_injected` / RegisterProgramLibrary hook).
2. Inject `table` (insert, remove, getn, …).
3. Add `GameController` table: `GameControlButtonDown(1|2)` / `GameControlButtonUp(1|2)`
   wired to `Caver::GameSceneController::GameControlButtonDown/Up` (resolve via sre_resolve_address).
4. `CameraController.SetOffset/GetOffset/DisableOptimizations` aliases.
5. `Program.NewThread/NewTimer` (engine ProgramState coroutine API — check how Program.Wait
   is provided and mirror it).
6. `Mega.FindAll` (Scene.FindAll wrapper over engine Scene).
7. `CollectableItem`, `AnimationController`, `EntityController` shims (verify engine entry points).
