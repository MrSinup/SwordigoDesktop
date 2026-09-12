# Lua FFI Integration Architecture for SRE

## 1. Current State and Pre-existing FFI Support
- **Repository Search**: The repository root contains `libffi-master`, indicating that libffi is available and intended for native FFI binding integration. 
- **Lua Environment**: `src/sre` contains a vanilla Lua 5.1 implementation compiled from source. There are no existing LuaJIT FFI or CFFI-Lua binding libraries present in the `src/sre` codebase.
- **Current Approach**: Currently, to call engine functions from Lua, C wrapper stubs are written in `src/sre/sre_mini_api.c` (e.g., `l_mini_char_die`, `l_mini_char_hurt`), which internally resolve pointers using `g_swordigo_base + offset`.

---

## 2. Advantages of Exposing the FFI API
Exposing a standard Lua FFI module (e.g., ported `luaffi` built over `libffi-master`) provides massive flexibility for SRE modding:
- **`ffi.cdef`**: Allows defining raw engine C/C++ structures (e.g., `SceneObject`, `CharControllerComponent`) and function signatures directly in Lua.
- **`ffi.cast`**: Scripts can cast a raw integer or symbol address into a function pointer and call it directly. It can also cast raw pointer userdata to typed structs to read/write memory.
- **`ffi.new`**: Allows Lua to dynamically allocate C structs to pass as arguments to engine functions by reference.
- **Benefit**: This entirely removes the bottleneck of writing and maintaining C stubs in `sre_mini_api.c` for every new engine API that modders want to access.

---

## 3. High-Tech Dynamic Symbol Resolution (`caver.resolve` / `sre_resolve_address`)
Because SRE owns the full emulation host (`SwordigoDesktop` + loader + IDA symbol map):
- SRE already exports `sre_resolve_address(const char* symbol)` and `caver.resolve("DemangledSymbolName")`.
- **FFI Symbol Resolution**: FFI does not need manual hex offsets! Modders can resolve demangled C++ engine symbols dynamically by name:
  ```lua
  ffi.cdef[[ void Caver_GameSceneController_CreateHeroObjectAt(void* sc, float* pos, int dir, int add_to_scene); ]]
  
  -- High-tech symbol lookup via SRE emulator symbol map:
  local create_hero_addr = caver.resolve("Caver::GameSceneController::CreateHeroObjectAt")
  local create_hero = ffi.cast("void(*)(void*, float*, int, int)", create_hero_addr)
  
  -- Call native C++ function directly from Lua!
  create_hero(sc_ptr, spawn_pos, 1, 1)
  ```

---

## 4. Integration Points
To integrate a Lua FFI wrapper (like `luaffi` built over `libffi-master`), the following files need updates:
- **`Makefile`**: 
  - Compile `libffi-master` for the target architecture (`x86_64` / `aarch64`).
  - Compile the Lua FFI binding source files.
  - Link them into `libsre.so` (`SRE_LUA_OBJS`).
- **`src/sre/sre_lua.h`**:
  - Add the global declaration for the FFI initialization function (e.g., `int luaopen_ffi(lua_State *L);`).
- **`src/sre/sre_lua_libs.c` / `src/sre/sre_mini_api.c`**:
  - Inside `sre_open_swkiwi_libs` (or wherever standard libraries are injected), call `luaopen_ffi(L)` or use `luaL_register` to expose the `ffi` table to the global Lua environment.

---

## 5. Memory Safety & Modding Implications
- **Safety**: Raw memory access via FFI is fundamentally unsafe. An incorrect struct offset or mismatched function signature in `ffi.cdef` will crash the engine (e.g., `SIGSEGV`).
- **C Header Parsing**: `luaffi` provides basic C parser capabilities. Complex C++ classes must be modeled as C structs containing standard types, bypassing virtual method dispatch manually by dereferencing vtables if necessary.
- **API Exposure**: The `ffi` module gives Lua scripts complete control over the application address space, providing maximum power for desktop modding.
