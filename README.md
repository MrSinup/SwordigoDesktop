# SwordigoDesktop & Swordigo Runtime Engine (SRE)

Standard Binary Compatibility Layer, ARM64 JIT Execution Runtime, and Software Development Kit for Linux.

---

## 1. Overview

**SwordigoDesktop** is a open-source native Linux compatibility layer, reverse-engineering SDK, and modding toolchain for Touch Foo's *Swordigo*.

Instead of relying on heavy OS virtualization or high-overhead Android emulators, SwordigoDesktop implements a hybrid binary-translation architecture:
* **Host Engine**: Parses and maps original ARM64 ELF binaries (`libswordigo.so`) into a contiguous 64-bit virtual memory space.
* **JIT CPU Acceleration**: Executes ARM64 machine instructions using **Dynarmic**, a high-performance A64 Just-In-Time compiler configured with 512MB code cache allocation and targeted x86_64 FMA fast-paths.
* **API Bridge Layer**: Intercepts Android Bionic libc calls, GLES 1.1/2.0 graphics routines, OpenSL audio, and JNI methods, routing them directly to host-native SDL3, OpenGL, and OpenAL Soft APIs.
* **Guest Trampoline Runtime (SRE)**: Injects the version SRE guest library (`libsre12.so` for 1.4.12 / `libsre13.so` for 1.4.13, shared base infra in `src/sre/base` compiled into each) at guest address `0x2000000` via 16-byte ARM64 function trampolines to replace legacy GNU atomic string spinlocks, thread contention bottlenecks, and mobile rendering restrictions.

---

## 2. System Architecture

```
+-----------------------------------------------------------------------+
|                         Host Process (x86_64 Linux)                   |
|                                                                       |
|  swordigo_boot          SDL3 Window          OpenGL / Vulkan Pipeline |
|  |-- ELF Loader         |-- Input Events     |-- FBO Viewport Scaler  |
|  |-- JNI Bridge         |-- Keybindings      |-- PostFX (SSAO, Rays)  |
|  `-- ImGui Overlay      `-- Display Config   `-- Upscaler (FSR/Sharp) |
|                                                                       |
+-----------------------------------------------------------------------+
|                  Dynarmic A64 JIT CPU Core (512MB Cache)              |
|                                                                       |
|  +-----------------------------------------------------------------+  |
|  |                     Guest Address Space (3.5 GB)                |  |
|  |                                                                 |  |
|  |  libswordigo.so (0x1000000)  libsre12.so / libsre13.so (0x2000000)  JNI Structs|  |
|  |                                                                 |  |
|  |  <-- ARM64 Instructions Translated via Dynarmic JIT Core -->   |  |
|  +-----------------------------------------------------------------+  |
|                                                                       |
+-----------------------------------------------------------------------+
|                       Bridge & Trampoline Layer                       |
|  ~400 JNI & Native Function Bridges mapped to Host C++ implementations|
+-----------------------------------------------------------------------+
```

---

## 3. Subsystem Breakdown

### 3.1 Host Frontend & Binary Loader (`src/loader/`)
* **ELF Parsing (`elf_loader_arm64.cpp`)**: Parses ELF64 headers, maps `PT_LOAD` segments with page-aligned permissions, and resolves `R_AARCH64_RELATIVE`, `R_AARCH64_GLOB_DAT`, and `R_AARCH64_JUMP_SLOT` relocations.
* **Virtual Memory Map**: Allocates a single 3.5GB page-aligned host buffer (`calloc`) ensuring low memory alignment.

### 3.2 CPU Execution Core (`src/platform/emulator_dynarmic64.cpp`)
* **JIT Tuning**: Configured with 512MB code cache to eliminate JIT block re-compilation stalls.
* **Unsafe Fast-Paths**: Enables `Unsafe_UnfuseFMA` to map ARM NEON FMA instructions to native x86 hardware FMA, and `Unsafe_InaccurateNaN` to bypass ARM canonical NaN bit pattern transformations.
* **Instruction Cache Invalidation**: Listens for `IC IVAU` / `IC IALLU` system instructions via `InstructionCacheOperationRaised` callbacks to invalidate JIT code blocks dynamically.

### 3.3 SRE Guest Runtime (`src/sre/` — `base/` shared infra, `sre12/` 1.4.12, `sre13/` 1.4.13)
* **Non-Atomic String Replacements (`sre_string.c`)**: Replaces GNU libstdc++ copy-on-write `std::string` atomic refcounting (`LDAXR`/`STLXR`) with single-threaded direct integer operations, eliminating exclusive monitor spinlocks.
* **GLES Render Hooking (`sre_background.c`, `sre_gui_native.c`)**: Intercepts immediate matrix calls and raw CPU vertex arrays, converting them into modern GPU-side Vertex Buffer Objects (VBO).

### 3.4 FBO Viewport Scaler & PostFX (`src/platform/fbo_scaler.cpp`)
* Decouples native 960x544 game rendering from host desktop display resolution.
* **Upscaling Algorithms**: AMD FidelityFX Super Resolution (FSR 1.0), Sharp Bilinear, CRT Scanline simulation.
* **Post-Processing Pass**: Multi-pass Screen Space Ambient Occlusion (SSAO), 64-sample radial god rays, bloom extraction, and chromatic aberration.

---

## 4. Building and Toolchain Setup

### 4.1 System Prerequisites

#### Debian / Ubuntu:
```bash
sudo apt update
sudo apt install build-essential gcc-aarch64-linux-gnu \
    libsdl3-dev libsdl3-image-dev libgl-dev \
    libunicorn-dev libopenal-dev libvorbis-dev zlib1g-dev pkg-config
```

#### Fedora / RHEL:
```bash
sudo dnf install gcc gcc-c++ gcc-aarch64-linux-gnu \
    SDL3-devel SDL3_image-devel mesa-libGL-devel \
    unicorn-devel openal-soft-devel libvorbis-devel zlib-devel pkg-config
```

### 4.2 Building from Source

```bash
# Clone the repository
git clone https://github.com/TheCorrectSynovian/SwordigoDesktop.git
cd SwordigoDesktop

# Configure and build with CMake
cmake -S . -B build-cmake
cmake --build build-cmake -j$(nproc)

# Run the test suite
ctest --test-dir build-cmake --output-on-failure

# Launch the runtime environment (built directly into bin/)
./bin/swordfare
```

---

## 5. Keybindings and Control Mapping

| Key | Action | Subsystem |
| :--- | :--- | :--- |
| **WASD** / **Arrow Keys** | Character Movement | Game Input |
| **Space** / **W** | Jump / Double Jump | Game Input |
| **J** / **Z** | Sword Attack | Game Input |
| **K** / **X** | Magic Spell | Game Input |
| **I** | Inventory & Item Management | Game Input |
| **Escape** | Pause Menu / Configuration | Game Input |
| **F1** | Toggle SRT Debug Overlay | Host Overlay |
| **F4** | Cycle Viewport Upscaler Mode | FBO Scaler |
| **F8** | Pause / Resume CPU Emulation | Emulator Core |
| **`** (Backtick) | Open Raijin Interactive Lua Debug Console | Developer Console |

### Runtime stability status

Previously reported Swordfare startup crashes caused by unresolved FFmpeg symbols in `libswgfx.so` are fixed in the current build. Scene transitions, launcher video startup, and Ruby asset resolution have also received dedicated hardening. If an older binary still exits with `undefined symbol: avformat_open_input`, rebuild all targets instead of reusing libraries from an earlier package.

A **SRE-only boot hang at the main menu** is fixed. With SRE enabled, the GUI / program-library hooks route menu initialization through a heavy zero-fill/init routine (`start=0x14789f0`, spinning at the `0x144fdc8` bulk-clear loop). The Dynarmic stall watchdog previously only allowed two hardcoded "heavy function" entry points to break out of a long same-PC loop; every other legitimate heavy loop hit `same_pc_count = 0`, which reset the counter so it could neither break nor reach the `MAX_CHUNKS` safety bound — an infinite benign loop, so the game froze at the menu (no-SRE mode was unaffected because it never took that path). The watchdog now breaks out of *any* heavy same-PC loop when there are **zero pending threads** (definitively not a spinlock), letting the host loop breathe while `MAX_CHUNKS` stays as the outer safety bound. Boot now proceeds past menu init through `applicationDidBecomeActive` into the game loop with SRE on. (Fix: `src/platform/emulator_dynarmic64.cpp`, Tier-3 heavy-function branch.)

A second **SRE-only frame-loop crash at `0x10771ac`** is fixed. `sre_CaverShell_Update` (`src/sre/sre12/sre_frame_loop.c`) dispatches the root scene view via `vtable[9]` (offset `0x48`). During early activation the root-view pointer at `shell+0x88` (or its vtable slot) can be transiently stale/poisoned (`0xdeadc0de12345678`), resolving the update function pointer into the `.dynstr` region (the `CaverShell` type-name string at `0x10771ac`) — calling it jumped the JIT into rodata and halted every frame. The call site now validates the vtable pointer with `sre_is_valid_vtable_ptr()` **and** checks the resolved function pointer lies in executable `.text` (`0x1203e90`–`0x1583478`) or libsre before dispatching; otherwise it skips that frame's root-view update (logged, capped) so the loop keeps running. Verified: `--sre` boots into the game loop and runs the 1000-frame stability test with zero `0x10771ac` exceptions. (Fix: `src/sre/sre12/sre_frame_loop.c`, `sre_CaverShell_Update` step 6.)

**SRE-only "scene objects never disappear" bug is fixed** (keys/swords staying after pickup, game notifications and chat bubbles staying forever). Root cause: with the NavController menu→game transition stalled inside `__do_upcast` (see the JOB1 bypass), the shell's per-frame root-view dispatch keeps ticking the stale menu view and `GameSceneView::Update` is **never called** — the world simulation only survived because the JOB1 fallback drives `Scene::Update` directly. But the entire game GUI subtree lives inside `GUIView::Update` (`0x49e55c`), which is only reached from `GameSceneView::Update`'s tail call: notification auto-dismiss timers (`NotificationView::Update` +0x18C), chat/text-bubble fades, GUIAnimation completion callbacks (`AnimationDidFinish` → dismiss) and the deferred subview-removal drain all never ran, so everything that should self-destruct just stayed. Verified live: `g_sre_gui_scene_active` stayed 0 for whole sessions while `world_drive` kept climbing. Fix (mirror of JOB1 for the view side): when the per-frame flag shows the normal chain never dispatched the GameSceneView, the frame loop locates it — first by walking the shell root view's subview tree for the GameSceneView vtable address point (`0x16cb580`), then via the GameViewController captured by the `FinishTransitionToViewController` hook (`GVC+0xD8` = `shared_ptr<GameSceneView>` px, validated against the live `Scene*`) — and dispatches its `vtable[9]` Update itself. Verified: `gsv=1` every frame, `GUIView::Update` relay counter climbing +1/frame, 1000-frame stability clean, menu state untouched (`gsv=0`). `sre_ProgramState_Execute` now also logs (capped) Lua errors it previously swallowed silently — item-collect handlers only flag `SceneObject:destroy()` at the end, so a silent mid-script error also left pickups in the scene. (Fix: `src/sre/sre12/sre_frame_loop.c` GUI repair drive, `src/sre/sre12/sre_gui_nav.c` GVC capture, `src/sre/sre12/sre_lua.c` error surfacing.)

The Scene & Display Toolbox is available with **F11**. It includes responsive render/output presets for 4:3, 5:4, 16:9, 16:10, ultrawide, laptop, native Android, and low-performance resolutions, plus custom dimensions and exact fullscreen display modes.

---

## 6. Technical Documentation Index

All technical documentation, reverse-engineering analyses, and API specifications are unified in the [`docs/`](docs/README.md) directory:

* **[Architecture Specifications](docs/architecture/)**: Engine architectural designs, SRE platform master plans, Yuzu Dynarmic JIT research, Linux ELF loader specifications.
* **[ARM64 & Emulation](docs/emulation_and_arm64/)**: JIT correctness logs, `LDXR`/`STXR` exclusive monitors, ELF relocation inventories, memory layout audits.
* **[Graphics & Rendering](docs/graphics_and_rendering/)**: FBO viewport scaler design, GLES2 pipeline plan, PostFX remastering plans, GPU thread architecture.
* **[APIs & Subsystems](docs/apis_and_subsystems/)**: Native SRE hook reference (34 active hooks), Lua scripting catalog, GUI overlay APIs, VFS virtual filesystem.
* **[Formats & Schemas](docs/formats_and_schemas/)**: PowerVR POD 3D model spec, PVR texture spec, Scene spec, Protobuf wire schema, Save file spec.
* **[Modding & SwKiwi](docs/modding_and_swkiwi/)**: SwKiwi modloader architecture, RLSwordigo 7.0 reversing, modding guides, API audits.
* **[Release Notes](docs/release_notes/)**: Version release notes from v1.0.0 through v8.0 Beta 2.
* **[Misc & Logs](docs/misc/)**: Diagnostic traces, platform compatibility matrices, build guides.

---

## 7. License and Copyright

* **SwordigoDesktop Engine & SRE Codebase**: Released under the [GNU General Public License v2.0 or later](LICENSE).
* **ufbx** (FBX importer, [github.com/bqqqqqq/ufbx](https://github.com/bqqqqqq/ufbx)): MIT-licensed single-file FBX loader, vendored in `src/tools/ufbx/`.
* **Swordigo Game Assets**: Original game assets, binaries, and trademarks remain the intellectual property of Touch Foo / Ville Mäkynen. This compatibility layer requires user-supplied game data files.
