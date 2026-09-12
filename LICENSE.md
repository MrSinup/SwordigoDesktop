# SwordigoDesktop Multi-License Notice

**AevoraLabs (prev OpenSwordigo) Project Licensing Framework**

> **Translations**: [हिन्दी (Hindi)](LICENSE_hi.md) | [Français (French)](LICENSE_fr.md) | [简体中文 (Chinese)](LICENSE_cn.md)

This repository is a composite project consisting of components licensed under three distinct terms:
1. **GNU General Public License v3.0 (GPLv3)** — Swordigo-specific tools, game frontend, and editors.
2. **MIT License** — Generic host infrastructure, Android emulation layers, and JNI bridges.
3. **All Rights Reserved (ARR)** — Proprietary Swordigo Runtime Environment (SRE).

Except where specifically co-attributed for SRE, all original works across this repository are **exclusively licensed to AevoraLabs (prev OpenSwordigo)** (`QuantumCreeper`, `Msinup`, `ManoK`).

---

## 1. GNU General Public License v3.0 (GPLv3)
### Swordigo-Specific Game & Editor Components

The following subsystems and directories are licensed under the terms of the **GNU General Public License, Version 3 (GPLv3)**:

- **Ruby & Ruby GG IDE Suite** (`src/ruby/`):
  - Qt6 Studio Editor, `Graphy` visual node editor, viewport shaders, lighting and post-processing pipeline, caver visual engine, and integrated tools.
- **Swordfare Launcher & Game Overlay** (`src/launcher/`, `src/platform/`):
  - In-game HUD overlay, mod manager, save editor UI, profile manager, video background player, and runtime frontend.
- **Swordigo Tooling & Converters** (`src/tools/`, `tools/`):
  - SCL/Scene to graph converters, boulder terrain generator, rubymesh formats, glTF bridge, and asset compilers.

**Copyright © 2026 AevoraLabs. All Rights Reserved.**
Licensed under GPLv3. See [`src/ruby/LICENSE.md`](src/ruby/LICENSE.md) and [`src/platform/LICENSE.md`](src/platform/LICENSE.md).

---

## 2. MIT License
### General Host Infrastructure, Android & JNI Bridges

The generic host environment, portable runtime shims, and low-level emulation plumbing that do not contain game-specific logic are licensed under the permissive **MIT License**:

- **General Android Emulation & JNI Bridges** (`src/jni/`, `src/android/`):
  - POSIX Android shim layers, asset managers, loggers, and JNI marshalling bridges.
- **Binary ELF Loader & Architecture Support** (`src/loader/`, `src/srehost/`):
  - Dynamic ELF loader, symbol relocation tables, and guest-host ABI boundary glue.
- **Generic Engine Platform Helpers** (portions of `src/platform/`):
  - Generic windowing wrappers, timer abstractions, and PVRTC/ASTC image decoders.

**Copyright © 2026 AevoraLabs.**
*(Portions Copyright © 2023 Rinnegatamante — Swordigo Vita Port; Imagination Technologies Ltd. — PVR SDK)*

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

---

## 3. All Rights Reserved (ARR)
### Swordigo Runtime Environment (SRE)

The entire **Swordigo Runtime Environment (SRE)** contained within `src/sre/` is strictly proprietary software held under **All Rights Reserved (ARR)**:

- **`src/sre/sre13/`**: Swordigo 1.4.13 guest runtime, Caver architecture hooks, rbmath Lua math library, and console/audio subsystems.
- **`src/sre/sre12/`**: Swordigo 1.4.12 guest runtime, core hooks, and mini API.
- **`src/sre/extras/`**: Closed-source SRE extensions, FFI interfaces, memory patches, and save file systems.
- **`src/sre/base/`**: SRE base engine plumbing and custom runtime ABI glue.

### Joint Rights Ownership:
All rights, titles, and intellectual property over SRE are jointly owned and held exclusively by:
- **AevoraLabs (prev OpenSwordigo)**: `QuantumCreeper`, `Msinup`, `ManoK`
- **Lawncher Team**: `Raijin`, `Kiziyon`

**No unauthorized redistribution, modification, sublicensing, decompilation, or public mirroring is permitted without express prior written authorization.**
See [`src/sre/LICENSE.md`](src/sre/LICENSE.md) for full terms.
*(Third-party vendored dependencies within `src/sre/base/`—such as upstream Lua 5.1, LuaSocket, LuaFileSystem, toml-c, and RakNet—retain their original open-source licenses).*

---

## Summary Matrix

| Directory / Component | License | Exclusivity / Copyright Holders |
| :--- | :--- | :--- |
| `src/sre/` (`sre12`, `sre13`, `extras`, `base`) | **All Rights Reserved (ARR)** | **AevoraLabs (prev OpenSwordigo)** (`QuantumCreeper`, `Msinup`, `ManoK`) & **Lawncher Team** (`Raijin`, `Kiziyon`) |
| `src/ruby/` (Ruby, Ruby GG Studio IDE) | **GNU GPLv3** | Exclusively **AevoraLabs** |
| `src/launcher/`, `src/platform/` (Swordfare UI) | **GNU GPLv3** | Exclusively **AevoraLabs** |
| `src/tools/`, `tools/` (Converters & Compilers) | **GNU GPLv3** | Exclusively **AevoraLabs** |
| `src/jni/`, `src/android/` (JNI Bridges & Shims) | **MIT** | Exclusively **AevoraLabs** |
| `src/loader/`, `src/srehost/` (Host ELF Loader) | **MIT** | Exclusively **AevoraLabs** |
| Generic Platform Decoders (ASTC, PVRTC) | **MIT** | Upstream authors & **AevoraLabs** |

---

## 4. Community Agreements & Governance Policies

All contributions and usage of online project infrastructure are subject to the following companion agreements:
- **Contributor License Agreement**: See [`.github/CLA.md`](.github/CLA.md) for contribution terms and 50/50 copyright retention rules.
- **Project Governance**: See [`.github/GOVERNANCE.md`](.github/GOVERNANCE.md) for project stewardship and decision-making authority.
- **Terms of Use**: See [`.github/TERMS_OF_USE.md`](.github/TERMS_OF_USE.md) for Online Mod Store and network infrastructure terms of use.
- **Code of Conduct**: See [`.github/CODE_OF_CONDUCT.md`](.github/CODE_OF_CONDUCT.md) for community standards.
