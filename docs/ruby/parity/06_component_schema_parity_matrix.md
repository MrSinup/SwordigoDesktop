# 06 — Component Schema Parity Matrix

## Method
- Extracted every `*Component` token from the web bundle (`index.beautified.js`)
  and from native schema files (`scene_schemas.cpp`, `filerift.cpp`,
  `scene_categories.h`), sorted unique, and diffed with `comm`.

## Headline result: schema parity is COMPLETE ✅

```
comm -23 web_components native_components   # web-only (real components)
→ (none)  — only grep noise: addComponent, getComponent, setComponent,
            removeComponent, selectComponent, pasteComponent, renderComponent,
            materializeComponent, objectComponent, freshComponent, maxComponent,
            blockNumComponent, decodeURIComponent, selectedComponent

comm -13 web_components native_components   # native-only
→ (none)  — only GetComponent (casing noise)
```

Every real gameplay component in the web editor exists in the native schema and
vice-versa. **The data model is at parity.**

## Full shared component set (both sides)
Entities & controllers:
`HeroEntityComponent, MonsterEntityComponent, EntityComponent,
EntityInfoComponent, EntityControllerComponent, EntityActionComponent,
CharControllerComponent, CharAnimControllerComponent,
AnimationControllerComponent, KeyframeAnimationComponent, BlendAnimationComponent`

Monsters:
`MonsterControllerComponent, MonsterDeathControllerComponent,
GenericMonsterControllerComponent, WalkingMonsterControllerComponent,
StaticMonsterControllerComponent, SnappingMonsterControllerComponent,
ShootingMonsterControllerComponent, SkellyMonsterControllerComponent,
LeapingMonsterControllerComponent, ChargingMonsterControllerComponent,
BouncingMonsterControllerComponent, BatMonsterControllerComponent,
BushControllerComponent, ProjectileControllerComponent`

Rendering / visual:
`ModelComponent, ModelTransformControllerComponent, SpriteComponent,
ShapeComponent, ShadowComponent, LightComponent, SimpleGlowComponent,
WeaponGlowComponent, BackgroundComponent, OverlayTextComponent,
TextureMappingComponent`

Geometry / physics:
`PhysicsObjectComponent, PhysicsPlatformComponent, CollisionShapeComponent,
BoneControlledCollisionShapeComponent, GroundPolygonComponent,
GroundMeshComponent, GroundMeshGeneratorComponent, WaterMeshComponent`

Interaction / world:
`PortalComponent, PortalEffectComponent, DoorControllerComponent,
ElevatorControllerComponent, PressureTriggerComponent,
ObjectLinkControllerComponent, OrbitControllerComponent, TouchableComponent,
SpawnPointComponent, SoundEffectComponent, BreakableObjectComponent`

Combat / items / magic:
`SwingComponent, SwingableWeaponComponent, SwingableWeaponControllerComponent,
WeaponTrailComponent, AttackComponent, DamageComponent, HealthComponent,
SkillComponent, SpellComponent, ProgramComponent, MagicBoltComponent,
MagicBombComponent, MagicExplosionComponent, MagicHookshotComponent,
MagicSpellCastComponent, ItemDropComponent, CollectableItemComponent`

Particles / fx:
`ParticleComponent, ParticleEmitterComponent, ParticleObjectComponent,
FireEmitterComponent, FireBreathComponent`

Misc: `PropertiesComponent`

## Interpretation
- **Authoring parity: DONE.** Native (`filerift`/`scene_schemas`/`scene_loader`)
  can serialize every component the web editor knows.
- **Rendering/interaction parity: NOT DONE.** The gap is entirely in *visual
  editing* — see files 03, 04, 05. Specifically the components that have no
  native **editor-viewport** visual are the camera-relative view + the two
  portal components (`PortalComponent`, `PortalEffectComponent`) and, secondarily,
  live previews for `LightComponent`/`ParticleEmitterComponent`/`WaterMeshComponent`
  (native has partial runtime/asset-view support but no unified scene preview).

## Notes on `NO`-render components (candidates for future editor previews)
| Component | Native runtime render? | Native editor-viewport render? |
|-----------|------------------------|--------------------------------|
| PortalComponent | via `fbo_scaler` (game) | ❌ |
| PortalEffectComponent | via `fbo_scaler` (game) | ❌ |
| LightComponent | `av_renderer` glow sprites | partial (asset viewer only) |
| WaterMeshComponent | `av_renderer` water sheet | partial |
| ParticleEmitterComponent | runtime | ❌ |
| Camera (view, not a component) | orbit only | ❌ ortho/framing |
