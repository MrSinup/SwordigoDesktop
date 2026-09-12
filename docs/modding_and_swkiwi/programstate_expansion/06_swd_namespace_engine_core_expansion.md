# Swd.* Namespace Engine & System Core Expansion

## Executive Summary
This document specifies the new **`Swd.*`** Lua API namespace. While `Mini.*` handles high-level gameplay features, `Swd.*` grants modders low-level access to the engine runtime, memory management, renderer pipeline, audio engine, performance telemetry, virtual file system, and Dynarmic JIT metrics.

---

## 1. System & Performance Telemetry (`Swd.System.*`)

| Lua Function | Return Type | Description |
| :--- | :--- | :--- |
| `Swd.System.GetPerfMetrics()` | `table` | Returns `{ fps, draws, verts, tex_binds, vtx_calls, texc_calls, matrix, state_changes }` |
| `Swd.System.SetFPSCap(target_fps)` | `boolean` | Configures frame rate cap (30, 60, 120, 144, 240, or 0 for uncapped) |
| `Swd.System.GetUptime()` | `number` | Returns total engine uptime in seconds with microsecond resolution |
| `Swd.System.GetGraphicsAPI()` | `string` | Returns active backend name (`"OpenGL"` or `"Vulkan"`) |

---

## 2. Audio Engine APIs (`Swd.Audio.*`)

| Lua Function | Return Type | Description |
| :--- | :--- | :--- |
| `Swd.Audio.PlayTrack(track_name, loop, fade_time)` | `boolean` | Plays background music track by name or custom `.ogg` path |
| `Swd.Audio.StopTrack(fade_time)` | `boolean` | Fades out and stops current background music |
| `Swd.Audio.PlaySound2D(sound_id, volume, pitch)` | `boolean` | Plays 2D UI or ambient sound effect |
| `Swd.Audio.PlaySound3D(sound_id, x, y, z, volume)` | `boolean` | Plays 3D positional sound effect at spatial coordinates |
| `Swd.Audio.SetMasterVolume(vol)` | `boolean` | Adjusts global audio master volume (0.0 to 1.0) |

---

## 3. Renderer & Shader Engine (`Swd.Gfx.*`)

| Lua Function | Return Type | Description |
| :--- | :--- | :--- |
| `Swd.Gfx.SetPostFXParam(param_name, value)` | `boolean` | Sets custom uniform in PostFX shader pipeline |
| `Swd.Gfx.SetSkyColor(top_rgba, bottom_rgba)` | `boolean` | Dynamically updates sky gradient background colors |
| `Swd.Gfx.SetRenderResolution(multiplier)` | `boolean` | Adjusts FBO render scale (0.5x, 1.0x, 2.0x, 4.0x) |
| `Swd.Gfx.CaptureScreenshot(file_path)` | `boolean` | Saves current frame buffer to PNG/TGA image file |

---

## 4. Virtual File System (`Swd.VFS.*`)

| Lua Function | Return Type | Description |
| :--- | :--- | :--- |
| `Swd.VFS.FileExists(virtual_path)` | `boolean` | Checks if asset exists in game package or `assets/resources/` |
| `Swd.VFS.ReadFile(virtual_path)` | `string` | Reads full text/binary content of asset file into Lua string |
| `Swd.VFS.ListDirectory(dir_path)` | `table` | Returns array of files and subdirectories in virtual path |
| `Swd.VFS.MountDirectory(real_path, mount_point)` | `boolean` | Dynamically mounts local directory into VFS resolution order |

---

## 5. Memory & Emulation JIT Control (`Swd.Mem.*` & `Swd.Emu.*`)

| Lua Function | Return Type | Description |
| :--- | :--- | :--- |
| `Swd.Mem.GetAllocatedBytes()` | `number` | Returns total native heap memory allocated by SRE |
| `Swd.Mem.TriggerGC()` | `boolean` | Triggers immediate native Redstell GC and Lua garbage collection |
| `Swd.Emu.GetJITStats()` | `table` | Returns Dynarmic JIT metrics `{ bridge_calls, slow_calls, code_cache_mb }` |
| `Swd.Emu.FlushCodeCache()` | `boolean` | Flushes Dynarmic JIT translation block cache |

---

## 6. C++ Registration Blueprint (`src/sre/sre_lua_libs.c`)

```c
static const luaL_Reg swd_system_funcs[] = {
    { "GetPerfMetrics",    l_swd_system_get_perf_metrics },
    { "SetFPSCap",         l_swd_system_set_fps_cap      },
    { "GetUptime",         l_swd_system_get_uptime       },
    { "GetGraphicsAPI",    l_swd_system_get_graphics_api },
    { NULL, NULL }
};

static const luaL_Reg swd_audio_funcs[] = {
    { "PlayTrack",         l_swd_audio_play_track        },
    { "StopTrack",         l_swd_audio_stop_track        },
    { "PlaySound2D",       l_swd_audio_play_sound_2d     },
    { "PlaySound3D",       l_swd_audio_play_sound_3d     },
    { "SetMasterVolume",   l_swd_audio_set_master_volume },
    { NULL, NULL }
};

void sre_register_swd_namespace(lua_State* L) {
    luaL_register(L, "Swd.System", swd_system_funcs);
    luaL_register(L, "Swd.Audio",  swd_audio_funcs);
    luaL_register(L, "Swd.Gfx",    swd_gfx_funcs);
    luaL_register(L, "Swd.VFS",    swd_vfs_funcs);
    luaL_register(L, "Swd.Mem",    swd_mem_funcs);
    luaL_register(L, "Swd.Emu",    swd_emu_funcs);
    fprintf(stderr, "[SRE/Lua] Swd.* engine core namespace registered.\n");
}
```
