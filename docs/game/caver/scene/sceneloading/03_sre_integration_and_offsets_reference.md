# SRE Integration & Offsets Reference

## Executive Summary
This document provides a reference table of all nm symbol offsets, virtual memory addresses for ARM64 and ARM32 `libswordigo.so` v1.4.12, SRE C hooks, and SRE Lua API functions for scene management and instant loading.

---

## 1. Symbol Offsets & Virtual Addresses (v1.4.12)

| Symbol Name | ARM64 Virtual Address | ARM32 Virtual Address | Function Signature |
| :--- | :--- | :--- | :--- |
| `Caver::GameViewController::GotoLevel` | `0x00358A74` | `0x0027E234` | `void(void* self, std::string const& level, std::string const& spawn)` |
| `Caver::GameViewController::LoadGameState` | `0x00351C5C` | `0x001FDD80` | `void(void* self)` |
| `Caver::GameViewController::SaveGameState` | `0x0035299C` | `0x001FEAC0` | `void(void* self, bool)` |
| `Caver::GameViewController::BackgroundLoad` | `0x0035303C` | `0x001FF160` | `void*(void* params)` |
| `Caver::SceneLoadingView::InitWithGameState` | `0x004358DC` | `0x00201BE0` | `void(void* self, void* gs, void* map_node)` |
| `Caver::SceneLoadingView::Update` | `0x0043650C` | `0x00202810` | `void(void* self, float dt)` |
| `Caver::SceneLoadingView::AnimateIn` | `0x00436A54` | `0x00202C78` | `void(void* self)` |
| `Caver::Scene::LoadFromFile` | `0x004640A0` | `0x00234010` | `bool(void* self, char const* path)` |
| `Caver::Scene::FinishLoad` | `0x004642A8` | `0x00234210` | `void(void* self)` |
| `Caver::GameSceneController::InitWithScene` | `0x00348920` | `0x00278390` | `void(void* self, void* scene_ptr)` |
| `Caver::GameSceneController::SpawnHeroAt` | `0x00348B6C` | `0x002785E0` | `void(void* self, std::string const& spawn)` |

---

## 2. SRE C API Hook Implementation

In `src/sre/sre_init.c`:
```c
/* Register scene loading hooks in SRE initialization table */
{ 0x00358A74, "sre_GameViewController_GotoLevel" },
{ 0x0043650C, "sre_SceneLoadingView_Update"      },
{ 0x004642A8, "sre_Scene_FinishLoad"             },
```

In `src/sre/sre_scene_update.c`:
```c
/* Hook implementation for Instant Load feature toggle */
extern int g_sre_instant_scene_load_enabled;

void sre_SceneLoadingView_Update(void* self, float dt) {
    if (g_sre_instant_scene_load_enabled) {
        float* progress = (float*)((char*)self + 0x48);
        int* is_complete = (int*)((char*)self + 0x50);
        *progress = 1.0f;
        *is_complete = 1;
    }
    if (g_orig_SceneLoadingView_Update) {
        g_orig_SceneLoadingView_Update(self, dt);
    }
}
```

---

## 3. SRE Lua API Reference

| Lua Function | Description | Example Usage |
| :--- | :--- | :--- |
| `Mini.GotoLevel(level, spawn)` | Force load any level by name | `Mini.GotoLevel("town_part1", "spawn_start")` |
| `Mini.GetCurrentScene()` | Get current active scene name | `local s = Mini.GetCurrentScene()` |
| `Mini.GetPreviousScene()` | Get previous scene name | `local p = Mini.GetPreviousScene()` |
| `Mini.SetInstantLoad(enable)` | Toggle 0-frame instant scene loading | `Mini.SetInstantLoad(true)` |
| `Mini.OnSceneChange(callback)` | Register callback on scene transition | `Mini.OnSceneChange(function(new_scene) print(new_scene) end)` |
