# 01 — Web Editor Feature Inventory (SwordigoEditor)

Reverse-engineered from `assets/index-u_qz_Wej.js` → beautified
`index.beautified.js` (31,818 lines). Line numbers below refer to the
beautified file.

## 1. Tech stack
- **React** UI shell + **Three.js** rendering core.
- `WebGLRenderer` (33 references) — full forward PBR pipeline with:
  - Point + directional shadow maps (`shadowCameraNear/Far`, `shadowBias`,
    `shadowRadius`, `shadowIntensity`, `shadowMapSize`) — lines 6245, 6705,
    8975, 12086, 12197.
  - Standard material define matrix (normal/rough/metal/clearcoat/sheen/
    transmission/iridescence maps) — lines 11497, 11499.
  - Tone mapping, dithering, logarithmic depth buffer.

## 2. Cameras (this is the key parity area)
- **`PerspectiveCamera`** — main scene camera.
- **`OrthographicCamera`** (`isOrthographicCamera`, `type:"OrthographicCamera"`,
  `zoom`, `left/right/top/bottom/near/far`) — lines 9901, 9934. Editor can
  switch projection modes.
- `makeOrthographic` matrix path — lines 2583, 2593.
- **Camera-relative helpers** in `TransformControls`: `cameraPosition`,
  `cameraQuaternion`, `_cameraScale`, `eye` — line 26726; gizmo scale adapts to
  perspective vs ortho zoom — lines 27210.
- `Raycaster` supports both perspective & orthographic unprojection — line 18689.

## 3. Editing / manipulation
- **`TransformControls`** gizmo (move/rotate/scale) with world/parent quaternion
  decomposition — lines 26723–26726. This is the interactive translate/rotate/
  scale handle the native side lacks in 3D.
- Object selection: `selectComponent`, `selectedComponent`, `Raycaster` picking.
- Component CRUD in the inspector: `addComponent`, `removeComponent`,
  `getComponent`, `setComponent`, `pasteComponent`, `materializeComponent`,
  `renderComponent` (React inspector rows).

## 4. Entity / component model
Full component set is enumerated in the bundle (each appears as a schema entry).
Representative list (all confirmed present):

```
HeroEntityComponent, MonsterEntityComponent, EntityComponent, EntityInfoComponent,
EntityControllerComponent, EntityActionComponent,
CharControllerComponent, CharAnimControllerComponent, AnimationControllerComponent,
KeyframeAnimationComponent, BlendAnimationComponent,
ModelComponent, ModelTransformControllerComponent, SpriteComponent, ShapeComponent,
ShadowComponent, LightComponent, SimpleGlowComponent, WeaponGlowComponent,
BackgroundComponent, OverlayTextComponent,
PhysicsObjectComponent, PhysicsPlatformComponent, CollisionShapeComponent,
BoneControlledCollisionShapeComponent, GroundPolygonComponent, GroundMeshComponent,
GroundMeshGeneratorComponent, WaterMeshComponent, TextureMappingComponent,
PortalComponent, PortalEffectComponent, DoorControllerComponent,
ElevatorControllerComponent, PressureTriggerComponent, ObjectLinkControllerComponent,
OrbitControllerComponent, TouchableComponent,
SwingComponent, SwingableWeaponComponent, SwingableWeaponControllerComponent,
WeaponTrailComponent, AttackComponent, DamageComponent, HealthComponent,
SkillComponent, SpellComponent, ProgramComponent,
MagicBoltComponent, MagicBombComponent, MagicExplosionComponent,
MagicHookshotComponent, MagicSpellCastComponent,
ItemDropComponent, CollectableItemComponent, BreakableObjectComponent,
SpawnPointComponent, SoundEffectComponent,
ParticleComponent, ParticleEmitterComponent, ParticleObjectComponent,
FireEmitterComponent, FireBreathComponent,
ProjectileControllerComponent, MonsterControllerComponent,
MonsterDeathControllerComponent, GenericMonsterControllerComponent,
WalkingMonsterControllerComponent, StaticMonsterControllerComponent,
SnappingMonsterControllerComponent, ShootingMonsterControllerComponent,
SkellyMonsterControllerComponent, LeapingMonsterControllerComponent,
ChargingMonsterControllerComponent, BouncingMonsterControllerComponent,
BatMonsterControllerComponent, BushControllerComponent, PropertiesComponent
```

## 5. Scene-graph / gameplay schema fields (portal & camera relevant)
Confirmed named schema fields (`name: "..."`):
- `PortalComponent` (line 21683), `PortalEffectComponent` (line 21707).
- `Portal` (21018), `PortalHint` (20749), `PreviousPortalLevel` (20409).
- `Scale` (20550, 21954), `FollowUpQuest` (20677),
  `TargetMeshId`, `TargetBoneIdentifier`, `TargetObjectIdentifier`,
  `TargetingDistance`.
- Lua-facing: `Game.EnterPortal(scenename, portalname)` — line 25127.

## 6. Renderers / controllers present as live objects
`*Controller` classes that drive live behaviour in the web preview:
`EntityController`, `CharController`, `AnimationController`,
`TransformController`, `ModelTransformController`, `DoorController`,
`ElevatorController`, `ObjectLinkController`, `OrbitController`,
`ProjectileController`, all `*MonsterController` variants, `PortalEffect`.

## 7. Summary of what the web editor uniquely offers
1. A **live, movable 3D camera** (perspective **and** orthographic) with zoom.
2. **In-scene transform gizmos** (translate / rotate / scale) via raycast pick.
3. A **PBR + shadow renderer** that draws every component visually, including
   the **portal effect** and **lights/glows**.
4. Camera-aware helpers (gizmo scaling, ortho depth) — the "camera modifier"
   surface the native side lacks.
