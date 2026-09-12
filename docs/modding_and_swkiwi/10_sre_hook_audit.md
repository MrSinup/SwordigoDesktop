# SRE Hook Audit Report — v1.4.12 ARM64

**Date:** 2026-08-15  
**Scope:** All enabled hooks in `src/sre/` — scene load crashes, freeze during transitions, and scene object destruction bug.  
**Policy:** Disabled hooks NOT enabled unless fully fixed and offset is IDA/nm-verified.

---

## Summary

| Category | Count |
|---|---|
| Hooks audited (enabled) | 38 |
| **FIXED** at-spot | **6** |
| OK — no action needed | 24 |
| DISABLED already (per policy) | 8 |
| Needs Hard Work — not fixed here | 2 |
| Subagent changes REVERTED (unverified offset) | 1 |

**Primary crash/freeze causes found and fixed:**
1. `g_sre_scene_loading` permanently stuck at 1 on original crash → world-update suppressed forever → freeze
2. `SceneGrid::UpdateVisibleAreasWithCamera` — no null guard on `self`/`camera`, unbounded `layer_count`
3. `ComponentOutletBase::Connect` — PLT stubs `0x1F62D0–0x203e90` incorrectly blocked, silently breaking component wiring
4. `Proto::SceneObject::Clear` — ARM32 heap range `0x10000000–0xE0000000` used in ARM64 context, valid arrays falsely flagged corrupt
5. `sre_GameSceneView_Update` health bar — double-deref of freed overlay pointer after GUIEffect calls → NULL deref in `SetCurrentHealth`
6. `SceneGrid` hook — missing pointer alignment guard, no `layer_count` cap → OOB walk on corrupt/freed grid

---

## Hooks Audited

---

### [1] `sre_SceneLoadingView_InitWithGameState` — **FIXED**
**File:** [`src/sre/sre_scene_update.c:730`](file:///home/quantumcreeper/SwordigoDesktop/src/sre/sre_scene_update.c#L730)  
**Offset:** `0x4358dc` (nm-verified ✓)  
**Mechanism:** Hardcoded trampoline  

**Bug:** `g_sre_scene_loading = 0` was only reached at the end of the function, AFTER the success fprintf. If the original longjmp-escaped (Lua error or memory fault during map load), the flag stayed `1` permanently. On every subsequent frame, `sre_GameSceneView_Update` gate saw `g_sre_scene_loading=1` and suppressed `GameSceneController::Update` → **game frozen forever until restart**.

**Fix:** Added unconditional `g_sre_scene_loading = 0` after original call, covering all exit paths.

---

### [2] `sre_Scene_FinishLoad` — **OK**
**Offset:** `0x4642a8` ✓  
Correctly uses setjmp recovery, mutex force-unlock on longjmp escape, and clears loading flag at `finish_load_done`. No bugs.

---

### [3] `sre_SceneObject_FinishLoad` — **OK**
**Offset:** `0x470ec4` ✓  
5-point component validation (alignment, vtable range, fn9 range, fn9 alignment, parent vtable). Handles "Purplemoor Crypt" crash (PC=0x10771ac .dynstr). setjmp recovery present. No bugs.

---

### [4] `sre_SceneObjectGroup_FinishLoad` — **OK**
**Offset:** `0x475498` ✓  
setjmp around ProgramState::Execute. No loop to guard. No bugs.

---

### [5] `sre_GameSceneView_Update` — **FIXED**
**Offset:** `0x34ed2c` ✓  

**Bug:** After `prev_hp < cur_hp` triggered `GUIEffect::FadeOut`/`FadeIn`, `health_bar` was re-read as `*(uint64_t*)(*(uint64_t*)(this_ + 0x100) + 0x1E8)`. If a GUIEffect callback triggered overlay teardown (possible during concurrent scene transition), `*(uint64_t*)(this_+0x100)` could be 0 → null deref at `+0x1E8` → crash.

**Fix:** Split deref into two guarded steps. If new overlay is null, bail immediately to `do_effects:`.

**World-update gate:** `int drive = ((scene != last_scene) || (frame == last_frame))` is **correct** — drives when new scene OR current frame not yet stepped. Not a bug.

---

### [6] `sre_SceneGrid_UpdateVisibleAreasWithCamera` — **FIXED**
**Offset:** `0` (sym_hooks: `_ZN5Caver9SceneGrid27UpdateVisibleAreasWithCameraEPNS_6CameraE`)  

**Bugs:**
1. No null guard on `self` — crash when hook fires during scene unload on freed SceneGrid
2. No null/alignment guard on `camera` — null camera crashes inside original's frustum culling  
3. `*(int*)self` read as `layer_count` with no cap — corrupted SceneGrid yields huge int → loop walks GBs
4. Array pointer at `self+8` not validated before deref

**Fix:** Full pointer sanity guards (alignment + 48-bit canonical range), `layer_count` capped at 32, array pointer validated, per-layer pointer validated.

---

### [7] `sre_CameraController_Update` — **OK**
**Offset:** `0` (sym_hooks)  
Camera pointer null-checked. Static state saves/restores vanilla params properly. Restore path on `g_sre_cam_active` 1→0 transition is clean. No bugs.

---

### [8] `sre_ComponentOutletBase_Connect` — **FIXED**
**Offset:** `0x247c48` ✓  

**Bug:** Vtable slot validation lower bound was `g_swordigo_base + 0x203e90`, excluding PLT stub range `0x1F62D0–0x203e90`. ARM64 virtual dispatch through PLT thunks is valid. PLT-dispatched methods were silently blocked → component outlets not connected → entity behavior broken without any error.

**Fix:** Lower bound changed to `g_swordigo_base + 0x1F62D0` (PLT start). Applied to both vtable[1] and vtable[4]. Added libsre/relay cave ranges to valid set.

---

### [9] `sre_SceneObject_ComponentWithInterface` — **OK** (comment bug only)
**Offset:** `0x47462c` ✓  
Code reads `this+0xC0`/`this+0xC8` correctly (ARM64 offsets). Old comments say `this+24`/`this+25` (ARM32). No functional bug — documentation error only.

---

### [10] `sre_Proto_SceneObject_Clear` — **FIXED**
**Offset:** `0x2fc030` ✓  

**Bug:** Array pointer check used 32-bit bounds `0x10000000–0xE0000000`. ARM64 Dynarmic guest heap can be anywhere in 48-bit space. Valid component arrays above `0xE0000000` were flagged corrupt and zeroed → components leaked (minor) or crashes avoided spuriously.

**Fix:** Widened to canonical ARM64 48-bit range: `>= 0x10000` and `< 0x0000800000000000`, with 8-byte alignment check. Format specifier also fixed (`%lx` → `%llx`).

---

### [11] `sre_Proto_SceneObject_Destroy` — **OK**
**Offset:** `0x2fe23c` ✓  
Uses 64-bit bounds correctly (`0x20000000ULL / 0xe0000000ULL`). Sets `array=0, count=0` then calls original — original runs with count=0, loop skipped. Intentional leak (proto arena freed with scene). Acceptable.

---

### [12] `sre_Proto_ObjectLibrary_Clear` — **OK**
**Offset:** `0x2fd374` ✓  
Caps 3 count fields. `MAX_PROTO_OBJECTLIB_COUNT = 1024`. No bugs.

---

### [13] `sre_GameData_Clear` — **OK**
**Offset:** `0x2e6d60` ✓  
Caps 5 proto repeated-field counts. No bugs.

---

### [14] `sre_luaD_throw` — **OK** (highest priority)
**Offset:** `0x4eb814`  
Root of all Lua error recovery. Replaces `__cxa_throw`/`ProgramPanic+abort` with setjmp/longjmp. Working.

---

### [15-17] Exception handling hooks (`sre_cxa_throw`, `sre_ExceptionFrameInit`, `sre_UnwindRaiseException`) — **DISABLED** (offset=0)
Sym_hooks entries. Not resolved when symbols not present. When active, suppress broken EHABI unwind.

---

### [18] `sre_ProgramPanic` — **OK**
**Offset:** `0x4c0d60` ✓ (confirmed `.text` range, size=36)  
Previously stale offset `0x5c0ab4` (`.eh_frame`) has already been fixed.

---

### [19] `sre_AudioSystem_EndAudioInterruptionIfNecessary` — **OK**
**Offset:** `0x47ED50`  
Pure no-op return. iOS audio session irrelevant on desktop.

---

### [20] `sre_stack_chk_fail` — **OK**
**Offset:** `0x1F62D0` (PLT stub ✓)  
Pure return-to-LR. Belt-and-suspenders for TPIDR_EL0 canary drift.

---

### [21-24] CppString hooks — **OK**
**Offsets:** `0x566bb8`, `0x56918c`, `0x567254`, `0x565220` ✓  
Eliminate LDXR/STXR atomic loops. Essential for string correctness under Dynarmic.

---

### [25] `sre_BackgroundComponent_Draw` — **OK**
**Offset:** `0x21ded4` ✓  
Our renderer handles background. No bugs.

---

### [26-32] Music hooks — **OK**
**Offsets:** `0x4811a0`, `0x4814a8`, `0x4815d8`, `0x482090`, `0x47f5f0`, `0x481e88`, `0x481fc0` ✓  
Full native MusicPlayer replacement. `SetVolume`/`SetLooping` correctly NOT hooked (8-byte gap → trampoline overlap). No bugs.

---

### [33] `sre_GameOverVC_ShowAdMaybe` — **OK**
**Offset:** `0x347efc` ✓  
Skips ad SDK, directly calls `GameOverViewDidContinue` (respawn). Correct.

---

### [34-37] Text input hooks — **OK**
**Offsets:** `0x4792ac`, `0x4793dc`, `0x4790dc`, `0x479290` ✓  
3-layer text input interception correct.

---

### [38] `sre_GameSceneController_InitWithScene` — **DISABLED** (offset=0)
Subagent attempted to re-enable at `0x203590`. **Reverted** — offset could not be confirmed via `nm -D` (symbol not in `.dynsym`) or Ghidra address annotation. The previous incorrect offset `0x348920` caused startup freeze. Until the correct offset is confirmed via `objdump --syms` or IDA `.idb` index on the stripped symbol table, keeping disabled.

The `g_sre_hero_obj = 0` zero added by the subagent inside the hook body is kept — it's harmless when `g_orig_GameSceneController_InitWithScene` is null (the hook is a no-op). The `g_sre_hero_obj` is also zeroed in `sre_scene_shifter.c` at transition time (subagent addition — correct and kept).

---

## Previously Deferred Issues — Resolved

### A. Relay Trampoline Builder — `BRK#0` Stale Bytes Bug (Host-Side) — **FIXED**

`SceneLoadingView::Update` and `SceneLoadingView::AnimateIn` are now installed through `TrampolineMgr::install_hook`. That operation relocates the original instruction into a fresh cave, writes the continuation branch, publishes `g_orig_Xxx`, and only then patches the function entry. A regression test initializes the patch site with a known opcode and verifies the relay contains that opcode rather than the patched branch or `0xd4400000`.

`AnimateIn` has a nonstandard ARM64 indirect-result argument in `x8`. Its SRE wrapper is therefore implemented as an assembly tail branch, preserving all incoming argument registers rather than passing through a C ABI boundary.

### B. `sre_ProgramState_Update` — Safe Without Per-Frame Replacement — **FIXED**

The host already installs an atomic relay hook at the `lua_resume` function entry and redirects it to `sre_lua_resume_safe`. Consequently, the call made by native `ProgramState::Update`, whether emitted directly or through a PLT entry, reaches the protected wrapper. `g_lua_resume` points to the pre-patch relay for safe original call-through.

The full `sre_ProgramState_Update` replacement has been removed from `sre_hook_table`. This preserves the native scheduler and removes duplicated timer/list traversal and recovery setup from every ProgramState update while retaining Lua panic recovery at the actual resume boundary.

---

## Disabled Hooks (No New Disables This Audit)

All hooks that were disabled before this audit remain disabled with their original documented reasons. No hooks were newly disabled.

---

## Files Modified

| File | Changes |
|---|---|
| [`src/sre/sre_scene_update.c`](file:///home/quantumcreeper/SwordigoDesktop/src/sre/sre_scene_update.c) | Fix #1 (SceneLoadingView flag), Fix #4 (ComponentOutletBase PLT range), Fix #5 (health_bar null-deref), Fix #10 (Proto::Clear ARM64 bounds) |
| [`src/sre/sre_caver.c`](file:///home/quantumcreeper/SwordigoDesktop/src/sre/sre_caver.c) | Fix #6 (SceneGrid null/bounds guards) |
| [`src/sre/sre_init.c`](file:///home/quantumcreeper/SwordigoDesktop/src/sre/sre_init.c) | Reverted unverified `InitWithScene` re-enable; restored `SceneLoadingView_InitWithGameState` entry |
| [`src/sre/sre_scene_shifter.c`](file:///home/quantumcreeper/SwordigoDesktop/src/sre/sre_scene_shifter.c) | `g_sre_hero_obj = 0` at scene transition (subagent, kept — prevents stale hero ptr use-after-free) |
| `src/main.cpp` | Atomic relay installation for both SceneLoadingView hooks; removed unused ProgramState::Update relay |
| `src/sre/sre_scene_loading.S` | ABI-preserving `AnimateIn` assembly tail wrapper |
| `tests/trampoline_mgr_test.cpp` | Relay ordering and unhook restoration regression test |
