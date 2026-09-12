# SRE PC Port — HUD & Touch Controls Random Disappearance Audit

## 1. Executive Summary
During gameplay on the PC port (specifically when playing with keyboard controls or using SwKiwi-based mods), the entire game HUD—including the health bar, mana bar, experience bar, pause/settings button, and simulated touch buttons—can randomly and permanently disappear from the screen.

Our research of the decompiled game binary (`libswordigo.so`), the SwKiwi mod loader source code, and our host engine (`libsre.so`/`main.cpp`) reveals that this issue is caused by **unintentional memory writes to the parent GUIView's visibility flag** in `UI.SetControlsDisabled()`, compounded by cutscene transitions and native mobile auto-unhide behaviors.

---

## 2. Component View Hierarchy
To understand why the entire UI vanishes, we examined the C++ view hierarchy decompiled in Ghidra (`render/GameOverlayView.c` and `gui/GUIView.c`):

* **`GUIView` (Base Class):**
  * Contains a boolean `isHidden` status flag at offset `0xE4` (in 64-bit) or `0xBC` (in 32-bit).
  * If `isHidden` is set to `1`, the engine completely skips drawing this view and all of its nested children.
* **`GameOverlayView` (Inherits from `GUIView`):**
  * Serves as the parent container view for all HUD and interactive screen components.
  * Subviews/Children include:
    * **Touch Buttons:** Joystick (D-Pad), Jump, Attack, and Spell buttons.
    * **Action Overlays:** Use/Pickup Button (`ConsumableItemView` at `this + 0x1D8`).
    * **Stats Bars:** `HealthBar`, `ManaBar` (at `this + 0x1F8`), and `ExperienceBar` (at `this + 0x208`).
    * **Utility:** `OverlayMenuButton` (Settings/Pause button at `this + 0x1B8`).
    * **Buttons Container:** A specific sub-view at offset `this + 0x288` that wraps the touch controls.

---

## 3. Root Cause Analysis

### Cause A: Unnecessary memory overwrite in `l_ui_set_controls_disabled` (Primary Culprit)
In [sre_mini_api.c](file:///home/quantumcreeper/SwordigoDesktop/src/sre/sre_mini_api.c#L3149), the API function `UI.SetControlsDisabled(disabled)` is implemented as follows:
```c
void* gameOverlayView = *(void**)((char*)gameSceneView + (is_64bit ? 0x100 : 0xcc));
if (gameOverlayView) {
    *(char*)((char*)gameOverlayView + (is_64bit ? 0xe4 : 0xbc)) = (char)(disabled != 0);
}
```
* **The Bug:** Writing `1` to `gameOverlayView + 0xE4` sets the `isHidden` flag of the **entire parent `GameOverlayView`**. This causes the health, mana, settings button, and touch buttons to vanish simultaneously.
* **Why it's random:** Mod GUIs (e.g., custom shops, settings pages, overlays) call `UI.SetControlsDisabled(true)` to freeze input and hide standard controls when they open, and call `UI.SetControlsDisabled(false)` when closed. If the GUI closes abruptly (due to a player skip, death, loading a snapshot, scene transition, or a script error), the cleanup step is bypassed, leaving `isHidden = 1` permanently.
* **Why it is redundant:** SRE's host input handler (`src/main.cpp`) already listens to the global flag `g_sre_controls_disabled` and skips routing key presses to JNI when active. Physically hiding the parent view is not needed to block inputs.

### Cause B: SwKiwi Native API Bug Replication
SwKiwi's original C codebase contains the exact same flaw in `controls.c` (Line 51):
```c
*$(bool, gameOverlayView, 0xbc, 0xe4) = hidden;
```
This direct memory write was copied into SRE and propagates the bug.

### Cause C: Safe Native C++ API vs. Manual Writes
The native engine provides a safe function to hide *only* the buttons while keeping stats and settings visible:
* **`Caver::GameOverlayView::SetControlsHidden(bool hide)`** (at address `0x2ADB2D` in 1.4.12 ARM64):
  * Only writes to `*(bool*)(*(long*)(this + 0x288) + 0xE4) = hide;` (hiding the touch buttons container view).
  * Hides the use/pickup button (`this + 0x1D8`).
  * **Leaves stats bars (`ManaBar`, `HealthBar`) and settings button (`OverlayMenuButton`) visible.**

---

## 4. Input System Interactions (Virtual Touch & Auto-Unhide)
1. **Auto-Unhide:** In mobile Swordigo, receiving any touch event calls `SetControlsHidden(false)` to ensure controls reappear if the player touches the screen.
2. **Keyboard Simulation:** SRE maps keyboard controls to simulated touch events (generating clicks at action button screen coordinates). Every keypress triggers a native `TOUCH_DOWN`/`TOUCH_UP` event.
3. **Control Graphics:** Because virtual touches trigger auto-unhide, SRE uses a secondary mechanism (`g_gl_hide_hud = true` inside `main.cpp`) to swap the controls atlas texture with a blank transparent texture (`g_controls_hidden_tex`), making buttons physically invisible but keeping them active and clickable.

---

## 5. Proposed Solution Plan
To fully fix the random HUD disappearance while keeping the keyboard/touch system working:

1. **Remove direct write to `0xE4`/`0xBC`:**
   * Modify `l_ui_set_controls_disabled` in `src/sre/sre_mini_api.c` to **never** write to `gameOverlayView + 0xe4` / `0xbc`.
   * Keep the update to `g_sre_controls_disabled` so the host engine still blocks keys/clicks.
2. **Clean up mod overrides:**
   * When mod APIs request hiding/showing buttons, they must exclusively use the native `g_sre_GameOverlayView_SetControlsHidden` pointer, which targets the nested `0x288` container rather than the parent view.
3. **Scene transition cleanup:**
   * Nullify `g_sre_gamesceneview_ptr` inside the guest scene destructor (`sre_scene_update.c`) to prevent dangling pointer references when loading new areas.

---

## 6. Feasibility Check: Migrating from Touch Simulation to Direct Native Engine Mapping

We evaluated the feasibility of replacing the current keyboard "touch simulation and macros" system with a modern system that directly invokes native C++ functions on the player's `CharControllerComponent`.

### A. Core Engine APIs (AArch64 / ARM32 Symbols)
The decompiled codebase exposes direct methods on the `CharControllerComponent` class:
* **Horizontal Movement:** `CharControllerComponent::StartMovingToDirection(int dir)` (where `dir` is `-1` for Left, `1` for Right) and `StopMovingToDirection(int dir)`.
* **Jumping:** `CharControllerComponent::StartJumping()` and `StopJumping()`.
* **Combat Actions:** `CharControllerComponent::Swing()` and `StopSwing()` (Sword).
* **Interactions:** `CharControllerComponent::Use()` (Use/Interact).

### B. Comparison of Approaches

| Criteria | Touch Simulation (Current) | Direct Engine Mapping (Proposed) |
| :--- | :--- | :--- |
| **HUD Side-Effects** | Simulating screen touches triggers native "auto-unhide" behavior, forcing us to hack the texture atlas (`g_gl_hide_hud`). | No touch events are sent, so the touch UI naturally remains hidden. Hacking texture files is completely avoided. |
| **Resize Resiliency** | Dragging windows shifts coordinates, causing keys to miss virtual buttons or click menu buttons. | 100% resilient. Character actions are triggered by memory pointers, unaffected by window resolution or aspect ratio. |
| **Response Latency** | Queue-based macro execution introduces slight frame delays. | Direct function calls execute instantly, eliminating control lag. |
| **Spell Hotkey Macros** | Requires a timed macro delay (e.g., `250ms`) to wait for the spell picker menu to open. | Can directly set the active spell ID in player memory and trigger immediate casting, yielding instant hotkeys. |

### C. Migration Implementation Steps
1. **Pointers Resolution:** Resolve the active player's `CharControllerComponent` pointer by reading the controller address at offset `0xF0` of `GameSceneView`.
2. **Keyboard Dispatcher Hook:** In the host `main.cpp` keyboard event loop, instead of calling `call_handle_touch_event` with simulated screen coordinates, call the resolved thunk pointers directly:
   * Key `A` Down: `StartMovingToDirection(-1)`
   * Key `A` Up: `StopMovingToDirection(-1)`
   * Key `Space` Down: `StartJumping()`
3. **Keep Mouse Touch Mapping for Menus:** The mouse cursor should continue to use the current `handleTouchEvent` simulation. This is required because menus (main menu, pause options, level ports) are GUI widgets that are clicked naturally, and they do not have a `CharControllerComponent` to target.

### D. Feasibility Verdict
**Highly Feasible and Recommended.** We have already resolved the necessary symbols and mapped out the offsets in `sre_scene_update.c`. Transitioning keyboard inputs to direct component method calls will completely bypass touch simulation coordinates drift, clean up the texture swapping hacks, and provide a native, high-performance desktop feel.
