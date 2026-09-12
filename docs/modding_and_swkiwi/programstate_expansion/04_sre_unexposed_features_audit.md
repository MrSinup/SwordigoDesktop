# Complete SRE Unexposed Capabilities & Subsystem Audit

## Executive Summary
This document provides an exhaustive technical audit of all internal C/C++ subsystems, data structures, performance counters, render states, and memory hooks present in the SRE runtime codebase (`src/sre/`) that are currently **unexposed** to Lua modders. It categorizes these features and defines how they will be exposed via the `Mini.*` and `Swd.*` Lua namespaces.

---

## 1. Audit of Unexposed SRE Subsystems

### A. Audio Engine (`src/sre/sre_music.c`)
- **Internal Features**: Custom OGG track loader, audio streaming buffer, volume gain controls, track crossfading state machine, 3D spatial audio listener parameters.
- **Current Status**: Hardcoded in C for background music loading (`boss` -> `1_boss23.ogg`). Unexposed to Lua.
- **Proposed Target**: `Swd.Audio.*`

### B. Frame Loop & Performance Metrics (`src/sre/sre_frame_loop.c`)
- **Internal Features**:
  - `g_sre_draws_per_frame`, `g_sre_verts_per_frame`, `g_sre_tex_binds_per_frame`.
  - `g_sre_vtx_calls`, `g_sre_texc_calls`, `g_sre_matrix_calls`, `g_sre_state_changes`.
  - `g_sre_dynarmic_bridge_calls`, `g_sre_fps_cap` (30/60/120/144/uncapped).
  - Coroutine execution time budget (`g_sre_coro_time_budget_us`).
- **Current Status**: Logged to stderr / ImGui debug HUD (`[Frame64 800]`). Unexposed to Lua.
- **Proposed Target**: `Swd.System.*` & `Swd.Emu.*`

### C. Graphics & PostFX Pipeline (`src/sre/sre_effects.c`, `src/sre/sre_background.c`)
- **Internal Features**:
  - FBO Scaler parameters (`g_fbo_mode`, render resolution multiplier).
  - PostFX shader parameters (Vignette intensity, film grain noise, chromatic aberration offset, bloom threshold, CRT scanline count).
  - Sky background colors (`g_sre_sky_top_color`, `g_sre_sky_bottom_color`) and rotating background rotation speeds (`sre_RotatingBackgroundComponent`).
  - Particle trails (`sre_WeaponTrailComponent`), portal swirl effects (`sre_PortalEffectComponent`).
- **Current Status**: Hardcoded in OpenGL/Vulkan backends or static C structures. Unexposed to Lua.
- **Proposed Target**: `Swd.Gfx.*`

### D. Virtual File System & PVR Loaders (`src/sre/sre_vfs.c`)
- **Internal Features**: `assets/resources/` file path remapper, PVR container parser, PVRTC 2bpp/4bpp hardware texture decoder, TOML configuration parser (`toml-c`).
- **Current Status**: Used internally by C loader. Unexposed to Lua.
- **Proposed Target**: `Swd.VFS.*`

### E. Native Character & Inventory State (`src/sre/sre_caver.h`)
- **Internal Features**:
  - Player profile experience points, level up skill points, coin count, soul shards.
  - Equipped armor ID, sword ID, trinket IDs, spell IDs.
  - Active quest progress map (`std::vector<QuestState*>`), map node completion percentages.
  - Character movement speed multiplier, jump height, gravity scale.
- **Current Status**: Partially exposed in `Mini.*`, missing full equipment/quest reflection.
- **Proposed Target**: `Mini.Hero.*` & `Mini.Quest.*`

### F. Scene Object Tree & Component Interfaces (`src/sre/sre_scene_update.c`)
- **Internal Features**:
  - Sentinel RB-Tree scene object iteration (`scene + 0xB8`).
  - Component interface table (`TransformComponent`, `CharControllerComponent`, `DamageComponent`, `HealthComponent`, `ItemDropComponent`, `PortalComponent`).
  - Hitbox / Hurtbox AABB boundaries (`CollisionShapeComponent`).
- **Current Status**: `Mini.SceneFindAll()` exists, but lacks deep component mutation APIs.
- **Proposed Target**: `Mini.Object.*`

---

## 2. Namespace Allocation Architecture

To maintain clear separation of concerns between game-level modding and engine-level system control:

```
                          ┌───────────────────────────┐
                          │   Lua Modding API Layer   │
                          └─────────────┬─────────────┘
                                        │
             ┌──────────────────────────┴──────────────────────────┐
             ▼                                                     ▼
┌───────────────────────────┐                         ┌───────────────────────────┐
│     Mini.* Namespace      │                         │      Swd.* Namespace      │
│  (Game & Gameplay API)    │                         │  (Engine & System Core)   │
├───────────────────────────┤                         ├───────────────────────────┤
│ • Mini.Hero.*             │                         │ • Swd.System.*            │
│ • Mini.Object.*           │                         │ • Swd.Audio.*             │
│ • Mini.Scene.*            │                         │ • Swd.Gfx.*               │
│ • Mini.Quest.*            │                         │ • Swd.VFS.*               │
│ • Mini.Camera.*           │                         │ • Swd.Emu.*               │
└───────────────────────────┘                         └───────────────────────────┘
```
