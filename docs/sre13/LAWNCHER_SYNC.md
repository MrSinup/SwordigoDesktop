# Lawncher ↔ SRE13 Sync

Reference tree: `lawncher_reference_for_sre13/app/src/main/cpp/`
Target tree:     `src/sre/sre13/`

This doc records the one-time port of the lawncher's post-fork commits into
SRE13 (completed pass), the analysis of why the lawncher's `api/` layer is
**not** copied verbatim (the desktop host supersedes it natively), and the
repeatable procedure for syncing future lawncher commits.

---

## 1. Are they the same codebase? — yes (fork of a fork)

Both trees descend from the same recovery effort and share the same
foundation macros:

| System | Lawncher | SRE13 | Compatible |
|---|---|---|---|
| Hook installer | `HOOK_SYMBOL(name, "mangled", ret, args)` | same macro, `srehost_*` backend | yes |
| Dynamic symbol resolve | `G_DL_SYMBOL` / `DL_SYMBOL` | `DL_SYMBOL` / `DL_SYMBOL_DECL` / `G_DL_SYMBOL` | yes |
| Offset helpers | `archSplit(a32, a64)` + `$(type, base, o32, o64)` | identical | yes |
| Logging | `LOGD/LOGI/LOGE` | identical (also `LOGW`) | yes |
| Lua | engine `lua_State` via `lua.h` | vendored Lua 5.1 compiled into `libsre13.so` | yes |

Shared `caver/` headers (`Camera`, `GameSceneController`, `GameViewController`,
`SceneObject`, `PlayerProfile`, …) are byte-compatible in layout; several are
**newer in sre13** (sre13 has recovered named fields where the lawncher still
has `// TODO: needs to be remapped!!` opaque types — e.g. `Scene.h`).

Verified drift (per shared file): sre13 is ahead in most files; the lawncher's
genuinely-new content is the list in §2. The two trees' hook *backends* differ
by platform only (Gloss/JNI on Android vs `srehost_install_hook` SVC ABI on
desktop) — every hook/struct/lua-surface ported here is platform-independent.

## 2. Ported into SRE13 (this pass)

### 2.1 New caver structs (copied, include paths re-rooted)

| File | What it is |
|---|---|
| `caver/PhysicsObjectState.h` | Inline physics state (position/velocity), embedded by `EntityComponent` |
| `caver/Component/CharControllerComponent.h` | Hero/monster controller: `facingDirection`, `moveDirection`, `jumpState`, `airJumpCount`, `jumpSpeed`, `runSpeed`, `fastRunSpeed`, `isCasting`, `skillComponent` (sizeof 0x1d8/0x300) |
| `caver/Component/EntityComponent.h` | Base living-thing component, embeds `PhysicsObjectState` inline (sizeof 0xdc/0x128) |

### 2.2 New hooks

| File | Symbol hooked | Notes |
|---|---|---|
| `hooks/PhysicsObjectState.c` | `Caver::PhysicsObjectState::Update(float)` | Passthrough; lawncher's airtime telemetry is commented out upstream too |
| `hooks/Component/CharControllerComponent.c` | `Caver::CharControllerComponent::Update(float)` | Caches `g_cc` per frame; exports `charControllerComponent_get()` / `charControllerComponent_from_L()` |
| `hooks/Component/EntityComponent.c` | `Caver::EntityComponent::Update(float)` | Exports `entityComponent_get(SceneObject*)` |

### 2.3 New core modules

| File | What it is |
|---|---|
| `core/toml.{h,c}` | Minimal TOML config parser (portable; `Map_SetOwned`-free). `rewind()` replaced with `fseek(SEEK_SET)` — the freestanding `src/sre/base/include/stdio.h` shim does not declare `rewind` |
| `hooks/ProgramState.c` | Portable half of lawncher `core/32patch.c`: `ps_setTimeScaleEnabled` / `ps_isTimeScaleEnabled` / `ps_remove` registry + `ProgramState::Update` Lua-error logging + `~ProgramState` cleanup. pthread mutex replaced with a GCC-`__atomic` spinlock (no `pthread.h` in the aarch64 sysroot) |

### 2.4 PlayerProfile delta

`hooks/PlayerProfile.c` gained the lawncher's `LoadFromProtobufMessage` hook
(`Caver::PlayerProfile::LoadFromProtobufMessage(Caver::Proto::PlayerProfile
const&, bool)` — verified at `0x47cba8` in the 1.4.13 binary). The lawncher's
second `Load` hook was **not** ported: it hooks the *same* symbol as
`LoadGameState` (a duplicate-hook artifact of the Gloss backend, logs
"Loaded." and forwards) — SRE13's `HOOK_SYMBOL` registers one installer per
symbol, and the log adds no capability.

### 2.5 Mini game-API surface (lawncher `api/mini/` — the real gap)

The lawncher exposes its game API through `RegisterProgramLibrary` (a
`ProgramState::RegisterProgramLibrary` hook). The desktop host deliberately
does **not** hook that symbol — SRE13 injects through
`sre13_ensure_injected()` → `sre13_extras_inject()`. The game API was missing
from that injection, so it is now registered there, in `sre13_extras_stubs.c`
(desktop-native: no JNI, vendored Lua C API):

`Mini.Arch`, `Mini.ToggleDebug`, `Mini.SetControlsHidden`,
`Mini.GetProfileID`, `Mini.RecreateHero`, `Mini.SetCoinLimit`, and the full
**`Mini.Character`** table from lawncher commit `c7d0a0b` ("Entirety of
Mini.Character from SwKiwi") — all **28** functions verified present:

- Attributes (8): `Get/SetWalkSpeed`, `Get/SetRunSpeed`, `Get/SetJumpSpeed`,
  `Get/SetAirJumpUsed` (named `CharControllerComponent` fields)
- Caver VOID (10): `DropQuickly`, `StartJumping`, `StopJumping`,
  `CancelCasting`, `FinishCasting`, `Die`, `Use`, `Hurt`, `Swing`,
  `StopSwing`
- Caver BOOL (6): `CanDoSomething`, `CanBeginCasting`, `CanUse`, `CanJump`,
  `CanSwing`, `CanPickup`
- Caver DIR (2): `StartMovingToDirection`, `StopMovingToDirection`
- Raw-offset (2): `SetMovementFacingLock` (0x1c4/0x2e4), `SetStunTime`
  (0x11c/0x220)

Every `DL_SYMBOL` target in this surface was verified against the 1.4.13
binary via `nm -D` (including `GameOverlayView::SetControlsHidden` and
`GameSceneController::CreateHeroObjectAt` — mangled form
`_ZN5Caver19GameSceneController18CreateHeroObjectAtERKNS_7Vector3Eib`).

**Injection restructure:** `sre13_extras_inject` now registers the
game-API + `Mini.Camera` surface *always* (both with and without
`libsre-extras.so`), then layers the memory surface on top — the extras'
`miniLL_open_memory` lib when loaded, the safe stub
`GetAddress/Malloc/Dlsym/GetComponentAddress` otherwise. Previously the
camera surface silently vanished when the extras were loaded.

New host-cooperative globals owned by SRE13 (mirroring sre12's
`g_sre_coin_limit = 9999` ownership pattern): `g_sre13_coin_limit` (init -1)
and `g_sre13_debug_active` (init 0), exported from `libsre13.so` for the
host to read via `get_symbol_vaddr`.

## 3. What was NOT ported (and why) — the `api/` layer analysis

The lawncher's `app/src/main/cpp/api/` is ~3.3k lines of:
`main.c` (RegisterProgramLibrary hook), `mini/{mini,mini_character}.c`,
`files/{fs,io,os}.c` + `PathParser`, and `overlay/*` (Android Java widget
JNI: button/seekbar/textinput/drawer/controller).

Desktop already supersedes each of these natively — copying them would mean
shimming JNI for a backend that isn't Android-limited:

| Lawncher api/ file | Desktop-native replacement |
|---|---|
| `main.c` — RegisterProgramLibrary hook | **Deliberately not hooked**; SRE13 injects via `sre13_ensure_injected()` → `sre13_extras_inject()` (sre13_lua.c) |
| `mini/mini.c` + `mini_character.c` | **Ported into `sre13_extras_stubs.c`** (§2.5) — desktop-native, no JNI |
| `files/fs.c` | `src/sre/extras/mod_fs.c` — its header literally says *"adapted from Kiwi Lawncher's fs.c"* |
| `files/io.c`, `files/os.c` | Same native extras layer (`mod_saves.c`, host VFS) |
| `files/PathParser.*` | `src/sre/sre12/sre_vfs.c` virtual roots `/Assets/ /Files/ /ExternalFiles/` (host-controlled VFS, SVC-driven) |
| `overlay/*` (Java widgets) | `src/sre/sre13/ui/sre13_button_controller.c` + extras `mod_button_hooks.c` (desktop input), no Android views |
| `java.h` JNI bridge | Not needed — the host ABI (`srehost_*` SVC calls) is the bridge |

The host-controlled VFS means files the lawncher reaches through Android
`AssetManager`/`JNIEnv` are plain host paths under the desktop's virtual
roots; mod FS/saves already route there.

## 4. ARM32 patch — why it's skipped (checked, as asked)

Lawncher `core/32patch.c` contains an ARM32-only text patch
(`Program::LoadIntoState` byte-swap hack). Two independent reasons it does
not port:

1. The lawncher itself wraps it in `#ifdef __arm__` — on aarch64 it compiles
   to nothing even in the reference tree.
2. SRE13 is aarch64-only (`bin/libs/libsre13.so`, cross-compiled with
   `aarch64-linux-gnu-gcc`).

Also verified: its hook target `_ZNK5Caver7Program13LoadIntoStateEP9lua_State`
is absent from the 1.4.13 arm64 binary's dynamic symbol table anyway.

The *portable* half of `32patch.c` (the `ps_*` time-scale registry + error
logging) **was** ported — `hooks/ProgramState.c` (§2.3).

## 5. Verified symbols (1.4.13 arm64 binary)

All hook/DL_SYMBOL targets used by this port confirmed via
`nm -D ~/.local/share/swordigo-desktop/engine/v1.4.13/arm64-v8a/libswordigo.so`:

| Symbol | Status |
|---|---|
| `Caver::PlayerProfile::LoadFromProtobufMessage(Caver::Proto::PlayerProfile const&, bool)` | ✓ 0x47cba8 |
| `Caver::GameOverlayView::SetControlsHidden(bool)` | ✓ |
| `Caver::GameSceneController::CreateHeroObjectAt(Caver::Vector3 const&, int, bool)` | ✓ 0x425ae8 |
| `Caver::CharControllerComponent::{Die,Use,Hurt,Swing,StopSwing,StartJumping,StopJumping,DropQuickly,CancelCasting,FinishCasting,CanDoSomething,CanBeginCasting,CanUse,CanJump,CanSwing,CanPickup,StartMovingToDirection(int),StopMovingToDirection(int),Update(float)}` | ✓ all |
| `Caver::PhysicsObjectState::Update(float)`, `Caver::EntityComponent::Update(float)` | ✓ |
| `Caver::ProgramState::{Update(float),~ProgramState}` | ✓ |
| `Caver::Program::LoadIntoState(lua_State*)` | ✗ absent — ARM32-only, correctly skipped |

Exported from `libsre13.so` after this pass (verified with `nm -D`):
`ps_setTimeScaleEnabled`, `ps_isTimeScaleEnabled`, `ps_remove`,
`g_sre13_coin_limit`, `g_sre13_debug_active` (plus pre-existing exports like
`sre13_extras_inject`).

## 6. Flagged items (do not treat as verified)

- `Mini.Character.SetStunTime` writes `CharControllerComponent+0x220` (64-bit)
  — inside the recovered header's unnamed pad region. Upstream offset, kept
  faithful, **unverified**.
- `Mini.Character.SetMovementFacingLock` writes `+0x2e4` (64-bit) — the
  recovered `CharControllerComponent.h` names that byte `isCasting`; the
  lawncher's name and the header disagree. Upstream offset, **unverified**.
- `Mini.ToggleDebug` sets the named `Scene.debugHitboxes` field rather than
  the lawncher's raw `Scene+0x2d0` write (upstream comment: "hitboxes should
  be at 0x1c4" — a guess). sre13's recovered layout puts `debugHitboxes`
  elsewhere; the field is the honest target but its offset deserves an IDA
  double-check.
- `g_sre13_coin_limit` is written by `Mini.SetCoinLimit` but **no host code
  consumes it yet** — wiring the clamp on the host side is an open TODO
  (the sre13.h comment already describes the intended contract).
- The lawncher's `Load` hook (duplicate of `LoadGameState`) was dropped as an
  artifact (§2.4).

## 7. Sync procedure for future lawncher commits

1. **Diff the trees** (shared paths only):
   ```bash
   L=lawncher_reference_for_sre13/app/src/main/cpp
   for f in $(cd $L && find caver hooks core -name '*.[ch]' | sort); do
     [ -f src/sre/sre13/$f ] || echo "NEW: $f"
     cmp -s $L/$f src/sre/sre13/$f || echo "DIFF: $f"
   done
   ```
2. **Classify each diff**: (a) sre13-side additions (usually ahead — keep),
   (b) include/guard noise (skip), (c) genuinely-new lawncher feature → port.
3. **Port rule**: never copy JNI/Android (`java.h`, `overlay/`, Gloss calls).
   If the desktop host already covers the feature (`mod_fs`, `sre_vfs`,
   `mod_button_hooks`, `sre_mini_api`), record it in §3 instead of porting.
4. **Verify every new symbol** against the 1.4.13 binary (`nm -D`) before
   trusting a hook target; check `archSplit` offsets against sre13's recovered
   headers (they may be newer than the lawncher's).
5. **Register** new `.c` files in `cmake/components/sre13.cmake`
   (`SRE13_CORE_SRCS`, relative to `src/sre/sre13/`), rebuild
   `cmake --build build-gg --target sre13`, and re-run the §5 export check.
6. Update this doc's tables.