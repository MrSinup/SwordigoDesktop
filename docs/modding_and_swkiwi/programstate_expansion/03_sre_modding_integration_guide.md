# SRE Modding Integration Guide & Lua Script Examples

## Executive Summary
This document provides a step-by-step technical guide for integrating the expanded `ProgramState` Lua API into the SRE runtime engine (`src/sre/sre_lua_libs.c` and `src/game/mod_tools.cpp`), along with practical Lua script examples for mod developers.

---

## 1. Wiring New APIs into SRE Lua Initialization

In `src/sre/sre_lua_libs.c`:

```c
/* Register the expanded ProgramState library during SRE Lua VM setup */
static const luaL_Reg sre_programstate_ext_funcs[] = {
    { "SetGlobalSpeed",            l_ProgramState_SetGlobalSpeed           },
    { "SpawnPrefab",               l_ProgramState_SpawnPrefab              },
    { "SetGravity",                l_ProgramState_SetGravity               },
    { "ExecuteStringInContext",    l_ProgramState_ExecuteStringInContext   },
    { "InterceptEvent",            l_ProgramState_InterceptEvent           },
    { "SetPostFXPreset",           l_ProgramState_SetPostFXPreset          },
    { "OverrideComponentProperty", l_ProgramState_OverrideComponentProperty },
    { "RegisterCustomCommand",     l_ProgramState_RegisterCustomCommand    },
    { "SaveCustomData",            l_ProgramState_SaveCustomData           },
    { "LoadCustomData",            l_ProgramState_LoadCustomData           },
    { "SetHitboxOverride",         l_ProgramState_SetHitboxOverride        },
    { NULL, NULL }
};

void sre_register_programstate_extensions(lua_State* L) {
    /* Register table ProgramState in global environment */
    luaL_register(L, "ProgramState", sre_programstate_ext_funcs);
    fprintf(stderr, "[SRE/Lua] Registered 10 ProgramState modding extensions successfully.\n");
}
```

---

## 2. Modder Usage Examples (Lua Scripts)

### Example 1: Matrix Slow-Motion Skill Mod (`slowmo_mod.lua`)
```lua
-- When player casts magic spell, activate slow motion for 3 seconds
ProgramState.InterceptEvent("ON_SPELL_CAST", function(caster, spell_type)
    if spell_type == "TIME_FREEZE" then
        print("[Mod] Time Freeze activated!")
        ProgramState.SetGlobalSpeed(0.25) -- 25% speed slow-mo
        ProgramState.SetPostFXPreset("CHROMATIC_DISTORTION")

        -- Restore speed after 3 seconds asynchronously
        ProgramState.Wait(3.0)
        ProgramState.SetGlobalSpeed(1.0)
        ProgramState.SetPostFXPreset("DEFAULT")
    end
end)
```

---

### Example 2: Boss Arena Spawns & Gravity Manipulation (`boss_arena.lua`)
```lua
-- Custom dev console command to trigger boss fight
ProgramState.RegisterCustomCommand("spawnboss", function(args)
    local hero = Mini.GetHeroObject()
    local x, y, z = hero:GetPosition()

    -- Spawn giant boss 5 units in front of player
    local boss = ProgramState.SpawnPrefab("MonsterGiantBoss", x + 5.0, y, z)
    
    -- Invert gravity for dramatic effect
    ProgramState.SetGravity(0.0, 15.0, 0.0)

    -- Save boss fight attempt count in profile save
    local attempts = tonumber(ProgramState.LoadCustomData("boss_attempts") or "0") + 1
    ProgramState.SaveCustomData("boss_attempts", tostring(attempts))
    print("[Mod] Boss Fight Attempt #" .. attempts)
end)
```

---

### Example 3: Dynamic Hitbox Range Expansion (`sword_buff.lua`)
```lua
-- Increase sword attack reach by 200%
ProgramState.InterceptEvent("ON_WEAPON_SWING", function(weapon_obj)
    ProgramState.SetHitboxOverride(weapon_obj, 6.0, 3.0, 2.0, 0.0)
    ProgramState.OverrideComponentProperty(weapon_obj, "DamageComponent", "damageMultiplier", 2.5)
end)
```

---

## 3. Summary Matrix of Modding Capabilities

| Feature | Legacy Lua Capabilities | New `ProgramState` Expansion | Modding Impact |
| :--- | :--- | :--- | :--- |
| **Game Speed** | Fixed at 1.0x | `ProgramState.SetGlobalSpeed()` | Slow-mo, fast-forward, matrix effects |
| **Entity Spawning** | Static `.scene` files only | `ProgramState.SpawnPrefab()` | Dynamic waves, chest drops, boss arenas |
| **Physics Gravity** | Static scene default | `ProgramState.SetGravity()` | Zero-g, moon jumps, inverted gravity |
| **Visual FX** | Fixed engine shaders | `ProgramState.SetPostFXPreset()` | CRT, Bloom, Night vision shaders |
| **Data Persistence** | None for custom scripts | `SaveCustomData` / `LoadCustomData` | Saved mod progress & settings |
