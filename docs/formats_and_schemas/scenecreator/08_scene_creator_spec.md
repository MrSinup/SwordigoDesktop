# 08 — Scene Creator Specification

> The practical design for a **"New Scene"** feature in the Ruby SDK that builds
> gameplay-compatible Swordigo scenes from templates instead of requiring users to
> reproduce hundreds of low-level protobuf properties.

---

## 1. Goals

- Click **New Scene** → pick a template → get a **playable, vanilla-compatible scene**
  in seconds.
- Place native objects, add lights, define bounds, place spawn points, wire portals and
  camera behavior — all through the UI.
- **Never break vanilla compatibility** (scenes must load in the unmodified game).

---

## 2. Template system

### 2.1 Template registry (from [06_scene_templates.md](06_scene_templates.md))

```
MinimalScene      ← base (ground + spawn + light + background + bounds)
StandardScene     ← Minimal + models + decor + item drops + optional enemies
OutdoorScene      ← Standard w/ outdoor bounds + sky bg + hills
IndoorScene       ← Standard w/ small bounds + extra point lights
DungeonScene      ← Indoor + water/lava + heavy collision + cave bg
BossArena         ← Dungeon + boss entity + arena props + portals + quest triggers
PortalScene       ← Minimal + portal objects (hub/transition)
MenuScene         ← idle-entity scene (menu.scene pattern)
```

Each template is a **parameterized recipe**:

```text
New Scene {
    template: OutdoorScene,
    bounds:   { x:-3500, y:-1000, w:5500, h:2500 },      # auto-computed from ground AABB
    background: 'grasslandsbackground_day',
    light:    { key:2/2, ambient:1/0.3, fill:4/1 },       # canonical triple
    ground:   [ polygon list ],                            # editable in the 2D mesh editor
    spawn:    { x:0, y:56, facing:1 },
    portals:  [],                                          # added via UI
}
```

### 2.2 What "New Scene" auto-creates (in order)

1. **Scene root + `Bounds`** (field 3) — computed from the ground AABB, padded ~200 px.
   This is mandatory so `Camera.ResetFocus()` has something to clamp (see [02](02_scene_bounds_and_camera.md)).
2. **`spawn_default`** spawn point.
3. **`DirectionalLight`** canonical triple.
4. **`Background`** object (category default texture).
5. **Ground** (`GroundPolygon` + `GroundMesh` + `CollisionShape(IsGround:1)` +
   `TextureMapping`×2), authored in the existing 2D mesh editor.
6. **Save** via the Ruby scene writer (`scene_loader` round-trip preserves all root fields).

### 2.3 The "StandardScene" skeleton the user starts from

```
Background{ … }
DirectionalLight{ Type2/Type1/Type4 }
world_base{ GroundPolygon(980) + GroundMesh(981) + CollisionShape(983,IsGround:1) + TextureMapping(984,985) }
spawn_default{ SpawnPoint(101) }
```

---

## 3. UI-editable properties per template piece

| Piece | Editable |
|-------|----------|
| Scene | template, name, OnLoad script (Lua), bounds (drag in viewport / numeric) |
| Background | texture name (combo of known bg textures), position |
| Light | per-component type/intensity/color; add point lights |
| Ground | polygon vertices (2D editor), min/max depth, textures (surface/front), GMG seed/mesh params |
| Spawn | position, facing, offset |
| Portal | destination scene (auto-creates `spawn_from_<dest>`), rect, tap-to-enter |
| Entity | template (`MonsterEntity` archetype), health, damage, EntityActions (Lua snippets), animations |
| Trigger | shape, Lua body from snippet library (camera, door, quest, platform) |
| Item drop | item type, position, respawn |

---

## 4. Wire-format checklist (what the writer must emit)

- Object serialization: `TemplateName?`, `Identifier`, components, `Position{X,Y}`,
  `Depth`, `Rotation`, `Scaling`, `LocalAabb?`, `Hidden`. (Ruby `proto::Writer` +
  `scene_loader` already implement this.)
- Component serialization: `ClassName` (field 1), `Identifier` (field 2),
  `ParentComponentIdentifier?`, payload fields per [05](05_component_catalog.md).
- Root: field 1 objects (repeated), **field 3 bounds** (4× fixed32 floats),
  optional fields 2/4/5 preserved verbatim when editing existing scenes.
- Component IDs must be unique per object (start at 100/101; the engine's own objects use
  980+ for GMG bundles — matching that avoids collisions and matches authoring tools).

---

## 5. Validation & safety

Before writing, validate:
1. `spawn_default` exists and sits above a ground polygon.
2. `Bounds` contains all objects + margin.
3. Every `Portal.DestinationSceneName` exists on disk (warn otherwise).
4. Every `Background.TextureName` / `TextureMapping.TextureName` / `Model` asset exists in
   the asset tree (Ruby's `scene_loader` already scans for asset refs on load).
5. All `TriggerShapeId`/`GroundPolygonId`/`TargetMeshId` references resolve to a local
   component id.
6. Lua scripts compile (`src/sre/lua` VM) before embedding.

---

## 6. Implementation plan (Ruby SDK)

| Step | Work |
|------|------|
| 1 | `scene_creator.h/.cpp` — template registry + `build_new_scene(template, params)` producing a `SceneData` in memory |
| 2 | Extend `scene_loader` to *emit* root field 3 `Bounds` (currently preserved-raw only) |
| 3 | "New Scene" menu entry + template picker dialog |
| 4 | Auto-bounds: compute from ground objects' AABB |
| 5 | Portal wiring: auto `spawn_from_<dest>` creation + cross-scene validation |
| 6 | Lua snippet library + `Program`/`EntityAction` writers |
| 7 | Entity templates (archetypes → component bundles) |
| 8 | Playtest hook: load the new scene in the Ruby scene player to verify |

---

## 7. Success criteria

A user can:
1. Click **New Scene**, pick `OutdoorScene`, get bounds + spawn + light + background + ground.
2. Draw ground in the 2D mesh editor; add models, lights, an enemy, a chest, a portal.
3. Wire the portal to `grass_part1`, save, and **play the scene in the vanilla game** —
   camera follows the hero, collision works, the enemy patrols (Lua), the chest opens.

---

## 8. Appendix — quick reference of evidence files

- Decoded scenes: any `*.scene` under the game's `assets/resources/`.
- Canonical minimal scene: `new_level.scene`.
- Hero/controller reference: `hero.scene`.
- Boss arena reference: `fire_partBoss.scene`.
- Day/night: `town_part1.scene`.
- Camera controller decomp: `OpenSwordigo/resources/ida_decompiled/functions/Caver/CameraController/`.
- Scene root parsing: `src/tools/scene_loader.cpp` (top-of-file comment).
- Component schemas: `src/tools/scene_schemas.cpp`.
