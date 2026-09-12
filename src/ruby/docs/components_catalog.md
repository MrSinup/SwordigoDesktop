# Swordigo Caver Engine: Complete Components Catalog

## 1. Overview
The Caver Engine implements a rich library of 76 specialized components registered during engine initialization in `DefaultComponents::RegisterAll` (at virtual address `0x1E7FA0` in the ARM32 binary). Each component provides modular functionality attached to `SceneObject` entities.

---

## 2. Rendering Components

### 2.1 `Model` (`ModelComponent`)
Renders a 3D textured, skinned or static mesh model from PowerVR `.pod` binary assets.
- **Fields**:
  - `ModelName` (`string`, default: `""`): Name of the `.pod` mesh asset file.
  - `TextureName` (`string`, default: `""`): Primary diffuse texture.
  - `CastShadow` (`bool`, default: `true`): Enables ground shadow projection.
  - `Wireframe` (`bool`, default: `false`): Debug rendering mode.
- **Real-World Usage (`monsters.scl`)**:
  ```filerift
  Component {
      ClassName: 'Model'
      ModelName: 'monster_corruptor'
      CastShadow: true
  }
  ```

### 2.2 `Sprite` (`SpriteComponent`)
Renders a 2D camera-facing or plane-aligned quad billboard.
- **Fields**:
  - `TextureName` (`string`): Diffuse texture mapping.
  - `Width`, `Height` (`float`): Dimensions in world units.
  - `BlendMode` (`int`, default: `0`): `0 = ALPHA`, `1 = ADDITIVE`.

### 2.3 `KeyframeAnimation` (`KeyframeAnimationComponent`)
Controls skeletal matrix animation playback over keyframed timeline channels.
- **Fields**:
  - `AnimationSpeed` (`float`, default: `1.0`): Playback rate multiplier.
  - `Loop` (`bool`, default: `true`): Automatic restart on completion.
- **Lua API**:
  `KeyframeAnimation.SetCurrentTime(self, animId, time)`, `KeyframeAnimation.TimeToCompletion(self, animId)`

### 2.4 `BlendAnimation` (`BlendAnimationComponent`)
Blends smooth transitions between disparate skeletal animation poses.

### 2.5 `GroundMesh` & `GroundMeshGenerator` (`GroundMeshComponent`, `GroundMeshGeneratorComponent`)
Generates and renders procedural continuous 2D/2.5D terrain surface meshes with collision edges.
- **Fields**:
  - `FillTexture` (`string`): Internal terrain fill pattern.
  - `BorderTexture` (`string`): Top rim edge border texture.
  - `Depth` (`float`, default: `100.0`): Extrusion depth.

### 2.6 `GroundPolygon` (`GroundPolygonComponent`)
Defines the boundary vertices for terrain surfaces.

### 2.7 `WaterMesh` (`WaterMeshComponent`)
Renders animated procedural water surfaces with sinusoidal wave displacement and reflection tints.

### 2.8 `Background` & `RotatingBackground` (`BackgroundComponent`, `RotatingBackgroundComponent`)
Parallax-scrolling scenery planes and rotating celestial backdrops (sun, moon, clouds).

---

## 3. Combat, Damage & Health

### 3.1 `Damage` (`DamageComponent`)
Inflicts damage on opposing faction entities upon physical intersection.
- **Fields**:
  - `Damage` (`int`, default: `10`): Hitpoints subtracted.
  - `DamageType` (`int`, default: `0`): `0 = PHYSICAL`, `1 = MAGIC`, `2 = FIRE`, `3 = COLD`, `4 = PURE`.
  - `SpecialDamageType` (`int`, default: `0`): `0 = NONE`, `1 = KNOCKBACK`, `2 = STUN`.
  - `Faction` (`int`, default: `1`): `0 = NEUTRAL`, `1 = ENEMY`, `2 = FRIENDLY / HERO`.
- **Real-World Usage (`traps.scl`)**:
  ```filerift
  Component {
      ClassName: 'Damage'
      Damage: 25
      DamageType: 2 # FIRE
      SpecialDamageType: 1 # KNOCKBACK
  }
  ```

### 3.2 `Health` (`HealthComponent`)
Maintains hitpoints, death transitions, and invulnerability timers.
- **Fields**:
  - `MaxHealth` (`int`, default: `100`): Maximum life total.
  - `CurrentHealth` (`int`, default: `100`): Initial hitpoints.
  - `HealthType` (`int`, default: `0`): `0 = NORMAL`, `1 = INVULNERABLE`, `2 = GHOST`.
  - `DeathProgram` (`string`, optional): Lua script triggered when health reaches zero.
- **Lua Hooks**:
  `OnHurt(self, damage, attacker)`, `OnDeath(self, attacker)`

### 3.3 `CollisionShape` & `BoneControlledCollisionShape`
Defines physical hitbox and hurtbox geometry (Box, Circle, Capsule, Polygon).
- **Fields**:
  - `ShapeType` (`string`): `"Box"`, `"Circle"`, `"Capsule"`.
  - `Width`, `Height`, `Radius` (`float`).
  - `IsTrigger` (`bool`, default: `false`): Intersect without physical blockage.
  - `BoneName` (`string`, optional): Bone matrix node to attach collision volume to.

### 3.4 `SwingableWeapon` & `SwingableWeaponController`
Weapon geometry and animation mechanics for swords and handheld arms.

### 3.5 `WeaponTrail` & `WeaponGlow`
Draws curved dynamic motion ribbon trails and magical elemental particle glows along the blade edge.

### 3.6 `BreakableObject` & `Shatter`
Destructible world props (pots, crates, urns, crystal barriers) that shatter into physical debris.

---

## 4. AI & Monster Controllers

### 4.1 `EntityController` (`EntityControllerComponent`)
Core decision tree and state machine for mobile actors and adversaries.
- **Fields**:
  - `DefaultMoveSpeed` (`float`, default: `100.0`): Standard patrol speed.
  - `RunSpeed` (`float`, default: `220.0`): Aggro chase speed.
  - `SightRange` (`float`, default: `300.0`): Target acquisition radius.
  - `MovementBehavior` (`string`, default: `"patrol"`): `"patrol"`, `"fight"`, `"none"`.

### 4.2 Specialized Monster Controllers
- **`WalkingMonsterController`**: Ground-based patrol, ledge-detection, and melee charge.
- **`ChargingMonsterController`**: Bull-rush attack with telegraph anticipation and wall stun.
- **`SnappingMonsterController`**: Ambush plant/creature snapping at passing heroes.
- **`LeapingMonsterController`**: Parabolic jumping attack toward hero coordinates.
- **`SkellyMonsterController`**: Skeleton warrior mechanics with sword swing and shield blocking.
- **`ShootingMonsterController` / `ProjectileMonsterController`**: Ranged spellcasting and projectile volley timing.
- **`BatMonsterController`**: Sine-wave flying patrol with swooping dive-bomb strikes.
- **`BouncingMonsterController`**: Elastic kinetic bouncing along terrain surfaces.
- **`MonsterDeathController`**: Death dissolve, soul dissipation, and coin drop dispatch.

---

## 5. Physics, Motion & Environment

### 5.1 `PhysicsObject` (`PhysicsObjectComponent`)
Simulates rigid body kinematics with gravity, mass, friction, and restitution.
- **Fields**:
  - `Mass` (`float`, default: `1.0`): Dynamic mass (0 = static).
  - `Friction` (`float`, default: `0.2`): Surface resistance.
  - `Restitution` (`float`, default: `0.0`): Bounciness.
  - `AffectedByGravity` (`bool`, default: `true`).

### 5.2 `PhysicsPlatform` (`PhysicsPlatformComponent`)
Moving, floating, or one-way jump-through platforms.
- **Fields**:
  - `PassDirection` (`int`, default: `1`): `1 = TOP_DOWN` (jump-through platform).
  - `CarryEntities` (`bool`, default: `true`): Transfers platform momentum to resting actors.

### 5.3 `ElevatorController` & `DoorController`
Mechanized moving elevators and locked/unlocked portal gates.

### 5.4 `TransformController` & `ModelTransformController`
Procedural interpolation components for movement, rotation, scaling, and oscillating hovering.

### 5.5 `ObjectLinkController`
Maintains rigid parent-child transform hierarchy attachments.

---

## 6. Spells & Magic System

- **`MagicBolt`**: Fast linear projectile with trail emission and direct magical damage.
- **`MagicBomb`**: Heavy projectile subject to gravity, rolling, and timed detonation.
- **`MagicHookshot`**: Grappling hook line latching onto wooden rings and pulling hero.
- **`DimensionSpell` & `DimensionObject`**: Dimensional rift ability revealing hidden spectral terrain and spectral platforms.
- **`MagicExplosion`**: Radial shockwave dealing pure/magic damage and destroying breakable obstacles.
- **`FireBreath`**: Continuous cone of flame damage.

---

## 7. FX, Lighting & Sound

- **`Light` (`LightComponent`)**: 2D/3D dynamic omni-directional point light with radius, color, and attenuation.
- **`ParticleEmitter` / `FireEmitter` / `MagicParticleEmitter`**: High-performance GPU billboard particle systems.
- **`SoundEffect` (`SoundEffectComponent`)**: Audio source emitting sound triggers based on proximity or events.
- **`PortalEffect`**: Swirling dimensional vortex visualization.

---

## 8. Interactive World & Triggers

- **`Portal` (`PortalComponent`)**: Scene transition trigger linking to target level maps and spawn markers.
- **`SpawnPoint` (`SpawnPointComponent`)**: Player respawn and world entry anchor.
- **`Touchable` & `PressureTrigger`**: Contact trigger surfaces activating scripts, doors, or traps.
- **`CollectableItem` & `ItemDrop`**: Coins, health orbs, shards, and treasure pickups.

---

## 9. GUI, Text & Scripting

- **`Program` (`ProgramComponent`)**: Holds embedded Lua 5.1 script coroutines.
- **`TextBubble` (`TextBubbleComponent`)**: In-world dynamic speech bubble rendering.
- **`OverlayText` (`OverlayTextComponent`)**: Screen-space floating damage numbers and status text.
