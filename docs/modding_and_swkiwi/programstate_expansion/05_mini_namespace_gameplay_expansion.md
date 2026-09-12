# Mini.* Namespace Gameplay & Entity API Expansion

## Executive Summary
This document specifies the expanded `Mini.*` Lua API namespace for Swordigo modders. It details new functions for inspecting and mutating player stats, equipment, active quests, spatial object queries, visual properties, particle triggers, and camera tracking.

---

## 1. Hero & Character State APIs (`Mini.Hero.*`)

| Lua Function | Return Type | Description |
| :--- | :--- | :--- |
| `Mini.GetHeroStats()` | `table` | Returns `{ level, xp, coins, health, max_health, mana, max_mana, armor, weapon }` |
| `Mini.SetHeroStats(stats_table)` | `boolean` | Updates hero level, xp, coins, health, mana, or stat points |
| `Mini.GetEquipmentList()` | `table` | Returns array of all unlocked weapons, armors, spells, and trinkets |
| `Mini.EquipItem(item_id)` | `boolean` | Instantly equips a sword, armor, spell, or trinket by ID string |
| `Mini.SetMovementSpeed(speed_mult)` | `boolean` | Multiplies hero base movement, sprinting, and climbing speed |
| `Mini.SetJumpPower(force_mult)` | `boolean` | Adjusts hero jump impulse force and air control |

---

## 2. Scene Object & Entity APIs (`Mini.Object.*`)

| Lua Function | Return Type | Description |
| :--- | :--- | :--- |
| `Mini.FindObjectsInRadius(x, y, z, radius)` | `table` | Returns array of `SceneObject` handles within sphere radius |
| `Mini.GetObjectType(obj)` | `string` | Returns class name of object (`Monster`, `Breakable`, `ItemDrop`, `NPC`) |
| `Mini.SetObjectScale(obj, sx, sy, sz)` | `boolean` | Dynamically resizes a 3D model/mesh entity in real-time |
| `Mini.SetObjectColor(obj, r, g, b, a)` | `boolean` | Applies color tint / opacity RGBA modulation to mesh/sprite |
| `Mini.SetObjectVelocity(obj, vx, vy, vz)` | `boolean` | Sets physics rigid body velocity vector directly |
| `Mini.DestroyObject(obj)` | `boolean` | Safely despawns and deletes an object from the active scene |

---

## 3. Quest & World Progression APIs (`Mini.Quest.*`)

| Lua Function | Return Type | Description |
| :--- | :--- | :--- |
| `Mini.GetAllQuests()` | `table` | Returns table of all active and completed quest IDs and states |
| `Mini.GetQuestState(quest_name)` | `table` | Returns status (`IN_PROGRESS`, `COMPLETED`, `UNTOUCHED`) and step count |
| `Mini.SetQuestState(quest_name, state)` | `boolean` | Force advances or completes any story/side quest |
| `Mini.GetMapCompletion()` | `number` | Returns overall world completion percentage (0.0 to 100.0) |

---

## 4. Visual Effects & Camera Control (`Mini.Camera.*` & `Mini.VFX.*`)

| Lua Function | Return Type | Description |
| :--- | :--- | :--- |
| `Mini.SetCameraTarget(obj_or_pos)` | `boolean` | Attaches main camera focus to an entity or static 3D coordinate |
| `Mini.SetCameraFov(fov_degrees)` | `boolean` | Adjusts camera field-of-view perspective |
| `Mini.ShakeCamera(intensity, duration)`| `boolean` | Triggers screenshake impulse with given amplitude and decay time |
| `Mini.TriggerParticleEffect(effect_id, x, y, z)` | `boolean` | Spawns particle swirl, explosion, magic glow, or trail at position |

---

## 5. C++ Registration Blueprint (`src/sre/sre_mini_api.c`)

```c
static const luaL_Reg mini_expanded_funcs[] = {
    { "GetHeroStats",        l_mini_get_hero_stats       },
    { "SetHeroStats",        l_mini_set_hero_stats       },
    { "GetEquipmentList",    l_mini_get_equipment_list   },
    { "EquipItem",           l_mini_equip_item           },
    { "SetMovementSpeed",    l_mini_set_movement_speed   },
    { "SetJumpPower",        l_mini_set_jump_power       },
    { "FindObjectsInRadius", l_mini_find_objects_radius  },
    { "SetObjectScale",      l_mini_set_object_scale     },
    { "SetObjectColor",      l_mini_set_object_color     },
    { "SetObjectVelocity",   l_mini_set_object_velocity  },
    { "DestroyObject",       l_mini_destroy_object       },
    { "GetAllQuests",        l_mini_get_all_quests       },
    { "SetQuestState",       l_mini_set_quest_state      },
    { "SetCameraTarget",     l_mini_set_camera_target    },
    { "ShakeCamera",         l_mini_shake_camera         },
    { "TriggerParticleEffect", l_mini_trigger_particle   },
    { NULL, NULL }
};
```
