# 📖 Ruby GG & Ruby CLI — User Manual (Public Beta 1)

Welcome to the official User Manual for **Ruby GG** (GUI Studio) and **Ruby CLI** (Headless Automation Suite). This guide covers installation, navigation, 3D asset conversion, level authoring, and CLI commands for both Windows and Linux.

---

## 📑 Table of Contents

1. [System Requirements](#1-system-requirements)
2. [Installation & Getting Started](#2-installation--getting-started)
   - [Windows Setup](#windows-setup)
   - [Linux Setup](#linux-setup)
3. [Ruby GG Studio Interface](#3-ruby-gg-studio-interface)
   - [Asset Browser Panel](#asset-browser-panel)
   - [3D Viewport Controls & Rendering](#3d-viewport-controls--rendering)
   - [Scene Hierarchy & Inspector Panel](#scene-hierarchy--inspector-panel)
   - [Model Converter Dialog](#model-converter-dialog)
   - [Desktop Integration & File Associations](#desktop-integration--file-associations)
4. [Ruby CLI Reference Manual](#4-ruby-cli-reference-manual)
   - [Basic Syntax & Modes](#basic-syntax--modes)
   - [FileRift Transcoding (Markup ⟷ Binary)](#filerift-transcoding-markup--binary)
   - [World Map (.scmap) SDK](#world-map-scmap-sdk)
   - [Scene Creator Tool](#scene-creator-tool)
   - [APK Packaging & Signing](#apk-packaging--signing)
   - [AI Model Context Protocol (MCP) Server](#ai-model-context-protocol-mcp-server)
5. [Keyboard Shortcuts Cheat Sheet](#5-keyboard-shortcuts-cheat-sheet)
6. [Troubleshooting & FAQ](#6-troubleshooting--faq)

---

## 1. System Requirements

| Specification | Windows | Linux |
| :--- | :--- | :--- |
| **Operating System** | Windows 10 (1809+) or Windows 11 (64-bit) | Linux x86_64 (glibc 2.34+, e.g. Ubuntu 22.04+, Fedora 36+, Arch) |
| **Graphics API** | OpenGL 3.3 Core Profile compatible GPU | OpenGL 3.3 Core Profile compatible GPU (Mesa / NVIDIA) |
| **Memory (RAM)** | 4 GB minimum (8 GB recommended for large models) | 4 GB minimum (8 GB recommended) |
| **Disk Space** | ~200 MB free disk space | ~150 MB free disk space |

---

## 2. Installation & Getting Started

Ruby GG is distributed as a **portable, zero-installer** package. It does not tamper with system files or require administrative rights.

### Windows Setup
1. Download **`ruby_gg_win64.zip`** (or **`ruby_gg_win64.7z`**).
2. Extract the archive into any folder of your choice (e.g. `C:\Games\RubyGG` or your Desktop).
3. **To launch the GUI Studio**:
   - Double-click **`run_ruby_gg.bat`** (or `ruby_gg.exe`).
4. **To use the CLI tool**:
   - Open Command Prompt or PowerShell in the extracted directory.
   - Run: `.\ruby_cli.exe --help`.

> [!TIP]
> Keep all `.dll` files and the `platforms/` folder in the same directory as `ruby_gg.exe` so Qt and runtime dependencies load properly.

---

### Linux Setup
1. Download **`ruby_gg_linux64.zip`**.
2. Extract the archive:
   ```bash
   unzip ruby_gg_linux64.zip -d ruby_gg_linux
   cd ruby_gg_linux
   ```
3. Make sure launcher scripts and binaries have execute permissions:
   ```bash
   chmod +x run_ruby_gg.sh run_ruby_cli.sh ruby_gg ruby_cli libs/*.so*
   ```
4. **To launch the GUI Studio**:
   ```bash
   ./run_ruby_gg.sh
   ```
5. **To use the CLI tool**:
   ```bash
   ./run_ruby_cli.sh --help
   ```

---

## 3. Ruby GG Studio Interface

### Asset Browser Panel
The **Asset Browser** is docked on the left side of the studio. It provides an unconstrained, breadcrumb-driven directory browser designed to navigate project trees and standalone asset folders.

- **Navigation Bar**:
  - `↑` (Up): Moves to the parent directory.
  - `←` / `→` (Back / Forward): Navigates browser history.
  - `🏠` (Home): Returns to the project's root asset directory.
  - **Address Bar**: Type or paste any absolute directory path and press `Enter` to jump instantly.
- **Search & Filter**:
  - Type in the filter box to filter items by filename or extension.
- **Asset Actions**:
  - Double-click a `.pod` model to inspect it in the 3D viewport.
  - Double-click a `.pvr` or `.png` texture to preview it in the Texture Viewer.
  - Double-click a `.scene` or `.scl` file to inspect or edit its component hierarchy.

---

### 3D Viewport Controls & Rendering
The 3D Viewport utilizes an OpenGL 3.3 rendering pipeline with real-time vertex skinning, bone visualizations, and PVRTC texture support.

- **Camera Navigation**:
  - **Orbit / Rotate**: Hold **Left Mouse Button (LMB)** and drag in the viewport.
  - **Pan Camera**: Hold **Right Mouse Button (RMB)** (or Middle Mouse) and drag.
  - **Zoom**: Scroll the **Mouse Wheel** up/down.
  - **Reset View**: Press `F` to focus on the selected mesh or center the scene.
- **Display Modes** (Viewport Toolbar):
  - **Textured**: Full material rendering with diffuse textures and specular highlights.
  - **Wireframe**: Displays the underlying polygon topology and tessellation.
  - **Bones / Skeleton**: Visualizes skeletal joint hierarchies and bone chains.
  - **Grid & Gizmos**: Toggles the world ground grid, axis trihedron, and light indicators.

---

### Scene Hierarchy & Inspector Panel
- **Hierarchy Tree**: Displays all nodes, meshes, cameras, lights, and collision bounds present in the currently opened model or scene file.
- **Inspector Panel**: Shows node properties including:
  - Local / World Transform (`Position`, `Rotation`, `Scale`).
  - Material parameters (ambient, diffuse, specular, texture assignment).
  - Vertex and polygon statistics.
  - Attached Lua scripts and game components.

---

### Model Converter Dialog
Accessed via **`Tools ▸ Convert Model...`** (or Ctrl+M):

1. **Input File**: Select any modern 3D file (`.glb`, `.gltf`, `.fbx`, `.obj`).
2. **Output Path**: Choose the destination `.pod` filename.
3. **Conversion Settings**:
   - **Scale Factor**: Adjust mesh unit scale (default `1.0` or `0.01` for DCC exports).
   - **Coordinate Transform**: Toggle Z-up vs Y-up conversion.
   - **Animation Export**: Extracts baked skeletal animation clips into Swordigo animation descriptors.
   - **Skinning Weights**: Automatically re-normalizes vertex influence weights to match mobile PowerVR vertex shader constraints (max 4 weights per vertex).
4. Click **Convert** to generate the native `.pod` file.

---

### Desktop Integration & File Associations
Accessed via **`Tools ▸ Desktop Integration & File Associations...`**:

Connects Ruby GG to your operating system so double-clicking `.pod`, `.pvr`, `.scene`, `.scl`, or `.glb` files anywhere on your computer immediately opens them in Ruby GG.

- **Linux**: Creates `.desktop` launcher and updates user MIME databases (`~/.local/share/mime`).
- **Windows**: Registers `HKCU\Software\Classes` ProgID associations and notifies the Windows Shell.
- Choose which formats you want associated and click **✦ Register Selected Formats**.

---

## 4. Ruby CLI Reference Manual

`ruby_cli` is a fast, native command-line utility for batch conversion, inspection, world map analysis, and AI agent integration.

### Basic Syntax & Modes

```bash
ruby_cli [MODE] [OPTIONS] [FILES...]
```

General Options:
- `-o, --output <PATH>`: Destination file or output directory.
- `-w, --working-dir <DIR>`: Set working directory (defaults to current dir).
- `-n, --no-colour`: Disable ANSI terminal colors.

---

### FileRift Transcoding (Markup ⟷ Binary)

Ruby CLI allows lossless conversion between binary Swordigo assets (`.scene`, `.scl`, `.gdata`, `.gopt`, `.gplayer`, `.gstate`, `.scmap`, `.sounds`, `.fnt`, `.atlas`, `.fr`) and human-readable FileRift text markup.

- **Decompile binary to text markup**:
  ```bash
  ruby_cli decode level1.scene -o level1.txt
  ```
- **Recompile text markup back to binary**:
  ```bash
  ruby_cli recode level1.txt -o level1.scene
  ```
- **Round-trip verify**:
  ```bash
  ruby_cli both level1.scene
  ```

---

### World Map (.scmap) SDK

Tools for inspecting and validating `.scmap` navigation graphs:

- **Map Summary**:
  ```bash
  ruby_cli map summary world.scmap
  ```
  *Prints total zones, nodes, portals, and per-zone linkage breakdown.*

- **Validate Map Integrity**:
  ```bash
  ruby_cli map validate world.scmap
  ```
  *Checks for orphan nodes, duplicate portal IDs, and broken transition references.*

- **Find Path Between Nodes**:
  ```bash
  ruby_cli map path world.scmap "Town_Center" "Forest_Dungeon"
  ```
  *Executes breadth-first search (BFS) to compute the shortest game transition path.*

---

### Scene Creator Tool

Generate boilerplate scene files with level geometry, background references, and collision hulls:

```bash
ruby_cli scene create new_level.scene \
    --level "Dungeon_Room_01" \
    --mesh "models/dungeon_block.pod" \
    --background "textures/cave_bg.pvr" \
    --width 2000 --height 1200 --depth 400
```

---

### APK Packaging & Signing

Helper commands for unpacking, building, and signing modded Android APKs:

- **Extract APK Assets**:
  ```bash
  ruby_cli apk extract base.apk ./extracted_game/
  ```
- **Sign Built APK**:
  ```bash
  ruby_cli apk sign modded.apk --apksigner /path/to/apksigner.jar
  ```

---

### AI Model Context Protocol (MCP) Server

Ruby CLI includes a native Model Context Protocol (MCP) server, allowing AI coding assistants (such as Claude Code, Cursor, or ChatGPT) to programmatically inspect, query, and edit Swordigo assets:

- **Standard I/O mode (for CLI agents & Cursor)**:
  ```bash
  ruby_cli mcp /path/to/assets
  ```
- **HTTP Server mode (for ChatGPT / web integrations)**:
  ```bash
  ruby_cli mcp-http 8765 /path/to/assets
  ```
  *Starts a JSON-RPC 2.0 endpoint listening on `http://127.0.0.1:8765/mcp` without external dependencies.*

---

## 5. Keyboard Shortcuts Cheat Sheet

| Action | Shortcut |
| :--- | :--- |
| **Open File** | `Ctrl + O` |
| **Save Active File** | `Ctrl + S` |
| **Model Conversion Dialog** | `Ctrl + M` |
| **Focus Camera on Selection** | `F` |
| **Toggle Wireframe Mode** | `W` |
| **Toggle Grid Visibility** | `G` |
| **Navigate Directory Up** | `Alt + Up` |
| **Navigate History Back** | `Alt + Left` |
| **Navigate History Forward** | `Alt + Right` |
| **Close Tab** | `Ctrl + W` |
| **Desktop Integration Dialog** | `Ctrl + Shift + I` |

---

## 6. Troubleshooting & FAQ

#### Q: On Windows, I get an error: *"This application failed to start because no Qt platform plugin could be initialized"*.
> **Fix**: Make sure you extract the entire zip archive together. Do not move `ruby_gg.exe` out of its folder away from the `platforms/` subfolder (which contains `qwindows.dll`) and `qt.conf`.

#### Q: On Linux, running `./ruby_gg` says *"error while loading shared libraries: libswcore.so: cannot open shared object file"*.
> **Fix**: Launch using the provided script `./run_ruby_gg.sh`. It automatically sets `LD_LIBRARY_PATH` to include the bundled `libs/` folder.

#### Q: A 3D model looks transparent or has inverted lighting.
> **Fix**: Open the **Model Converter (`Ctrl+M`)**, enable **Recalculate Normals** and ensure **Flip Z** is checked if your DCC software exported in a right-handed coordinate system.

#### Q: How do I open files with spaces in the path via CLI?
> **Fix**: Enclose the path in quotation marks:
> `ruby_cli decode "My Mod/assets/level 1.scene"`

---

*Ruby Studio GG & Ruby CLI are developed by TheAevoraLabs and the SRE / Caver Engine open-source community.*
