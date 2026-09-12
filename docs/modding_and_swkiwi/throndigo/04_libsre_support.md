# Throndigo — what libsre (SRE) must provide for the mod

The mod is implemented entirely in Lua resources plus SwMini's
`libmini.so`. On the desktop SRE build, the game runs under `libsre.so`
(the Swordigo Runtime Engine). SRE already ships native implementations
of the SwMini/Mini Lua API surface (`src/sre/sre_mini_api.c`,
`src/sre/sre_lua_libs.c`), so **no guest-side `libmini.so` is loaded** —
SRE answers the same calls `libmini.so` would.

This note maps every engine call the Throndigo mod makes onto the SRE
provision, and lists what still needs host-level help.

## Lua API the mod's `hiro.scl` calls

| Call | Provided by SRE |
|------|-----------------|
| `OverlayController.RemoveAll` / `NewButton` / `NewOverlay` / `HideAll` | Yes — RLSW compat layer aliasing `ButtonController`/`MiniButton`/`MiniOverlay` (sre_mini_api.c ~6390) |
| `OverlayController.NewButton` returns object with `setAlwaysActive`, `setClickable`, `setBackgroundResource`, `setTextColor`, `setText`, `setTextScale`, `setBackgroundAlpha`, `setPosition`, `makeMovable`, `isPressed`, `isDeleted`, `delete` | Yes (`MiniButton`) |
| `broken_socket.udp()`, `udp:setpeername`, `udp:settimeout`, `udp:send`, `udp:receive`, `udp:close` | Yes — `broken_socket` registered as `luaopen_socket_core` closure (sre_mini_api.c ~4945) |
| `socket.core` / `mime.core` / `lfs` / `toml` in `package.preload` | Yes (sre_mini_api.c ~6236) |
| `CameraController.SetOffset` / `GetOffset` / `DisableOptimizations` / `SetUpVector` / `GetUpVector` | Yes — SwKiwi alias table (~2254, registered ~4696) |
| `Camera.JumpToFocus` / `Camera.FocusAtPoint` | Engine-native (game registers `Camera`); SRE keeps engine table |
| `GameController.GameControlButtonDown(1/2)` / `...Up(1/2)` | Yes — engine `GameController` table, plus SRE z-walk shim uses the same calls |
| `Game.ShowNotification`, `Game.CurrentLevelName`, `Game.FadeIn/FadeOut`, `Game.SetCinematicMode`, `Game.DefaultMusicName`, `Game.TitleForItem` | Injected into engine `Game` table (~4958) |
| `hero:setVelocity/position` , `hero:velocity()` + `:x()/:y()`, `hero.curranim`, `hero:setAlwaysActive` | Engine object userdata via SRE host ABI |
| `Vector3.New` | Engine `Vector3` constructor (SRE forwards) |
| `Mega.FindAll()` | Engine `Mega` table (SRE forwards) |
| `ModelTransformController.SetRotationAngle(hero, θ)` | Yes — native impl used by z-walk shim |
| `Entity.GetFacingDirection` / `Entity.SetFacingDirection` | Engine |
| `Scene.CreateObject` / `Scene.Find` | Engine |
| `os.time()`, `tostring`, table lib | stdlib (SRE host Lua) |
| `ObjectLinkController.LinkToBone` | Engine (animcheck.scl) |
| `Program.Wait` / `Program.NewThread` | Engine coroutine system |

## Z-walk host bridge (Mini.HostZAxis)

SRE doesn't just provide the API — it ships a **keyboard-driven port of
the mod's front/back Z-walk** (sre_mini_api.c ~6130): a per-frame
`Program.NewThread('sre_zwalk_loop')` coroutine that reads a host
keyboard axis via `Mini.HostZAxis()` (registered ~4074) and replays the
exact `hiro.scl` interaction:

- `front` → `GameControlButtonDown(2)`, `SetRotationAngle(90)`,
  `setVelocity(-10, vy, 0)`, `setPosition(+ (0,0,-10))`, `JumpToFocus`,
  `SetFacingDirection(hero, 0)`
- `back`  → `GameControlButtonDown(1)`, mirror image (`+10`, `(0,0,10)`)
- `...Up()` releases on key-up, and liveness is gated on
  `Game.CurrentLevelName` not being `menu`/`hero`.

So a desktop user can hold W/S for depth movement without the on-screen
d-pad. The on-screen `oc.menu` arrows still work via the same calls.

## Networking: the guest socket path

`broken_socket` registers the real LuaSocket `socket.core` into the SRE
Lua state. LuaSocket's UDP methods reach the **host** network stack
(`sendto`/`recvfrom`) through the SRE host ABI. Requirements:

- UDP connect + send/receive must not be sandboxed into the emulated
  socket layer (the guest game doesn't use sockets itself; SRE owns the
  stdlib callbacks).
- `settimeout(0)` non-blocking semantics must return `nil, "timeout"`
  instead of blocking the Lua thread.
- Target `192.168.0.100:13337` must be reachable from the host.

## What SRE may still need for full Throndigo parity

1. **`dummy` remote-player visuals**: `handle.scl` relies on
   `WeaponTemplateName 'dummy_thorn'` — the mod must ship that POD (it
   does via resources) so the SRE VFS resolves it like any other asset.
2. **`animcheck.scl` bone-linking** requires `ObjectLinkController
   .LinkToBone` to operate on per-bone `Transform` objects — engine-side,
   should work; verify with a live remote player.
3. **The full `oc.menu` button API**: `onTouch/onHeld/onHeldFinish` and
   thread-based `isPressed` polling need SRE's `Program.Wait(0.05)` to
   advance per-frame from within button threads.
4. **UDP broadcast servers**: nothing in-box serves `13337`; for testing,
   a small host UDP relay that mirrors dash-frames back as `;`-records
   is needed (protocol in `02_server_host_api.md`).
5. **Camera sanity**: with `SetOffset(±500,100,0)` the engine's
   background quad (`obj.scl`) must follow the hero — SRE must keep the
   simulated camera focus correct so the far offset never shows void.

## Bottom line

SRE already provides 100% of the mod's Lua API surface natively
(OverlayController, CameraController, GameController input, ModelTransform
Controller rotation, Vector3, Mega, broken_socket UDP, os/fs, z-walk).
Remaining risk is host plumbing: UDP passthrough reachability, asset
VFS coverage for `dummy_thorn`, and per-frame poll threads — all
engine/host concerns, not new Lua API.