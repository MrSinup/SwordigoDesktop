# Throndigo — Resource Formats & FileRift Decoding

The desktop game stores its data as protobuf files (`.scene`, `.scl`,
`.gdata`, `.gstate`, `.gplayer`, `.gopt`, `.sounds`, `.scmap`, ...).
FileRift (via the native `bin/ruby_cli` wrapper) decodes them into a
readable `markup` format. These notes document the schemas and the
workflow used to produce the `decoded/` files in this folder (also the
basis for `00_mod_overview.md`, `01_3d_movement.md`, and
`02_server_host_api.md`).

## Workflow

```
bin/ruby_cli  (native FileRift v5.8.6 compute)
   reads  de_in/  (raw binary resources)
   writes de_out/ (decoded markup, same filenames)

usage: ruby_cli -d
```

1. Copy a `.scl` / `.scene` resource into `de_in/`.
2. Run `/home/quantumcreeper/SwordigoDesktop/bin/ruby_cli -d`.
3. Read/parse the resulting markup in `de_out/`.

Decoded outputs from the mod were archived under `docs/throndigo/decoded/`
so they survive `/tmp` cleanup:

```
decoded/
  animcheck.scl    # susianimcheck bone-linker (add-on lib)
  async.scl        # async() thread helper
  handle.scl       # 'dummy' remote-player template
  hiro.scl         # hero template: 3D movement + MP client
  menu.scene       # menu tweak (button text color)
  obj.scl          # 'background' follow-quad
  oc.scl           # OverlayControls button library
```

## The protobuf schemas

Located at `~/.local/share/swordigo-desktop/ios-assets/Swordigo.app/`:

| File | Size | Contents |
|------|------|----------|
| `Common.proto` | 159 L | scalars/fields used everywhere (Vector-ish, colors, `Bytes`, `String`, identifiers like `Identifier`, `String`, `ByName`) |
| `Scene.proto` | 1048 L | the scene/template container: `Scene`, `Object`, `Component`, `Template{...}`, `Template Object{...}`, `OnLoad{ String }`, `Name`, `Position{ X Y }`, `Depth`, `Rotation`, `Scaling`, `LocalAabb{ X Y Width Height }`, `ImportedLibrary{ 'name' }`, `ModelComponent{...}`, `AnimationControllerComponent`, ... |
| `GameData.proto` | 118 L | save-game data containers (`.gdata`) |
| `GameState.proto` | 106 L | state containers (`.gstate`) |

Swordigo uses protobuf **LITE_RUNTIME** and each file is prefixed with a
`Bytes`/header block; FileRift recovers the real fields. `package
Caver.Proto`.

## Characteristic shapes you meet in the mod's decodes

A `.scl` file is a set of `Template` blocks, each wrapping one
`Object` with components keyed to classes (e.g. `CharController`,
`CollisionShape`, `KeyframeAnimation`, `HeroEntity`, `Skill`, `Swing`,
`ParticleEmitter`, `TransformController`) plus an optional `OnLoad{ String }
` whose content is **Lua source** embedded as a string (FileRift also
keeps the compiled `\x1bLuaQ...` bytecode in a `Bytes` field, so scripts
are patchable either as source or as bytecode).

`ImportedLibrary : 'name'` lines at the end of `hiro.scl` list the other
`.scl` libraries the hero script pulls in:

```lua
ImportedLibrary : 'magic'
ImportedLibrary : 'oc'
ImportedLibrary : 'obj'
ImportedLibrary : 'async'
ImportedLibrary : 'handle'
```

which mirrors the engine loading `oc.scl`, `obj.scl`, `async.scl`,
`handle.scl` (and `animcheck.scl` via `SetUpAnimCheck()`) alongside the
hero.

## Notable discoveries from the decodes

- `hiro.scl` grew 8,557 → 22,323 bytes: the OnLoad Lua now implements
  the 3D camera/d-pad (`01_3d_movement.md`) and the UDP multiplayer
  client (`02_server_host_api.md`).
- `menu.scene` only differs from vanilla in the "More\nOptions" button
  text color (`GUIButton.Find` + `setTextColor`), confirming the mod
  leaves menus alone.
- `handle.scl`'s `dummy` uses `WeaponTemplateName 'dummy_thorn'` — a
  custom weapon POD the mod adds (see `00_mod_overview.md` resource
  table).
- `animcheck.scl` links one `Transform` object per hero bone
  (`Bone`, `Bone01`, `Bone04..Bone22`) via `ObjectLinkController
  .LinkToBone`, then `susianimcheckmain` polls their positions to print
  the currently-played animation index — a debug/animation-sync aid.

## Re-decoding tip

The decode tool uses the current working directory for `de_in`/`de_out`.
Run it from the folder you want:

```sh
cd /tmp/opencode/thornfield          # or anywhere
cp res.hiro.scl de_in/
bin/ruby_cli -d                      # writes de_out/hiro.scl markup
```