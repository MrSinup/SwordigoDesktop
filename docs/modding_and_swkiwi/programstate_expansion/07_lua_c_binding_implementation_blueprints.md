# Lua C-Binding Implementation Blueprints

## Executive Summary
This document provides exact C/C++ implementation code blueprints for the `Mini.*` and `Swd.*` Lua bindings. It details stack argument parsing, safety wrappers (`recovery_push`, `setjmp`), memory safety checks, and pointer resolution for SRE native calls.

---

## 1. Safety Pattern for All SRE Lua Bindings

Every native C function bound to Lua MUST adhere to SRE safety guidelines to prevent Unicorn/Dynarmic stalls:

```c
static int l_sre_safe_binding_template(lua_State* L) {
    if (!L) return 0;
    
    // 1. Push setjmp recovery frame
    int depth = recovery_push(L);
    if (depth >= 0 && sre_setjmp(g_sre_recovery_stack[depth].buf) != 0) {
        recovery_pop(depth);
        fprintf(stderr, "[SRE/Lua] Exception caught inside Lua binding — returning nil\n");
        pthread_mutex_unlock(g_lua_mutex_ptr);
        lua_pushnil(L);
        return 1;
    }

    // 2. Perform native C++ logic
    // ...

    // 3. Pop recovery frame and return results
    if (depth >= 0) recovery_pop(depth);
    return 1;
}
```

---

## 2. Blueprint Implementations

### Blueprint A: `Swd.System.GetPerfMetrics()`
```c
static int l_swd_system_get_perf_metrics(lua_State* L) {
    extern uint32_t g_sre_draws_per_frame;
    extern uint32_t g_sre_verts_per_frame;
    extern uint32_t g_sre_tex_binds_per_frame;
    extern uint64_t g_sre_vtx_calls;
    extern uint64_t g_sre_texc_calls;
    extern uint64_t g_sre_matrix_calls;
    extern uint64_t g_sre_state_changes;

    lua_newtable(L);
    
    lua_pushinteger(L, g_sre_draws_per_frame);    lua_setfield(L, -2, "draws");
    lua_pushinteger(L, g_sre_verts_per_frame);    lua_setfield(L, -2, "verts");
    lua_pushinteger(L, g_sre_tex_binds_per_frame);lua_setfield(L, -2, "tex_binds");
    lua_pushnumber(L, (double)g_sre_vtx_calls);    lua_setfield(L, -2, "vtx_calls");
    lua_pushnumber(L, (double)g_sre_texc_calls);   lua_setfield(L, -2, "texc_calls");
    lua_pushnumber(L, (double)g_sre_matrix_calls); lua_setfield(L, -2, "matrix");
    lua_pushnumber(L, (double)g_sre_state_changes);lua_setfield(L, -2, "state");

    return 1;
}
```

### Blueprint B: `Swd.Audio.PlayTrack(track_name, loop, fade_time)`
```c
static int l_swd_audio_play_track(lua_State* L) {
    const char* track_name = luaL_checkstring(L, 1);
    int loop = lua_isboolean(L, 2) ? lua_toboolean(L, 2) : 1;
    float fade_time = lua_isnumber(L, 3) ? (float)lua_tonumber(L, 3) : 0.5f;

    extern bool sre_music_play(const char* name, bool loop, float fade);
    bool ok = sre_music_play(track_name, loop != 0, fade_time);

    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}
```

### Blueprint C: `Mini.Hero.GetHeroStats()`
```c
static int l_mini_get_hero_stats(lua_State* L) {
    SceneController* sc = sre_scene_controller_from_L(L);
    if (!sc) { lua_pushnil(L); return 1; }

    SceneObject* hero = sre_hero_object_from_sc(sc);
    if (!hero) { lua_pushnil(L); return 1; }

    int health = sre_hero_get_health(hero);
    int max_health = sre_hero_get_max_health(hero);
    int coins = sre_hero_get_coins(hero);
    int level = sre_hero_get_level(hero);

    lua_newtable(L);
    lua_pushinteger(L, level);      lua_setfield(L, -2, "level");
    lua_pushinteger(L, health);     lua_setfield(L, -2, "health");
    lua_pushinteger(L, max_health); lua_setfield(L, -2, "max_health");
    lua_pushinteger(L, coins);      lua_setfield(L, -2, "coins");

    return 1;
}
```

### Blueprint D: `Swd.VFS.ReadFile(virtual_path)`
```c
static int l_swd_vfs_read_file(lua_State* L) {
    const char* vpath = luaL_checkstring(L, 1);
    
    extern char* sre_vfs_read_file_to_string(const char* path, size_t* out_len);
    size_t len = 0;
    char* data = sre_vfs_read_file_to_string(vpath, &len);
    
    if (data) {
        lua_pushlstring(L, data, len);
        free(data);
    } else {
        lua_pushnil(L);
    }
    return 1;
}
```
