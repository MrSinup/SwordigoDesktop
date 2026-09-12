# Complete Modder API Reference & Example Scripts

## Executive Summary
This document serves as the complete developer API cheatsheet and reference guide for modders extending Swordigo using the `Mini.*` and `Swd.*` Lua namespaces. It contains comprehensive reference tables and 4 complete production-grade example mod scripts.

---

## 1. Quick Reference Cheatsheet

### Gameplay & Entity APIs (`Mini.*`)
```lua
Mini.GetHeroStats()                        --> { level, health, max_health, coins, xp }
Mini.SetHeroStats({ health = 100 })        --> boolean
Mini.GetEquipmentList()                    --> { "sword_magic", "armor_iron", ... }
Mini.EquipItem("sword_magic")              --> boolean
Mini.SetMovementSpeed(1.5)                 --> boolean
Mini.FindObjectsInRadius(x, y, z, 10.0)    --> { SceneObject, ... }
Mini.SetObjectScale(obj, 2.0, 2.0, 2.0)    --> boolean
Mini.SetObjectColor(obj, 1.0, 0.0, 0.0, 1.0) --> boolean (RGB Red Tint)
Mini.SetCameraTarget(obj)                  --> boolean
Mini.ShakeCamera(0.5, 1.0)                 --> boolean
```

### Engine & System Core APIs (`Swd.*`)
```lua
Swd.System.GetPerfMetrics()                --> { fps, draws, verts, tex_binds }
Swd.System.SetFPSCap(144)                  --> boolean
Swd.Audio.PlayTrack("boss_theme", true, 0.5)--> boolean
Swd.Audio.PlaySound3D("explosion", x,y,z, 1.0)--> boolean
Swd.Gfx.SetSkyColor({0.1,0.1,0.2}, {0.8,0.4,0.1}) --> Sunset Sky
Swd.VFS.ReadFile("res/custom_level.scene") --> string
Swd.Mem.GetAllocatedBytes()                --> number
Swd.Emu.GetJITStats()                      --> { bridge_calls, slow_calls }
```

---

## 2. Complete Example Mod Scripts

### Example 1: Dynamic Boss Encounter Mod (`boss_encounter_mod.lua`)
```lua
-- Custom Boss Wave Trigger
ProgramState.RegisterCustomCommand("startwave", function(args)
    local hero = Mini.GetHeroObject()
    local x, y, z = hero:GetPosition()

    -- Play Custom Boss BGM
    Swd.Audio.PlayTrack("boss_epic_theme", true, 1.0)

    -- Change Sky to Dark Purple Storm
    Swd.Gfx.SetSkyColor({ 0.1, 0.0, 0.2, 1.0 }, { 0.3, 0.0, 0.4, 1.0 })

    -- Spawn 3 Giant Bosses in a Circle
    for i = 1, 3 do
        local angle = (i * 2.0 * math.pi) / 3.0
        local bx = x + math.cos(angle) * 8.0
        local bz = z + math.sin(angle) * 8.0
        
        local boss = ProgramState.SpawnPrefab("MonsterGiantBoss", bx, y, bz)
        Mini.SetObjectScale(boss, 1.8, 1.8, 1.8) -- Make boss 80% bigger
        Mini.SetObjectColor(boss, 1.0, 0.2, 0.2, 1.0) -- Red tint
    end

    -- Trigger Camera Shake
    Mini.ShakeCamera(1.2, 2.0)
    print("[Mod] Boss Wave Started!")
end)
```

---

### Example 2: Real-time Dev HUD & Performance Monitor (`dev_hud_mod.lua`)
```lua
-- Custom ImGui Performance Monitor Overlay
Mini.OnFrameRender(function()
    local metrics = Swd.System.GetPerfMetrics()
    local jit = Swd.Emu.GetJITStats()
    local mem_mb = Swd.Mem.GetAllocatedBytes() / (1024 * 1024)

    -- Render Custom On-Screen HUD Overlay
    DrawText(10, 10, string.format("FPS: %d | Draws: %d | Verts: %d", metrics.fps, metrics.draws, metrics.verts))
    DrawText(10, 25, string.format("JIT Bridge Calls: %d | Native Memory: %.2f MB", jit.bridge_calls, mem_mb))
end)
```

---

### Example 3: Custom Quest & RPG Inventory Mod (`custom_rpg_mod.lua`)
```lua
-- RPG Quest Event Interceptor
ProgramState.InterceptEvent("ON_MONSTER_KILLED", function(monster_obj, killer_obj)
    local monster_type = Mini.GetObjectType(monster_obj)
    
    if monster_type == "MonsterCorruptedKnight" then
        local kills = tonumber(ProgramState.LoadCustomData("knight_kills") or "0") + 1
        ProgramState.SaveCustomData("knight_kills", tostring(kills))
        
        print("[RPG Mod] Corrupted Knights Slain: " .. kills .. " / 5")
        
        if kills >= 5 and not ProgramState.LoadCustomData("quest_knight_rewarded") then
            ProgramState.SaveCustomData("quest_knight_rewarded", "1")
            
            -- Reward Player with Legendary Magic Sword
            Mini.EquipItem("sword_fire_legendary")
            Swd.Audio.PlaySound2D("quest_complete", 1.0, 1.0)
            print("[RPG Mod] Quest Complete! Fire Sword Awarded!")
        end
    end
end)
```

---

### Example 4: Sunset Environment & Atmosphere Mod (`sunset_atmosphere.lua`)
```lua
-- Change world lighting and audio when entering Town
Mini.OnSceneChange(function(new_scene_name)
    if new_scene_name == "town_part1" then
        -- Set Golden Sunset Sky Gradient
        Swd.Gfx.SetSkyColor({ 0.9, 0.4, 0.1, 1.0 }, { 0.2, 0.1, 0.3, 1.0 })
        
        -- Play Peaceful Town Ambience
        Swd.Audio.PlayTrack("town_sunset_ambient", true, 2.0)
        
        -- Apply Subtle CRT & Bloom Shader FX
        ProgramState.SetPostFXPreset("SUNSET_BLOOM")
    end
end)
```
