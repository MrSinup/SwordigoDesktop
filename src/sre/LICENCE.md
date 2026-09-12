# Swordigo Runtime Environment (SRE) — Software License

**Copyright © 2026 OpenSwordigo Org & Lawncher Team. All Rights Reserved.**

---

### Copyright Holders & Authorship Attribution
The entirety of the **Swordigo Runtime Environment (SRE)** is proprietary software owned and held under All Rights Reserved (ARR) by:

- **OpenSwordigo Org**:
  - `QuantumCreeper`
  - `Msinup`
  - `ManoK`
- **Lawncher Team**:
  - `Raijin`
  - `Kiziyon`

---

### Scope of this License
This license applies to all modules, components, headers, source files, and assembly routines contained within `src/sre/`, specifically including without limitation:

1. **`src/sre/sre13/`**:
   - The Swordigo 1.4.13 guest runtime environment.
   - Caver architecture hooks (`Camera`, `CameraController`, `CaverShell`, `Component`, `CharControllerComponent`, `EntityComponent`, `GameSceneController`, `GameState`, `GameViewController`, `ModelLibrary`, `PhysicsObjectState`, `PlayerProfile`, `ProgramState`, `RenderingContext`, `Scene`, `SceneObject`, `TextureLibrary`).
   - `rbmath` raymath Lua extensions and C bindings.
   - SRE13 console, audio subsystem, recovery engine, safety hooks, and scene shifter.
   - In-game touch UI and button controllers.

2. **`src/sre/sre12/`**:
   - The Swordigo 1.4.12 guest runtime environment (`sre_init`, `sre_lua`, `sre_mini_api`, `sre_scene_update`, `sre_gui_native`, `sre_frame_loop`, `sre_vfs`, etc.).

3. **`src/sre/extras/`**:
   - Closed-source and extended SRE capabilities (`sre_extras`, `sre_ffi`, `mod_fs`, `mod_saves`, `memory`, `raijin_ffi`).

4. **`src/sre/base/`**:
   - SRE base engine plumbing, custom ABI interfaces, and platform runtime glue (`sre_base.c`, `sre_setjmp.S`, `sre_host_abi.h`).

---

### Terms & Conditions (All Rights Reserved)
1. **Proprietary & Confidential**: The software is proprietary and subject to strict copyright protection.
2. **No Unauthorized Redistribution**: Redistribution, sublicensing, publication, mirroring, or public hosting of any source code or compiled binaries belonging to SRE, in whole or in part, in original or modified form, is strictly prohibited without explicit, prior written consent from both **OpenSwordigo Org** and **Lawncher Team**.
3. **No Reverse Engineering**: Decompilation, disassembly, or reverse engineering of any proprietary portions of SRE beyond fair-use interoperability is strictly prohibited.
4. **Third-Party Vendored Components**:
   - Upstream vendored components within `src/sre/base/` (such as the standard Lua 5.1 engine, LuaSocket, LuaFileSystem, toml-c, and RakNet) retain their original upstream licenses (e.g. MIT, BSD, zlib).
   - All custom patches, wrappers, glue code, ABI integrations, and Swordigo-specific modifications applied to these libraries are Copyright © OpenSwordigo Org and Lawncher Team under All Rights Reserved terms.

---

### Inquiries & Licensing Permissions
For licensing inquiries, permissions, or collaborative usage, please contact **OpenSwordigo Org** or **Lawncher Team**.
