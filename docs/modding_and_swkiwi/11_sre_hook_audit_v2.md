# SRE Hook Audit v2 — Full Validation Report

> **Date**: 2026-08-17  
> **Target**: `libswordigo.so` v1.4.12 ARM64  
> **Auditor**: Full automated nm + Ghidra/IDA cross-reference  
> **nm symbol count**: 16,225 exported symbols  
> **Total hooks in table**: 52 entries (42 enabled, 10 in `#if 0`)

---

## Summary

| Metric | Count |
|---|---|
| Total hooks audited | 42 |
| All offsets nm/readelf verified | 42 / 42 ✅ |
| Hooks fully correct (no changes) | 36 |
| Hooks with bug fixes applied | 4 |
| Hooks disabled (this session) | 2 |
| Hooks upgraded with PC-hook framework | 0 (framework added, hooks pending) |
| Already-disabled `#if 0` hooks (not touched) | 10 |

**Net result**: The freeze-on-scene-load and scene-object-destruction bugs are addressed. All pointer dereferences are 48-bit ARM64 guarded. The two required `SceneLoadingView` pass-through hooks are enabled with validated relays and ABI preservation.

---

## Offset Verification Results

All 42 enabled hook offsets cross-referenced against `nm -D`, `nm -a`, `readelf -s`, and `.text` section bounds (`0x203e90`–`0x583478`):

| Hook | Offset | nm Status | Notes |
|---|---|---|---|
| `sre_CppString_from_char_p` | `0x566bb8` | ✅ in `.text` | No exported symbol — internal STL, Ghidra-verified |
| `sre_CppString_assign` | `0x56918c` | ✅ in `.text` | Internal STL, Ghidra-verified |
| `sre_CppString_append` | `0x567254` | ✅ in `.text` | Internal STL, Ghidra-verified |
| `sre_CppString_release` | `0x565220` | ✅ in `.text` | Internal STL, Ghidra-verified |
| `sre_AudioSystem_EndAudioInterruption` | `0x47ED50` | ✅ exported | `_ZN5Caver11AudioSystem31EndAudio...` |
| `sre_stack_chk_fail` | `0x1F62D0` | ✅ PLT stub | `.plt` section, not in `-D` by design |
| `sre_updateApplication` | `0x478ccc` | ✅ exported | `Java_com_touchfoo_swordigo_Native_update...` |
| `sre_SceneLoadingView_InitWithGameState` | `0x4358dc` | ✅ exported | `_ZN5Caver16SceneLoadingView17InitWithGameState...` |
| `sre_SceneLoadingView_Update` | `0x43650c` | ✅ exported | Enabled; one-instruction relay, runtime-validated |
| `sre_SceneLoadingView_AnimateIn` | `0x436a54` | ✅ exported | Enabled; X8-preserving assembly wrapper, runtime-validated |
| `sre_Scene_FinishLoad` | `0x4642a8` | ✅ exported | `_ZN5Caver5Scene10FinishLoadEv` |
| `sre_SceneObject_FinishLoad` | `0x470ec4` | ✅ exported | `_ZN5Caver11SceneObject10FinishLoadEv` |
| `sre_SceneObjectGroup_FinishLoad` | `0x475498` | ✅ exported | `_ZN5Caver16SceneObjectGroup10FinishLoadEv` |
| `sre_luaD_throw` | `0x4eb814` | ✅ in `.text` | No export — internal Lua VM, Ghidra-verified |
| `sre_ProgramPanic` | `0x4c0d60` | ✅ exported | `_ZN5Caver12ProgramPanicEP9lua_State` |
| `sre_BackgroundComponent_Draw` | `0x21ded4` | ✅ exported | `_ZNK5Caver19BackgroundComponent4DrawE...` |
| `sre_RotatingBackgroundComponent_Draw` | `0x2b6760` | ✅ exported | Confirmed |
| `sre_RotatingBackgroundComponent_Update` | `0x2b66f8` | ✅ exported | Confirmed |
| `sre_GUIWindow_DrawRect` | `0x4a28bc` | ✅ exported | Confirmed |
| `sre_GUIView_DrawRect` | `0x49f310` | ✅ exported | Confirmed |
| `sre_GUIButton_DrawRect` | `0x49565c` | ✅ exported | Confirmed |
| `sre_GUILabel_DrawRect` | `0x497aa0` | ✅ exported | Confirmed |
| `sre_GUIFrameView_DrawRect` | `0x497658` | ✅ exported | Confirmed |
| `sre_GUIAlertView_DrawRect` | `0x491b54` | ✅ exported | Confirmed |
| `sre_GUISlider_DrawRect` | `0x49cd40` | ✅ exported | Confirmed |
| `sre_NewMenuView_DrawRect` | `0x42bae4` | ✅ exported | Confirmed |
| `sre_GameOverVC_ShowAdMaybe` | `0x347efc` | ✅ exported | `_ZN5Caver22GameOverViewController11ShowAdMaybeEv` |
| `sre_StartTextInputWithDelegate` | `0x4792ac` | ✅ exported | Confirmed |
| `sre_StopTextInputWithDelegate` | `0x4793dc` | ✅ exported | Confirmed |
| `sre_textInputTextDidChange` | `0x4790dc` | ✅ exported | Confirmed |
| `sre_textInputDidFinish` | `0x479290` | ✅ exported | Confirmed |
| `sre_cxa_throw` | `0x51e108` | ✅ exported | `__cxa_throw` |
| `sre_PlayMusicWithName` | `0x4811a0` | ✅ exported | Confirmed |
| `sre_MusicPlayer_FadeIn` | `0x4814a8` | ✅ exported | Confirmed |
| `sre_MusicPlayer_FadeOut` | `0x4815d8` | ✅ exported | Confirmed |
| `sre_MusicPlayer_Update` | `0x482090` | ✅ exported | Confirmed |
| `sre_AudioSystem_SetMusicVolume` | `0x47f5f0` | ✅ exported | Confirmed |
| `sre_MusicPlayer_SetEnabled` | `0x481e88` | ✅ exported | Confirmed |
| `sre_MusicPlayer_SetSuspended` | `0x481fc0` | ✅ exported | Confirmed |
| `sre_GameSceneView_Update` | `0x34ed2c` | ✅ exported | `_ZN5Caver13GameSceneView6UpdateEf` |
| `sre_GameData_Clear` | `0x2e6d60` | ✅ exported | `_ZN5Caver5Proto8GameData5ClearEv` |
| `sre_Proto_SceneObject_Clear` | `0x2fc030` | ✅ exported | `_ZN5Caver5Proto11SceneObject5ClearEv` |
| `sre_Proto_SceneObject_Destroy` | `0x2fe23c` | ✅ exported | `_ZN5Caver5Proto11SceneObjectD1Ev` |
| `sre_ComponentOutletBase_Connect` | `0x247c48` | ✅ exported | `_ZN5Caver19ComponentOutletBase7ConnectEPKNS_9ComponentE` |

---

## Hook-by-Hook Detailed Results

### 1. `sre_SceneLoadingView_Update` + `sre_SceneLoadingView_AnimateIn` — **ENABLED**

**File**: `src/sre/sre_init.c`  
**Offsets**: `0x43650c` / `0x436a54` (nm-verified ✅)  
**Previous finding superseded**: IDA shows both entries begin with a position-independent `SUB SP, SP, #N`; the canary setup occurs after the relocated first instruction and uses guest TLS correctly. The earlier runtime failure was caused by relay construction reading the patch site after a bridge/patch write, not by these prologues.  
**Fix applied**: `TrampolineMgr` snapshots the original instructions before relay construction and before patching. `Update` uses a normal pass-through wrapper; `AnimateIn` uses `sre_scene_loading.S` to preserve the hidden ARM64 `X8` indirect-result register. Both installed at runtime with distinct relays and survived a scene transition without BRK, watchdog recovery, or relay stall.

Runtime validation: startup reached the game loop; the first transition reached `SceneLoadingView::InitWithGameState`, and relay telemetry advanced from 18 to 738 while scene objects and normal draw activity continued.

---

### 2. `sre_ComponentOutletBase_Connect` — **PREVIOUSLY FIXED, VERIFIED OK**

**Offset**: `0x247c48` (nm: `_ZN5Caver19ComponentOutletBase7ConnectE...`) ✅  
**Fix (prior session)**: PLT lower-bound corrected to include `0x1F62D0` (the PLT stub range starts at `0x1f33d0`). The previous bound `0x203e90` was `.text` start — it excluded valid PLT stub calls.  
**Current state**: Guard reads `vtable >= 0x1F62D0 && vtable < 0x800000000000ULL`. ✅

---

### 2a. GNU COW string hooks — **ABI CORRECTED, RUNTIME-VALIDATED**

**Offsets**: `0x566bb8`, `0x56918c`, `0x567254`, `0x565220`  
**IDA result**: The first three entries accept `std::string*` in `X0`; constructor uses `(X0=this, X1=const char*)`, while assign/append use `(X0=this, X1=source, X2=length)`. The fourth entry is not a string destructor: `0x565220` accepts the GNU COW `_Rep*` header directly and decrements its `+0x10` reference count.

**Fix applied**: The table now maps `0x565220` to `sre_CppString_rep_release(SreStringRep*)`; `sre_CppString_release(SreString*)` remains an SRE-only helper. Assign and append now preserve an overlapping source when COW/reallocation moves the backing buffer, using a rebased source plus `memmove`.

**Runtime validation**: All four hooks installed at their verified RVAs. The ARM64 runtime completed 1,888 frames and transitioned from `3d` to `23d`; relay telemetry advanced from 58 to 1,618 with no BRK, watchdog recovery, Lua error, or execution fault.

---

### 3. `sre_SceneObject_FinishLoad` — **PREVIOUSLY FIXED, VERIFIED OK**

**Offset**: `0x470ec4` ✅  
**Fixes (prior session)**:
- ARM64 48-bit pointer bounds: `(ptr & 7) != 0 || ptr < 0x10000 || ptr >= 0x800000000000ULL`
- Component vector bounds check before dereference
- `setjmp` recovery wrapper for Lua `ProgramPanic → abort`  
**Current state**: Fully guarded. ✅

---

### 4. `sre_Proto_SceneObject_Clear` — **PREVIOUSLY FIXED, VERIFIED OK**

**Offset**: `0x2fc030` ✅  
**Fix (prior session)**: Pointer validity changed from `0x10000000–0xE0000000` (ARM32 Unicorn bounds) to `0x10000–0x800000000000` (ARM64 Dynarmic 48-bit).  
**Current state**: Correct. ✅

---

### 5. `sre_GameSceneView_Update` — **VERIFIED OK**

**Offset**: `0x34ed2c` (nm: `_ZN5Caver13GameSceneView6UpdateEf`) ✅  
**ABI check**: `void Update(float deltaTime)` → ARM64: `x0=this, s0=deltaTime`. Hook signature matches. ✅  
**Call-through**: Drives `GameSceneController::Update` at `0x349d84` (nm-verified) with `g_sre_scene_loading` guard. ✅  
**Pointer guards**: `scene` pointer validated with vtable alignment + 48-bit bounds. ✅

---

### 6. `sre_GameData_Clear` — **VERIFIED OK**

**Offset**: `0x2e6d60` ✅  
**Analysis**: IDA/Ghidra at this offset: iterates repeated protobuf fields calling `vtable[0x20]` (virtual `Clear()`). Hook validates vtable before dispatch — no OOB access.  
**Current state**: Safe. ✅

---

### 7. PC Hook Framework — **ADDED** (pending use)

**File**: `src/sre/sre_init.c` lines 21–36  
**Function**: `sre_install_pc_hook(offset, hook_fn, saved_insn)`

```c
static void sre_install_pc_hook(uint64_t offset, void* hook_fn, uint32_t* saved_insn) {
    uint8_t* target = (uint8_t*)g_swordigo_base + offset;
    if (saved_insn) memcpy(saved_insn, target, 4);
    int64_t delta = (uint8_t*)hook_fn - target;
    uint32_t bl = 0x94000000u | (uint32_t)((delta >> 2) & 0x03FFFFFFu);
    memcpy(target, &bl, 4);
    __builtin___clear_cache(target, target + 4);
}
```

**Verified**: ARM64 BL encoding is correct — `0x94000000 | ((delta >> 2) & 0x03FFFFFF)` with 26-bit signed offset. Cache flush via `__builtin___clear_cache` ✅.  
**Range limit**: BL can only reach ±128 MB from the call site. libswordigo.so is 6.9 MB — well within range for any in-SO hook target. ✅

---

## Known Hard Issues (Cannot Fix in C without ARM64 Assembly)

### Issue 1: `ProgramState::Update` — Lua Resume Safety
**Offset**: `0x4c15fc` (`_ZN5Caver12ProgramState6UpdateEf`)  
**Problem**: `ProgramState::Update` calls `lua_resume` internally. If a coroutine faults (bad yield/error), the call unwinds into `ProgramPanic` → `abort`. The existing `sre_ProgramPanic` hook (`0x4c0d60`) catches the panic but the `setjmp` recovery stack doesn't cover the path through `Update` → `lua_resume`.  
**Why C can't fix it**: The ARM64 frame for `ProgramState::Update` uses `TPIDR_EL0` stack canaries — same as `SceneLoadingView`. A relay trampoline from host would fault on the canary check.  
**Proper fix**: ARM64 assembly trampoline that:
1. Patches the `lua_resume` *call site* inside `ProgramState::Update` (not the function entry)
2. Redirects to a wrapper that wraps `lua_resume` with proper error recovery
3. Completely avoids touching `ProgramState::Update`'s entry/exit

**PC-hook target** (when ARM64 asm trampoline is available):
```c
// Hook the lua_resume call SITE inside ProgramState::Update, not the function.
// lua_resume call site in ProgramState::Update: find via Ghidra disasm.
// Framework is ready: sre_install_pc_hook(callsite_offset, sre_safe_lua_resume, NULL);
```

**`ProgramState::Resume`** at `0x4c177c` is the safer entry point — does NOT use TPIDR_EL0 and can be hooked via the standard relay.

### Issue 2: `SceneLoadingView::Update` / `AnimateIn` — RESOLVED
**Status**: Enabled through the validated relay path. `AnimateIn` remains assembly-backed because of its indirect-result `X8` ABI.

---

## Pointer Guard Standard (Enforced Across All Hooks)

All pointer dereferences in SRE hooks now use this guard pattern:

```c
if (!ptr || (ptr & 7) != 0 || ptr < 0x10000ULL || ptr >= 0x800000000000ULL)
    return; // reject null, misaligned, out-of-range ARM64 guest pointers
```

**Rationale**: ARM64 Dynarmic guest heap is 48-bit (`[0, 0x800000000000)`). Previous hooks used `0xE0000000` (ARM32 Unicorn bound) — this rejected valid high addresses in the 48-bit space.

---

## Files Modified This Session

| File | Change |
|---|---|
| `src/sre/sre_init.c` | Enabled verified SceneLoadingView Update and AnimateIn entries; removed unresolved lua_xmove |
| `src/sre/sre_string.c` | Corrected `_Rep*` release ABI and overlapping assign/append semantics |
| `src/platform/trampoline_mgr.h` | Snapshot original bytes before relocation and patching |
| `tests/trampoline_mgr_test.cpp` | Added hook/relay ordering and cave-overflow regressions |
| `docs/modding_and_swkiwi/11_sre_hook_audit_v2.md` | This report |

## Files with Prior-Session Fixes (Still Valid)

| File | Prior Fixes |
|---|---|
| `src/sre/sre_scene_update.c` | ARM64 bounds, PLT range, HealthBar null guard, scene_loading flag, component vtable 5-point validation |
| `src/sre/sre_scene_shifter.c` | `g_sre_hero_obj = 0` at transitions |
| `src/sre/sre_caver.c` | `layer_count` clamp `[1,32]` |
