# Caver ProgramState Architecture & Lua Binding Engine

## Executive Summary
This document provides a reverse-engineering analysis of `Caver::ProgramState` in Swordigo (`libswordigo.so` ARM64 / ARM32). It details how `ProgramState` wraps the underlying Lua 5.1 virtual machine (`lua_State*`), how native C/C++ libraries and classes are registered into Lua, and how SRE / custom modding frameworks can extend `ProgramState` with powerful new Lua APIs.

---

## 1. ProgramState Core Struct & Methods

In the Caver Engine architecture, `ProgramState` represents a Lua execution context attached to a `Scene`, `SceneObject`, or global game manager.

### Key Methods & Virtual Address Table (v1.4.12)

| Method Signature | ARM64 VA (v1.4.12) | ARM32 VA (v1.4.12) | Description |
| :--- | :--- | :--- | :--- |
| `Caver::ProgramState::FromLuaState` | `0x004C12E0` | `0x002FA3B0` | Converts a `lua_State*` pointer to its parent `ProgramState*` wrapper |
| `Caver::ProgramState::RegisterLibrary` | `0x004C1B58` | `0x002FAAE0` | Registers a table of C functions: `RegisterLibrary(libname, funcs)` |
| `Caver::ProgramState::RegisterClass` | `0x004C1B88` | `0x002FAAF0` | Registers a metatable class with member methods and static constructors |
| `Caver::ProgramState::RegisterFunctions` | `0x004C1B20` | `0x002FAAA0` | Registers global C functions directly into the global `_G` table |
| `Caver::ProgramState::ExecuteString` | `0x004C17C4` | `0x002FA7B0` | Compiles and executes a raw Lua script string |
| `Caver::ProgramState::Execute` | `0x004C182c` | `0x002FA810` | Executes a pushed Lua function with `nargs` |
| `Caver::ProgramState::Resume` | `0x004C177C` | `0x002FA760` | Resumes a suspended Lua coroutine |
| `Caver::ProgramState::CreateChildState` | `0x004C133C` | `0x002FA410` | Creates a child coroutine/sub-state linked to parent state |

---

## 2. Struct Memory Layout (`ProgramState`)

```
ProgramState Layout (ARM64):
+0x000: vtable pointer
+0x008: lua_State* L                   (Raw Lua 5.1 state pointer)
+0x010: std::list<boost::shared_ptr<ProgramState>> child_states
+0x028: boost::intrusive_ptr<SceneObject> parent_object
+0x038: float timer_wait_seconds
+0x03C: int execution_flags
```

---

## 3. How Native C/C++ Functions are Bound to Lua

Caver Engine uses a lightweight `LibFunction` struct which maps 1-to-1 with Lua 5.1's `luaL_Reg`:

```cpp
struct LibFunction {
    const char*     name;
    lua_CFunction   func;
};
```

When `ProgramState::RegisterLibrary("MyLib", functions)` is invoked:
1. `ProgramState` calls `luaL_register(this->L, "MyLib", functions)`.
2. Lua creates a global table named `MyLib` and binds each C function pointer to its string key.
3. Mod scripts can then invoke `MyLib.MyFunction(...)` directly in `.lua` scripts.

When `ProgramState::RegisterClass("MyClass", methods, static_methods)` is invoked:
1. `luaL_newmetatable(this->L, "MyClass")` creates the class metatable.
2. `__index` is assigned to point back to the metatable so methods are looked up dynamically.
3. `methods` are bound to the metatable for instance calls (`obj:method()`).
4. `static_methods` are bound to a global class table (`MyClass.New()`).

---

## 4. Extension Architecture for Modders

To expose new modding APIs, SRE Hooks intercept `ProgramState::RegisterProgramLibrary` (`0x004769F4` / `0x004821C8`) or inject custom `LibFunction` tables during scene loading:

```cpp
// Example: Exposing new ProgramState functions into Lua VM
static const LibFunction g_programstate_mod_funcs[] = {
    { "SetGlobalSpeed",           l_ProgramState_SetGlobalSpeed           },
    { "SpawnPrefab",              l_ProgramState_SpawnPrefab              },
    { "SetGravity",               l_ProgramState_SetGravity               },
    { "ExecuteStringInContext",   l_ProgramState_ExecuteStringInContext   },
    { "InterceptEvent",           l_ProgramState_InterceptEvent           },
    { "SetPostFXPreset",          l_ProgramState_SetPostFXPreset          },
    { "OverrideComponentProperty",l_ProgramState_OverrideComponentProperty},
    { "RegisterCustomCommand",    l_ProgramState_RegisterCustomCommand    },
    { "SaveCustomData",           l_ProgramState_SaveCustomData           },
    { "LoadCustomData",           l_ProgramState_LoadCustomData           },
    { "SetHitboxOverride",        l_ProgramState_SetHitboxOverride        },
    { NULL, NULL }
};

void inject_mod_programstate_library(lua_State* L) {
    luaL_register(L, "ProgramState", g_programstate_mod_funcs);
}
```
