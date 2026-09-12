# 05 — Component Catalog

> The complete registry of scene-component classes known to the Ruby SDK
> (`src/tools/scene_schemas.cpp`, 134 schema entries). Field numbers are the protobuf
> field numbers **inside each component payload**; the object-level registry (field numbers
> inside the `Object` message) is listed in §1.

---

## 1. Object-level component registry (field numbers in the `Object` message)

These are the canonical protobuf field numbers used when serializing components onto an
object. **Observed** in `scene_schemas.cpp`:

```
882  GroundPolygonComponent        970  CollisionShapeComponent
890  GroundMeshComponent           978  DamageComponent
898  GroundMeshGeneratorComponent  986  HealthComponent
906  TextureMappingComponent       994  BoneControlledCollisionShapeComponent
914  WaterMeshComponent            1002 ObjectLinkControllerComponent
962  ShapeComponent                1042 LightComponent
                                   1050 ShadowComponent
1122 SoundEffectComponent
1194 AnimationControllerComponent  1202 CharAnimControllerComponent
1210 CharControllerComponent       1218 EntityComponent
1226 BushControllerComponent       1234 ElevatorControllerComponent
1242 PressureTriggerComponent      1250 DoorControllerComponent
1258 ProgramComponent              1266 MonsterEntityComponent
1274 PhysicsObjectComponent        1282 BreakableObjectComponent
1290 EntityControllerComponent     1298 EntityActionComponent
1306 PhysicsPlatformComponent      1314 EntityInfoComponent
1322 HeroEntityComponent           1602 BackgroundComponent
1682 PropertiesComponent           2002 ParticleEmitterComponent
2050 OrbitControllerComponent
2418 MonsterControllerComponent    2426 WalkingMonsterControllerComponent
2434 ChargingMonsterControllerComponent  2442 SnappingMonsterControllerComponent
2450 AttackComponent               2458 LeapingMonsterControllerComponent
2466 SkellyMonsterControllerComponent     2474 StaticMonsterControllerComponent
4002 PortalComponent               4010 SpawnPointComponent
4018 CollectableItemComponent      4034 TouchableComponent
4042 ItemDropComponent             4050 OverlayTextComponent
4058 PortalEffectComponent
4402 MagicBoltComponent            4410 MagicExplosionComponent
4418 SkillComponent                4426 MagicSpellCastComponent
4434 FireBreathComponent           4442 ProjectileControllerComponent
4450 MagicBombComponent            4458 MagicHookshotComponent
4466 SpellComponent
```

---

## 2. The workhorse components (full payload schemas)

### `BackgroundComponent`
| Field | Name |
|-------|------|
| 10 | `TextureName` (e.g. `grasslandsbackground_day`, `cavesbackground2`) |

### `LightComponent`
| Field | Name |
|-------|------|
| 8 | `Type` (1 ambient / 2 key / 3 main / 4 black-fill) |
| 21 | `Intensity` |
| 26 | `Color` (FloatColor RGBA) |
| 37 | `LinearAttenuation` |
| 45 | `QuadraticAttenuation` |
| 50 | `Offset` (Vector3) |
| 61 | `Radius` |

### `CollisionShapeComponent` — see [04 §3](04_spawn_points_portals_triggers.md)
`IsGround, Collides, ReceivesDamage, InflictsDamage, MinDepth, MaxDepth, SpecialType,
OnCollide, OnCollisionEnd, Enabled, OnReceiveDamage, Friction, UnsafeGround`

### `SpawnPointComponent` — see [04 §1](04_spawn_points_portals_triggers.md)
`FacingDirection, SpawnOffset`

### `PortalComponent` / `PortalEffectComponent` — see [04 §2](04_spawn_points_portals_triggers.md)
Portal: `DestinationSceneName, SpawnPointName, TapToEnter, TriggerShapeId`
PortalEffect: `PolygonId, TextureMappingId, Color, Speed`

### `GroundPolygonComponent` (Observed in decoded scenes)
```
Polygon{ Vertex{X,Y}…, Convex : 0, Closed : 1 }
Collides : 1
MinDepth : -45
MaxDepth : 45
```

### `GroundMeshComponent` (Observed in `new_level.scene`)
```
LocalAabb{ X,Y,Width,Height }
SurfaceMesh{ NumVertices, NumFaces, Indices{…}, Vertices{…}, Normals{…}, TexCoordSet{…},
             Material{ AmbientColor, DiffuseColor, SpecularColor, Shininess,
                       Texture{ Name, PixelFormat, ImageType } },
             BoundingBox{ X,Y,Z,Width,Height,Depth }, VertexData, IndexData }
FrontMesh{ … }          # front-facing wall strip
Color{ R,G,B,A }
```

### `GroundMeshGeneratorComponent` (Observed schema + `new_level.scene` values)
| Field | Name | Example value |
|-------|------|---------------|
| 8 | `GroundPolygonId` | 980 |
| — | `TargetMeshId` | 981 |
| — | `FrontTextureMappingId` | 985 |
| — | `SurfaceTextureMappingId` | 984 |
| — | `RandomSeed` | 1291618994 |
| — | `HorizNoise` | 0 |
| — | `MeshType` | 1 |
| — | `SurfaceWidth` | 80 |
| — | `HatHeight` | 25 |
| — | `HatWidthOffset1` / `HatWidthOffset2` | 5 / 5 |

> This is the engine's procedural ground-mesh generator — the Ruby SDK's own GMG tool
> (`boulder.h`, ported from DanielSpaniel's Boulder engine) writes these.

### `TextureMappingComponent` (Observed)
```
TextureName : 'fire_grass'
Scale : 250
Offset{ X:0  Y:0 }
```

### `ShapeComponent`
`Rectangle{X,Y,Width,Height}` | `Polygon{Vertex…, Convex, Closed}` | `Box{X,Y,Z,Width,Height,Depth}` (schema)

### `WaterMeshComponent` — see [07 §5](07_lua_scripting_api.md)
Water/lava mesh (observed in `florennum_cave1`, `fire_part1`, `florennum_jail_part1`).

---

## 3. Entities & AI (Observed schemas)

### `EntityComponent`
`FacingDirection, PhysicsEnabled` (observed in `hero.scene`: `FacingDirection:1, PhysicsEnabled:0`)

### `HeroEntityComponent`
`OnItemGet{ String : <Lua>, Bytes : <compiled> }` (observed in `hero.scene` — the
item-pickup cinematic script)

### `MonsterEntityComponent`
Marker that makes an object an AI entity (combined with `EntityController`,
`EntityAction`, `Health`, `Damage`, `KeyframeAnimation`, `AnimationController`).
Observed on `boss`, `obj3` (`fire_partBoss`), `darkhero` (`menu.scene`).

### Monster controller components (AI archetypes)
| Component | Observed/anthropology |
|-----------|----------------------|
| `WalkingMonsterControllerComponent` | ground walkers (`WalkAnimationId`, …) |
| `BatMonsterControllerComponent` | flyers (`FlyAnimationId`, `FlapSoundId`) |
| `BouncingMonsterControllerComponent` | hoppers (`JumpAnimationId`, `FallAnimationId`, `JumpAngle`, `JumpSpeed`) |
| `ChargingMonsterControllerComponent` | chargers |
| `LeapingMonsterControllerComponent` | lurkers (cave lurkers) |
| `SkellyMonsterControllerComponent` | skeletons |
| `ShootingMonsterControllerComponent` | ranged |
| `SnappingMonsterControllerComponent` | snap/plant (carniplant) |
| `StaticMonsterControllerComponent` | turret-like |
| `GenericMonsterControllerComponent` | generic |

> **Key finding:** most enemy behavior lives in **Lua** (`Program`/`EntityAction`),
> not in these components (see [07](07_lua_scripting_api.md)). The controller components
> mostly pick animation + movement archetypes.

### `EntityControllerComponent` / `EntityActionComponent`
- `EntityAction` has a `Label` (e.g. `'ballistic_bolt'`) + `OnActivate{ String, Bytes }`
  Lua script. Actions are invoked by number (`EntityController.PerformAction(self, 113/136/140/145)`).

### `CharAnimControllerComponent`
`StandAnimationId, WalkAnimationId, JumpAnimationId, FallAnimationId, CastAnimationId, AirJumpAnimationId`

### `CharControllerComponent` (hero controller)
`DefaultAnimationControllerId, RightWeaponControllerId, LeftWeaponControllerId,
NormalRunSpeed, JumpSpeed, NormalMaxJumpTime, EntityId, SwingComponentId,
LiftAnimationControllerId, LiftAnimationId, DropAnimationId, ThrowAnimationId,
HurtAnimationId, DieAnimationId`

### `HealthComponent`
`MaxHealth, HEALTH_TYPE, BarOffset(Vector3)`

### `DamageComponent`
`MinDamage, DamageType`

### `AnimationControllerComponent`
`ModelId, DefaultAnimationId, SelfUpdate`

### `AttackComponent`
`AnimationId, CollisionShapeId, AttackAreaId, SoundEffectId, AttackInterval,
AttackDuration, DamageStartTime, DamageEndTime, AnimationStartBlendTime,
AnimationEndBlendTime, OnAttack(Program), DamageStartTime2, DamageEndTime2`

### `BreakableObjectComponent`
`BreaksOnImpact, NumHitsToBreak, RequiredDamageType, OnBreak(Program)`

---

## 4. Placement/behavior components

### `KeyframeAnimationComponent`
Keyframe animation tracks (position/rotation/scale keyframes) — observed on the boss,
`obj3`, `darkhero`, moving objects.

### `OrbitControllerComponent`
`RotationAxis(Vector3), …` — object orbits an axis (observed in `grove_part1`: 2 objects).

### `TransformControllerComponent` / `ModelTransformControllerComponent`
Lua-driven transform animation (`TranslateBy`, `ScaleTo`, `SetOrigin`, `SetRotationSpeed`).

### `PhysicsObjectComponent`
`SetEnabled`, `SetGravityDirection`, `SetGravityMagnitude`, `SetDecelerationForce` via Lua
(observed in boss script).

### `ParticleEmitterComponent` / `ParticleComponent`
Particle systems (fire, sparks, glow) — `SimpleGlow` for additive glow.

### `ItemDropComponent` / `CollectableItemComponent` / `TouchableComponent`
Pickup + drop systems. `CollectableItem.RequiresPickup(item)` in hero's OnItemGet.

### `SoundEffectComponent` / `OverlayTextComponent` / `PropertiesComponent`
Sound, floating text (NPC dialog), and arbitrary key-value properties
(`Properties.GetProperty(self, "targetPosition")`).

### `ProgramComponent`
The universal Lua container: `{ String : <source>, Bytes : <compiled Lua> }`,
with wrapper flags `Enabled` + `Trigger`.

---

## 5. Component census across all 118 scenes (Observed, top classes)

```
6059 CollisionShape   2286 GroundPolygon   2221 GroundMesh    1469 Model
 995 Program           625 Particle         567 Light          393 SpawnPoint
 287 EntityController  274 Portal           241 ParticleEmitter 227 Background
 214 AnimationController 197 PhysicsObject  183 SimpleGlow     164 Damage
 140 DirectionalLight  130 MonsterEntity    124 Character      114 EntityAction
 110 Health            104 ItemDrop          98 … (long tail)
```

These are the *scene-native* classes (strings census also catches Lua API names like
`Camera`, `Scene`, `Game`, `Wait`, `Find` — filtered in the docs' API file).
