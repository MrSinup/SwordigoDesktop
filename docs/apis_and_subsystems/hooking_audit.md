# SRE Native-Hooking Upgrade — AUDIT Phase

> **Scope**: Authorized local game-modding / reverse-engineering research on `libswordigo.so` (Swordigo v1.4.12 ARM64), executed under emulation by the SwordigoDesktop host.
> **Deliverable**: This document. No source, decompiled, or reference files were modified.
> **Method**: Direct reading of `exptsrc/sre/`, `exptsrc/main.cpp`, `src/main.cpp`, `src/platform/trampoline_mgr.h`, `src/srehost/*`, the GlossHook `Gloss.h` + prebuilt libraries, the SwKiwi/SwMini C tree, and live disassembly of `./libswordigo.so` with `aarch64-linux-gnu-objdump` / `readelf`.

---

## 0. Target binary (verified)

```
./libswordigo.so : ELF 64-bit LSB shared object, ARM aarch64, Android 21,
                   built by NDK r17c, STRIPPED, 7,230,152 bytes
Entry point      : 0x203e90  ( == .text start )
Dynamic symbols  : 16,226 (readelf --dyn-syms)
```

Section map (`readelf -SW`) — used to reason about offset↔vaddr identity:

| Section | VAddr | File Off | Identity? |
|---|---|---|---|
| `.plt` | 0x1f33d0 | 0x1f33d0 | ✅ vaddr == off |
| `.text` | 0x203e90 | 0x203e90 | ✅ (ends 0x583478) |
| `.rodata` | 0x583480 | 0x583480 | ✅ |
| `.eh_frame_hdr` | 0x5a1148 | 0x5a1148 | ✅ |
| `.eh_frame` | 0x5bce98 | 0x5bce98 | ✅ |
| `.gcc_except_table` | 0x637d50 | 0x637d50 | ✅ |
| `.data.rel.ro` | 0x6b6a80 | **0x6b2a80** | ❌ (diverge by 0x4000) |
| `.data` | 0x6e8000 | 0x6e4000 | ❌ |
| `.bss` | 0x6e8a60 | 0x6e4a60 | ❌ (NOBITS) |

**Offset semantics finding.** Every entry in the hook table is used at runtime as `load_addr + offset` against **guest virtual memory**, i.e. the numbers are **load-bias RVAs**, not literal file offsets. They coincide with the file offset only because `.text`/`.rodata`/`.eh_frame` are identity-mapped (VAddr == Off). The `SreHookEntry.target_offset` comment `/* file offset in libswordigo.so */` in `sre.h` is therefore numerically correct **for `.text` targets** but conceptually imprecise (they are RVAs). It breaks down for anything in `.data.rel.ro`/`.data`/`.bss` (the ITextInputDelegate vtable at RVA `0x7e1688` and the empty-string sentinel BSS offset `0x14880` are RVAs whose file offset differs).

Cross toolchain confirmed present: `aarch64-linux-gnu-nm`, `-objdump`, `-gcc`, plus host `nm`/`objdump`/`readelf`.

---

## 1. SRE Hook Inventory

SRE is compiled as a guest ARM64 shared library (`libsre.so`) loaded at guest base `0x2000000`; `libswordigo.so` is loaded at `0x1000000`. The host reads the exported `sre_hook_table` (`exptsrc/sre/sre_init.c`) and writes trampolines into `libswordigo.so`. There are **two host installers**: the legacy Unicorn path (`exptsrc/main.cpp`) and the newer Dynarmic/ProHook path (`src/main.cpp`).

### 1a. Distinct hook mechanisms found

| # | Mechanism | Location | Arch | Creates trampoline? | Preserves original? | Relocates insns? | Status / unfinished? |
|---|---|---|---|---|---|---|---|
| M1 | **Fixed 16-byte abs trampoline** `LDR X16,[PC,#8]; BR X16; .quad dst` | `exptsrc/main.cpp` (legacy) | ARM64 | Yes (in-place patch) | No (only via hand-saved relays) | No | Legacy path; clobbers 16 bytes |
| M2 | **4-byte `B` trampoline** (`0x14000000\|imm26`) via `TrampolineMgr::install_hook` | `src/main.cpp` + `trampoline_mgr.h` | ARM64 | Yes | Only if a relay cave is built | Yes, but **only the 1st instruction** (`copy_and_relocate(...,1)`) | Current path; main hook-table loop bypasses TrampolineMgr and writes raw `code[0]=B` |
| M3 | **Relay cave (call-through)** = `copy_and_relocate(N=1)` + `B target+4` | `src/main.cpp`, `trampoline_mgr.h` | ARM64 | Yes (64-byte arena slot 0x3000000–0x30FFFFF) | Yes (sets `g_orig_*`) | Yes (ADRP/ADR/B/BL/B.cond/CBZ/CBNZ/TBZ/TBNZ/LDR-lit) but **N is hard-coded to 1** | Works only when byte-0 insn is relocatable & within-range |
| M4 | **Verbatim 16-byte relay** (memcpy 16B, no relocation) + `LDR X16;BR X16` back to +16 | `exptsrc/main.cpp` | ARM64 | Yes | Yes | **No** — copies bytes verbatim | Unsafe for any function whose first 16 bytes contain PC-relative code |
| M5 | **Direct vtable-pointer overwrite** (ITextInputDelegate @ RVA 0x7e1688) | `exptsrc/sre/sre_init.c` `sre_init()` | ARM64 | No | N/A (replaces pointers) | N/A | Complete; safe (RWX guest mem) |
| M6 | **`.text` STXR/STLXR→STR scanner** (NOPs the retry CBNZ/rewrites CBZ) | `src/main.cpp`, `exptsrc/main.cpp` | ARM64 | No (binary rewrite) | No | N/A | Complete; global atomics fix, not a function hook |
| M7 | **ProHook SVC gateway** `SVC #0x5352` → `SREHost_InstallHook` → `sre_host_patch_guest` → TrampolineMgr; plus **zero-patch PC hook** (`sre_emulator_install_pc_hook`, BRK #0x42 returned from `MemoryReadCode`) | `src/srehost/*`, `src/platform/emulator_dynarmic64.cpp` | ARM64 (Dynarmic) | Yes (routes to M2/M3) / zero-patch | Yes | via M3 | **Partial** — `SREHost_RemoveHook` is a stub (`// TODO: implement real opcode restore`); BRK-hook path is a newer, unused-by-table zero-patch backend |
| M8 | **Late-install Lua trampolines** (`lua_call`→`sre_lua_call_safe`, `lua_resume`→`sre_lua_resume_safe`) after `sre_init_lua()` | `exptsrc/main.cpp` (16-byte), `src/main.cpp` (4-byte + relay) | ARM64 | Yes | `lua_resume` keeps a relay; `lua_call` fully replaced (uses `g_lua_pcall`) | M4 (exptsrc) / M3 (src) | Complete but backend-dependent |

### 1b. Hook-table entries (`sre_init.c`) — 34 active

- **CppString (4)** — `sre_CppString_from_char_p` `0x566bb8`, `_assign` `0x56918c`, `_append` `0x567254`, `_release` `0x565220`. Full non-atomic replacements (no call-through). Purpose: kill LDAXR/STLXR refcount spin loops under emulation.
- **Lua error safety** — `sre_luaD_throw` `0x4eb814`, `sre_ProgramPanic` `0x4c0d60`, `sre_cxa_throw` `0x51e108`, `sre_ProgramState_Execute`/`_Resume` (offset 0 → resolved by symbol), plus an atomic function-entry relay from `lua_resume` to `sre_lua_resume_safe`. Mechanism: replace C++ exception unwind with `setjmp`/`longjmp` (`sre_setjmp.S`, 176-byte `jmp_buf`, 16-deep recovery stack in `sre_lua.c`). `sre_ProgramState_Update` remains native to avoid per-frame wrapper overhead; its resume call is protected by the engine-wide `lua_resume` hook.
- **Background (3)** — `BackgroundComponent::Draw` `0x21ded4`, `RotatingBackgroundComponent::Draw` `0x2b6760` / `::Update` `0x2b66f8`. Full re-implementation.
- **GUI stack (10)** — `GUIWindow`/`GUIView`/`GUIButton`/`GUILabel`/`GUIFrameView`/`GUIAlertView`/`GUISlider`/`NewMenuView` `DrawRect`, `MainMenuVC_DidOpenShop` `0x36f394`, `GameOverVC_ShowAdMaybe` `0x347efc`. Use **relay stubs** (`g_orig_*`) for call-through.
- **Text input (4)** — `StartTextInputWithDelegate` `0x4792ac`, `StopTextInputWithDelegate` `0x4793dc`, `textInputTextDidChange` `0x4790dc`, `textInputDidFinish` `0x479290`. Full replacements + the M5 vtable fix.
- **MusicPlayer (7)** — `PlayMusicWithName` `0x4811a0`, `FadeIn` `0x4814a8`, `FadeOut` `0x4815d8`, `Update` `0x482090`, `AudioSystem::SetMusicVolume` `0x47f5f0`, `SetEnabled` `0x481e88`, `SetSuspended` `0x481fc0`. Full command-buffer replacement routed to host OpenAL.
- **Game state (1)** — `GameSceneView::Update` `0x34ed2c`.

**Disabled / unfinished entries (commented in `sre_init.c`):**
`PortalEffectComponent::Draw` `0x2ae884`, `SimpleGlowComponent::Draw` `0x2b0e90`, `WeaponGlowComponent::Draw` `0x2cb828` *(“needs relay trampoline”)*, `WeaponTrailComponent::Draw` `0x2ccf30` *(“needs relay trampoline”)*, `RegisterProgramLibrary` `0x4c0f18`, `luaL_newstate` (symbol), `FileExistsAtPath` `0x4b44b8` *(“stub returns 1 optimistically, breaking real file checks”)*, `MusicPlayer::SetVolume` `0x482064` / `SetLooping` `0x48206c` *(**8-byte collision**, see §3)*.

`sre_hooks_stubs.c` (`sre_luaL_newstate`) is a placeholder that only calls the original — confirms the injection path is unfinished.

---

## 2. Kiwi + GlossHook Architecture (from source)

### 2.1 SwKiwi/SwMini hook pipeline (verified in `app/src/main/cpp/`)

- `core/hooks.c` `hook_address()` calls
  `GlossHookAddr(func_addr, new_addr, orig_addr, /*is_4_byte_hook=*/false, archSplit(I_THUMB, I_ARM64))`.
- `hook_symbol()` resolves via `dlsym(engine_dl_handle, symName)` then `hook_address()`.
- `hook_engine_offset()` = `hook_address(engine_load_bias + offset, …)`.
- `branch_within_engine()` uses `GlossHookRedirect(bias+from, bias+to, use_small, mode)` on ARM64 and hand-emitted Thumb `B.T2/B.T4` (`emit_b_t2`/`emit_b_t4`) on ARM32.
- The `DL_HOOK_SYMBOL` / `DL_HOOK_OFFSET` / `DL_HOOK_ADDR` macros (`hooks.h`) expand to `_define_hook` which stores `orig_##name` and a `stub_##name` returned by GlossHook — i.e. **GlossHook returns a trampoline that preserves and can call the original** (used e.g. in `features/cstrings/hooks.c`: `orig_create_basic_string(...)` after replacement).
- 34 `DL_HOOK_*` sites across the tree (cstrings, panic, program_state, achievements, armor, weapon_trinket, GUI button/skeleton, 32patch, assets, network, lua_load_file).
- `engine_load_bias` is obtained robustly via `dladdr(dlsym("__bss_start"))` (GlossHook’s own bias lookup is noted broken past minSDK 22).

### 2.2 GlossHook API (from `Gloss.h` + exported symbols of the prebuilt `libGlossHook.so`)

Verified exported symbols (ARM64 `.so`): `GlossHook`, `GlossHookAddr`, `GlossHookBranchBL`, `GlossHookInternal`, `GlossHookRedirect`, `GlossGotHook`, `GlossHookByName`, `GlossPltHook`, `GlossHookAddrByName`, `GlossHookAddTrampolines`, `GlossHookDisable/Enable/Delete(+All)`, `GlossHookGetOldFunc`, `GlossHookGetOriglFunc`, `GlossHookGetFixedInst`, `GlossHookGetBackupsInst`, `GlossInit`, `SetMemoryPermission`/`Unprotect`, `WriteMemory`/`ReadMemory`.

- **Inline patching / trampoline generation**: default ARM64 head hook writes the same 16-byte `LDR X16,#8; BR X16; .quad NEW` sequence; `is_4_byte_hook=true` writes a single `B` (±128 MB) into a proximity trampoline. `GlossHookAddTrampolines()` lets you donate in-range memory to solve the ±128 MB reach problem.
- **Original-function preservation + relocation**: `old_func`/`GlossHookGetOriglFunc` returns a trampoline holding the **relocated** overwritten head instructions (the `Asm::A64` helpers `MakeArm64AbsoluteJump*`, `GetArm64BranchDestination`, `MakeArm64B/BCond/CB` are the relocation primitives). `GlossHookGetBackupsInst`/`GetFixedInst` expose the original vs fixed bytes.
- **ARM32/Thumb**: full Thumb16/Thumb32/ARM support (`emit_b_t2/t4`, `IsThumb32`, `MakeThumb16/32NOP`, `GET_INST_SET(addr&1)`); Kiwi passes `archSplit(I_THUMB, I_ARM64)` so the same call works on both ABIs.
- **Memory protection / cache**: `SetMemoryPermission`/`Unprotect` (mprotect); the `.so` `NEEDED` list is `libandroid.so, liblog.so, libdl.so, libc.so, libm.so` and imports only `mmap`/`memcpy` — cache flushing is done via the compiler `__builtin___clear_cache` intrinsic (no external cacheflush symbol).
- **Removal / lifecycle / chaining / multi-hook**: `GlossHookDisable/Enable/Delete` (+`*All`), doubly-linked multi-hook chain per address (`GlossHookGetPrev/Next/LastHook`, `GlossHookGetCount/TotalCount`, `GlossHookReplaceNewFunc`).
- **Re-entrancy**: documented pattern using `pthread` TLS guard + `GlossHookGetOriglFunc` to skip the chain.
- **GOT/PLT + branch-site + mid-function**: `GlossGotHook`/`GlossPltHook`, `GlossHookBranchBL` (single call site), `GlossHookInternal` (full `gloss_reg` X0–X30/SP/PC/CPSR/Q0–Q31 register access).

### 2.3 `__ANDROID__` guard — implication (critical)

`Gloss.h` opens with:

```c
#ifndef __ANDROID__
#error GlossHook only support android
#else
#if !(defined __arm__) && !(defined __aarch64__)
#error GlossHook only support arm and arm64
#endif
#endif
```

The prebuilt libraries confirm it: both `libGlossHook.so` (ARM64) and the ARM build `NEEDED` `libandroid.so`/`liblog.so` and are `for Android 21`.

**Implication**: GlossHook **cannot be compiled or linked unmodified on the x86_64 desktop host**. It is a *target-side, ARM/ARM64-only, on-device* inline hooker. In the SwordigoDesktop model the “target” (`libswordigo.so`) never runs natively on the host — it runs **inside the Dynarmic/Unicorn ARM64 emulator**. Therefore libglooshook is **not usable and not necessary on the host**: the host is x86_64, and the only ARM64 code that exists is guest code the emulator already fully controls. Any GlossHook-equivalent must be re-implemented as guest-side ARM64 emission or host-side guest-memory patching — which is exactly what SRE already does.

### 2.4 Kiwi ARM32 vs ARM64 offsets (reference data — keep separated)

From `offsets/base.h` `offset_(o32, o64)` and `offsets/cpp_strings.h`:

| Symbol | ARM32 (o32) — reference only | ARM64 (o64) |
|---|---|---|
| CppString from_char_p | 0x37bc60 | **0x566bb8** |
| CppString append_impl | 0x379988 | **0x567254** |
| CppString assign | 0x37aa1c | **0x56918c** |
| CppString unsafe_release | 0x3787c8 | **0x565220** |
| CppString safe_release | 0x379768 (ARM32-only; inlined on 64-bit) | — |
| empty-string sentinel (BSS) | 0x6a04 | **0x14880** |
| Achievements branch_within | 0x2891aa→0x28a0ac | 0x376088→0x377ba8 |

**The four ARM64 CppString offsets are byte-identical to SRE’s `sre_init.c`**, confirming SRE reused SwKiwi/SwMini’s reverse-engineering and that the sampled offsets are correct engine addresses.

---

## 3. ARM64 Verification (live disassembly of `./libswordigo.so`)

`aarch64-linux-gnu-objdump -d --start-address=… --stop-address=…`. "PC-rel in first 16B" = does any of the 4 head instructions use ADR/ADRP/B/BL/B.cond/CBZ/CBNZ/TBZ/TBNZ/LDR-literal (which a relocating relay must fix).

| Offset | Symbol | Head insns (first 1–2) | PC-rel in first 16B? | 16-byte overwrite safe? |
|---|---|---|---|---|
| 0x566bb8 | CppString(const char*) | `stp x29,x30,[sp,#-64]!` / `mov x29,sp` | No | ✅ |
| 0x56918c | CppString::assign | `stp x29,x30,[sp,#-48]!` / `mov x29,sp` | No | ✅ |
| 0x567254 | CppString::append | `stp x29,x30,[sp,#-64]!` / `mov x29,sp` | No | ✅ |
| 0x565220 | CppString::release | **`adrp x2, 0x6e4000` / `ldr x2,[x2,#3704]`** | **YES (ADRP @ +0)** | ⚠️ verbatim copy breaks; **must relocate** |
| 0x4a28bc | GUIWindow::DrawRect | `sub sp,sp,#0xa0` / `stp x26,x25,…` (ADRP appears later @ +0x28) | No (in first 16B) | ✅ |
| 0x4eb814 | luaD_throw | `stp x22,x21,[sp,#-48]!` … | No | ✅ |
| 0x4811a0 | MusicPlayer::PlayMusicWithName | `sub sp,sp,#0x70` / `stp x24,x23,…` | No | ✅ |
| 0x482064 | MusicPlayer::SetVolume | `str s0,[x0,#4]` / **`b UpdatePlayerVolume@plt`** | function is only **8 bytes** | ❌ collides (see below) |
| 0x48206c | MusicPlayer::SetLooping | `mov x8,x0` / `ldr x0,[x8,#16]` … | No | ❌ (starts 8B into SetVolume’s tramp) |

### 3.1 CONFIRMED 8-byte collision (SetVolume / SetLooping)

`objdump` of `0x482040–0x482090` shows `SetVolume` (0x482064) is exactly **two instructions / 8 bytes** — `str s0,[x0,#4]` then `b …UpdatePlayerVolume@plt` — and `SetLooping` begins immediately at **0x48206c (Δ = 0x8)**. IDA confirms `SetVolume` is a 2-line thunk. A 16-byte trampoline at `SetVolume` overwrites all 8 bytes of `SetVolume` **plus the first 8 bytes of `SetLooping`**. This exactly matches the `sre_init.c` / `sre_music.c` comment. **Both are correctly left unhooked**; SRE instead syncs volume/loop by reading `this+4`/`this+8` inside `MusicPlayer::Update` and hooks the high-level `AudioSystem::SetMusicVolume` (0x47f5f0, a 3-insn thunk → `MusicPlayer::SetVolume@plt`).

Neighboring MusicPlayer gaps (all safe for a 16-byte tramp): FadeIn→FadeOut = 0x130, SetEnabled→SetSuspended = 0x138, SetLooping→Update = 0x24.

### 3.2 PC-relative relocation findings

- **`CppString::release` (0x565220)** starts with `ADRP x2, 0x6e4000` — a PC-relative literal. A relay that copies the head **verbatim** (mechanism M4, `exptsrc/main.cpp`) would leave the ADRP pointing at the wrong page. It is safe **today** only because release is a *full replacement with no call-through relay*. `src/main.cpp`’s `copy_and_relocate` **does** rewrite ADRP correctly, so M3 would handle it.
- **`textInputDidFinish` (0x479290)** also starts with **`adrp x8, 0x6e9000`** — same hazard, same mitigation (full replacement, no relay today).
- All other sampled targets begin with `sub sp`/`stp` (non-PC-relative) → a byte-verbatim 16-byte relay is safe for them.

### 3.3 Offset kind (file-offset vs vaddr) — resolved

- `.text` VAddr == File Off (identity), verified from the section table (`0x203e90/0x203e90`). Since all `.text` hook targets satisfy `0x203e90 ≤ off < 0x583478`, their numeric offset equals both file offset and load-bias RVA. The table comment “file offset” is thus numerically correct **for these**.
- **`ProgramPanic` 0x5c0ab4 is OUTSIDE `.text`** (`.text` ends 0x583478; 0x5c0ab4 falls inside **`.eh_frame`** 0x5bce98–0x637d50). Raw bytes there (`10 9e 02 9d …`) are not a function prologue. **Finding**: the `sre_ProgramPanic` target `0x5c0ab4` is **stale/incorrect for this build** — writing a trampoline there would corrupt the C++ unwind tables. It is described in-source as a “backup that should never fire now that luaD_throw is hooked,” so it is latent, but it is a real bug and must be re-resolved (by symbol) or removed. `lua_atpanic` in this build is at `0x4e688c`.

### 3.4 Cache coherency / memory protection (host side)

Guest memory is mapped RWX (`sre_init.c`: “Unicorn maps all guest memory as RWX”), so no `mprotect` is needed for patching. For Dynarmic, patches written **before** the run loop need no flush; runtime patches call `EmulatorDynarmic64::invalidate_cache_range()` / `jit->InvalidateCacheRange()` (used by the SVC path and the BRK zero-patch path). The legacy hook-table loop patches at init time, so it does not flush — acceptable only because it runs pre-execution.

---

## 4. SRE-vs-Kiwi Comparison

Classification: **[COMPLETE] / [PARTIAL] / [UNFINISHED] / [MISSING] / [NOT NEEDED]** applies to the SRE column.

| Feature | Kiwi (GlossHook) | SRE | Assessment |
|---|---|---|---|
| Inline patching | Head hook (16B abs / 4B B) + proximity tramps | 16B abs (`exptsrc`) or 4B `B` (`src`), guest-mem write | **[COMPLETE]** for entry-point hooks |
| Trampoline | Auto-allocated in-range, donate via `AddTrampolines` | Fixed arena `0x3000000–0x30FFFFF`, 64-byte slots (`TrampolineMgr`) | **[PARTIAL]** — fixed arena, no proximity fallback |
| Original function | `GlossHookGetOriglFunc`/`old_func` returns relocated tramp | `g_orig_*` relay caves (only for ~15 relayed hooks) | **[PARTIAL]** — most hooks are full replacements w/o call-through |
| Instruction relocation | Full (ADRP/B/BL/B.cond/CB/TB/LDR-lit, both ABIs) | `copy_and_relocate` handles ADRP/ADR/B/BL/B.cond/CBZ/CBNZ/TBZ/TBNZ/LDR-lit — **but only N=1 instruction**, and 19/14-bit forms overflow across the 32 MB arena gap | **[PARTIAL]** — correct for 1 head insn; range-unsafe for CBZ/B.cond/TBZ/LDR-lit relocated into the far cave |
| ARM64 | ✅ | ✅ (only target) | **[COMPLETE]** |
| ARM32 | ✅ (Thumb16/32/ARM) | — (target is ARM64-only) | **[NOT NEEDED]** |
| Thumb | ✅ | — | **[NOT NEEDED]** |
| PC-relative handling | ✅ full | Partial (see relocation row); verbatim M4 relay in `exptsrc` does **not** relocate | **[PARTIAL]** |
| Multiple hooks (same addr) | ✅ chain | ❌ last-write-wins; the 34-entry loop writes raw `B` with no dedup (TrampolineMgr has `check_conflicts()` but the main loop bypasses it) | **[MISSING]** |
| Hook removal | `GlossHookDelete/Disable` | `SREHost_RemoveHook` is a `// TODO` stub (branch left installed); table hooks have no uninstall | **[UNFINISHED]** |
| Cache flush | `__builtin___clear_cache` | `InvalidateCacheRange` on SVC/BRK paths; init-time table patches unflushed (OK pre-run) | **[PARTIAL]** |
| Memory protection | `SetMemoryPermission`/mprotect | Guest RWX → not required | **[NOT NEEDED]** |
| Thread safety | mutex/TLS re-entrancy guard | Single-threaded guest; `TrampolineMgr` has no lock; SREHost registry has a `std::mutex` | **[PARTIAL]** (adequate given single-threaded guest) |
| Error handling | Handle returns / logs | Trampoline install prints, best-effort skip; setjmp/longjmp recovery for Lua/C++ throw | **[PARTIAL]** |
| Chaining | ✅ | ❌ | **[MISSING]** |
| Reentrancy | TLS guard | Recovery-stack depth guard for Lua; no general hook-reentry guard | **[PARTIAL]** |
| Symbol resolution | `dlsym`/`GlossSymbol` (dyn + debug) | Host resolves via ELF loader `get_symbol_vaddr` / `elf_lookup_symbol` + mangled-name table + offset table | **[COMPLETE]** |

---

## 5. Missing / Unfinished Functionality

1. **Hook removal / uninstall** — `SREHost_RemoveHook` is a stub; the branch is left in guest memory (`// TODO: implement real opcode restore`). No uninstall for `sre_hook_table` entries.
2. **Multi-hook / dedup** — the 34-entry install loop writes raw branches with no conflict check; `TrampolineMgr::check_conflicts()` covers relay caves only. No hook chaining.
3. **Multi-instruction relocation** — `copy_and_relocate` is always called with `N=1`. Any function needing >1 head instruction relocated (or whose byte-0 is a range-limited CBZ/B.cond/TBZ/LDR-literal that overflows the 32 MB target→arena distance) cannot be safely relayed. This is exactly the `updateApplication` CBZ-overflow bug already documented in `trampoline_mgr.h`.
4. **Verbatim relay hazard (legacy)** — `exptsrc/main.cpp` M4 relays copy 16 bytes without relocation; unsafe for ADRP-headed functions (`CppString::release`, `textInputDidFinish`).
5. **Disabled hooks needing relays** — WeaponGlow/WeaponTrail Draw explicitly “needs relay trampoline”; `FileExistsAtPath` disabled (optimistic stub breaks real checks); `RegisterProgramLibrary`/`luaL_newstate` injection unfinished (`sre_hooks_stubs.c`).
6. **Stale/unsafe offset** — `sre_ProgramPanic` `0x5c0ab4` points into `.eh_frame`, not `.text` (§3.3).
7. **No proximity trampoline pool** — fixed `0x3000000` arena; if a hooked function were placed >128 MB from `libsre.so` the 4-byte `B` would be out of range (currently OK: swordigo@0x1000000, sre@0x2000000 → 16 MB; return-branch from cave ≤ 33 MB).
8. **No branch-site (`BL`) or mid-function register hooks** — SRE has no `GlossHookBranchBL`/`GlossHookInternal` equivalent (the BRK zero-patch backend is the nearest, but it is not wired to the table).

---

## 6. Recommended Architecture & PATH decision

**Chosen: PATH B — the SRE mechanism is partially built and should be *finished / hardened*, not rebuilt from scratch, and it should NOT adopt libglooshook.**

Rationale (evidence-based):

- **Not PATH A (already sufficient).** The evidence disproves “sufficient”: fixed 16-byte / single-instruction relocation, no unhook, no dedup/chaining, a verbatim-copy relay that breaks on ADRP heads, one stale offset in `.eh_frame`, and known CBZ-overflow when relocating into the far arena.
- **Not PATH C (build a whole new dedicated backend, e.g. port GlossHook).** GlossHook is `#error`-guarded to `__ANDROID__` + arm/arm64 and its prebuilt `.so` `NEEDED` `libandroid.so`; it cannot run on the x86_64 host, and the ARM64 target only ever executes inside the emulator, which the host already fully controls. A from-scratch dedicated backend is unjustified because ~80% already exists (`TrampolineMgr` + `copy_and_relocate` + `SREHost` SVC ABI + Dynarmic BRK zero-patch).
- **PATH B is the fit.** The correct primitives are present; they need completion and hardening.

Recommended target architecture (`SRE_HookEngine`, per `docs/inline-trampline-for-sre/03…`), built on the existing `TrampolineMgr`/`SREHost`:

1. **Route every hook (table + late-install + relays) through one `TrampolineMgr`/`SREHost_InstallHook` API.** Kill the raw `code[0]=B` loop so dedup/conflict-check/logging apply uniformly.
2. **Generalize `copy_and_relocate` to N instructions**, choose N = min instructions ≥ trampoline size, and reject/redirect range-limited forms (CBZ/B.cond/TBZ/LDR-lit) — or place caves within ±1 MB of the target to keep them relocatable.
3. **Prefer the Dynarmic zero-patch BRK backend** (`sre_emulator_install_pc_hook`) for functions that are short (SetVolume/SetLooping), PC-relative-headed (CppString::release, textInputDidFinish), or need call-through — it needs no guest-memory rewrite and no relocation, sidestepping both the 8-byte collision and the ADRP hazard.
4. **Implement real `SREHost_RemoveHook`** (save original bytes at install, restore + `InvalidateCacheRange` on remove) and add enable/disable + chaining.
5. **Re-resolve stale offsets by symbol** (fix `ProgramPanic`), and treat the `SreHookEntry.target_offset` comment as an RVA, not a file offset.

---

## 7. Migration plan for the existing primitive hooks

1. **Freeze behavior**: keep `sre_hook_table` as the source of truth; add an adapter so each entry is installed via `SREHost_InstallHook(load_addr+RVA, sre_func, &orig, backend)` instead of raw branch writes. No SRE reimplementation changes.
2. **Backend per entry** (auto-select):
   - Long, non-PC-relative head, no call-through → **4-byte `B` trampoline** (current M2).
   - Needs call-through → **relay cave with N-instruction relocation** (generalized M3).
   - Short (<16B) / PC-relative head / SetVolume-SetLooping-style neighbors → **BRK zero-patch** (M7) — resolves the 8-byte collision and ADRP hazards without touching guest code.
3. **Fix known offenders**: re-enable WeaponGlow/WeaponTrail via relays once N-relocation lands; move `CppString::release` + `textInputDidFinish` relays (if ever needed) to relocating caves or BRK; correct/remove `ProgramPanic` `0x5c0ab4`.
4. **Add lifecycle**: implement `RemoveHook` (byte restore + JIT invalidate) and run `TrampolineMgr::check_conflicts()`/`dump()` for the full set (not just relays) at startup.
5. **Regression gate**: after each migration step, boot the emulator and confirm CppString spin-loop elimination, Lua error recovery (Wastelands), music, GUI draw, and text input still work; diff `sre_hook_debug.txt` head-instruction dumps against this audit.
6. **Do not vendor GlossHook**; instead keep it as the *reference spec* for the API surface (`Enable/Disable/Delete`, chaining, `GetOriglFunc`, branch-site, mid-function).

---

## 8. Concise summary

- **PATH: B (finish/harden the existing SRE hook backend).** The primitives exist (`TrampolineMgr`, `copy_and_relocate`, `SREHost` SVC ABI, Dynarmic BRK zero-patch); they are incomplete, not absent — PATH A and PATH C are both wrong for this evidence.
- **Key SRE limitations found**: fixed-arena single-instruction relocation (CBZ/B.cond/TBZ/LDR-lit overflow across the 32 MB gap); `RemoveHook` is a TODO stub (no true unhook); no dedup/chaining in the main install loop; verbatim (non-relocating) 16-byte relays in the legacy path; most hooks are full replacements with no call-through; the `ProgramPanic` offset `0x5c0ab4` is stale (points into `.eh_frame`, not `.text`).
- **Is libglooshook necessary on the host?** **No.** `Gloss.h` is `#error`-guarded to `__ANDROID__`+arm/arm64 and the prebuilt `.so` requires `libandroid.so`; it cannot build/run on the x86_64 host, and the ARM64 target runs inside the emulator the host already controls. GlossHook is useful only as an API-design reference.
- **Confirmed ARM64 findings**: **SetVolume 0x482064 / SetLooping 0x48206c are exactly 8 bytes apart** — a 16-byte trampoline collides (correctly left unhooked; volume/loop read from `this+4`/`this+8` in `Update`). **CppString::release 0x565220 and textInputDidFinish 0x479290 begin with `ADRP`** (PC-relative head) → verbatim relays break; must relocate or use the zero-patch backend. `.text` offsets are identity-mapped (RVA == file offset); the four sampled CppString offsets match SwKiwi’s ARM64 offsets exactly.
- **Deliverable**: `/home/quantumcreeper/SwordigoDesktop/docs/hooking_audit.md`.
