# Throndigo / ThornField — Mod Overview

Research notes on the Swordigo desktop mod installed under the
`Throndigo` instance. Focus: how the mod implements 3D movement and a
multiplayer "server/host" client using the SwMini mod loader and its
bundled LuaSocket library.

## What the mod is

A Lua-level mod distributed as an APK (`ThornField.apk`) whose resources
were re-packed into the desktop instance `inst-Throndigo`. It loads
through SwMini (`libmini.so`), the third-party "Swordigo Mini" mod
loader by ItsJustSomeDude, then re-points the game's Lua engine
(`libswordigo.so`) at modified `.scl` / `.scene` resources.

## Where things live

| Path | Purpose |
|------|---------|
| `~/.local/share/swordigo-desktop/inst-Throndigo/` | mod assets instance |
| `.../inst-Throndigo/resources/` | the mod's resource pack (modified scenes/scripts) |
| `.../inst-Throndigo/mini.toml`, `achievements.toml`, `cstrings.toml` | SwMini configuration |
| `.../inst-Throndigo/tests/*.lua` | SwMini self-tests (assets, files, lfs, require) |
| `.../engine/custom-Throndigo/arm64-v8a/instance.ini` | instance wiring (deps `libsre.so`) |
| `.../ios-assets/Swordigo.app/*.proto` | protobuf schemas for `.scene`/`.scl`/`.gdata`/etc. |
| `ThornField.apk` (repo root) | original APK, mod's native libs |
| `MultiSW-Server-master.zip` (repo root) | TypeScript UDP room server (reference) |
| `bin/ruby_cli` | FileRift binary decoder used to hex-decode resources |

## Binary identity: the game itself is vanilla

`libswordigo.so` shipped in `ThornField.apk` is byte-identical to the
stock v1.4.12 binary — same BuildID, same size, only ~251 bytes of
ELF-header noise differ. The engine is **not** modified. The mod is
entirely:

1. modified resource files (`hiro.scl`, `menu.scene`, `*.POD`, scenes),
2. the SwMini loader (`libmini.so`), and
3. `libGlossHook.so` (gloss / UI hook, clang-18 build).

## What SwMini provides (does the game's Lua host)

`libmini.so` exports its own Lua 5.1 interpreter and modules:

- `luaopen_socket_core` — **LuaSocket 3.1.0** (`broken_socket` module,
  the `socket` core renamed/broken; here it is the networking base for
  the multiplayer client)
- `luaopen_lfs` — LuaFileSystem (`lfs`), used for file/`os` helpers
- `luaopen_io`, `luaopen_os`, `luaopen_math`, `luaopen_table`,
  `luaopen_package`, `luaopen_string`, `luaopen_debug`, `luaopen_base`
- `initP_lua_loadfile`, `initP_lua_panic`, `ffi_lua_call`

So the Lua runtime under which the mod's `.scl` scripts run has a full
5.1 stdlib plus UDP sockets and LFS. `broken_socket` is the LuaSocket
UDP userdata (`udp()`, `setpeername`, `settimeout`, `send`, `receive`,
`close`).

## The instance is SRE-hosted

`engine/custom-Throndigo/arm64-v8a/instance.ini` declares
`dependencies = libsre.so`, i.e. the game runs under the Swordigo
Runtime Engine (`libsre.so`). SRE already implements the SwMini "Mini"
Lua API surface **natively** (see `src/sre/sre_mini_api.c`,
`src/sre/sre_lua_libs.c`) rather than emulating `libmini.so`:

- `Mini.*`, `LNI`, `Components`, `Entity`, `Skeleton`,
  `CharAnimController`, `Bauble`, `EdgeTest`
- `ButtonController`/`Button`/`OverlayController` (RLSW compat),
  `Keyboard`, `CameraController`, `DB`, `Character`, `hero`/`Hero`,
  `Camera`, `Game`, `Health`, `fs`/`lfs`
- `broken_socket` (the LuaSocket `socket.core` closure), plus
  `socket.core` / `lfs` preloaded into `package.preload`

Because of that native surface, the mod's Lua scripts resolve the
engine-side objects (`OverlayController.NewButton`, `CameraController`,
`ModelTransformController`, `GameController`) with no guest-side
`libmini.so` at all.

## Mod resources: what actually changed

Diffed against `ios-assets/Swordigo.app/` resource baseline:

| File | Change |
|------|--------|
| `hiro.scl` | 8,557 → 22,323 B: the hero template — adds 3D movement + multiplayer client Lua |
| `hiro.POD` + all `hiro_*.POD` | larger: modified hero model |
| `oc.scl` | **added** — "Overlay Controls" button/menu library |
| `obj.scl` | **added** — `background` object (tracks hero) |
| `async.scl` | **added** — tiny `async()` thread helper |
| `handle.scl` | **added** — `dummy` remote-player template |
| `animcheck.scl` | **added** — per-bone animation checker |
| `background.POD` | **added** |
| `menu.scene` | tiny tweak: "More\nOptions" button text color |
| `town_herohouse.scene`, `town_woods1.scene` | → 2,346,647 B each (heavily rebuilt) |
| fire/forest/florennum/grove/plains/* scenes | rebuilt |
| `credits.scene`, `traps.scl` | changed |

Decoded copies of the added/modified `.scl` files live in `decoded/`.

## References

- `01_3d_movement.md` — how the on-screen controls + camera do 3D
- `02_server_host_api.md` — the UDP multiplayer client and wire protocol
- `03_resources_protobuf.md` — FileRift decoding and `.proto` schemas
- `04_libsre_support.md` — what SRE must provide for the mod to run