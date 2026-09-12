# Forced Arbitrary Scene Loading Mechanics

## Executive Summary
This document provides a reverse-engineering analysis of scene and level loading in Swordigo (ARM64 / ARM32 `libswordigo.so`). It details how the engine manages scene state, the call graph of `Caver::GameViewController::GotoLevel`, struct layouts of `GameState` and `GameSceneController`, and how to force the engine to load any arbitrary `.scene` file.

---

## 1. Symbol Offsets & Virtual Memory Mapping

| Class & Method | ARM64 VA (v1.4.12) | ARM32 VA (v1.4.12) | Calling Convention / Registers |
| :--- | :--- | :--- | :--- |
| `Caver::GameViewController::GotoLevel` | `0x00358A74` | `0x0027E234` | `X0`/`R0` = `this`, `X1`/`R1` = `&level_name`, `X2`/`R2` = `&spawn_point` |
| `Caver::GameViewController::PortalViewControllerDidGotoLevel` | `0x00357A14` | `0x0027DA10` | `X0`/`R0` = `this`, `X1`/`R1` = `portal_vc`, `X2`/`R2` = `&level_name` |
| `Caver::GameState::StateForLevelWithName` | `0x003B7C8C` | `0x002BC5B0` | `X0`/`R0` = `this`, `X1`/`R1` = `&level_name`, `W2`/`R2` = `create_if_missing` |
| `Caver::GameState::CurrentLevelState` | `0x003B7E60` | `0x002BC784` | `X0`/`R0` = `this`, `W1`/`R1` = `create_if_missing` |
| `Caver::GameSceneController::InitWithScene` | `0x00348920` | `0x00278390` | `X0`/`R0` = `this`, `X1`/`R1` = `&scene_ptr` |
| `Caver::GameSceneController::SpawnHeroAt` | `0x00348B6C` | `0x002785E0` | `X0`/`R0` = `this`, `X1`/`R1` = `&spawn_point_name` |

---

## 2. Reverse-Engineered Control Flow (`GotoLevel`)

When a portal is entered, a script executes, or a level transition occurs, the engine enters `Caver::GameViewController::GotoLevel(this, level_name, spawn_point)`:

```c
// Decompiled IDA / Ghidra Execution Flow (ARM32/ARM64 normalized)
int Caver::GameViewController::GotoLevel(GameViewController* self, std::string const& level_name, std::string const& spawn_point)
{
    // 1. Get pointer to current GameState
    GameState* gs = *(GameState**)((char*)self + 88); // 32-bit: self+88, 64-bit: self+0xB8

    // 2. Clear previous pending questitem level/spawn overrides in GameState
    gs->pending_quest_level = "";
    gs->pending_quest_spawn = "";

    // 3. If spawn_point is "portal", check if player holds a quest item portal component
    if (spawn_point == "portal") {
        SceneObject* hero = self->gameSceneController->heroObject;
        if (hero) {
            PropertiesComponent* props = hero->GetComponent<PropertiesComponent>();
            if (props && props->HasProperty("questitem")) {
                gs->pending_quest_level = hero->GetProperty("portal_target_level");
                gs->pending_quest_spawn = hero->GetProperty("portal_target_spawn");
            }
        }
    }

    // 4. Update GameState with targeted level and spawn point strings
    gs->next_level_name = level_name;   // GameState + 0xBC (ARM64)
    gs->next_spawn_name = spawn_point;  // GameState + 0xC0 (ARM64)

    // 5. Create new SceneLoadingView instance
    boost::shared_ptr<SceneLoadingView> loading_view(new SceneLoadingView());
    loading_view->InitWithGameState(gs, self->mapNode);

    // 6. Transition ViewControllers (presents loading screen and starts async load)
    self->vtable->TransitionToViewController(self, loading_view, 1.0f, 1.0f, false);

    // 7. Track analytics event for level transition
    OnlineController::SharedController()->TrackEvent("between_levels", level_name);

    return 0;
}
```

---

## 3. GameState & GameViewController Struct Memory Offsets

```
GameViewController Layout (ARM64):
+0x000: vtable pointer
+0x020: GUIViewController base
+0x058: GameState* (gs)
+0x070: MapNode* (current map node)
+0x0D8: GameSceneView*
+0x0E0: GameSceneController*

GameState Layout (ARM64):
+0x000: vtable pointer
+0x018: GameData*
+0x0B8: std::string current_level_name
+0x0BC: std::string next_level_name
+0x0C0: std::string next_spawn_name
+0x0C8: std::string pending_quest_level
+0x0D0: std::string pending_quest_spawn
```

---

## 4. Forced Scene Loading Techniques

To force the game to load any arbitrary scene file (e.g. `town_part1.scene`, `boss_room.scene`, or custom `.scene` files):

### Method A: Direct Execution of `GameViewController::GotoLevel`
Using the Unicorn emulator API or SRE native bridge:
```cpp
void force_load_scene(const std::string& level_name, const std::string& spawn_point) {
    uint64_t gvc_ptr = get_active_game_view_controller(); // GameViewController*
    if (!gvc_ptr) return;

    // Allocate guest std::string objects for level_name and spawn_point
    uint64_t level_str_addr = write_guest_std_string(level_name);
    uint64_t spawn_str_addr = write_guest_std_string(spawn_point);

    // Call Caver::GameViewController::GotoLevel(gvc_ptr, level_str_addr, spawn_str_addr)
    // ARM64 calling convention: X0 = gvc_ptr, X1 = level_str_addr, X2 = spawn_str_addr
    emulator_call_function(0x00358A74, { gvc_ptr, level_str_addr, spawn_str_addr });
}
```

### Method B: SRE Lua API Integration
In SRE Lua scripts:
```lua
-- Force load any scene file directly from Lua console or mod script
Mini.GotoLevel("boss_dragon", "spawn_entry")
```
