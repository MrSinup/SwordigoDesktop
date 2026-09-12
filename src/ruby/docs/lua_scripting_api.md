# Swordigo Caver Engine: Lua Scripting API Specification

## 1. Overview
Scripts in Swordigo are written in **Lua 5.1** and embedded within FileRift `.scl` or `.scene` assets between dollar delimiters (`$ ... $end`) or as compiled bytecodes. Each script block executes in an asynchronous coroutine inside the active scene's `ProgramState`.

Event entry points pass arguments via Lua varargs (`...`):
```lua
local self, target = ...;
```
- `self`: The `SceneObject` executing the script block.
- `target`: The secondary `SceneObject` involved in the trigger (e.g. the colliding entity, attacker, or activator).

---

## 2. `SceneObject` Instance Methods

When a `SceneObject` is pushed to Lua, it provides the following member methods invoked with `:` syntax:

| Method | Parameters | Return | Description |
| :--- | :--- | :--- | :--- |
| `:identifier()` | `()` | `string` | Returns the unique string name of the entity in the scene graph. |
| `:position()` | `()` | `Vector3` | Returns the current world-space coordinates as a 3D vector. |
| `:setPosition(pos)` | `Vector3` | `void` | Teleports the entity instantly to the specified position. |
| `:velocity()` | `()` | `Vector3` | Returns the current linear velocity vector. |
| `:setVelocity(vel)` | `Vector3` | `void` | Sets the instantaneous linear velocity vector. |
| `:scaling()` | `()` | `number` | Returns the scalar size factor of the entity. |
| `:setScaling(scale)` | `number` | `void` | Sets the scalar size factor of the entity. |
| `:hidden()` | `()` | `boolean` | Returns `true` if the entity's render components are currently hidden. |
| `:setHidden(hide)` | `boolean` | `void` | Toggles entity visibility. |
| `:setAlwaysActive(active)` | `boolean` | `void` | Prevents the entity from freezing or culling when outside the viewport frustum. |
| `:alpha()` | `()` | `number` | Returns the opacity factor (range `0.0` to `1.0`). |
| `:setAlpha(alpha)` | `number` | `void` | Sets the opacity factor for sprite and model rendering. |
| `:destroy()` | `()` | `void` | Deallocates and permanently removes the entity from the active scene. |
| `:damage(amount, type, origin)` | `number, number, Vector3` | `void` | Inflicts damage using engine `DamageType` constants. |

---

## 3. Static Engine Modules

### 3.1 `Scene`
Manages the level entity hierarchy, object instantiation, and searching.

```lua
-- Find an existing object by identifier
local boss = Scene.Find("boss_phase2");

-- Dynamically instantiate an object from a template
local blast = Scene.CreateObject("blast_template", "unique_blast_id", parent);

-- Spawn an object into the scene at coordinates
local obj = Scene.AddObject("coin_gold", 150.0, 320.0);

-- Deallocate and remove an object from the active scene
Scene.RemoveObject(blast);
```

### 3.2 `EntityController`
Controls AI navigation, behavior trees, movement speeds, and combat actions.

```lua
-- Query current combat target
local target = EntityController.Target(self);

-- Movement & Navigation
EntityController.SetMovementBehavior(self, "fight"); -- Options: "fight", "patrol", "none"
EntityController.SetMoveSpeed(self, 200.0);
EntityController.SetMoveDirection(self, 1.0);       -- 1.0 = Right, -1.0 = Left
EntityController.SetFacingDirection(self, 1.0, 0.2); -- Direction, Turn Duration
local currentDir = EntityController.MoveDirection(self);
local defaultSpeed = EntityController.DefaultMoveSpeed(self);

-- State & Actions
if EntityController.IsIdle(self) then
    EntityController.PerformAction(self, 126); -- Trigger action sequence by ID
end
```

### 3.3 `Entity`
Direct manipulation of character entity properties.

```lua
Entity.SetFacingDirection(hero, -1);
Entity.SetPosition(hero, Vector3.New(100, 50, 0));
local dir = Entity.FacingDirection(hero);
```

### 3.4 `Character`
Accesses global player progression, inventory, and level save state flags.

```lua
-- Check and modify persistent scene progression flags
if not Character.HasSceneFlag("woodkeep_boss_defeated") then
    Character.AddSceneFlag("woodkeep_boss_defeated");
end

-- Inventory & Weapon queries
if Character.HasItem("iselon_shard_1") then
    DoorController.Open(Scene.Find("endDoor"));
end
local level = Character.ExperienceLevel();
local weapon = Character.EquippedWeapon();
```

### 3.5 `KeyframeAnimation` & `AnimationController`
Controls skeletal / vertex timeline playback and animation cross-fading.

```lua
local animRun = 132;
KeyframeAnimation.SetCurrentTime(self, animRun, 0.0);
AnimationController.BlendToAnimation(self, animRun);

-- Wait for animation track to complete playback
Program.Wait(KeyframeAnimation.TimeToCompletion(self, animRun));
```

### 3.6 `Health`
Manages entity hitpoints, damage mitigation, and life status.

```lua
local percent = Health.CurrentPercent(self); -- Returns 0.0 to 100.0
local curHp   = Health.CurrentHealth(self);
local maxHp   = Health.MaxHealth(self);

-- Set exact hitpoints
Health.SetHealth(self, maxHp);

-- Apply explicit damage calculation
Health.Damage(self, 25, 0); -- Amount, DamageType (0 = PHYSICAL)
```

### 3.7 `Camera`
Viewport camera control, cinematics, focus points, and shake.

```lua
-- Focus camera smoothly onto a target coordinate or collision volume
Camera.FocusAtPoint(self:position() + Vector3.New(0, 50, 0));
Camera.FocusAtShape(self, shapeId);

-- Trigger screen rumble / trauma shake
Camera.Rumble();

-- Return control to default player tracking
Camera.ResetFocus();
```

### 3.8 `Game`
Global game state, screen transitions, and cinematic presentation.

```lua
-- Flash screen bright white (impact / explosion effect)
Game.Flash();

-- Toggle cinematic letterboxing and input disabling
Game.SetCinematicMode(true, false); -- CinematicMode, HideUI

-- Trigger checkpoint save
Game.SaveGame();
```

### 3.9 `TransformController`
Procedural linear / angular interpolation of transforms over time.

```lua
-- Interpolate position, scale, or rotation over duration (in seconds)
TransformController.MoveTo(self, 400.0, 150.0, 1.5);
TransformController.ScaleTo(self, 0.001, 3.0);
TransformController.RotateBy(self, 3.14159, 0.5);
TransformController.SetOrigin(self, Vector3.New(0, 40, 0));
```

### 3.10 `PhysicsObject` & `CollisionShape`
Controls physical rigid bodies, velocities, impulses, and collision shape toggles.

```lua
-- Physics Body
PhysicsObject.SetEnabled(self, true);
PhysicsObject.ApplyImpulse(self, 0, 500); -- Jump impulse
local vel = PhysicsObject.GetVelocity(self);

-- Collision Shapes
CollisionShape.SetEnabled(self, 113, true);  -- Enable hurtbox / hitbox by shape ID
CollisionShape.SetEnabled(self, 113, false);
```

### 3.11 `DoorController` & `ElevatorController`
Controls physical doors and moving platforms.

```lua
DoorController.Open(Scene.Find("dungeon_gate"));
DoorController.Close(Scene.Find("dungeon_gate"));
local isOpen = DoorController.IsOpen(Scene.Find("dungeon_gate"));
```

### 3.12 `SoundLibrary` & `MusicPlayer`
Audio and background music playback.

```lua
-- Global and localized sound effects
SoundLibrary.PlayEffect("bossgrowl");
Sound.Play("sword_slash");
Sound.PlayAt("explosion_distant", 1200, 350);

-- Music tracks
MusicPlayer.PlayMusic("bosskill", false, false); -- TrackName, Loop, Crossfade
MusicPlayer.FadeOut(1.0);                         -- Duration in seconds
```

### 3.13 `Program`
Coroutine flow control and thread suspension.

```lua
-- Suspends current script execution for specified time in seconds
Program.Wait(0.25);
```

---

## 4. Global Utility Functions

| Function | Parameters | Return | Description |
| :--- | :--- | :--- | :--- |
| `Vector3.New(x, y, z)` | `number, number, number` | `Vector3` | Creates a new 3D spatial coordinate vector. |
| `Vector2.New(x, y)` | `number, number` | `Vector2` | Creates a new 2D vector. |
| `Math.RandomInt(min, max)` | `integer, integer` | `integer` | Generates a pseudo-random integer in the range `[min, max]`. |
| `DirectionToTargetFromPosition(targetPos, selfPos)` | `Vector3, Vector3` | `number` | Calculates normalized horizontal direction (`1.0` or `-1.0`) toward target. |
| `ShowTextBubble(id, pos, text)` | `string, Vector3, string` | `SceneObject` | Spawns a floating speech bubble entity. |
| `ShowTextBubbles(id, pos, loop, list)` | `string, Vector3, bool, table` | `SceneObject` | Spawns a multi-line sequential dialogue sequence. |
| `HideTextBubble(id)` | `string` | `void` | Dismisses the specified dialogue bubble. |

---

## 5. Authentic Real-World Script Example

The following script from `Scener/data/decoded_scenes/plains_woodkeep3.scene` demonstrates realistic orchestration of the engine APIs during a boss defeat sequence:

```lua
local self = ...;

self:setAlwaysActive(true);
Program.Wait(0.01);

Game.Flash();
SoundLibrary.PlayEffect("bossgrowl");

Game.SetCinematicMode(true, false);
Camera.FocusAtPoint(self:position() + Vector3.New(0, 50, 0));

local hero = Scene.Find("hero");
local heroDirection = -1;
if hero:position():x() > self:position():x() then
    heroDirection = 1;
end

Entity.SetFacingDirection(hero, -heroDirection);

ShowTextBubbles("bossBubble", self:position() + Vector3.New(0, 80, 0), false, {
    "NOOOO.....",
});

TransformController.SetOrigin(self, Vector3.New(0, 40, 0));
TransformController.ScaleTo(self, 0.001, 3.0);

Program.Wait(1.0);
HideTextBubble("bossBubble");

Program.Wait(1.0);
MusicPlayer.FadeOut(1.0);

Program.Wait(1.0);
SoundLibrary.PlayEffect("bossgrowl2");

local explosion = Scene.CreateObject("boss_explosion", "boss_explosion", self);
Game.Flash();

MusicPlayer.PlayMusic("bosskill", false, false);
Program.Wait(0.8);

Camera.ResetFocus();
self:destroy();

if Character.HasItem("iselon_shard_1") then
    DoorController.Open(Scene.Find("endDoor"));
end

Game.SetCinematicMode(false, true);
```
