# 05 — Modding API (FileRift)

> FileRift is Swordigo Desktop's protobuf encode/decode library. It converts
> binary game data (scenes, SCLs, save files, UI atlases, world maps) to
> human-readable markup and back — **byte-exact** round-trip. This is the
> foundation for all modding.

---

## 1. FileRift Overview

```
Binary (.scene, .scl, .gdata, etc.)
    │
    ├── decode_protobuf() ──→ Human-readable markup (text)
    │                              │
    │                         Edit in text editor
    │                              │
    └── recode_markup()  ──→ Binary (byte-exact to original)
```

### Supported File Types

| Extension | Content | Notes |
|-----------|---------|-------|
| `.scene` | Level/room data | Objects, components, scripts, bounds |
| `.scl` | Object templates | Reusable entity blueprints |
| `.gdata` | Game data | Global configuration |
| `.gopt` | Game options | Settings |
| `.gplayer` | Player save | Profile data (items, quests, coins) |
| `.gstate` | Game state | World state flags |
| `.scmap` | World map | Zone/node/portal definitions |
| `.atlas` | UI atlas | Texture atlas definitions |
| `.fnt` | Font data | Bitmap font metrics |
| `.sounds` | Sound config | Audio bank definitions |
| `.fr` | FileRift project | Custom project format |

---

## 2. Ruby CLI Commands

### 2.1 Decode (Binary → Markup)

```bash
# Decode entire directory
ruby_cli -d <input_dir> -o <output_dir>

# Decode with forced file type
ruby_cli -d -t scene -o <output_dir> <file>

# Decode from stdin
ruby_cli -d -t scene --decode-stdin < file.scene

# Decode user save data
ruby_cli -d -u
```

### 2.2 Recode (Markup → Binary)

```bash
# Recode directory
ruby_cli -r <input_dir> -o <output_dir>

# Recode with forced type
ruby_cli -r -t scene -o <output_dir> <file>

# Both: recode then decode (verify round-trip)
ruby_cli --both

# Force recode (always rewrite even if unchanged)
ruby_cli --force
```

### 2.3 Scene Creator

```bash
# Create a new scene
ruby_cli scene create output.scene \
    --level "MyLevel" \
    --namespace "mymod" \
    --mesh "cave_rock" \
    --background "cavesbackground2" \
    --map worldmap.scmap \
    --width 5000 --height 2000 --depth 100 \
    --facing right
```

### 2.4 World Map

```bash
ruby_cli map summary <file.scmap>     # zone/node/portal counts
ruby_cli map decode <file.scmap>      # full markup output
ruby_cli map validate <file.scmap>    # report issues
ruby_cli map path <scmap> <from> <to> # BFS travel path
ruby_cli map list-nodes <file.scmap>  # tab-separated node list
```

### 2.5 Batch Texture Conversion

```bash
ruby_cli batch <src_dir> <dst_dir>           # convert textures
ruby_cli batch --import <src_dir> <dst_dir>  # import textures
```

### 2.6 APK Tools

```bash
ruby_cli apk extract <apk> <dest>       # extract APK contents
ruby_cli apk build <project>            # build APK from project
ruby_cli apk sign <apk>                 # sign APK
```

---

## 3. Scene File Format (FileRift Markup)

### 3.1 Root Structure

```
## FileRift decoded Swordigo file type: scene

Object{
    TemplateName : 'hiro'
    Identifier : 'hero'
    Component{ ... }
    Position{ X : 2134.82  Y : 522.84 }
    Depth : 0
    Rotation : 0
    Scaling : 1
    LocalAabb{ X : -30  Y : -30  Width : 60  Height : 60 }
    Hidden : 0
}
Object{ ... }
ObjectLibrary : <raw bytes>
Bounds : <raw bytes>       ← camera constraint box
Group : <raw bytes>
OnLoad{ ... }              ← scene-level Lua script
```

### 3.2 Object Structure

```
Object{
    TemplateName : 'template_name'    # optional (from .scl)
    Identifier : 'unique_name'        # required
    Component{
        ClassName : 'TypeName'
        Identifier : 101              # local component ID
        <ComponentPayload>{ ... }
    }
    Component{ ... }                  # multiple components allowed
    Position{ X : float  Y : float }
    Depth : float
    Rotation : float                  # radians
    Scaling : float
    LocalAabb{ X : float  Y : float  Width : float  Height : float }
    Hidden : 0 | 1
}
```

### 3.3 Component Payloads

**Background:**
```
Component{
    ClassName : 'Background'
    Identifier : 101
    BackgroundComponent{
        TextureName : 'forest_background_2x'
    }
}
```

**Light (directional):**
```
Component{
    ClassName : 'Light'
    Identifier : 101
    LightComponent{
        Type : 2           # 1=ambient, 2=key, 3=main, 4=shadow
        Intensity : 2
        Color{ R : 1  G : 1  B : 1  A : 1 }
    }
}
```

**Spawn Point:**
```
Component{
    ClassName : 'SpawnPoint'
    Identifier : 101
    SpawnPointComponent{
        FacingDirection : 1        # 1=right, -1=left
        SpawnOffset{ X : 0  Y : 0  Z : 0 }
    }
}
```

**Portal:**
```
Component{
    ClassName : 'Portal'
    Identifier : 101
    PortalComponent{
        DestinationSceneName : 'forest_part1'
        SpawnPointName : ''         # empty = default spawn
        TapToEnter : 1
        TriggerShapeId : 102        # → CollisionShape id
    }
}
```

**Collision Shape:**
```
Component{
    ClassName : 'CollisionShape'
    Identifier : 102
    ShapeComponent{
        Rectangle{ X : -41.5  Y : -20  Width : 83  Height : 110 }
    }
    CollisionShapeComponent{
        MinDepth : -15
        MaxDepth : 50
        SpecialType : 2             # 2 = portal zone
        Enabled : 1
    }
}
```

**Program (Lua script):**
```
Component{
    ClassName : 'Program'
    Identifier : 201
    ProgramComponent{
        String : $ local self = ...; \r\n Camera.ResetFocus(); $end
        Bytes : '\x1bLuaQ...'       # compiled Lua bytecode
    }
    Enabled : 1
    Trigger : 1                      # 1 = trigger-style script
}
```

**Model:**
```
Component{
    ClassName : 'Model'
    Identifier : 301
    ModelComponent{
        ModelName : 'hiro.pod'
        TexturePrefix : 'hiro'
    }
}
```

**Entity:**
```
Component{
    ClassName : 'Entity'
    Identifier : 401
    EntityComponent{
        FacingDirection : 1
        PhysicsEnabled : 1
    }
}
```

**Health:**
```
Component{
    ClassName : 'Health'
    Identifier : 501
    HealthComponent{
        MaxHealth : 100
        HEALTH_TYPE : 0
        BarOffset{ X : 0  Y : 40  Z : 0 }
    }
}
```

---

## 4. SCL Template Format

SCL files define reusable object templates:

```
## FileRift decoded Swordigo file type: scl

Name : 'template_name'
Template{
    Object{
        Identifier : 'entity_name'
        Component{ ... }
        Position{ X : 0  Y : 0 }
        Depth : 0
        Rotation : 0
        Scaling : 1
        Hidden : 0
    }
    Scaling : 1.5          # template-level scale multiplier
}
Template{ ... }            # multiple templates per SCL
```

### Using Templates

In a scene file, reference templates by name:
```
Object{
    TemplateName : 'beetle_wasteland'   # from beetle.scl
    Identifier : 'beetle1'
    Position{ X : 500  Y : 200 }
    Depth : 0
    Rotation : 3.14
    Scaling : 1
    Hidden : 0
}
```

The loader resolves all components from the template — only the transform
needs to be specified in the scene.

---

## 5. Mod VFS (Virtual File System)

The SRE mod system uses a VFS to overlay modded files on top of vanilla assets:

```
~/.local/share/swordigo-desktop/
├── assets/                    ← vanilla game files
│   └── resources/
│       ├── *.scene
│       ├── *.scl
│       └── *.tex.png
├── save/                      ← save data
├── cache/                     ← cached data
└── external/                  ← mod overlay
    └── resources/
        ├── modded.scene       ← replaces vanilla scene
        └── custom_texture.png ← adds new texture
```

### Mod Loading Order

1. Vanilla assets (read-only baseline)
2. External/mod overlay (higher priority)
3. Runtime patches (SRE hooks)

### Creating a Mod

```bash
# 1. Decode the scene you want to modify
ruby_cli -d -t scene -o decoded/ vanilla_scene.scene

# 2. Edit the markup text
vim decoded/vanilla_scene.scene

# 3. Recode to binary
ruby_cli -r decoded/ -o modded/

# 4. Place in mod overlay
cp modded/vanilla_scene.scene ~/.local/share/swordigo-desktop/external/resources/
```

---

## 6. Protobuf Wire Format Reference

### Field Types

| Wire Type | Name | Example |
|-----------|------|---------|
| 0 | Varint | `int`, `bool`, `enum` |
| 1 | 64-bit | `double`, `fixed64` |
| 2 | Length-delimited | `string`, `bytes`, embedded message |
| 5 | 32-bit | `float`, `fixed32` |

### Encoding Example (Bounds)

The `Bounds` field is root field 3 (tag `0x1a`), length-delimited (20 bytes):
```
0x1a 0x14              # field 3, wire type 2, length 20
0x0d [4 bytes float]   # field 1 (X), fixed32
0x15 [4 bytes float]   # field 2 (Y), fixed32
0x1d [4 bytes float]   # field 3 (Width), fixed32
0x25 [4 bytes float]   # field 4 (Height), fixed32
```

### Component Field Numbers

From `scene_schemas.cpp` (134 schema entries):

| Field # | Component |
|---------|-----------|
| 882 | GroundPolygonComponent |
| 890 | GroundMeshComponent |
| 898 | GroundMeshGeneratorComponent |
| 906 | TextureMappingComponent |
| 970 | CollisionShapeComponent |
| 978 | DamageComponent |
| 986 | HealthComponent |
| 1042 | LightComponent |
| 1122 | SoundEffectComponent |
| 1258 | ProgramComponent |
| 1266 | MonsterEntityComponent |
| 1602 | BackgroundComponent |
| 2002 | ParticleEmitterComponent |
| 4002 | PortalComponent |
| 4010 | SpawnPointComponent |
| 4018 | CollectableItemComponent |
| 4418 | SkillComponent |

---

## 7. Round-Trip Integrity

FileRift guarantees **byte-exact** round-trips:

```bash
# Decode
ruby_cli -d -o decoded/ scene.scene

# Recode
ruby_cli -r -o recoded/ decoded/scene.scene

# Verify byte-exact
diff <(xxd scene.scene) <(xxd recoded/scene.scene)
# No output = byte-exact match
```

This means:
- Editing markup and recoding produces a valid game file
- Unmodified files pass through unchanged
- The `Bytes` field (compiled Lua bytecode) is preserved as-is

---

## 8. Complete Component Census

Across all 116 shipped scenes:

| Component | Count | Category |
|-----------|-------|----------|
| CollisionShape | 6059 | Physics |
| GroundPolygon | 2286 | Terrain |
| GroundMesh | 2221 | Terrain |
| Model | 1469 | Rendering |
| Program | 995 | Scripting |
| Particle | 625 | Effects |
| Light | 567 | Lighting |
| SpawnPoint | 393 | Level |
| EntityController | 287 | AI |
| Portal | 274 | Level |
| ParticleEmitter | 241 | Effects |
| Background | 227 | Rendering |
| AnimationController | 214 | Animation |
| PhysicsObject | 197 | Physics |
| SimpleGlow | 183 | Effects |
| Damage | 164 | Combat |

---

## 9. Modding Workflow

### Quick Start

```bash
# 1. Set up workspace
mkdir mymod && cd mymod
ruby_cli -d ~/.local/share/swordigo-desktop/assets/resources -o vanilla/

# 2. Find the scene to edit
grep -rl "shopkeeper" vanilla/*.scene

# 3. Edit
vim vanilla/town_shop.scene

# 4. Build mod
ruby_cli -r vanilla/ -o modded/
cp modded/*.scene ~/.local/share/swordigo-desktop/external/resources/

# 5. Test
./bin/ruby  # launch game
```

### Adding a New Entity

1. Create an SCL with the entity template
2. Reference it in a scene file via `TemplateName`
3. Add `ProgramComponent` for behavior
4. Add `CollisionShapeComponent` for interaction
5. Place at desired position in the scene

### Adding a New Quest

1. Choose a unique quest ID (e.g., `"mod_myquest_01"`)
2. Create a trigger zone (`CollisionShape` + `Program`)
3. In the trigger script:
   ```lua
   Character.AddQuest("mod_myquest_01")
   Game.ShowNotification("New Quest: My Quest")
   ```
4. Add completion check in another trigger:
   ```lua
   if Character.IsQuestInProgress("mod_myquest_01") then
       Character.SetQuestCompleted("mod_myquest_01")
       Game.ShowNotification("Quest Complete!")
   end
   ```

### Adding a New Portal

1. In source scene, add portal object:
   ```
   Object{
       Identifier : 'portal_to_mylevel'
       Component{ ClassName:'Portal' Identifier:101
           PortalComponent{
               DestinationSceneName : 'mylevel'
               SpawnPointName : ''
               TapToEnter : 1
               TriggerShapeId : 102
           }
       }
       Component{ ClassName:'CollisionShape' Identifier:102
           ShapeComponent{ Rectangle{ X:-41.5 Y:-20 Width:83 Height:110 } }
           CollisionShapeComponent{ SpecialType:2 MinDepth:-15 MaxDepth:50 Enabled:1 }
       }
       Position{ X:500  Y:200 }  Depth:0  Hidden:0
   }
   ```
2. In destination scene, add return spawn:
   ```
   Object{
       Identifier : 'spawn_from_source_scene'
       Component{ ClassName:'SpawnPoint' Identifier:101
           SpawnPointComponent{ FacingDirection:1 SpawnOffset{X:0 Y:0 Z:0} }
       }
       Position{ X:100  Y:100 }  Depth:0  Hidden:0
   }
   ```

---

## 10. API Reference (Ruby SDK)

### FileRift Namespace

```cpp
namespace filerift {
    // Decode binary protobuf to human-readable markup
    std::string decode_protobuf(const std::string& bytes,
                                 const std::string& filetype);
    
    // Encode markup back to binary protobuf
    std::string recode_markup(const std::string& text,
                               const std::string& filetype);
    
    // Extract embedded Lua source from compiled bytecode
    std::string extract_lua_generic(const std::string& bytes);
}
```

### Scene Loader

```cpp
// Load a scene from binary
SceneData scene_loader::load(const std::string& path);

// Save a scene to binary (byte-exact round-trip)
void scene_loader::save(const SceneData& data, const std::string& path);

// Create a new scene from parameters
SceneData scene_loader::create(const SceneParams& params);
```

### Map Loader

```cpp
// Load world map
MapData map_loader::load(const std::string& path);

// Validate map integrity
MapValidation map_loader::validate(const MapData& data);

// BFS pathfinding between nodes
std::vector<std::string> map_loader::find_path(
    const MapData& data, 
    const std::string& from, 
    const std::string& to);
```

---

## 11. Modding Notes

- **FileRift is byte-exact** — you can safely edit and recode without losing data.
- **Lua bytecode is preserved** — the `Bytes` field in `ProgramComponent` is
  kept as-is. Only the `String` field (source code) needs to match.
- **Template resolution** — when `TemplateName` is set, the loader copies
  components from the template. You only need to override what differs.
- **Component IDs are local** — each object has its own ID space. The same
  ID (e.g., 101) can appear in multiple objects.
- **Bounds are required** — always include `Bounds` in new scenes or the
  camera won't work.
- **Portal naming convention** — portal objects are named
  `spawn_from_<destination>` to match the return spawn in the target scene.
