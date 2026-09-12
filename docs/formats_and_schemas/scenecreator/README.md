# Swordigo Scene Creator — Research Documentation

> **Purpose:** Reverse-engineered, evidence-based documentation of the Swordigo scene
> format and its reusable systems, written to power a **from-scratch Scene Creator**
> in the Ruby SDK. Every claim is tagged **Observed**, **Inferred**, or **Unknown**.

---

## What this suite contains

| File | Contents |
|------|----------|
| [`01_scene_file_anatomy.md`](01_scene_file_anatomy.md) | The complete structural anatomy of a `.scene` file: root protobuf fields, `Object` records, transforms, components, libraries, groups, scene-level OnLoad scripts. |
| [`02_scene_bounds_and_camera.md`](02_scene_bounds_and_camera.md) | The `Bounds` root field (the "SceneBounds" system) — present in **all 118 shipped scenes** — plus the engine-side **Camera** architecture and a ready-to-use `CameraTemplate`. |
| [`03_lighting_system.md`](03_lighting_system.md) | The `Light` object anatomy, `LightComponent` type semantics (types 1/2/3/4), real intensity/color statistics across every scene, and a reusable `LightTemplate`. |
| [`04_spawn_points_portals_triggers.md`](04_spawn_points_portals_triggers.md) | `SpawnPoint` objects (incl. the `spawn_default` + `spawn_from_<scene>` naming convention), `Portal` objects, and trigger zones (`CollisionShape` + `Trigger: 1`). |
| [`05_component_catalog.md`](05_component_catalog.md) | The full component registry: every component class known to Ruby (`scene_schemas.cpp`, 134 schema entries) with its protobuf field numbers and payload fields. |
| [`06_scene_templates.md`](06_scene_templates.md) | The deliverable templates: `MinimalScene`, `StandardScene`, `OutdoorScene`, `IndoorScene`, `DungeonScene`, `BossArena`, `PortalScene` — each with REQUIRED / COMMON / OPTIONAL / SPECIALIZED marking. |
| [`07_lua_scripting_api.md`](07_lua_scripting_api.md) | The data-driven behavior layer: `Program`/`EntityAction` Lua scripts, the Lua API census (Camera.*, EntityController, Health, PhysicsObject, Scene, Game, …), scene OnLoad scripts, and trigger wiring. |
| [`08_scene_creator_spec.md`](08_scene_creator_spec.md) | The practical specification for the Scene Creator feature in Ruby: template system, "New Scene" flow, auto-generated bounds, and UI-editable properties. |

---

## Methodology (how this was researched)

1. **Decoded 118 shipped scene files** (from `~/.local/share/swordigo-desktop/assets/resources/*.scene`)
   with the Ruby SDK's native FileRift decoder (`dec_scene`, a C++ port of DanielSpaniel's
   Python FileRift).
2. **Compared scenes across every category** the research prompt asked for:
   - Towns: `town_part1`, `florennum_part1`, `plains_part1`, `grass_part1`, `wasteland_town`
   - Outdoor: `forest_part1`, `fire_part1`, `grove_part1`, `worldsend_part1`, `lowergrove_part1`, `snowy_part1`
   - Caves: `florennum_cave1`, `forest_cave1`, `thecave_part1`, `plains_cave0`, `wasteland_cave1`
   - Dungeons/interiors: `icecastle_part1`, `florennum_jail_part1`, `town_shop`, `plains_house1`
   - Boss arenas: `fire_partBoss`, `icecastle_partBoss`, `florennum_jail_boss`
   - Transitions: `town_woods_end`, `beyond_graveyard`, `grove_graveyard`
   - Special: `hero.scene`, `menu.scene`, `credits.scene`, `new_level.scene` (hand-made test scene), `theend.scene`
3. **Parsed the raw protobuf root** of every scene to extract the root-level `Bounds`
   field (field 3, four `fixed32` floats) and detect scene-level OnLoad scripts (field 5).
4. **Cross-referenced the C++ scene loader** (`src/tools/scene_loader.cpp`, `scene_schemas.cpp`)
   for the authoritative component field numbers and loader behavior.
5. **Cross-referenced the decompiled Caver engine** (`OpenSwordigo/arm32/libswordigo_ida32.c`
   and `OpenSwordigo/resources/ida_decompiled/functions/Caver/`) for the camera controller,
   light component, and game-scene controller semantics.
6. **Censused the Lua API** actually used inside shipped scenes' bytecode strings.

---

## Evidence tags

| Tag | Meaning |
|-----|---------|
| **Observed** | Seen directly in decoded scene files (multiple examples where noted). |
| **Inferred** | Reasonable conclusion from observed data / decompiled code, not directly proven. |
| **Unknown** | Not yet determined; explicitly flagged so nobody assumes it's a fact. |

## Ground rules for contributors

- Never turn an assumption into a fact — keep the **Observed / Inferred / Unknown** tags.
- When adding a template value, cite the scene it came from.
- This is documentation for a *generalized scene-construction system*, not a collection
  of hardcoded copies of existing scenes. Templates must be *parametric*, with evidence-backed defaults.
