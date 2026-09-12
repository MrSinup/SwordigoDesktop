# 04 — Spawn Points, Portals & Triggers

---

## 1. `SpawnPoint` (Observed)

Every scene has a `spawn_default` object, plus one `spawn_from_<scene>` object for each
incoming portal. Full decoded example (`town_part1.scene`):

```
Object{
    Identifier : 'spawn_default'
    Component{
        ClassName : 'SpawnPoint'   Identifier : 101
        SpawnPointComponent{
            FacingDirection : 1          # 1 = facing right (in-game convention)
            SpawnOffset{ X : 0  Y : 0  Z : 0 }
        }
    }
    Position{ X : 2134.82739  Y : 522.848511 }
    Depth : 0   Rotation : 0   Scaling : 1
    LocalAabb{ X:-30 Y:-30 Width:60 Height:60 }
    Hidden : 0
}
```

### `SpawnPointComponent` fields (Observed schema)

| Field # | Name | Notes |
|---------|------|-------|
| — | `FacingDirection` | 1 = right; used to orient the hero on spawn |
| — | `SpawnOffset` | Vector3 (X,Y,Z); usually 0,0,0 |

### Naming conventions (Observed across all scenes)

| Name | Meaning |
|------|---------|
| `spawn_default` | The level's default spawn (first entry, continue point) |
| `spawn_from_<scenename>` | Spawn used when entering from `<scenename>` (matches a portal's `DestinationSceneName`) |
| `quest07_complete` (`florennum_jail_part1`) | Quest/story-triggered spawn |
| `king`, `lower`, `middle`, `upper` | Bespoke story spawns |

> **Scene Creator rule:** when a portal is added pointing at scene `X`, auto-create
> `spawn_from_X` on the other side. When a new scene is created, `spawn_default` is created
> first, at a sensible location above the ground.

---

## 2. `Portal` (Observed)

A portal object is a **three-component composite**: `Portal` + `CollisionShape` (the touch
zone) + `SpawnPoint` (where you land on return). Full example (`town_part1.scene`):

```
Object{
    Identifier : 'spawn_from_town_elderhouse'
    Component{
        ClassName : 'Portal'   Identifier : 101
        PortalComponent{
            DestinationSceneName : 'town_elderhouse'
            SpawnPointName : ''              # empty → use scene's default spawn
            TapToEnter : 1                   # 1 = tap screen/button to enter
            TriggerShapeId : 102             # → CollisionShape id below
        }
    }
    Component{
        ClassName : 'CollisionShape'   Identifier : 102
        ShapeComponent{ Rectangle{ X:-41.5 Y:-20 Width:83 Height:110 } }
        CollisionShapeComponent{
            MinDepth : -15   MaxDepth : 50
            SpecialType : 2                  # 2 = portal/transition zone (see §4)
            Enabled : 1
        }
    }
    Component{
        ClassName : 'SpawnPoint'   Identifier : 105
        SpawnPointComponent{ FacingDirection : 1  SpawnOffset{0,0,0} }
    }
    Position{ ... }  Depth : 0  Rotation : 0  Scaling : 1  Hidden : 0
}
```

### `PortalComponent` fields (Observed schema)

| Field # | Name | Notes |
|---------|------|-------|
| 10 | `DestinationSceneName` | target scene file stem (no extension) |
| 18 | `SpawnPointName` | empty = default; else name of a `spawn_from_…` object |
| 24 | `TapToEnter` | 0/1 |
| 32 | `TriggerShapeId` | local id of the CollisionShape that activates the portal |

> **The `Identifier` and the portal's `DestinationSceneName` naming loop:** in `town_part1`
> the portal object is literally named `spawn_from_town_elderhouse` — i.e. the object name
> mirrors the *destination* spawn convention. (That name is the return-spawn used when you
> come *back* from `town_elderhouse`.)

---

## 3. Trigger zones (Observed)

Swordigo has **no separate "Trigger" component**. Triggers are `CollisionShape`s carrying a
script (`Program`/`EntityAction`), with `Trigger : 1` on the script wrapper:

```
Component{
    ClassName : 'Program'   Identifier : ...
    ProgramComponent{ ... Lua bytes ... }
    Enabled : 1
    Trigger : 1            # ← marks this as a trigger script
}
```

Examples observed:
- `questtrigger` (`fire_partBoss`): bare `CollisionShape` — quest trigger zone.
- `trigger` (`fire_partBoss`): CollisionShape + TransformController + Model + PhysicsObject
  + ParticleEmitter (a *moving* trigger platform).
- `CollisionShape` with `OnCollide`/`OnCollisionEnd` Program fields (see schema below) —
  the engine hooks entity overlap to Lua.
- Elevator/lift scripts (florennum_tower1): `CollisionShape.SetEnabled(Scene.Find("shape"..id), 101, true/false)`.

### `CollisionShapeComponent` fields (Observed schema — the trigger/ground workhorse)

| Field # | Name | Notes |
|---------|------|-------|
| 16 | `IsGround` | 1 = walkable ground (5035 occurrences of `IsGround:1` across all scenes) |
| 24 | `Collides` | 0/1 |
| 32 | `ReceivesDamage` | 0/1 |
| 40 | `InflictsDamage` | 0/1 |
| 53 | `MinDepth` | collision depth band (ground: -45) |
| 61 | `MaxDepth` | collision depth band (ground: 45) |
| 64 | `SpecialType` | see below |
| 74 | `OnCollide` | Program (Lua) |
| 82 | `OnCollisionEnd` | Program (Lua) |
| 88 | `Enabled` | 0/1 |
| 98 | `OnReceiveDamage` | Program (Lua) |
| 109 | `Friction` | float |
| 112 | `UnsafeGround` | 0/1 (hazard ground) |

### `SpecialType` census (Observed)

| Value | Count | Meaning (Inferred) |
|-------|-------|--------------------|
| 2 | 275 | **Portal/transition zone** (every portal's touch shape uses it) |
| 5 | 16 | Unknown — likely one-way or ladder/climb |
| 7 | 4 | Unknown |
| 3 | 4 | Unknown |
| 4 | 2 | Unknown |
| 1 | 2 | Unknown |

> Only SpecialType 2 has a confident mapping (portal zones). Flag 1/3/4/5/7 as UNKNOWN.

---

## 4. Templates for the Scene Creator

### `PlayerSpawn` template
- Object `spawn_default` + `SpawnPointComponent{FacingDirection:1, SpawnOffset(0,0,0)}`
- Position = chosen point (UI editable); Depth 0.

### `Portal` template
- Object named `spawn_from_<dest>` (auto).
- `PortalComponent{DestinationSceneName:<dest>, SpawnPointName:"", TapToEnter:1, TriggerShapeId:<id>}`
- `CollisionShape` Rectangle sized by user, `SpecialType:2`, MinDepth -15 / MaxDepth 50.
- `SpawnPointComponent` for the return spawn.
- Auto-requires: a `spawn_from_<thisScene>` object in `<dest>.scene` (warn if missing).

### `TriggerZone` template
- `CollisionShape` (Rectangle/Polygon) + `Program` component with `Trigger:1`,
  `Enabled:1`, empty Lua stub (`local self = ...;`).
- UI: pick a Lua snippet template (camera rumble, door open, quest flag, …).
