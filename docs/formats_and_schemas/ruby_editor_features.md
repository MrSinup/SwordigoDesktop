# Ruby Editor Feature Extraction — Complete Technical Documentation

> **Research Stage Only** — No implementation changes proposed.  
> Based on exhaustive analysis of `src/tools/` Ruby/C++ editor codebase.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Scene Graph & Object Management](#2-scene-graph--object-management)
3. [Scene Player & Game Simulation](#3-scene-player--game-simulation)
4. [Lua Scripting Engine](#4-lua-scripting-engine)
5. [Entity & AI System](#5-entity--ai-system)
6. [Collision & Physics](#6-collision--physics)
7. [Asset & File Management](#7-asset--file-management)
8. [Editor UI & Viewport](#8-editor-ui--viewport)
9. [MCP Server](#9-mcp-server)
10. [CLI & Batch Processing](#10-cli--batch-processing)
11. [Scene Generation](#11-scene-generation)
12. [File Format Support](#12-file-format-support)

---

## 1. Overview

The Ruby editor is a **custom C++17** desktop application for editing Swordigo game assets. It provides:

- Proprietary rendering pipeline (`av_renderer`)
- Custom scene graph with protobuf-based serialization
- Game-accurate playback simulation
- Full Lua scripting integration
- Complete decode/recode pipeline for binary game files
- MCP server for AI agent integration
- Native CLI for headless batch processing

---

## 2. Scene Graph & Object Management

### 2.1 SceneData Structure

The `SceneData` struct (`scene_loader.h`) is the root container:
- `objects` — vector of `SceneObject`
- `groups` — scene grouping
- `templates` — reusable object templates
- `libraries` — external script libraries
- `terrain` — terrain data
- `game` — game state data

### 2.2 SceneObject Structure

Each `SceneObject` (`scene_loader.h:46-100`) contains:

```cpp
struct SceneObject {
    std::string template_name;   // Template identifier
    std::string name;            // Object name
    std::string identifier;      // Unique identifier (Lua self:identifier())
    
    // Transform (note: rot_y is actually Z-rotation)
    float pos_x = 0.0f, pos_y = 0.0f, pos_z = 0.0f;
    float rot_y = 0.0f;          // Tag 6: Rotation (actually Z-axis)
    float scale_x = 1.0f, scale_y = 1.0f, scale_z = 1.0f;
    float template_scaling = 1.0f;
    
    // Model Y-rotation (compensation for rot_y being Z-rotation)
    float model_y_rotation = 0.0f;  // From ModelComponent payload field 2
    bool has_model_y_rotation = false;
    
    // Components
    std::vector<SceneComponent> components;
    
    // Special flags
    bool is_spawn_point = false;
    bool is_ground = false;
};
```

### 2.3 SceneComponent Types

Components are identified by protobuf tag number (field = tag >> 3):

| Component | Tag | Field | Description |
|-----------|-----|-------|-------------|
| MeshComponent | 905 | 113 | 3D mesh geometry |
| ModelComponent | 913 | 114 | Model with animation |
| LightComponent | 921 | 115 | Light source |
| CameraComponent | 929 | 116 | Camera |
| AudioComponent | 937 | 117 | Audio source |
| ParticleComponent | 945 | 118 | Particle emitter |
| SpriteComponent | 953 | 119 | 2D sprite |
| EntityComponent | 1218 | 152 | Entity AI |
| EntityControllerComponent | 1290 | 161 | Entity controller |
| HeroEntityComponent | 1322 | 165 | Player character |
| MonsterEntityComponent | 1266 | 158 | Enemy AI |
| ShapeComponent | 962 | 120 | Collision shape |
| CollisionShapeComponent | 970 | 121 | Collision geometry |
| BoneControlledCollisionShapeComponent | 994 | 124 | Skeletal collision |
| GroundPolygonComponent | 882 | 110 | Ground polygon |
| PhysicsComponent | - | - | Physics properties |
| TerrainComponent | - | - | Terrain data |

### 2.4 Scene Creation

In `scene_creator.cpp`:
- `create_scene_object(template_name)` — Creates a new object from a template
- Default rotation: `rot_y = 0.0f`
- Default scale: `scale_x = scale_y = scale_z = 1.0f`
- Default position: `pos_x = pos_y = pos_z = 0.0f`

### 2.5 Scene Loading & Saving

In `scene_loader.cpp`:
- Protobuf-based binary serialization
- Tag 6: `rot_y` (rotation)
- Tag 12: `pos_x`, `pos_y`, `pos_z` (position)
- Tag 16: `scale_x`, `scale_y`, `scale_z` (scale)
- ModelComponent payload field 2: `model_y_rotation`
- Template field 2: `template_scaling`
- Component payload: nested messages with specific field numbers

---

## 3. Scene Player & Game Simulation

### 3.1 Playback Modes

The `scene_player` module (`scene_player.h`) provides three modes:

| Mode | Enum Value | Description |
|------|-----------|-------------|
| Off | `Mode::Off` | Static editor view |
| Visualise | `Mode::Visualise` | Play scene AI/logic/animations (no hero) |
| PlayHiro | `Mode::PlayHiro` | Spawn Hiro at spawn point and play |

### 3.2 PlayObject Structure

The `PlayObject` struct (`scene_player.h:58-80`) holds runtime state:

```cpp
struct PlayObject {
    int    index = -1;
    float  pos[3] = {0, 0, 0};
    float  home[3] = {0, 0, 0};
    float  rot = 0.0f;           // Current rotation (radians; 0 = +X, pi = -X)
    float  frame = 0.0f;         // Current POD animation frame
    bool   moving = false;
    float  phase = 0.0f;
    AiKind kind = AiKind::None;
    float  patrol_range = 80.0f;
    float  detect_range = 320.0f;
    float  chase_speed  = 60.0f;
    float  dir = 1.0f;           // Current patrol direction
    bool   chasing = false;
    float  attack_cd = 0.0f;
    float  vel_y = 0.0f;
    
    // SCL Lua AI runtime state
    bool   lua_driven = false;
    std::string name;
    float  vel[3] = {0.0f, 0.0f, 0.0f};
    float  gravity[3] = {0.0f, -1472.0f, 0.0f};
    bool   physics_enabled = true;
};
```

### 3.3 Animation System

- Per-object POD animation clocks
- Frame-based animation with frame interpolation
- `pod_loader.cpp` provides `slerp_quat` for bone animation
- Animation blending via frame weights
- Animation switching based on AI state (moving, attacking, idle)

### 3.4 Camera Controller

The scene player provides a game-style camera controller:
- Follows Hiro (player character) in PlayHiro mode
- Camera mimics the actual game camera
- Overlay state exposed to the visualizer

### 3.5 Game State

The `scene_game.h` module provides:
- XP, levels, items, coins
- Enemy spawn management
- Game state persistence
- Player inventory tracking

---

## 4. Lua Scripting Engine

### 4.1 SCL Lua Integration

The `scene_lua` module (`scene_lua.cpp`) provides full SCL Lua AI hosting:

**Lua API**:
- `Scene.CreateObject(template_name)` — Create object at runtime
- `SetVelocity(x, y, z)` — Set object velocity
- `SetAcceleration(x, y, z)` — Set acceleration
- `SetMoveAnimation(name)` — Set movement animation
- `BlendToAnimation(name, duration)` — Blend to animation
- `SetRunning(enabled)` — Enable/disable running
- `self:identifier()` — Get object identifier
- `self:onload(handler)` — Register onload handler
- `self:onkill(handler)` — Register onkill handler
- `self:onhurt(handler)` — Register onhurt handler

**Lua Runtime State**:
- `so.rot_y = po.rot` — Write rotation back to scene object (line 1178)
- `so.pos_x = po.pos[0]` — Write position back
- `so.pos_y = po.pos[1]` — Write position back
- `so.pos_z = po.pos[2]` — Write position back
- `so.frame = po.frame` — Write animation frame

### 4.2 Lua Behavior Patterns

Lua scripts define entity behavior:
- **Follow**: Object follows another entity
- **Patrol**: Object patrols between waypoints
- **Freeze**: Object freezes in place
- **Attack**: Object attacks when in range
- **Custom**: User-defined behavior

---

## 5. Entity & AI System

### 5.1 Entity Data Structures

The `scene_entity.h` module provides entity component data:

```cpp
struct EntityComponentData {
    int  facing_direction  = 1;    // 1=right, -1=left
    bool physics_enabled   = true;
};

struct EntityControllerData {
    std::string entity_id;
    std::string animation_controller;
};

struct HeroEntityData {
    bool present = false;
};

struct MonsterEntityData {
    bool present = false;
    // OnKill (Program), OnHurt (Program), GivesExperience, DefaultDeathAnimation
};
```

### 5.2 AI Archetypes

The `scene_player.h:47-55` defines enemy AI archetypes:

| Archetype | Behavior |
|-----------|----------|
| `Walker` | Patrols X between home ±range, turns at edges |
| `Bat` | Hovers/bobs around home, chases hero when close |
| `Charger` | Idles until hero in range, then charges |
| `Bouncer` | Bounces vertically around home |
| `Static` | Stays put, faces hero when in range |
| `Archer` | Stays put, faces + aims at hero |

### 5.3 Entity Manager

In `scene_entity.cpp`:
- Parses all entity components from SceneObject components
- Creates entity controllers for each entity
- Sets `e.rot = (e.dir < 0.0f) ? kPi : 0.0f` — **the rotation constraint bug**
- Manages entity AI state machines

---

## 6. Collision & Physics

### 6.1 Collision Shape Types

In `scene_collision.cpp`:

| Shape | Tag | Field | Description |
|-------|-----|-------|-------------|
| Rectangle | 962 | 120 | Rectangular collider |
| Circle | 970 | 121 | Circular collider |
| Polygon | 882 | 110 | Polygonal collider |
| BoneControlled | 994 | 124 | Skeletal animation collision |

### 6.2 Collision Properties

Each collision shape has:
- `IsGround` — Whether it's ground
- `Collides` — Whether it collides
- `ReceivesDamage` — Whether it takes damage
- `InflictsDamage` — Whether it deals damage
- `MinDepth`, `MaxDepth` — Collision depth range
- `SpecialType` — Special collision type
- `Enabled` — Whether collision is active
- `Friction` — Friction coefficient
- `UnsafeGround` — Unsafe ground flag

### 6.3 Collision Detection

In `scene_collision.cpp`:
- `std::cos(o.rot_y)` and `std::sin(o.rot_y)` for direction calculation
- Rectangle vs circle collision
- Polygon collision via point-in-polygon tests
- Ground polygon collision for terrain
- Bone-controlled collision shape updates

### 6.4 Physics Simulation

In `scene_player.cpp`:
- Gravity: `gravity[3] = {0.0f, -1472.0f, 0.0f}` (Swordigo gravity constant)
- Velocity-based movement with collision response
- Ground detection via collision shapes
- Player physics (Hiro): A/D movement + Space (jump)

---

## 7. Asset & File Management

### 7.1 File Format Support

The Ruby editor supports **10+ Swordigo-specific file formats**:

| Format | Description | Loader |
|--------|-------------|--------|
| `.scene` | Scene files (protobuf) | `scene_loader` |
| `.scl` | Script libraries (protobuf) | `scene_lua` |
| `.gdata` | Game data files | `filerift` |
| `.gopt` | Game optimization data | `filerift` |
| `.gplayer` | Player game state | `filerift` |
| `.gstate` | Game state files | `filerift` |
| `.scmap` | World map files | `filerift` |
| `.fnt` | Font files | `filerift` |
| `.atlas` | Texture atlases | `filerift` |
| `.sounds` | Sound files | `filerift` |
| `.fr` | FileRift archive | `filerift` |
| `.pod` | PowerVR 3D models | `pod_loader` |
| `.pvr`, `.tex` | Compressed textures | `pvr_loader` |
| `.apk` | Android packages | `ruby_cli` |
| `.glb`, `.gltf` | 3D model formats | `gltf_glb` |
| `.obj` | 3D model format | `obj_loader` |
| `.ani` | Animation files | `ani_loader` |
| `.scn` | Scene format | `scn_loader` |

### 7.2 Decode/Recode Pipeline

The `filerift` library provides lossless binary ↔ markup conversion:
- **Decode**: Binary → Human-readable markup
- **Recode**: Markup → Binary
- **Both**: Decode then recode (round-trip)
- **Batch**: Headless batch conversion
- **Recursive**: Directory-wide processing
- **APK Pipeline**: Base + Add → Recode → Sign

### 7.3 POD Model Loading

`pod_loader.cpp` provides:
- `.pod` format parsing (PowerVR)
- Frame data with `slerp_quat` for bone animation
- Mesh data: positions, normals, UVs, indices
- Material properties
- Animation frame extraction

### 7.4 Texture Loading

`pvr_loader` provides:
- `.pvr` compressed texture format
- `.tex` texture format
- Mipmap support
- Alpha channel handling

---

## 8. Editor UI & Viewport

### 8.1 Asset Viewer (`asset_viewer.cpp`)

The main editor UI provides:

**Viewport Features**:
- 3D POD model viewport with orbit camera
- Wireframe mode toggle
- Texturing mode toggle
- Interactive lighting adjustments (elevation, azimuth, light & ambient colors)
- Checkerboard background for texture preview
- PVR/PNG texture preview with zoom/pan
- Audio WAV playback with waveform visualization
- Scene file inspection with object tree and component details
- FontAwesome 6/7 solid icons integration
- Blender-like flat neutral dark theme

**Rotation UI** (lines 4180-4208):
- `DragFloat("Rotation", &rot_deg, 1.0f)` — Rotation in degrees
- Converts to radians: `rot_rad = rot_deg * π / 180.0f`
- Clamps to [-π, π]
- Sets `obj.rot_y = rot_rad`

**Texture UV Flip** (lines 1816):
- UV flip handling with `1.0 - v_texcoord` in shader
- Double V-inversion potential bug

### 8.2 Scene Workspace (`scene_workspace.h/cpp`)

The scene workspace provides:
- **Matrix construction**: `object_world_matrix`, `object_render_matrix`
- **Collision picking**: `pick_scene_object`, `pick_scene_ground_mesh`
- **Normal computation**: `compute_smooth_normals`, `recompute_ground_mesh_geometry`
- **Geometry processing**: `ground_mesh_delete_vertex`, `ground_mesh_delete_triangle`, `ground_mesh_subdivide_triangle`, `ground_mesh_split_edge`

### 8.3 Scene Player UI (`scene_player.h`)

ImGui overlay panel:
- Play mode selection (Off/Visualise/PlayHiro)
- AI debug info
- Animation frame display
- Velocity display
- Camera state

---

## 9. MCP Server

The `ruby_mcp` module exposes **19+ tools** via JSON-RPC 2.0 over stdio:

### 9.1 Scene Tools

| Tool | Description |
|------|-------------|
| `scl_decode` | Decode .scl script to markup/Lua |
| `scene_decode` | Decode .scene file to FileRift markup |
| `scene_objects` | Structured object/component/entity dump |
| `scene_summary` | Counts + bounds + libraries + waters/lights |
| `scene_programs` | Onload Lua programs attached to objects |
| `scene_templates` | Add-object palette |
| `scene_libraries` | External .scl libraries |

### 9.2 Search Tools

| Tool | Description |
|------|-------------|
| `search` | String search across a folder (decoded-aware) |
| `search_scl` | String search inside decoded .scl |

### 9.3 File Info Tools

| Tool | Description |
|------|-------------|
| `file_info` | Size + detected type + quick details |
| `pod_info` | Structural summary of .POD model |
| `pod_blocks` | POD block-id histogram |
| `texture_info` | Dimensions/format of .pvr/.tex/.png |
| `read_file` | Raw file view (text, offset, hex dump) |
| `list_dir` | Directory listing with sizes/types |
| `find_files` | Recursive glob/substring search |
| `list_files` | Enumerate assets under directory |

### 9.4 Server Modes

- `bin/ruby --mcp-server [--mcp-root DIR]` — Headless stdio server
- `bin/ruby_cli mcp [DIR]` — Same server, client-friendly
- In-app: Ruby menu → Help → MCP Console — Interactive JSON-RPC tester

---

## 10. CLI & Batch Processing

### 10.1 Ruby CLI (`ruby_cli.h`)

The native CLI tool (`ruby_cli`) provides:

| Mode | Description |
|------|-------------|
| `DECODE` | Binary → markup |
| `RECODE` | Markup → binary |
| `BOTH` | Recode then decode |
| `USER` | Decode user folder |
| `FORCE` | Recode with force flag |
| `BUILD` | Build APK from .frproject |
| `PASS` | Nothing to do |

### 10.2 APK Tools

- **APK Extractor**: Unpack full APK to directory
- **APK Signer**: Sign with Android apksigner.jar
- **APK Builder**: Build from .frproject (base + add + recode + sign)

### 10.3 Batch Converter

- Headless batch texture conversion
- PVR/TEX → PNG export
- PNG → PVR/TEX import
- Drives `batch::run_batch_headless()`

---

## 11. Scene Generation

### 11.1 Scene Generator (`scene_generator.cpp`)

Procedural scene generation with:
- Tree placement with random rotation (`rot_y = -1.5707963f` for 45% of trees)
- Rock placement (no randomization, `rot_y = 0.0f`)
- Grass placement (no randomization, `rot_y = 0.0f`)
- Random density and spread
- Terrain-aware placement

### 11.2 Scene Generator V2/V3

Additional generation modules with updated logic and improved algorithms.

### 11.3 Decoration Randomization

In `scene_generator.cpp:1624`:
```cpp
d.rot_y = (opt.randomize_deco_rotation && rng_float(drng,0,1.0f) < 0.45f) ? -1.5707963f : 0.0f;
```

45% chance of -90° Z-rotation for trees. Asymmetric treatment: trees get random rotation, rocks/grass don't.

---

## 12. File Format Support

### 12.1 Scene File Format (.scene)

Protobuf-based binary format:
- Root message: `SceneData`
- Objects: repeated `SceneObject` messages
- Components: nested messages with specific field numbers
- Tags:
  - Tag 6: `rot_y` (rotation, Z-axis)
  - Tag 12: Position (pos_x, pos_y, pos_z)
  - Tag 16: Scale (scale_x, scale_y, scale_z)
  - ModelComponent payload field 2: `model_y_rotation`
  - Template field 2: `template_scaling`

### 12.2 SCL Script Library (.scl)

Protobuf-based Lua script libraries:
- Onload/onkill/onhurt programs
- Function definitions
- API bindings
- External library references

### 12.3 FileRift Format (.fr)

Archive format for game assets:
- Directory-based structure
- Lossless binary ↔ markup conversion
- APK build pipeline support

### 12.4 POD Model Format (.pod)

PowerVR 3D model format:
- Mesh data with positions, normals, UVs
- Bone animation with `slerp_quat`
- Frame-based animation
- Material properties

---

## Appendix A: Ruby Editor Module Dependency Graph

```
ruby_mcp.cpp
    ├── filerift (decode/recode)
    ├── scene_loader (scene file parsing)
    ├── pod_loader (POD model loading)
    ├── pvr_loader (texture loading)
    └── scene_lua (Lua scripting)

ruby_cli.cpp
    ├── filerift (decode/recode)
    ├── batch_converter (texture conversion)
    └── apk_tools (APK build pipeline)

asset_viewer.cpp
    ├── av_renderer (GPU rendering)
    ├── scene_loader (scene file parsing)
    ├── scene_creator (object creation)
    ├── scene_generator (procedural generation)
    ├── scene_player (game simulation)
    ├── scene_entity (entity data)
    ├── scene_collision (collision parsing)
    ├── scene_lua (Lua scripting)
    ├── pod_loader (POD model loading)
    ├── pvr_loader (texture loading)
    ├── gltf_glb (GLTF model loading)
    ├── obj_loader (OBJ model loading)
    ├── ani_loader (Animation loading)
    ├── scn_loader (Scene loading)
    ├── av_audio (Audio playback)
    └── ImGui (UI framework)

scene_player.cpp
    ├── scene_loader
    ├── scene_lua
    ├── scene_terrain
    ├── scene_game
    ├── scene_entity
    ├── scene_collision
    └── pod_loader

scene_entity.cpp
    ├── scene_loader
    └── scene_physics

scene_workspace.cpp
    └── scene_loader

scene_collision.cpp
    └── scene_loader

scene_generator.cpp
    └── scene_loader

scene_lua.cpp
    └── scene_loader

scene_creator.cpp
    └── scene_loader
```

---

## Appendix B: Key Source Files

| File | Lines | Purpose |
|------|-------|---------|
| `scene_loader.h` | ~100 | SceneObject, SceneData structs |
| `scene_loader.cpp` | ~500 | Scene file parsing (protobuf) |
| `scene_player.h` | ~259 | PlayObject, AiKind, playback modes |
| `scene_player.cpp` | ~1121 | Game simulation engine |
| `scene_entity.h` | ~241 | Entity component data structures |
| `scene_entity.cpp` | ~600+ | Entity AI management |
| `scene_workspace.h` | ~80 | Matrix construction, picking |
| `scene_workspace.cpp` | ~400+ | World matrix, collision, normals |
| `scene_collision.cpp` | ~943 | Collision shape parsing & detection |
| `scene_lua.cpp` | ~1200+ | Lua scripting engine |
| `scene_generator.cpp` | ~1700+ | Procedural scene generation |
| `scene_generator_v2.cpp` | ~? | Updated generation logic |
| `scene_generator_v3.cpp` | ~? | Further updated generation |
| `scene_creator.cpp` | ~? | Scene object creation |
| `scene_game.h` | ~? | Game state management |
| `scene_terrain.h` | ~? | Terrain data |
| `asset_viewer.cpp` | ~17714 | Main editor UI |
| `av_renderer.cpp` | ~? | Custom GPU renderer |
| `av_audio.cpp` | ~? | Audio playback |
| `ruby_mcp.cpp` | ~? | MCP server |
| `ruby_mcp.h` | ~71 | MCP server header |
| `ruby_cli.h` | ~110 | CLI header |
| `pod_loader.h/cpp` | ~? | POD model loading |
| `pvr_loader.h/cpp` | ~? | PVR texture loading |
| `filerift` | ~? | Binary ↔ markup pipeline |

---

*Documentation generated from exhaustive analysis of OpenSwordigo src/tools/ codebase.*
