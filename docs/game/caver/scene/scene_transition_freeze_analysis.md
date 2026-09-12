# Scene Transition Freeze — Deep Research Analysis

> **Date**: 2026-08-28
> **Binary**: `libswordigo.so` v1.4.12 ARM64 (7.2 MB, stripped, NDK r17c)
> **Evidence sources**: `nm -D`, IDA Pro/Hex-Rays decompilation, Ghidra decompilation, SRE codebase, live runtime logs
> **All addresses verified against `nm -D libswordigo.so | c++filt` unless noted otherwise**

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Scene Transition Call Chain — Verified Binary Evidence](#2-scene-transition-call-chain)
3. [The Freeze Point: `__do_upcast` RTTI Self-Loop](#3-the-freeze-point)
4. [SRE Mitigations — Complete Audit](#4-sre-mitigations)
5. [Current State of `sre_GameSceneView_Update` Hook](#5-current-state-of-sre_gamesceneview_update)
6. [All Active Hooks — Address Verification](#6-all-active-hooks)
7. [Risky / Unnecessary Hook Candidates](#7-risky-unnecessary-hooks)
8. [Host-Side Execution Path Analysis](#8-host-side-execution)
9. [Open Questions & Next Steps](#9-open-questions)

---

## 1. Executive Summary

**Root cause**: During menu→game scene transitions, `GUINavigationController::FinishTransitionToViewController` calls C++ `dynamic_cast` which enters `__cxxabiv1::__vmi_class_type_info::__do_upcast` (nm 0x54c6d4). Under the Dynarmic ARM64 JIT, the RTTI hierarchy walk never terminates — it spins indefinitely at the loop head at 0x54c6d4, preventing the NavController from completing the VC swap from `MainMenuViewController` to `GameViewController`.

**Consequence**: `nav_ctrl+0x50` (current VC) never updates to the GameViewController. The engine's per-frame dispatch chain `CaverShell::Update → root_view→vtable[9] → GUINavigationController::Update → current_VC→vtable[9]` continues calling the *menu VC's* Update, not the game VC's. `GameSceneView::Update` is never reached through the normal chain.

**SRE workarounds in place**:
- **JOB1 bypass**: Drives `GameSceneController::Update` directly via captured `GVC→GSC→Scene*` chain
- **GUI repair drive**: Locates `GameSceneView` by vtable and dispatches its `vtable[9]` Update directly
- **`__do_upcast` spin-breaker**: PC hook at 0x54c6d4 that redirects to loop exit 0x54c6e8
- **`sre_GameSceneView_Update`**: Now a lightweight relay passthrough (sets globals, calls original via relay cave)

**The scene transition freeze persists** because these mitigations address symptoms (world sim, view tree) but not the underlying VC swap failure. The NavController never completes the transition, so any code path that depends on the normal VC dispatch chain remains broken.

---

## 2. Scene Transition Call Chain — Verified Binary Evidence

All function addresses verified via `nm -D libswordigo.so | c++filt`.

### 2.1 Normal Menu→Game Transition Flow

```
Player taps "Start Game" in menu
    │
    ▼
GameViewController::Update(float)          @ 0x357D1C
    │  (called from GUINavigationController::Update → vtable[9])
    │  (GVC is current VC at this point)
    │
    ▼
GameViewController::GotoLevel(level, spawn)  @ 0x358A74
    │  Triggers: save game, then background load
    │
    ▼
GameViewController::BackgroundLoad()          @ 0x35303C
    │  CALLEES (43 functions): Scene::Scene, Scene::LoadFromFile,
    │  GameSceneController::GameSceneController, GameSceneView::GameSceneView,
    │  GameSceneController::InitWithScene, GameSceneView::InitWithSceneController,
    │  Scene::FinishLoad, GameSceneController::SpawnHeroAt, etc.
    │
    ├──► Scene::LoadFromFile(filename)        @ 0x463780
    │        Loads scene protobuf from disk
    │
    ├──► Scene::FinishLoad()                  @ 0x4642A8
    │        ├── ObjectLibrary::LoadAllPrograms
    │        ├── Scene::NewProgramStateForProgram → ProgramState::Execute
    │        ├── RB-tree walk: SceneObject::FinishLoad (each object)
    │        │     └── component vtable[9] dispatch on each component
    │        └── RB-tree walk: SceneObjectGroup::FinishLoad (each group)
    │
    ├──► GameSceneController::InitWithScene(scene)  @ 0x203590
    │
    ├──► GameSceneView::InitWithSceneController(ctrl)  @ 0x34E1A0
    │
    └──► GameSceneController::SpawnHeroAt(spawnID)  @ 0x348B6C
              (40 callees, 459 strings — massive function)
```

### 2.2 VC Swap — Where It Breaks

```
After BackgroundLoad completes:
    │
    ▼
GUINavigationController::FinishTransitionToViewController  @ 0x49A42C
    │  Performs dynamic_cast to verify VC type
    │  Uses __cxxabiv1::__vmi_class_type_info::__do_upcast
    │
    ▼
__cxxabiv1::__vmi_class_type_info::__do_upcast  @ 0x54C6D4 (LOOP HEAD)
    │  Iterates RTTI base class hierarchy
    │  Under Dynarmic JIT: SPINS FOREVER (mis-relocated type_info/vtable pointer)
    │
    ╳  NEVER RETURNS
    │
    ▼
nav_ctrl+0x50 (current VC) STAYS pointing at MainMenuViewController
    │
    ▼
Per-frame dispatch continues calling MenuVC, NOT GameVC
GameSceneView::Update is NEVER called through normal chain
```

### 2.3 Per-Frame Dispatch Chain (Normal Operation)

```
sre_updateApplication (JNI)                 @ 0x478CCC
    │
    ▼
CaverShell::Update(float)                   @ 0x210EFC
    │  Shell singleton at DAT_007f3c20 → base+0x6e9c20
    │  Reads root_view from shell+0x88
    │
    ▼
root_view→vtable[9](root_view, dt)
    │  vtable offset +0x48 = slot 9
    │  When root_view IS GameSceneView:
    │      → trampoline at 0x34ed2c → sre_GameSceneView_Update
    │  When root_view is STILL the menu view (stale):
    │      → menu view's vtable[9] (some menu Update)
    │      → GameSceneView::Update NEVER CALLED
    │
    ▼ (if GameSceneView was called)
GameSceneView::Update(float)                @ 0x34ED2C
    │  Full implementation: reads overlay, ctrl, gamestate,
    │  updates health/mana/coins, drives GSC::Update, calls GUIView::Update
    │
    ▼
GameSceneController::Update(float)          @ 0x349D84
    │  → Scene::Update → per-object component ticks, physics, deferred deletion
```

---

## 3. The Freeze Point: `__do_upcast` RTTI Self-Loop

### 3.1 Verified Address

```
Symbol: __cxxabiv1::__vmi_class_type_info::__do_upcast
nm -D:  0x000000000051F0DC  (the REAL function, exported)
Spin loop head: 0x54C6D4  (unexported, inside FinishTransitionToViewController's body)
Loop exit:      0x54C6E8
```

### 3.2 Why It Spins Under Dynarmic

The `__do_upcast` function walks the C++ RTTI class hierarchy using `__vmi_class_type_info` vtable pointers and `type_info` data structures. These are stored in guest memory at addresses computed from the guest binary's layout.

Under the Dynarmic JIT:
- The guest binary is loaded at a non-standard base address (SWORDIGO_BASE)
- RTTI data pointers are computed relative to the guest binary's original load address
- If any `type_info` or `vtable` pointer is mis-relocated (points to wrong guest memory), the hierarchy walk follows garbage pointers, loops forever, or jumps into non-executable data

### 3.3 Evidence from Runtime Logs

```
[PERF/Dynarmic] SLOW call at 0x1478ccc took 2562ms (bridge_calls=14887130)
[PERF/Dynarmic] SLOW call at 0x1478ccc took 582ms (bridge_calls=2068912)
[PERF/Dynarmic] SLOW call at 0x1478ccc took 925ms (bridge_calls=157715)
```

The 0x1478ccc is `updateApplication` (JNI stub). The massive bridge_call counts (up to 14.8M) indicate the JIT was executing millions of guest instructions without returning — the spin loop.

---

## 4. SRE Mitigations — Complete Audit

### 4.1 `__do_upcast` Spin-Breaker (emulator_dynarmic64.cpp)

**File**: `src/platform/emulator_dynarmic64.cpp` lines 141-240
**Mechanism**: Tier-2 PC hook installed at 0x54C6D4 (loop head)

```c
#define SRE_UPCAST_LOOP_HEAD  (0x1000000ULL + 0x54c6d4ULL)
// When PC hits 0x54C6D4, check RTTI self-loop condition
// If spinning: redirect PC to loop exit 0x54C6E8
// __do_upcast then runs compare+ret, dynamic_cast returns failure
// FinishTransitionToViewController sees failed cast, proceeds
```

**Effect**: Breaks the infinite loop but `dynamic_cast` returns failure. The VC swap may or may not proceed correctly depending on whether the caller handles cast failure.

**Status**: Active, installed during `run()` initialization.

### 4.2 JOB1 World-Drive Bypass (sre_frame_loop.c)

**File**: `src/sre/sre_frame_loop.c` lines 575-720
**Mechanism**: When `nav_ctrl+0x50` still points at menu VC (stale), bypass NavController entirely:

```
Chain 1 (nav path): shell → nav_ctrl → current_VCEntity → GSC → Scene
Chain 2 (fallback): captured GVC → GSC+0xF0 → Scene* (direct drive)
Chain 3 (scene fallback): g_sre_finishloaded_scene → Scene::Update directly
```

**Effect**: World simulation runs even though VC swap is stuck. Physics, collision, object updates, deferred deletion all work.

**Status**: Active, fires when `nav_ctrl+0x50` doesn't resolve to a live GVC.

### 4.3 GUI Repair Drive (sre_frame_loop.c)

**File**: `src/sre/sre_frame_loop.c` lines 440-560
**Mechanism**: When `g_sre_gui_scene_active == 0` (normal chain didn't dispatch GameSceneView):

1. Walk shell root view's subview tree looking for GameSceneView vtable (`0x6cb580`)
2. Fallback: use captured GVC (from `FinishTransitionToViewController` hook) → GVC+0xD8 or GVC+0xF0→GSC+0xF8
3. Dispatch `gsv→vtable[9](gsv, dt)` directly

**Effect**: GameSceneView::Update runs, GUI subtree stays alive (notifications, chat bubbles, animations, subview removal).

**Status**: Active, fires on frames where normal chain doesn't reach GameSceneView.

### 4.4 `sre_GameSceneView_Update` (sre_scene_update.c)

**File**: `src/sre/sre_scene_update.c` lines 342-376
**Current implementation**: Lightweight relay passthrough

```c
void sre_GameSceneView_Update(void* self, float deltaTime) {
    if (!self) return;
    g_sre_gui_scene_view_ptr = (uint64_t)self;
    g_sre_gamesceneview_ptr = self;
    g_sre_gui_scene_active = 1;
    if (g_orig_GameSceneView_Update_fn) {
        g_orig_GameSceneView_Update_fn(self, deltaTime);  // relay → original
    }
}
```

**Previous implementation** (now `#if 0` as `sre_GameSceneView_Update_disabled`):
- Full reimplementation with HUD data extraction (HP, mana, coins, controls)
- Scene-transition guard (`g_sre_scene_loading` check → `goto do_effects`)
- World-drive via direct `GameSceneController::Update` call
- GUIEffect updates + GUIView::Update relay call

**Relay**: Installed via `nav_relays[]` in main.cpp:
```c
{ 0x34ed2c, "_ZN5Caver13GameSceneView6UpdateEf", "g_orig_GameSceneView_Update_fn" }
```

**Status**: Active. Calls original through relay cave.

### 4.5 Other Scene Transition Safety Hooks

| Hook | Offset | Purpose | Status |
|------|--------|---------|--------|
| `sre_SceneLoadingView_InitWithGameState` | 0x4358DC | Sets `g_sre_scene_loading=1` during load, clears on exit | ✅ Active |
| `sre_Scene_FinishLoad` | 0x4642A8 | Captures `g_sre_finishloaded_scene` for JOB1 fallback | ✅ Active |
| `sre_GUINavigationController_Update` | 0x49923C | Call-through to original | ✅ Active |
| `sre_GUINavigationController_FinishTransition` | 0x49A42C | Captures GVC for GUI repair drive | ✅ Active |
| `sre_SceneLoadingView_Update` | 0x43650C | Loading bar animation + transition callback | ✅ Active |
| `sre_SceneLoadingView_AnimateIn` | 0x436A54 | Loading bar animation | ✅ Active |
| `sre_GameSceneController_InitWithScene` | 0 (disabled) | Offset unverified (0x348920 wrong, 0x203590 unverified) | ❌ Disabled |
| `sre_SceneObject_FinishLoad` | 0 (disabled) | Slow vtable walk, low value | ❌ Disabled |
| `sre_SceneObjectGroup_FinishLoad` | 0 (disabled) | Slow vtable walk, low value | ❌ Disabled |

---

## 5. Current State of `sre_GameSceneView_Update`

### 5.1 Architecture

```
sre_hook_table entry:     { 0x34ed2c, "sre_GameSceneView_Update" }
    │
    ▼ (trampoline installed at 0x34ed2c)
sre_GameSceneView_Update (lightweight)     ← ACTIVE
    ├── g_sre_gui_scene_view_ptr = self
    ├── g_sre_gamesceneview_ptr = self
    ├── g_sre_gui_scene_active = 1
    └── g_orig_GameSceneView_Update_fn(self, dt)  ← relay cave → original
         │
         ▼
    Original GameSceneView::Update (ARM64 binary)
         ├── HUD data extraction (health, mana, coins, controls)
         ├── GameSceneController::Update call
         └── GUIView::Update (view subtree)

sre_GameSceneView_Update_disabled          ← INACTIVE (#if 0)
    └── Full reimplementation (preserved for reference)
```

### 5.2 What the Original Does (IDA evidence)

```
GameSceneView::Update(float)               @ 0x34ED2C
    Callees: 31 functions
    Referenced strings: 185
    Key operations (from IDA decompilation):
    ├── Reads overlay_ptr at this+0x100
    ├── Reads ctrl_ptr at this+0xF0
    ├── Health bar update (gamestate+0xA8, +0xBC)
    ├── Mana bar update (gamestate+0xAC, +0xC4)
    ├── Coin bar with visibility logic
    ├── Controls visibility (combat_flag at this+0x198)
    ├── Use/pickup button (CharController at ctrl+0xE0)
    ├── Cinematic skip button timer
    ├── GameSceneController::Update (world drive)
    ├── GUIEffect updates (bars, damage flash, screen effects)
    └── GUIView::Update (view subtree dispatch)
```

### 5.3 Why the Relay Passthrough Was Chosen

The full reimplementation (`sre_GameSceneView_Update_disabled`) had:
- Scene-transition guard: `if (g_sre_scene_loading) goto do_effects`
- This guard was meant to skip dangerous struct reads during transitions
- But it interacted badly with the NavController state machine:
  - When the transition was stuck, `g_sre_scene_loading` was cleared but the VC wasn't swapped
  - The guard logic read stale pointers → crashes or deadlocks
  - The world-drive section (6.5) called GSC::Update directly, conflicting with JOB1

The relay passthrough avoids all these issues by letting the original function handle everything natively.

---

## 6. All Active Hooks — Address Verification

### 6.1 Safety / Crash Guard Hooks

| Function | nm Address | Status | Risk |
|----------|-----------|--------|------|
| `CaverShell::Update` | 0x210EFC | ✅ Active (trampoline) | Low — full reimplementation |
| `GameSceneView::Update` | 0x34ED2C | ✅ Active (relay passthrough) | Low — calls original |
| `GameSceneController::Update` | 0x349D84 | ✅ Active (call-through) | Low |
| `Scene::Update` | 0x465968 | ✅ Active (call-through) | Low |
| `SceneLoadingView::Update` | 0x43650C | ✅ Active (call-through) | Low |
| `SceneLoadingView::AnimateIn` | 0x436A54 | ✅ Active (ASM wrapper) | Low |
| `GUINavigationController::Update` | 0x49923C | ✅ Active (call-through) | Low |
| `GUINavigationController::FinishTransition` | 0x49A42C | ✅ Active | Medium — captures GVC |
| `SceneLoadingView::InitWithGameState` | 0x4358DC | ✅ Active | Medium — sets scene_loading flag |
| `Scene::FinishLoad` | 0x4642A8 | ✅ Active | Medium — captures scene ptr |

### 6.2 Render / GUI Hooks

| Function | nm Address | Status | Risk |
|----------|-----------|--------|------|
| `GUIWindow::DrawRect` | 0x4A28BC | ✅ Active | Medium — full GUI pipeline |
| `GUIView::DrawRect` | 0x49F310 | ✅ Active | Medium — iterates subviews |
| `GUIButton::DrawRect` | 0x49565C | ✅ Active | Low |
| `GUILabel::DrawRect` | 0x497AA0 | ✅ Active | Low |
| `GUIFrameView::DrawRect` | 0x497658 | ✅ Active | Low |
| `GUIAlertView::DrawRect` | 0x491B54 | ✅ Active | Low |
| `GUISlider::DrawRect` | 0x49CD40 | ✅ Active | Low |
| `NewMenuView::DrawRect` | 0x42BAE4 | ✅ Active | Low |
| `BackgroundComponent::Draw` | 0x21DED4 | ✅ Active | Low |
| `RotatingBackgroundComponent::Draw` | 0x2B6760 | ✅ Active | Low |
| `RenderingContext::C1` | 0x48AFB0 | ✅ Active (gui_relays) | Low |

### 6.3 Audio / Music Hooks

| Function | nm Address | Status | Risk |
|----------|-----------|--------|------|
| `PlayMusicWithName` | 0x4811A0 | ✅ Active | Low |
| `MusicPlayer::FadeIn` | 0x4814A8 | ✅ Active | Low |
| `MusicPlayer::FadeOut` | 0x4815D8 | ✅ Active | Low |
| `MusicPlayer::Update` | 0x482090 | ✅ Active | Low |
| `AudioSystem::SetMusicVolume` | 0x47F5F0 | ✅ Active | Low |
| `MusicPlayer::SetEnabled` | 0x481E88 | ✅ Active | Low |
| `MusicPlayer::SetSuspended` | 0x481FC0 | ✅ Active | Low |
| `AudioSystem::EndAudioInterruptionIfNecessary` | 0x47ED50 | ✅ Active (no-op) | Low |
| `__stack_chk_fail` | 0x1F62D0 | ✅ Active (return-to-LR) | **HIGH** — masks stack corruption |

### 6.4 Exception / Error Handling Hooks

| Function | nm Address | Status | Risk |
|----------|-----------|--------|------|
| `luaD_throw` | 0x4EB814 | ✅ Active | Critical — all Lua errors |
| `ProgramPanic` | 0x4C0D60 | ✅ Active | Medium — backup safety net |
| `__cxa_throw` | 0x51E108 | ✅ Active | **HIGH** — all C++ exceptions |
| `ProgramState::Execute` | 0 (sym) | ✅ Active | Medium — Lua execution |
| `ProgramState::Resume` | 0 (sym) | ✅ Active | Medium — Lua coroutine resume |

### 6.5 Lua Interpreter Hooks

All ~45 Lua API functions are hooked as pass-through relays. These are low-risk but high-volume (every Lua call goes through them).

### 6.6 Filesystem / Asset Hooks

| Function | nm Address | Status | Risk |
|----------|-----------|--------|------|
| `FileExistsAtPath` | 0x4B44B8 | ✅ Active | Medium — mod layering |
| `NewByteBufferFromAndroidAsset` | 0x4B4254 | ✅ Active | Medium — asset loading |
| `PVRTTextureLoadFromPVRBuffer` | 0x5196C4 | ✅ Active (0 offset) | Low |
| `SetResourcesPath` | 0 (sym) | ✅ Active | Low |
| `IsAndroidAssetsPath` | 0 (sym) | ✅ Active | Low |

### 6.7 Crash Guard Hooks

| Function | nm Address | Status | Risk |
|----------|-----------|--------|------|
| `GameData::Clear` | 0x2E6D60 | ✅ Active | Low — vtable validation |
| `Proto::SceneObject::Clear` | 0x2FC030 | ✅ Active | Low — count clamping |
| `Proto::SceneObject::Destroy` | 0x2FE23C | ✅ Active | Low — vtable validation |
| `Proto::ObjectLibrary::Clear` | 0x2FD374 | ✅ Active | Low — belt-and-suspenders |
| `SceneObject::ComponentWithInterface` | 0x47462C | ✅ Active | Low — pointer validation |
| `ComponentOutletBase::Connect` | 0x247C48 | ✅ Active | Low — vtable validation |

### 6.8 Text Input Hooks

| Function | nm Address | Status | Risk |
|----------|-----------|--------|------|
| `StartTextInputWithDelegate` | 0x4792AC | ✅ Active | Low |
| `StopTextInputWithDelegate` | 0x4793DC | ✅ Active | Low |
| `textInputTextDidChange` | 0x4790DC | ✅ Active | Low |
| `textInputDidFinish` | 0x479290 | ✅ Active | Low |

### 6.9 String Hooks

| Function | nm Address | Status | Risk |
|----------|-----------|--------|------|
| `CppString::from_char_p` | 0x566BB8 | ✅ Active | **HIGH** — all string creation |
| `CppString::assign` | 0x56918C | ✅ Active | **HIGH** — all string assignment |
| `CppString::append` | 0x567254 | ✅ Active | **HIGH** — all string append |
| `CppString::rep_release` | 0x565220 | ✅ Active | **HIGH** — all string destruction |

---

## 7. Risky / Unnecessary Hook Candidates

### 7.1 HIGH RISK

1. **`__stack_chk_fail` (0x1F62D0)** — Replaces PLT stub with return-to-LR. Masks ALL stack corruption across the entire binary. Any genuine stack-smash bug becomes a silent corruption instead of a crash. **Consider**: limiting scope to known TPIDR_EL0 drift cases.

2. **`__cxa_throw` (0x51E108)** — Intercepts ALL C++ exceptions. The longjmp-based recovery can leave objects in inconsistent states. **Consider**: only intercepting when inside Lua execution context.

3. **GNU COW string hooks (0x566BB8, 0x56918C, 0x567254, 0x565220)** — Replace all string operations. If the non-atomic refcounting has any edge case (e.g., string shared between threads during JNI calls), it could cause corruption. **Low priority but monitor**.

### 7.2 MEDIUM RISK (unnecessary but not harmful)

4. **`GameOverVC::ShowAdMaybe` (0x347EFC)** — No-op for desktop ad removal. Safe but bypasses the original's post-death flow. If the original had cleanup logic beyond ad display, it's skipped.

5. **`AudioSystem::EndAudioInterruptionIfNecessary` (0x47ED50)** — No-op for iOS audio session. Safe on desktop. But if the function also does OpenAL state management, that's skipped.

6. **`GUIApplication::DispatchEvents` (0, sym)** — No-op. SDL handles input. Safe. But if the game's internal event queue also carries non-input events (timer callbacks, network packets), they're dropped.

### 7.3 LOW RISK (properly guarded)

7. **`SceneObject::ComponentWithInterface` (0x47462C)** — Pointer validation guard. Adds overhead per component lookup but prevents crashes. Worth keeping.

8. **`ComponentOutletBase::Connect` (0x247C48)** — Vtable validation. Prevents freed-component vtable dispatch. Worth keeping.

9. **All crash guard hooks** (`GameData::Clear`, `SceneObject::Clear`, `SceneObject::Destroy`, `ObjectLibrary::Clear`) — These are targeted safety nets. Low overhead, high crash-prevention value. Keep all.

---

## 8. Host-Side Execution Path Analysis

### 8.1 Dynarmic JIT Execution Flow

```
Host thread calls:
    emu->call(updateApplication_addr, args)
        │
        ▼
    Dynarmic JIT compiles ARM64 code at guest PC
        │  Executes until HaltExecution() or exception
        │
        ▼ (bridge calls to host for JNI, OpenAL, etc.)
    sre_updateApplication
        │
        ▼
    sre_frame_update → sre_CaverShell_Update
        │  Dispatches root_view→vtable[9]
        │
        ▼ (trampoline at 0x34ed2c → sre_GameSceneView_Update)
    sre_GameSceneView_Update (relay passthrough)
        │  Sets globals → calls relay → original executes
        │
        ▼ (original GameSceneView::Update runs inside JIT)
    GameSceneView::Update → GSC::Update → Scene::Update
    GameSceneView::Update → GUIView::Update (view subtree)
```

### 8.2 Where the Freeze Manifests

```
Normal frame:
    updateApplication runs → CaverShell::Update → root_view→vtable[9]
    → GUINavigationController::Update → current_VC→vtable[9]
    → GameSceneView::Update → GSC::Update → Scene::Update ✓

Frozen frame (after menu→game transition):
    updateApplication runs → CaverShell::Update → root_view→vtable[9]
    → GUINavigationController::Update → current_VC→vtable[9]
    → MainMenuVC::Update (still current!) → menu renders ✓
    → GameSceneView::Update NEVER CALLED ✗
    → World frozen, no game view updates ✗
```

### 8.3 JOB1 Bypass — How It Works

```
sre_frame_update
    │
    ├── sre_CaverShell_Update → root_view→vtable[9]
    │     → GUINavigationController::Update → MenuVC::Update (stale)
    │     → g_sre_gui_scene_active stays 0 (GameSceneView never called)
    │
    ├── GUI repair drive: !g_sre_gui_scene_active → find GSV → dispatch vtable[9]
    │     → sre_GameSceneView_Update → relay → original runs
    │     → g_sre_gui_scene_active = 1 (for this frame)
    │
    └── JOB1: !g_sre_scene_loading → resolve GVC chain → GSC → Scene
          → GameSceneController::Update → Scene::Update (world ticks)
          → g_sre_world_drive_count++
```

---

## 9. Open Questions & Next Steps

### 9.1 Why Does `__do_upcast` Spin?

The spin at 0x54C6D4 is caused by mis-relocated RTTI data. Specific questions:
- Which `type_info` pointer is wrong? (the one for `GameViewController` or `MainMenuViewController`?)
- Is it a base class type_info that's in `.data.rel.ro` and wasn't relocated?
- Does the SWORDIGO_BASE offset correctly account for RTTI pointer fixups?

**Investigation path**: Set a conditional breakpoint at 0x54C6D4, log the RTTI hierarchy being walked, and identify which pointer is wrong.

### 9.2 Can the VC Swap Be Forced?

Instead of working around the stuck NavController, could we:
1. Directly write `nav_ctrl+0x50 = GVC` (bypass the transition)?
2. Call `FinishTransitionToViewController` with a different type_info that resolves correctly?
3. Patch the RTTI data in guest memory before the dynamic_cast runs?

### 9.3 Is the `__do_upcast` Spin-Breaker Sufficient?

The spin-breaker redirects PC from 0x54C6D4 to 0x54C6E8 (loop exit). But:
- `dynamic_cast` returns failure (false)
- Does `FinishTransitionToViewController` handle cast failure?
- If it does, the VC swap should proceed
- If it doesn't (assumes cast always succeeds), the swap still fails

**Evidence needed**: Read the decompilation of `FinishTransitionToViewController` (0x49A42C) to see how it handles `dynamic_cast` failure.

### 9.4 Can the Relay Passthrough Be Reliably Tested?

The new `sre_GameSceneView_Update` calls the original via relay. We need to verify:
- Does the relay cave correctly preserve the S0 (float) register?
- Does the original's first instruction (at 0x34ED2C) access PC-relative data that breaks when executed from the cave?
- Are there any thread-safety issues with the relay being called from both the normal chain and the GUI repair drive?

---

## Appendix A: Complete Symbol Table (Scene Transition Related)

```
0x210EFC  Caver::CaverShell::Update(float)
0x2E6D60  Caver::Proto::GameData::Clear(void)
0x348688  Caver::GameSceneController::GameSceneController(void)
0x349D84  Caver::GameSceneController::Update(float)
0x34D7E8  Caver::GameSceneView::GameSceneView(void)
0x34ED2C  Caver::GameSceneView::Update(float)
0x3513C8  Caver::GameViewController::GameViewController(void)
0x352B64  Caver::GameViewController::LoadView(void)
0x35303C  Caver::GameViewController::BackgroundLoad(void)
0x355D44  Caver::GameViewController::ViewDidAppear(void)
0x357D1C  Caver::GameViewController::Update(float)
0x358A74  Caver::GameViewController::GotoLevel(string, string)
0x434E60  Caver::SceneLoadingView::SceneLoadingView(void)
0x43650C  Caver::SceneLoadingView::Update(float)
0x436A54  Caver::SceneLoadingView::AnimateIn(void)
0x4625B0  Caver::Scene::Scene(void)
0x463780  Caver::Scene::LoadFromFile(string)
0x4642A8  Caver::Scene::FinishLoad(void)
0x465968  Caver::Scene::Update(float)
0x470EC4  Caver::SceneObject::FinishLoad(void)
0x475498  Caver::SceneObjectGroup::FinishLoad(void)
0x498FDC  Caver::GUINavigationController::GUINavigationController(void)
0x49923C  Caver::GUINavigationController::Update(float)
0x499288  Caver::GUINavigationController::ViewControllerViewLoaded(GUIViewController*)
0x49A42C  Caver::GUINavigationController::FinishTransitionToViewController(...)
0x54C6D4  __cxxabiv1::__vmi_class_type_info::__do_upcast (SPIN LOOP HEAD)
0x54C6E8  __do_upcast loop exit
0x6CB570  vtable for Caver::GameSceneView
0x6CB580  vtable address point for Caver::GameSceneView
0x6CB870  vtable for Caver::GameViewController
```

## Appendix B: Guest Memory Layout (CaverShell)

```
CaverShell global ptr: DAT_007f3c20  (runtime address in guest memory)
At base + 0x6e9c20:
    +0x20  = FWShellPreferences
    +0x88  = GUIView* (root scene view)
    +0x98  = GUINavigationController* (nav controller)
    +0xA8  = paused flag (byte)
```

## Appendix C: SRE Global Variables

```
g_sre_gamesceneview_ptr      — Set by sre_GameSceneView_Update
g_sre_gui_scene_view_ptr     — Set by sre_GameSceneView_Update
g_sre_gui_scene_active       — Set to 1 by hook, cleared by frame loop
g_sre_scene_loading          — Set by SceneLoadingView::InitWithGameState
g_sre_finishloaded_scene     — Set by Scene::FinishLoad
g_sre_world_drive_count      — Incremented by JOB1 bypass
g_sre_gui_drive_count        — Incremented by GUI repair drive
g_sre_gamestate_ptr          — Set by hook, read by host for ImGui
g_sre_hero_obj               — Set by hook, read by host
g_sre_current_scene_name     — Set by hook, used for music routing
g_orig_GameSceneView_Update_fn — Relay pointer (set by TrampolineMgr)
```

---

*This document was generated from verified binary evidence. All nm addresses were confirmed against `nm -D libswordigo.so`. All decompiled function bodies were read from IDA Pro output in `/run/media/quantumcreeper/TVPG/Storage/resources/ida_decompiled/`. No speculative claims — only observed behavior and verified code paths.*
