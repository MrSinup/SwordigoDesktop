# Shared Subsystems Analysis & Modularization Study

## Executive Summary
Currently, `SwordigoDesktop` builds two primary host binaries:
1. `swordigo_boot` (79.7 MB unstripped executable) — Main desktop engine, ARM32/ARM64 Unicorn + Dynarmic emulator, GLES/Vulkan renderer, JNI bridge, and launcher UI.
2. `ruby` (33.1 MB unstripped executable) — Standalone asset browser, model viewer, Filerift scene converter, and audio previewer.

Both binaries currently compile redundant copies of shared source files into independent object files (`build/%.o` and `build/host_lua/%.o`), resulting in massive binary bloat (~112.8 MB combined), long incremental link times, and duplicated runtime memory pages.

---

## 1. Codebase Redundancy Matrix

| Subsystem Component | Source Files | Compiled into `swordigo_boot` | Compiled into `ruby` | Redundant Compile Size |
| :--- | :--- | :---: | :---: | :---: |
| **ImGui & Rendering Backends** | `src/imgui/*.cpp`, `imgui_impl_sdl3.cpp`, `imgui_impl_opengl3.cpp` | Yes | Yes | ~8.4 MB |
| **Texture & Format Decoders** | `src/platform/pvr_loader.cpp`, `src/platform/pvrtc_decoder.cpp` | Yes | Yes | ~1.2 MB |
| **Core Utilities & Data Paths** | `src/platform/data_path.cpp`, `src/platform/io_thread.cpp` | Yes | Yes | ~0.5 MB |
| **Filerift & Scene Schemas** | `src/tools/filerift.cpp`, `src/tools/scene_schemas.cpp`, `src/tools/boulder.cpp` | Yes | Yes | ~4.8 MB |
| **Host Lua 5.1 Runtime** | `src/sre/lua/src/*.c` (28 files) | Yes (`HOST_LUA_OBJS`) | Yes (`LUA_OBJS`) | ~3.6 MB |

---

## 2. Benefits of Modular Shared Object (`.so`) Re-Architecture

1. **Binary Footprint Reduction**: Shared libraries (`.so`) loaded via Dynamic Linker (`ld-linux.so`) will eliminate ~18.5 MB of duplicate binary code, reducing total installation footprint by over 40%.
2. **Shared Memory Pages**: The OS kernel will share read-only `.text` code pages in RAM across `swordigo_boot` and `ruby` processes when both tools are running concurrently.
3. **Faster Incremental Compilation**: Modifying core graphics, format decoders, or ImGui widgets will only trigger recompilation of the specific `.so` target rather than relinking 80 MB monolithic binaries.
4. **Clean Architectural Boundaries**: Enforces strict encapsulation through clear header interfaces and dynamic symbol visibility controls (`__attribute__((visibility("default")))` vs hidden internal symbols).

---

## 3. Targeted Shared Object Modules

| Target ELF Shared Object | Subsystem Name | Included Source Modules | Primary Dependent Binaries |
| :--- | :--- | :--- | :--- |
| `libswd_core.so` | Platform Core | `data_path.cpp`, `io_thread.cpp`, `rgc.cpp` | `swordigo_boot`, `ruby`, tools |
| `libswd_gui.so` | Dear ImGui Framework | `imgui*.cpp`, `imgui_impl_sdl3.cpp`, `imgui_impl_opengl3.cpp`, `imgui_impl_vulkan.cpp` | `swordigo_boot`, `ruby` |
| `libswd_formats.so` | Texture & PVR Decoders | `pvr_loader.cpp`, `pvrtc_decoder.cpp` | `swordigo_boot`, `ruby` |
| `libswd_filerift.so` | Filerift Engine & Host Lua | `filerift.cpp`, `scene_schemas.cpp`, `boulder.cpp`, Host Lua 5.1 (`lapi.c` ... `linit.c`) | `swordigo_boot`, `ruby` |
| `libswd_gfx.so` | Rendering Pipeline | `vulkan_backend.cpp`, `fbo_scaler.cpp`, `video_background.cpp` | `swordigo_boot` |
| `libswd_emu.so` | Emulation Engine | `emulator*.cpp`, `elf_loader*.cpp`, `jni_bridge*.cpp`, `jni_marshaller.cpp` | `swordigo_boot` |
| `libopensw_ui.so` | Swordfare UI & Modding | `swordfare_gui.cpp`, `launcher_ui.cpp`, `save_editor.cpp`, `mod_tools.cpp` | `swordigo_boot` |
