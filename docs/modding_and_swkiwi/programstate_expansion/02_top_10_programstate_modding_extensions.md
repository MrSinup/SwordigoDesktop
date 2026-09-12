# Top 10 ProgramState Modding Extension Functions

## Executive Summary
This document specifies the **Top 10 most powerful `ProgramState` functions** to create and expose to Lua modders in Swordigo. These APIs enable real-time time dilation, dynamic entity spawning, custom physics gravity, live sandboxed script compilation, event bus interception, dynamic PostFX shader preset switching, component property reflection, dev console commands, persistent mod save data, and custom hitbox/hurtbox overrides.

---

## 1. `ProgramState.SetGlobalSpeed(multiplier)`
### Description
Controls the execution time step of the game loop and physics update rate. Enables slow-motion matrix effects, bullet time, or fast-forward speedups for speedrunning and testing.

### Signature & C++ Implementation
```cpp
// Lua: ProgramState.SetGlobalSpeed(0.5) -- Slow motion half speed
static int l_ProgramState_SetGlobalSpeed(lua_State* L) {
    float speed = (float)luaL_checknumber(L, 1);
    if (speed <= 0.0f) speed = 0.001f;
    g_sre_global_time_multiplier = speed; // Read by ProgramState::Update and Scene::Update
    lua_pushboolean(L, 1);
    return 1;
}
```

---

## 2. `ProgramState.SpawnPrefab(prefab_name, x, y, z)`
### Description
Instantiates any entity, item drop, chest, or monster dynamically into the active scene by name at given 3D coordinates. Modders can spawn boss waves, item drops, or custom interactive objects on demand.

### Signature & C++ Implementation
```cpp
// Lua: local obj = ProgramState.SpawnPrefab("MonsterGiantBat", 120.0, 45.0, 0.0)
static int l_ProgramState_SpawnPrefab(lua_State* L) {
    const char* prefab = luaL_checkstring(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float z = (float)luaL_checknumber(L, 4);

    SceneObject* spawned = sre_spawn_prefab_at(prefab, x, y, z);
    if (spawned) {
        sre_push_scene_object_userdata(L, spawned);
    } else {
        lua_pushnil(L);
    }
    return 1;
}
```

---

## 3. `ProgramState.SetGravity(gx, gy, gz)`
### Description
Overrides scene physics gravity vector in real-time. Allows modders to implement low-gravity moon levels, inverted gravity rooms, or directional wind/repulsion fields.

### Signature & C++ Implementation
```cpp
// Lua: ProgramState.SetGravity(0.0, -9.81, 0.0) -- Normal earth gravity
// Lua: ProgramState.SetGravity(0.0, 9.81, 0.0)  -- Inverted gravity
static int l_ProgramState_SetGravity(lua_State* L) {
    float gx = (float)luaL_checknumber(L, 1);
    float gy = (float)luaL_checknumber(L, 2);
    float gz = (float)luaL_checknumber(L, 3);

    g_sre_custom_gravity_x = gx;
    g_sre_custom_gravity_y = gy;
    g_sre_custom_gravity_z = gz;
    g_sre_override_gravity = 1;

    lua_pushboolean(L, 1);
    return 1;
}
```

---

## 4. `ProgramState.ExecuteStringInContext(script_code, env_table)`
### Description
Compiles and executes arbitrary Lua code string dynamically inside a isolated environment table. Enables live-reloading mod scripts, dev console commands, and runtime script patching.

### Signature & C++ Implementation
```cpp
// Lua: ProgramState.ExecuteStringInContext("hero:SetHealth(100)", custom_env)
static int l_ProgramState_ExecuteStringInContext(lua_State* L) {
    const char* code = luaL_checkstring(L, 1);
    if (luaL_loadstring(L, code) != 0) {
        const char* err = lua_tostring(L, -1);
        lua_pushboolean(L, 0);
        lua_pushstring(L, err);
        return 2;
    }
    // If env_table provided at arg 2, set fenv
    if (lua_istable(L, 2)) {
        lua_pushvalue(L, 2);
        lua_setfenv(L, -2);
    }
    int status = lua_pcall(L, 0, 1, 0);
    if (status != 0) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, lua_tostring(L, -1));
        return 2;
    }
    return 1;
}
```

---

## 5. `ProgramState.InterceptEvent(event_type, callback)`
### Description
Registers a Lua callback to intercept native engine events before they are processed by the game engine (e.g. `ON_DAMAGE_TAKEN`, `ON_ITEM_COLLECTED`, `ON_PORTAL_ENTER`, `ON_DEATH`). Allows modders to cancel, alter, or enhance game mechanics.

### Signature & C++ Implementation
```cpp
// Lua: ProgramState.InterceptEvent("ON_DAMAGE_TAKEN", function(target, damage) return damage * 2.0 end)
static int l_ProgramState_InterceptEvent(lua_State* L) {
    const char* event_type = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    sre_register_event_interceptor(event_type, L, 2);
    lua_pushboolean(L, 1);
    return 1;
}
```

---

## 6. `ProgramState.SetPostFXPreset(preset_id, parameters)`
### Description
Triggers real-time graphical PostFX changes (CRT scanlines, chromatic aberration, bloom intensity, sepia/grayscale filters, night vision, underwater distortion).

### Signature & C++ Implementation
```cpp
// Lua: ProgramState.SetPostFXPreset("CRT_VINTAGE", { scanline_intensity = 0.8 })
static int l_ProgramState_SetPostFXPreset(lua_State* L) {
    const char* preset_name = luaL_checkstring(L, 1);
    sre_apply_postfx_preset_by_name(preset_name);
    lua_pushboolean(L, 1);
    return 1;
}
```

---

## 7. `ProgramState.OverrideComponentProperty(object, component_name, prop_name, value)`
### Description
Reflection API that allows modders to directly inspect or mutate any property of any native C++ `Component` attached to a `SceneObject` at runtime.

### Signature & C++ Implementation
```cpp
// Lua: ProgramState.OverrideComponentProperty(hero_obj, "CharControllerComponent", "moveSpeed", 15.0)
static int l_ProgramState_OverrideComponentProperty(lua_State* L) {
    SceneObject* obj = sre_to_scene_object(L, 1);
    const char* comp_name = luaL_checkstring(L, 2);
    const char* prop_name = luaL_checkstring(L, 3);

    bool ok = sre_component_set_property(obj, comp_name, prop_name, L, 4);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}
```

---

## 8. `ProgramState.RegisterCustomCommand(cmd_name, callback)`
### Description
Registers custom developer console slash commands that can be executed directly from the in-game Swordfare GUI dev console or hotkeys.

### Signature & C++ Implementation
```cpp
// Lua: ProgramState.RegisterCustomCommand("godmode", function(args) hero:SetInvincible(true) end)
static int l_ProgramState_RegisterCustomCommand(lua_State* L) {
    const char* cmd_name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);

    sre_register_dev_command(cmd_name, L, 2);
    lua_pushboolean(L, 1);
    return 1;
}
```

---

## 9. `ProgramState.SaveCustomData(key, value)` & `LoadCustomData(key)`
### Description
Stores and retrieves arbitrary JSON-serializable mod data inside the player's profile save file (`.sav`). Ensures mod settings, quest progress, and custom inventory survive game restarts.

### Signature & C++ Implementation
```cpp
// Lua: ProgramState.SaveCustomData("mod_coins", 1500)
// Lua: local coins = ProgramState.LoadCustomData("mod_coins")
static int l_ProgramState_SaveCustomData(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    const char* val = luaL_checkstring(L, 2);
    sre_save_profile_custom_data(key, val);
    lua_pushboolean(L, 1);
    return 1;
}

static int l_ProgramState_LoadCustomData(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    std::string val = sre_load_profile_custom_data(key);
    if (val.empty()) lua_pushnil(L);
    else lua_pushstring(L, val.c_str());
    return 1;
}
```

---

## 10. `ProgramState.SetHitboxOverride(object, width, height, offset_x, offset_y)`
### Description
Dynamically adjusts collision hurtbox and attack hitbox dimensions for any entity or spell at runtime. Modders can expand weapon attack range or shrink character hitboxes for special power-ups.

### Signature & C++ Implementation
```cpp
// Lua: ProgramState.SetHitboxOverride(hero_obj, 2.5, 4.0, 0.0, 1.0)
static int l_ProgramState_SetHitboxOverride(lua_State* L) {
    SceneObject* obj = sre_to_scene_object(L, 1);
    float w = (float)luaL_checknumber(L, 2);
    float h = (float)luaL_checknumber(L, 3);
    float ox = (float)luaL_checknumber(L, 4);
    float oy = (float)luaL_checknumber(L, 5);

    bool ok = sre_override_scene_object_hitbox(obj, w, h, ox, oy);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}
```
