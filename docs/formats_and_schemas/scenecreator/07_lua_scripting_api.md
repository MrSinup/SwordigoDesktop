# 07 — Lua Scripting & the Data-Driven Behavior Layer

> The single most important discovery for a Scene Creator: **Swordigo's behaviors — enemy
> AI, boss attacks, elevators, doors, chests, quests — are authored in Lua**, compiled to
> Lua 5.x bytecode and embedded in scene files inside `Program`/`EntityAction` components
> (and scene-level `OnLoad` scripts). The Ruby SDK already has a Lua 5.x VM
> (`src/sre/lua`) and FileRift can extract the embedded Lua source from the binary
> (`extract_lua_generic`).

---

## 1. Where Lua lives in a scene

| Container | Root/Object | Purpose | Example |
|-----------|-------------|---------|---------|
| `ProgramComponent{ String, Bytes }` | any object | looping/per-frame behavior | elevator, fire, boss AI |
| `EntityActionComponent{ Label, OnActivate{String,Bytes} }` | entities | named actions triggered by `EntityController.PerformAction(self, <id>)` | boss attacks (`ballistic_bolt`) |
| `CollisionShapeComponent.OnCollide` / `OnCollisionEnd` | collision shapes | overlap hooks | triggers |
| `HeroEntityComponent.OnItemGet` | hero | item pickup cinematic | `hero.scene` |
| Root field 5 `OnLoad` | scene | runs at scene load | `town_part1` (2015 B), `plains_part2` (609 B), `icecastle_*` (215 B) |

The decoded structure of a Program component:

```
Component{
    ClassName : 'Program'   Identifier : <id>
    ProgramComponent{
        String : $ local self = ...; ... source ... $end
        Bytes : '\x1bLuaQ...'          # compiled Lua 5.x (LuaQ magic)
    }
    Enabled : 1
    Trigger : 1                        # wrapper flag; 1 for trigger-style scripts
}
```

> `Bytes` begins with `\x1bLuaQ` (Lua 5.0-style "LuaQ" magic). FileRift's
> `extract_lua_generic()` recovers the source — see `tests/filerift_smoke.cpp`.

---

## 2. Lua API census — every API used by shipped scenes (Observed)

### Camera
```
Camera.ResetFocus()                # 262 — return to default follow/bounds
Camera.FocusAtShape(obj[, rect])   # 206 — focus an object (+ optional screen rect)
Camera.Rumble()                    # 112 — shake
Camera.FollowShape(obj[, rect])    #  62 — constrained follow
Camera.FocusAtPoint(pos)           #  60 — pan to point
Camera.JumpToFocus()               #  30 — snap to focus
Camera.IsPointVisible(pos)         #  10 — frustum test
Camera.FollowObject(obj)           #   2
```
(Rectangle.New(x, y, w, h) is the constraint rect.)

### Entities / AI
```
EntityController.PerformAction(self, <id>)   # run EntityAction by id (113/136/140/145 seen)
EntityController.IsIdle(self)
Health.CurrentHealth(self) / Health.MaxHealth(self)
MonsterEntity/Entity components: FacingDirection, PhysicsEnabled
```

### Physics
```
PhysicsObject.SetGravityDirection(self, vec)
PhysicsObject.SetGravityMagnitude(self, f)      # boss script: 0.3
PhysicsObject.SetDecelerationForce(self, f)     # boss script: 2
PhysicsObject.SetEnabled(obj, false)
```

### Scene / objects
```
Scene.Find("name") / Scene.CreateObject(template, name, parent)
self:position() / :rotation() / :setPosition / :setRotation / :setHidden / :setAlwaysActive / :destroy()
```

### Transform / animation
```
TransformController.TranslateBy(self, Vector3.New(0,150,0), 2)   # move over 2 s
TransformController.ScaleTo(self, target, time)
TransformController.SetOrigin(self, Vector3.New(0,40,0))
ModelTransformController.SetRotationSpeed(self, 720)
```

### Collision
```
CollisionShape.SetEnabled(Scene.Find("shape"..id), 101, true/false)   # id = local comp id
CollisionShape.DisableAll(obj)
```

### Props / state
```
Properties.GetProperty(self, "key") / Properties.SetProperty(self, "key", value)
```

### Audio / effects
```
SoundLibrary.PlayEffect("name")   # "item_get", "bossgrowl", "bossgrowl2"
MusicPlayer.PlayMusic("bosskill", false, false) / MusicPlayer.FadeOut(t)
```

### Game / UI
```
Game.SetCinematicMode(true, true) / Game.Flash()
Math.RandomInt(1, 3)
Program.Wait(seconds)   # coroutine sleep — the heart of scripted sequences
CharController.SetWeaponsHidden(self, true) / CharController.PickupObject(self, item, true)
CollectableItem.RequiresPickup(item)
```

---

## 3. Real script anatomy — the boss AI (`fire_partBoss`, Observed)

The `boss` object's Program script is a complete data-driven state machine:

```lua
local self = ...;
local timeToAttack = 0;
local targetPosition = Properties.GetProperty(self, "targetPosition");
local previousHealth = Health.CurrentHealth(self);

while true do
    if previousHealth > Health.CurrentHealth(self) then
        Program.Wait(1.0);
        while not EntityController.IsIdle(self) do Program.Wait(0.2); end
        EntityController.PerformAction(self, 145);      -- enrage
        previousHealth = Health.CurrentHealth(self);
    end

    Program.Wait(0.20);
    timeToAttack = timeToAttack - 0.20;
    targetPosition = Properties.GetProperty(self, "targetPosition");

    local delta = Scene.Find(targetPosition):position() - self:position();
    PhysicsObject.SetGravityDirection(self, delta:normalized());
    PhysicsObject.SetGravityMagnitude(self, 0.3);
    PhysicsObject.SetDecelerationForce(self, 2);

    if (Health.CurrentHealth(self)/Health.MaxHealth(self) < 0.65) and
       EntityController.IsIdle(self) and timeToAttack < 0 and delta:y() < 100 then
        EntityController.PerformAction(self, Math.RandomInt(1,3)==1 and 136 or 140);
        timeToAttack = 3; SwitchPlace();
    elseif EntityController.IsIdle(self) and timeToAttack < 0 and delta:y() < 100 then
        EntityController.PerformAction(self, Math.RandomInt(1,2)==1 and 113 or 136);
        timeToAttack = 5; SwitchPlace();
    end
end
```

**Implications for the Scene Creator:**
- Entity *behavior* = Lua, not hardcoded C++. The editor must ship a **snippet library**
  of these patterns (patrol, chase, attack, death) and a Lua editor + the embedded
  Lua VM to preview.
- `EntityAction` components define the actual moves (animations + hitboxes), referenced by
  numeric id — the Scene Creator should auto-generate `EntityAction` ids when adding actions.

---

## 4. Trigger wiring recipe (Observed)

1. Object with `CollisionShape` (the zone).
2. `Program` component with `Trigger : 1`, `Enabled : 1`, Lua `local self = ...;` body.
3. Optionally `CollisionShapeComponent{ OnCollide : Program, OnCollisionEnd : Program }`
   for enter/leave hooks.
4. Or a bare `CollisionShape` + separate controller object with `Scene.Find` (e.g.
   `questtrigger` + `Properties`/`Game` flags in `fire_partBoss`).

Common observed trigger behaviors:
- Camera cutscene: `Camera.FocusAtPoint(...)` → `Program.Wait(...)` → `Camera.ResetFocus()`.
- Platform toggle: `CollisionShape.SetEnabled(Scene.Find("shape"..id), 101, true/false)`.
- Quest completion: `Properties.SetProperty(self, "quest07_complete", ...)` style flags.

---

## 5. Special scene systems (Observed)

### `WaterMeshComponent` (lava/water)
Present in `florennum_cave1` (3), `florennum_jail_part1` (4), `fire_part1` (1). Animated
surface mesh + damage for lava. **Scene Creator:** template object
`WaterMesh + CollisionShape(SpecialType: hazard)` + optional `Damage`.

### `OrbitControllerComponent`
Camera-adjacent orbiting objects (`grove_part1` has 2).

### `OverlayTextComponent`
NPC speech bubbles — the "NPC" system (no dedicated NPC component exists; NPCs are
`Model + OverlayText + SoundEffect + Properties`).

---

## 6. Scene-level `OnLoad` scripts (root field 5)

Present in 20/118 scenes. Sizes 126–2015 bytes. Purposes observed/inferred: day/night
setup, intro camera, music, quest state (e.g. `town_herohouse` 1843 B = story setup,
`theend` 628 B = credits flow).

**Scene Creator:** "OnLoad" should be a first-class editable script attached to the scene,
with templates: *intro camera, day/night, boss door lock, quest flag*.
