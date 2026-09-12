# Caver Engine RE Report — Top 10 Powerful Features for the Ruby SDK

**Sources:** `OpenSwordigo/resources/ida_decompiled/functions/Caver/` (614 decompiled functions),
`OpenSwordigo/caver_symbols_clean.txt` (12,258 symbols), `OpenSwordigo/caver_rtti.txt` (269 RTTI types),
`OpenSwordigo/arm32/libswordigo_ida32.c`.

**Scope:** This report reverse-engineers the **Caver game engine** — the C++ engine behind Swordigo —
and ranks the ten most powerful, unique and niche capabilities we can port into the Ruby SDK
(scene editor + scene player). Everything below is grounded in the decompiled evidence with the
exact function/component names so implementation can start immediately.

---

## 0. What the caver engine actually is (one paragraph)

Caver is a **component-based 2.5D game engine** built on protobuf-serialized scene files. Every
entity is a `SceneObject` that owns a list of **Components** (269 RTTI types: `EntityComponent`,
`CollisionShapeComponent`, `PhysicsObjectComponent`, `CharControllerComponent`, `GroundMeshComponent`,
`ItemDropComponent`, …). Each component exposes **bindings** (`GetBindings` / `SetValueForBindedProperty`
/ `PerformBindedAction`) — i.e. the *entire game is data-driven*: enemy walk speed, hero jump impulse,
particle parameters, item drops are all bindable values in the scene, not hardcoded. Rendering is
PowerVR POD models + GroundMeshGenerator-built procedural terrain; physics is circle-vs-shape
(circle entities vs rectangle/polygon/circle collision shapes); the GUI is a full in-house widget
toolkit (`GUI*`); the scene player is our re-implementation of `Scene::Process` + `PhysicsObjectState::Update`.

---

## The Top 10

### 1. 🎨 GroundMeshGenerator — the game's own procedural ground-mesh builder
**Evidence:** `Caver::GroundMeshGenerator::GenerateFrontMesh`, `GenerateSurfaceMesh(int, Vector2 const*, float const*, TextureMapping const*)`, `GeneratePlainSurfaceWithHatGaps`, `GenerateSurfaceMeshWithRoundHat(float,float,float,float)`, `InsertRoundHatVertices`, `InsertCapForRoundHat`, `InsertPlainSurfaceVertices` (arm64 @ 0x2B95E0…0x2BB010).
**What it is:** the exact algorithm the game uses to turn a 2D sketch profile + per-vertex depth into a 3D ground mesh — with **round-hat caps** (rounded hills), **plain surface gaps**, and texture mapping. Our `boulder.cpp` sketch generator was written from scratch and differs subtly.
**Ruby payoff:** replace our sketch→mesh pipeline with the true algorithm → generated meshes render *identically* in Ruby and in-game. Round-hat hills, cap insertion and gap handling for free. This is the single highest-fidelity win for the mesh-drawer feature.

### 2. 🎬 AnimBlendNode / AnimKeysNode — the animation graph (fixes T-pose & blends)
**Evidence:** `AnimBlendNode`, `AnimKeysNode`, `AnimNode`, `BlendAnimationComponent`, `AnimationControllerComponent` (RTTI + `Caver/AnimBlendNode/`, `Caver/AnimKeysNode/` function dirs).
**What it is:** the engine does NOT just switch POD files — it evaluates a **node graph**: `AnimKeysNode` (keyframe tracks per bone) → `AnimBlendNode` (blends N inputs by weight). The "T-pose bug" we fixed earlier was actually the game *blending* from rest pose toward the target; we approximated it with a per-anim frame loop.
**Ruby payoff:** port the node evaluator → proper rest-pose→anim blending, smooth run↔idle↔jump transitions (the game's `BlendAnimationComponent` does exactly this), and a data-driven anim-graph inspector in the editor.

### 3. 🧩 Full bindings system — ComponentOutlet + GetBindings (the "property grid" of the game)
**Evidence:** `Component::GetBindings`, `SetValueForBindedProperty`, `ValueForBindedProperty`, `PerformBindedAction`, `ComponentOutlet<...>::ConnectTo/target/setIdentifier` (e.g. `CollisionShapeComponent::GetBindings @0x218038`, `ComponentOutlet` RTTI).
**What it is:** every component's editable properties are exposed as **bindings** (name → type → value), and **outlets** let one component reference another (e.g. `CollisionShapeComponent.CollisionShapeId` outlet → the shape it controls).
**Ruby payoff:** a real **Inspector panel** generated from each component's bindings (not hardcoded fields) — the same data the game's own editor showed. Plus a **outlet visualizer** (draw arrows between connected components). This is the "objects panel with full per-model stats" the user asked for, done the *engine-faithful* way.

### 4. 💥 ParticleEmitter suite — Blast/Spark/Trail/Whoosh/Fountain + FireEmitter
**Evidence:** `Caver::BlastParticleEmitter`, `SparkParticleEmitter`, `TrailParticleEmitter`, `WhooshParticleEmitter`, `FountainParticleEmitter` (each with `TitleForParameter(unsigned)::titles` — parameterized), `ParticleEmitterComponent`, `ParticleComponent`, `FireEmitterComponent`, `MagicParticleEmitterComponent`.
**What it is:** the game's data-driven particle system — every emitter has named parameters (count, spread, color, gravity, lifetime) exposed as bindings; `FireEmitterComponent`/`MagicParticleEmitterComponent` drive the in-game fire/magic effects.
**Ruby payoff:** a **particle editor** in Ruby (edit emitter params → live preview) AND particle rendering in the scene player (fires, sparks on hit, magic bolts). Completely absent today.

### 5. 💡 LightComponent / PointLightManager / ShadowVolumeComponent — real lighting + shadows
**Evidence:** `LightComponent`, `PointLightGroup`, `PointLightManager`, `ShadowComponent::CreateShadowWithShapes(CollisionShapeComponent**, int, FastVector<ShadowVertex>*)`, `ShadowVolumeComponent`, `SimpleGlowComponent`, `WeaponGlowComponent`, `LightOverlay`.
**What it is:** the engine renders **point lights** with **shadow volumes** cast by collision shapes — the game's signature moody lighting. We currently use flat/static lighting.
**Ruby payoff:** dynamic point lights in the scene player (torch flicker, magic glow) + a real-time **shadow overlay** debug view (draw the `ShadowVertex` volumes). Huge visual-polish win, and the data (`LightComponent` bindings) is already in the scene files.

### 6. 🎯 ItemDropComponent — the drop-table system (loot, coins, collectables)
**Evidence:** `ItemDropComponent::CreateItemObjects @0x1F71A0`, `DropItemObjects @0x202C70`, `Trigger`, `ItemDropEntry`/`ItemDrop` structs (FastVector instantiations), `CollectableItemComponent`, `ConsumableItemView`, `CoinBar`, `ExperienceBar`, `HealthBar` (RTTI).
**What it is:** enemies carry **item-drop entries** (type, count, probability); on death `DropItemObjects` spawns collectables in the world; the HUD (`CoinBar`, `ExperienceBar`, `HealthBar`) renders them.
**Ruby payoff:** in the scene player — real loot on monster death (coins/HP/XP), a **drop-table editor** in the inspector, and HUD widgets. Our gamestate already has XP/coins/HP — wire the data-driven drops to it.

### 7. 🚰 WaterMeshComponent + fluid rendering (lava, water, slime)
**Evidence:** `Caver::WaterMeshComponent` (RTTI), `FireBreathComponent`, plus the game's fluid shader pipeline (`FastVector<WaterMeshComponent::Vertex>` instantiations in symbols).
**What it is:** a dedicated **water/lava mesh component** — a flat animated surface (the liquid levels in Swordigo) rendered with a fluid pass.
**Ruby payoff:** render water/lava/slime in the scene player and editor — a translucent animated surface using the component's rect/polygon + height. The user explicitly asked for this ("fluid render — lava, water"); the component data is parsed but never drawn today.

### 8. 🎭 GUI toolkit — the full in-house widget system (menus, HUD, popovers)
**Evidence:** 30+ `GUI*` types: `GUIButton`, `GUISlider`, `GUITextField`, `GUIScrollView`, `GUIWindow`, `GUIDraggableItem`, `GUIEffectView`, `GUINavigationController`, `GUIPopoverView`, `GUISwitch`, `GUIFileDialog`… with `GUIControl::SetValueForBindedProperty` etc.
**What it is:** Caver ships a complete **retained-mode GUI** used for the real game's menus (age gate, shop, settings, level select).
**Ruby payoff:** a scriptable **in-game UI layer** in the scene player (dialogue boxes, HP bars, shop panels) and a reusable widget kit for Ruby's own tool panels — cleaner than raw ImGui calls for complex layouts.

### 9. 🎛️ CameraController — the game's real camera (focus, rumble, targets)
**Evidence:** `Caver::CameraController::Update @0x1F6480`, `FocusAtShape @0x202C40/0x458658`, `FocusAtPoint`, `GotoTargetImmediately`, `ResetFocus`, `RegisterProgramLibrary` + `viewOffset {0,0,2835.6}` (already cited in scene_player.cpp).
**What it is:** the engine's camera is a **target+position pair with per-axis smoothing**, focus-at-shape/point targets, immediate goto and rumble. We ported a partial version into `scene_player.cpp`.
**Ruby payoff:** finish the port — `FocusAtShape` (frame the selected object), smooth zooms, and the game's exact follow feel. Also expose `CameraController` as a reusable camera module for the editor viewport.

### 10. 🛗 PhysicsPlatformComponent + ElevatorControllerComponent — moving/spring platforms & elevators
**Evidence:** `PhysicsPlatformComponent` (schema 1306; Mass @field13, SpringForce @field21 — already parsed in `scene_physics.cpp`), `ElevatorControllerComponent` (RTTI), `PhysicsObjectState::HandleGroundCollision` (platform velocity transfer).
**What it is:** the game supports **moving platforms and elevators** — entities rest on them and inherit platform velocity (we already store `platform_vel` in `PhysicsBody` but never populate it).
**Ruby payoff:** in the scene player, honor `PhysicsPlatformComponent`/`ElevatorControllerComponent` so objects actually ride elevators/moving platforms — the last missing physics feature in the mini-Swordigo.

---

## Bonus honourable mentions (already partly in Ruby, worth deepening)
- **`Scene::LineSegmentIntersectsGround`** (@0x1F78B0) — the game's exact ground probe; our `game_ground_at` + terrain heightfield is a raster approximation. Port the segment-vs-ground-polygon query for pixel-exact ground.
- **`AttackComponent::WorldPointInsideAttackArea`** (@0x292408) — hit-zone testing for combat; could drive sword/hitbox debug rendering.
- **`DimensionObjectComponent`/`DimensionSpellComponent`** — the "other dimension" mechanic; a scene-player dimension-shift toggle would be a showpiece.
- **`ShatterComponent`/`BreakableObjectComponent`/`BushControllerComponent`** — breakable pots/bushes; simple and satisfying to add.
- **`TextBubbleComponent`** — in-game dialogue bubbles.

---

## Collision audit — what was wrong & what changed (2026-08)

Reference implementations (already in our tree, now verified against decompilation):

| Game primitive (decompiled) | Our port | Status |
|---|---|---|
| `CircleIntersectsRectangle` @0x4AD158 | `wall_clamp_circle` (segment distance) | ✅ used by entities + hero |
| `CircleIntersectsPolygon` @0x4ACC90 | `wall_clamp_circle` + polygon walls | ✅ (was hero-missing → fixed) |
| `RectangleIntersectsRectangle` @0x4ACA14 | `ob_intersects_on_axis` SAT | ✅ |
| `IsCollisionNormalValidForPolygonLineSegment` @0x4AC2A8 | `collision_normal_valid_for_polygon` | ✅ |
| `PhysicsObjectState::HandleGroundCollision` @0x280658 | `physics_ground_collision` | ✅ |
| `Scene::LineSegmentIntersectsGround` @0x1F78B0 | terrain heightfield + `game_ground_at` | ⚠️ raster approx (see #10 bonus) |

**Bugs fixed in this pass:**

1. **"Jump under a mesh → Hiro goes up through it"** — the terrain grid stored only *up-facing* triangle tops, so `terrain_ceiling_near` scanned surface TOPS and a rising head only bumped when it was within 6 units of the mesh **top** (tunnelling through the whole body). Fixed: `TerrainGrid` now keeps a **separate ceiling list** (`ceil`/`ceil_cnt`) fed by *down-facing* triangles + ground-polygon bottom edges; `terrain_ceiling_near` reads the true underside. Verified on `florennum_jail_part1`: platform surf −142 / ceil −301 → head from 3 below now bumps at **−301**.

2. **"Hero teleports onto platforms he jumps up under"** — `game_resolve_y`'s landing clause `straddles_now && prev_top >= c.y` fired for a body *rising* into a platform (head crossed the top plane → snap to top). Fixed: land only when coming from above the top plane (`prev_bottom >= c_top − 0.5 && y < c_top`). Unit-tested: 6/6 (fall-land, rise-bump, fast-fall-sweep, rest, pass-through).

3. **"Collision misses sloped polygons"** — the hero only got the AABB-based `game_resolve_x`; the polygon-accurate `wall_clamp_circle` was reserved for monsters. Fixed: Hiro now clamps his body circle (chest center, radius = half hitbox width) against the `CollisionWorld` walls after the AABB pass — the exact treatment monsters get in `scene_entity.cpp`.
