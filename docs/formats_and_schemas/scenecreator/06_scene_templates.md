# 06 — Scene Templates

> The Scene Creator's template system, built from the invariants discovered across 118
> real scenes. Each template is a *parametric recipe*, not a copy of an existing scene.

---

## 0. The template anatomy key

Every template is described by sections marked:

| Mark | Meaning |
|------|---------|
| **REQUIRED** | Present in ~every scene; omitting it breaks loading/rendering/gameplay |
| **COMMON** | Present in most scenes of this type |
| **OPTIONAL** | Present in some scenes, always safe to omit |
| **SPECIALIZED** | Only for specific gameplay (boss arenas, story beats) |

---

## 1. `MinimalScene` — smallest scene that should work

**Evidence base:** `new_level.scene` (hand-made; 2 ground objects + spawn + GMG-generated
meshes) + the invariant list from [01 §4](01_scene_file_anatomy.md).

| Section | Mark | Contents |
|---------|------|----------|
| Root field 3 `Bounds` | **REQUIRED** | X/Y/W/H of the play area (new_level: -160,-24,320,240) |
| `spawn_default` | **REQUIRED** | SpawnPointComponent + Position above ground |
| Ground object | **REQUIRED** | `GroundPolygon` (Collides:1, MinDepth -45, MaxDepth 45) |
| `CollisionShape` (IsGround:1) | **REQUIRED** | walkable collision matching the polygon |
| `DirectionalLight` | **REQUIRED** | canonical triple (Type 2/1/4) |
| `Background` | **REQUIRED** | BackgroundComponent + valid TextureName |
| ObjectLibrary / Groups / OnLoad | OPTIONAL | not needed for a bare test scene |

> **Caveat (Inferred):** `new_level.scene` omits Background/Light/Bounds tuning; a truly
> minimal scene *should* still include them (all shipped scenes do).

### Minimal template — object list

```
1. Background{ BackgroundComponent{ TextureName : '<bg_texture>' } }
2. DirectionalLight{ Light(101,Type2,I2,white) Light(103,Type1,I0.3,white) Light(105,Type4,I1,black) }
3. world_base{ GroundPolygon(980){Polygon,Collides:1,MinDepth:-45,MaxDepth:45}
               GroundMesh(981){SurfaceMesh+FrontMesh}            # visible floor
               CollisionShape(983,parent 980){IsGround:1,MinDepth:-45,MaxDepth:45}
               TextureMapping(984){surface tex} TextureMapping(985){front tex} }
4. spawn_default{ SpawnPoint(101){FacingDirection:1,SpawnOffset(0,0,0)}  Position:(0, 56) }
```

---

## 2. `StandardScene` — the common template

**Evidence base:** `town_part1`, `plains_part1`, `forest_part1`, `florennum_part1`, … (all
outdoor/story scenes share this shape).

| Section | Mark | Contents |
|---------|------|----------|
| `Bounds` | **REQUIRED** | outdoor scale (X≈-3500, W≈5000–11000; Y≈-1000, H≈2000–3000) |
| `spawn_default` | **REQUIRED** | |
| `spawn_from_<X>` × N | **REQUIRED** | one per incoming scene/portal |
| `DirectionalLight` | **REQUIRED** | |
| `Background` | **REQUIRED** | |
| Ground objects (GroundPolygon+GroundMesh+CollisionShape+TextureMapping×2) | **REQUIRED** | 10–30 objects |
| `CollisionShape` obstacles | COMMON | 2–10 |
| `Model` decorations | COMMON | trees, houses, rocks (5–70) |
| `ItemDrop`/`CollectableItem` | COMMON | coins, potions |
| Enemies (`MonsterEntity` + controllers + `EntityAction` + Lua) | COMMON | 0–10 |
| NPCs (`OverlayText`, `SoundEffect`, `Properties`) | OPTIONAL | |
| Moving platforms (`TransformController`/`KeyframeAnimation`) | OPTIONAL | |
| Extra point lights | OPTIONAL | indoor spots |
| `OnLoad` script | OPTIONAL | 20/118 scenes |
| Day/night lights (`DirectionalLight_day`+`_night`) | SPECIALIZED | only `town_part1` |

---

## 3. Category templates (deltas from StandardScene)

### `OutdoorScene`
- Bounds: X≈-3500, Y≈-500..-1500, W≈5000–11000, H≈2000–3000.
- 1 `DirectionalLight` (canonical triple), 1 Background (sky texture).
- Lots of `Model` decor + `GroundPolygon` hills.
- Examples: `fire_part1`, `grass_part1`, `grove_part1`, `worldsend_part1`.

### `IndoorScene` (houses/shops/towers)
- Bounds small: W≈1500–3000, H≈800–1700 (`florennum_shop`: -450,-300,1750,1000;
  `plains_house1`: -450,-100,1450,800).
- Extra point lights (`Light` objects at Depth 30) — `florennum_healerhouse`,
  `florennum_shop`.
- Fewer models, more `CollisionShape` walls.
- Examples: `town_shop`, `grass_house`, `florennum_healerhouse`, `plains_house1`.

### `DungeonScene` (caves/jails/ice)
- Bounds medium: W≈3000–7000, H≈2000–5000.
- 1 Background (cave/ice texture), canonical triple + extra point lights
  (`florennum_cave1` has 4 Light objects; `florennum_jail_part1` has 2).
- `WaterMesh` (lava/water) present in cave scenes.
- Heavy `CollisionShape` usage (`florennum_jail_part1`: 13).
- Examples: `florennum_cave1`, `forest_cave1`, `thecave_part1`, `icecastle_part1`,
  `florennum_jail_part1`.

### `BossArena`
- Bounds ≈ arena size (fire_partBoss: -3500,-500,3500,2500; icecastle_partBoss:
  -2000,-1500,5000,4500).
- Boss entity object: `Model + AnimationController + KeyframeAnimation×3 +
  EntityController + MonsterEntity + Health + Damage + EntityAction(Lua boss AI)`.
- 2+ portal exit objects (`CollisionShape + Portal`), `spawn_default` +
  `spawn_from_<prev>`.
- Arena props: moving ground platforms (`TransformController`), collectible
  (`PhysicsObject + CollectableItem + SimpleGlow`), fire/particle programs,
  `questtrigger` zone, `questheromarker`.
- Example: `fire_partBoss` (75 objects, fully inventoried), `florennum_jail_boss`,
  `icecastle_partBoss`.

### `PortalScene` (transition hubs)
- Focus on portals + spawns; minimal gameplay.
- Examples: `town_woods_end`, `beyond_graveyard`, `grove_graveyard`.

### Special scenes
- `hero.scene` — the hero + `DirectionalLight` (dimmed) + `spawn_default`. Contains the
  canonical `HeroEntity`/`OnItemGet` script. Reference for hero-spawn code.
- `menu.scene` — `darkhero` model with `MonsterEntity`+`EntityController` (idle animation)
  + `Background` + `DirectionalLight`. Reference for menu/attract scenes.

---

## 4. Cross-scene dependencies (what breaks what — Observed/Inferred)

| Omission | Symptom |
|----------|---------|
| No `Bounds` | camera/focus misbehavior (`Camera.ResetFocus` has nothing to clamp) |
| No `spawn_default` | hero cannot spawn / spawns at origin |
| No `DirectionalLight` | scene renders unlit/black |
| No `Background` | no sky/backdrop layer |
| No ground `CollisionShape` (IsGround:1) | hero falls through the floor |
| Portal → missing `spawn_from_X` in target | landing at default instead of intended point |
| Wrong `TextureName` in Background/TextureMapping | missing/invisible texture |
| `GroundMeshGenerator` without matching `TextureMapping` ids | untextured/missing generated mesh |

---

## 5. Template defaults table (for the Scene Creator UI)

| Property | Default | Editable | Evidence |
|----------|---------|----------|----------|
| Bounds X,Y,W,H | ground AABB + 200px | ✅ | §1, `new_level` |
| Background texture | per-category set | ✅ | all scenes |
| DirectionalLight triple | Type2 I2 / Type1 I0.3 / Type4 I1 | ✅ | canonical |
| Spawn facing | 1 | ✅ | `spawn_default` |
| Ground MinDepth/MaxDepth | -45 / 45 | ✅ | all ground objects |
| Portal SpecialType | 2 | fixed | 275 occurrences |
| Portal TriggerShape depth | -15 / 50 | ✅ | `town_part1` portal |
| Hero controller defaults | `hero.scene`'s CharController | ✅ | `hero.scene` |
