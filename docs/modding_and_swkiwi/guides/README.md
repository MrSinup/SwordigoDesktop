# Swordigo Modding Documentation

> Complete reference for modding Swordigo using the Ruby SDK, FileRift protobuf
> system, and the SRE (Swordigo Runtime Environment) hook framework.

All documentation is **observed** from decoded shipped scenes, IDA/Ghidra decompiled
ARM64 binaries, and the Ruby CLI toolchain.

---

## Documentation Index

| # | Document | Description |
|---|----------|-------------|
| ★ | [**Quick-Start Tutorial**](QUICKSTART.md) | **Build your first mod in 15 minutes** — step-by-step with examples |
| 01 | [Scene Camera System](01_scene_camera_system.md) | Camera singleton API, bounds constraints, focus/follow model, all Lua functions |
| 02 | [Quest System](02_quest_system.md) | Quest lifecycle, Character API, scene flags, item management, progression tracking |
| 03 | [Game Progression Systems](03_game_progression_systems.md) | World map (.scmap), save system, coins/mana/health, skill tree, hardmode |
| 04 | [Scene Cutscene System](04_scene_cutscene_system.md) | Cinematic mode, dialogue bubbles, fade transitions, camera cuts, Program coroutine model |
| 05 | [Modding API (FileRift)](05_modding_api_filerift.md) | FileRift encode/decode, scene format, Ruby CLI commands, protobuf schemas, mod VFS |

---

## Quick Reference — FileRift Decode/Encode

```bash
# Decode all game assets to markup
ruby_cli -d ~/.local/share/swordigo-desktop/assets/resources -o decoded/

# Recode markup back to binary
ruby_cli -r decoded/ -o recoded/

# Decode a single file type
ruby_cli -d -t scene -o decoded/ path/to/file.scene

# Create a new scene
ruby_cli scene create output.scene --level "MyLevel" --namespace mymod

# World map operations
ruby_cli map summary worldmap.scmap
ruby_cli map decode worldmap.scmap
ruby_cli map list-nodes worldmap.scmap
ruby_cli map path worldmap.scmap "town_part1" "fire_part1"
```

---

## Key Concepts

1. **Everything is protobuf** — scenes, SCLs, save data, world map, UI atlases.
   FileRift decodes binary → human-readable markup and re-encodes byte-exact.

2. **Camera is NOT a scene object** — it's an engine singleton driven entirely by
   Lua scripts. Scene files control it via `Bounds` (root field 3) and Lua calls.

3. **Quests are character-side** — `Character.AddQuest()`, `Character.SetQuestCompleted()`.
   The save file (snapshot.bin) holds all persistent state.

4. **Cutscenes = Program coroutine + cinematic mode** — `Game.SetCinematicMode(true)`
   freezes the hero, then scripts drive camera, dialogue, fades, and door unlocks.

5. **Entity behavior lives in Lua** — boss AI, enemy patrol, elevator movement,
   door locks — all authored as `ProgramComponent` scripts embedded in scene files.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│                    Swordigo Desktop                      │
├─────────────────────────────────────────────────────────┤
│  swordfare_boot (main exe)                              │
│    ├─ loading_screen.cpp     (boot screen)              │
│    ├─ display.cpp            (SDL3 + OpenGL context)    │
│    └─ swordfare_gui.cpp      (ImGui overlay)            │
├─────────────────────────────────────────────────────────┤
│  Swordigo Engine (ARM64, dynarmic JIT)                  │
│    ├─ Caver Engine (scene, entities, physics, camera)   │
│    ├─ Lua 5.1 VM (embedded, all game logic)            │
│    └─ OpenGL ES 1.x (fixed-function pipeline)          │
├─────────────────────────────────────────────────────────┤
│  SRE (Swordigo Runtime Environment)                    │
│    ├─ libsre.so  (open-source hooks + console)         │
│    └─ libsre-extras.so  (closed-source, FFI + mods)    │
├─────────────────────────────────────────────────────────┤
│  Ruby SDK (ruby_cli)                                    │
│    ├─ FileRift (protobuf encode/decode)                 │
│    ├─ Scene Creator + GMG (ground mesh generator)       │
│    ├─ Map Editor (world map decode/validate/path)       │
│    └─ Batch Texture Converter                           │
└─────────────────────────────────────────────────────────┘
```
