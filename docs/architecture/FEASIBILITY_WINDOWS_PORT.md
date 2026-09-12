# Feasibility Study: Windows + Linux Multiplatform Architecture

Status: **Adopted (in progress)**
Scope: Turn SwordigoDesktop from a Linux-only app into a true multiplatform
(applies to Windows first, macOS later) build with modular CMake and an
optional, runtime-loadable Unicorn backend.

---

## 1. Executive summary

SwordigoDesktop is already *better* ported than it first looks:

- The CPU layer is abstracted behind `IEmulatorArm64` (`src/platform/i_emulator_arm64.h`)
  with two interchangeable backends: `EmulatorDynarmic64` (JIT) and `EmulatorArm64` (Unicorn).
- The guest is an ARM ELF that we parse **ourselves** (`src/loader/elf_loader*.cpp`,
  self-contained `elf_types*.h`) — no host `elf.h`, no host ELF loader, so the guest
  load path is already OS-independent.
- Graphics is SDL3 + Dear ImGui; the Vulkan backend uses **volk** (dynamic function
  loading) and `VK_NO_PROTOTYPES`, so no hard link-time GPU dependency.
- `main.cpp` already has `#ifdef _WIN32` scaffolding for paths and `<windows.h>`
  includes (lines 2–8, 6753–6760).
- Dynarmic is built from source in-tree (`deps/dynarmic`) with a CMake custom target;
  it already produces static archives we link — works on MSVC/MinGW too.

The remaining blockers are **not** the CPU emulation. They are:

1. **Unicorn is a hard, REQUIRED build dependency** even though Dynarmic is the
   default engine. This is the single biggest Linux-only assumption in the build.
2. **Linux syscall idioms in the host layer**: `mmap` (4 GiB guest allocation),
   `readlink("/proc/self/exe")`, `fork()`/`execlp("xdg-open")`, `execv("/proc/self/exe")`
   death-restart, `dlopen`, `__attribute__((weak))`, `-Wl,--whole-archive`,
   `-Wl,-rpath,$ORIGIN`, `.so` naming, `pkg-config`.
3. **A monolithic `CMakeLists.txt`** with hardcoded Linux flags (`-rdynamic`,
   `--allow-shlib-undefined`, `_GNU_SOURCE`) and no platform branching.

None of these are architectural; all are mechanical. Estimated effort to first
**compile** on Windows: moderate. Estimated effort to first **run** correctly on
Windows: moderate-to-high (any emulator port always needs tuning; Dynarmic + the
guest JNI bridge are already cross-platform in design).

---

## 2. What is already multiplatform (verified by reading the code)

| Area | Mechanism | Portable? |
|------|-----------|-----------|
| ARM64 CPU JIT | `IEmulatorArm64` interface, `EmulatorDynarmic64` | Yes (Dynarmic builds on MSVC/MinGW/clang) |
| ARM64/ARM32 ELF load | self-written parser (`elf_loader*.cpp`) | Yes |
| Graphics | SDL3 (Windows backend built-in) | Yes |
| Vulkan | volk + `VK_NO_PROTOTYPES` (`vulkan_backend.cpp`) | Yes |
| GUI | Dear ImGui, GL/Vulkan backends | Yes |
| Audio | OpenAL (Windows has native drivers) | Yes |
| Image | SDL3_image / STB | Yes |
| User paths | `data_path.cpp` has `#ifdef _WIN32` (APPDATA) | Mostly — needs exe-dir + fixes |
| Window/input | SDL3 | Yes |
| Lua host | Lua 5.1 sources compiled in-tree | Yes |
| Dynarmic | built in-tree via CMake custom target | Yes (CMake picks MSVC) |

## 3. Inventory of Linux-exclusive assumptions

### 3.1 Build system (`CMakeLists.txt`, `Makefile`)

| Location | Assumption | Windows impact |
|----------|-----------|----------------|
| `find_library(UNICORN_LIBRARY NAMES unicorn REQUIRED)` | Unicorn dev package must exist at configure time | Build fails on clean Windows unless unicorn.dll dev is provided. **Fix: make optional / runtime-loaded.** |
| `pkg_check_modules(...)` for SDL3_image, vorbisfile, mpg123 | pkg-config + those .pc files | Not on Windows. **Fix: CMake `find_package`/FetchContent fallbacks.** |
| `-rdynamic`, `-Wl,--allow-shlib-undefined`, `-Wl,--whole-archive` | GNU ld flags | MSVC/lld-link rejects or ignores them. **Fix: branch by `WIN32`.** |
| `-Wl,-rpath,$ORIGIN/...` | ELF rpath | No rpath on Windows; DLLs resolve from exe dir. **Fix: copy DLLs next to exe.** |
| `swordigo_library(... SHARED)` + `LIBRARY_OUTPUT_DIRECTORY` | `.so` naming | `LIBRARY_OUTPUT_DIRECTORY` handles `.dll` on Windows automatically; need `RUNTIME_OUTPUT_DIRECTORY` for DLLs + imports. |
| `target_link_libraries(... m)` | libm | Absent on MSVC (provided by CRT). **Fix: `if(NOT MSVC)`.** |
| `-D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE -D_DEFAULT_SOURCE` | glibc feature macros | MSVC doesn't know them. **Fix: Linux-only.** |
| `filerift` `LUAI_FUNC=extern __attribute__((visibility("default")))` | GCC visibility | MSVC: `__declspec(dllexport)` or just remove. **Fix: platform macro.** |
| `find_program(AARCH64_CC aarch64-linux-gnu-gcc REQUIRED)` | cross-compiler for guest `libsre.so` | **Not needed to build libsre.so.** Build it once (CI) and ship it; the host ELF loader + `libsre.so` file path is already abstracted. Optionally use a bundled prebuilt `libsre.so`. |
| static FFmpeg built via `configure && make` in `src/tools/ffmpeg` | Unix build script | **Biggest build-toolchain issue.** Options: (a) bundle prebuilt FFmpeg DLLs; (b) use a CMake FFmpeg build; (c) drop video and ifdef `video_background`. |

### 3.2 Host runtime code (Linux syscall idioms)

| Location | Assumption | Fix |
|----------|-----------|-----|
| `main.cpp:661` `mmap(nullptr, 0x100000000ULL, ..., MAP_NORESERVE)` | `mmap` | Cross-platform `platform_alloc_guest_memory()` → `VirtualAlloc(MEM_RESERVE\|MEM_COMMIT)` on Windows. |
| `main.cpp:1466` `execv("/proc/self/exe", ...)` death-restart | `/proc/self/exe` | Cross-platform `platform_restart_process()` → `GetModuleFileNameW` + `CreateProcess`. |
| `launcher_ui.cpp:52-59,1171,1395,1449,2758` `fork()`+`execlp("xdg-open"/"ruby")` | POSIX spawn + xdg-open | `platform_open_external()` → `ShellExecuteW`/`SDL_openURL` on Windows. |
| `data_path.cpp:215` `readlink("/proc/self/exe")` | `/proc` | `GetModuleFileNameW` on Windows. |
| `binary_selector.cpp:653` `system("command -v unzip")` | POSIX `unzip` | Use bundled extractor (miniz/`7z` fallback) or `tar` on Windows (built-in). |
| `openswordigo_host.cpp` `dlopen`/`dlsym` | `dlfcn.h` | `platform_dlopen.h` wrapper (`LoadLibrary`/`GetProcAddress`). |
| `__attribute__((weak)) std::string g_save_dir` (`data_path.cpp:86`) | GCC weak symbol | `SWORDIGO_WEAK` macro → MSVC `__declspec(selectany)`. |
| `swordfare_gui.cpp`/`jni_bridge*.cpp` sockets `arpa/inet.h`, `sys/socket.h`, `fcntl.h` | BSD sockets | `SDL_net` or Winsock wrapper; or restrict RakNet/network bridges to POSIX for now. |
| `getenv("HOME")/XDG_*` | POSIX env | `main.cpp:6753` already handles `_WIN32`; `data_path.cpp` needs the same treatment for exe-relative fallbacks. |

### 3.3 Platform expectations baked into paths

- `.so` in `g_lib_name = "engine/v1.4.12/arm64-v8a/libswordigo.so"` — this is a **guest
  file name** (a file on disk loaded by our ELF loader), so it must stay `.so` on
  every platform. Correct as-is.
- `libsre.so` is likewise a guest-side file loaded by the ELF loader, not a host
  shared library. Correct as-is.
- System install paths `/usr/share/swordigo/...` are only fallbacks; Windows uses
  `%LOCALAPPDATA%`.

## 4. Recommended architecture changes (this work)

1. **Unicorn becomes optional and runtime-loadable** (`src/platform/unicorn_dyn.*`):
   - No `find_library(unicorn REQUIRED)`. The build never fails because of unicorn.
   - At runtime the loader looks for `libunicorn.so` (Linux: app dir → system paths)
     or `unicorn.dll` (Windows: app dir → PATH). If found, the Unicorn backend works;
     if not, `unicorn_available()` returns false.
   - The launcher shows a prompt when the user selects Unicorn but it is unavailable:
     *drop libunicorn.so / unicorn.dll next to the app, or install the system package.*
2. **Dynarmic is the permanent default**: `g_use_dynarmic = true` by default,
   `SWORDIGO_USE_DYNARMIC=ON` default in CMake (already ON), `run_swordigo.sh`
   default Dynarmic (already true).
3. **Modular CMake**: split the monolithic `CMakeLists.txt` into per-component files
   (`src/platform`, `src/imgui`, `src/loader`, `src/jni`, `src/game`, `src/tools`,
   `src/sre`) + a `cmake/` helpers module for platform branching. All options live
   at the top level; consumers stay target-based.
4. **`cmake/Platform.cmake`**: wraps every Linux-only compiler/linker flag behind
   `if(UNIX)`/`if(WIN32)`/`if(NOT MSVC)`.
5. **Cross-platform shim layer** (`src/platform/os_*` or `platform_*` functions):
   guest memory alloc, exe-dir, process restart, open-external, dlopen, weak symbol.
6. **Windows output layout**: host DLLs land next to `swordfare.exe`; guest files
   (`libswordigo.so`, `libsre.so`, `engine/`, `assets/`) stay in the user data dir.

## 7. Windows toolchain options (recommended)

- **MSVC + CMake + Ninja** (best Windows integration; SDL3 ships `.lib` import libs;
  Dynarmic builds with MSVC).
- or **MinGW-w64 + CMake** (closer to existing GCC flags).
- `aarch64-linux-gnu-gcc` is only needed to *recompile* `libsre.so`. Recommended:
  build `libsre.so` in Linux CI and ship the artifact in the Windows package.
- FFmpeg: prefer **prebuilt Windows FFmpeg DLLs** (from FFmpeg's gyan.dev or similar)
  over re-running `configure`; or gate video behind `SWORDIGO_VIDEO=OFF`.

## 8. Known Windows-specific risks (to validate on real hardware)

1. **Vulkan on Windows** (`vulkan_backend.cpp`): swapchain/extension differences —
   SDL3 abstracts most of it. The user's stated concern ("vulkan thingy tends to do
   issues on Windows") is handled by keeping Vulkan experimental + OpenGL default,
   and by isolating the backend so a Vulkan crash only loses that path.
2. **4 GiB `VirtualAlloc`** with `MEM_RESERVE` is fine on 64-bit; ensure x64 build.
3. **Signals**: Dynarmic guest faults are handled via its own callbacks, not host
   signals (this codebase already uses Dynarmic's `MemoryIsWriteable`/hook mechanism),
   so no `sigaction`/`ucontext` dependency for the JIT path.
4. **Paths/case**: guest asset paths are forward-slash; `std::filesystem` on Windows
   handles both — the VFS resolver in `data_path.cpp` already normalizes `/`.
5. **Long paths / Unicode**: prefer `GetModuleFileNameW` + `std::wstring` conversions
   in the shim.

## 9. Effort breakdown

| Work item | Effort | Depends on | Status |
|-----------|--------|-----------|--------|
| Unicorn optional runtime loader | Small | none | ✅ done |
| Dynarmic default + launcher prompt | Small | loader | ✅ done |
| Cross-platform shims (mmap/exe-dir/restart/open-external/dlopen/weak) | Small–Medium | none | ✅ done (Linux green) |
| CMake modularization + platform branches | Medium | none | ✅ done (Linux green) |
| Winsock/network bridge port | Medium | decision: bundle SDL_net vs ifdef | open |
| FFmpeg Windows distribution | Medium | decide prebuilt DLLs vs drop video | open |
| Guest `libsre.so` Windows artifact | Small (CI) | Linux cross-compile in CI | open |
| Full Windows test pass | Large | hardware + Windows VM/CI | open |

## 10. Progress log (implementation notes)

1. **`src/platform/unicorn_dyn.{h,cpp}`** — runtime Unicorn loader. Enum ABI verified
   against the installed `/usr/include/unicorn/{arm,arm64}.h` (caught + fixed a
   spurious `UC_ARM64_REG_W31` that shifted X/V/PC by one). Resolution order:
   `<exe_dir>/unicorn.dll|libunicorn.so` → system search. `unicorn_backend_available()`
   is cached; a missing backend returns false with a human message.
2. **Dynarmic default** — `main.cpp` `g_use_dynarmic = true`; launcher `engine_sel`
   already defaults to Dynarmic. `--engine=unicorn`/`--unicorn` explicitly opt in.
   If Unicorn is requested but unavailable, `main.cpp` prints an actionable error and
   exits; the launcher (`launcher_ui.cpp`) blocks the launch with a modal prompt.
3. **`emulator.cpp` / `emulator_arm64.cpp`** — swapped `#include <unicorn/unicorn.h>`
   → `platform/unicorn_dyn.h`; constructor checks backend availability; `run()`
   guards against a null engine.
4. **`src/platform/os_external.{h,cpp}`** — cross-platform shim: `exe_dir()`,
   `home_dir()`, `open_in_file_manager()`, `spawn_detached()`, `restart_process()`,
   `reserve_large_region()` (4 GiB guest memory), `load_library/find_symbol/close_library
   /library_error()` (dlopen family), `SWORDIGO_WEAK` macro. Consumers migrated:
   `main.cpp` (mmap + death-restart execv), `launcher_ui.cpp` (ruby viewer + 4×
   xdg-open + readlink), `data_path.cpp` (readlink + weak `g_save_dir`),
   `swordfare_gui.cpp` (6× `getenv("HOME")` + weak SRE globals),
   `openswordigo_host.cpp` (dlopen/dlsym/dlclose), `srehost_impl.cpp` (weak symbol).
5. **Linux-only headers guarded** — `unistd.h`, `sys/socket.h`, `netinet/in.h`,
   `arpa/inet.h`, `dirent.h`, `sys/time.h`, `sched.h`, `fcntl.h`, `netdb.h` now
   wrapped in `#ifndef _WIN32` (jni_bridge*, swordfare_gui, launcher_ui).
6. **Build-system optionality** — CMake `find_library(unicorn)` no longer REQUIRED;
   `unicorn_dyn.cpp` added to swemu. Makefile `-lunicorn` is now `UNICORN_LINK`,
   auto-detected (empty when the dev package is absent).
7. **CMake modularization** — the monolithic `CMakeLists.txt` was split into:
   `cmake/Swordigo{Options,Platform,Deps,Helpers}.cmake` + one file per component
   under `cmake/components/` (swcore, swgui, swfmt_swpod, filerift, swgfx, swemu,
   swordfare, swordfare_boot, ruby, sre, misc). All Linux-only flags
   (`-fno-strict-aliasing`, `_GNU_SOURCE`, `-rdynamic`, `--whole-archive`,
   `--allow-shlib-undefined`, pkg-config audio, `util`) are now gated behind
   `if(NOT WIN32)`/`if(MSVC)`, with MSVC equivalents (`/O2`, `/WHOLEARCHIVE`).
   On Windows component DLLs land next to `swordfare.exe`; the guest `libsre.so`
   stays in `bin/libs` (built by the aarch64 cross compiler, unchanged).
   Verified with both an incremental and a from-scratch build.
8. Verified: full CMake build green, `bin/swordfare` launches, loader round-trips
   `uc_open`/`uc_reg_write`/`uc_reg_read` against system Unicorn 2.1.

## 11. Scope decision for this iteration

This pass delivers items 1–5 (loader, default, prompt, shims, modular CMake) and
keeps the Linux build green. Networking (RakNet) and FFmpeg distribution are
documented but not force-migrated — they are cleanly separable later because the
host is already modular.
