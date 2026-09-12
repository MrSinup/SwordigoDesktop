# Swordigo Modding — Quick-Start Tutorial

> Build your first mod in 15 minutes. This guide walks you through creating a
> custom level with new terrain, a custom enemy encounter, a simple puzzle, and
> a Lua-scripted event — all using the Ruby CLI and the SRE mod loader.

---

## Prerequisites

- Swordigo Desktop installed (the `bin/ruby` binary)
- A text editor (VS Code, vim, nano — anything)
- Linux, macOS, or Windows (WSL)

Verify your tools work:

```bash
cd ~/SwordigoDesktop   # or wherever you cloned
./bin/ruby --help
./bin/ruby_cli --help
```

---

## Step 1 — Create Your Mod Directory

The game's VFS (Virtual Filesystem) looks for mods in:
```
~/.local/share/swordigo-desktop/mods/<mod_name>/resources/
```

Create your mod:

```bash
# Create the mod directory structure
mkdir -p ~/.local/share/swordigo-desktop/mods/my_first_mod/resources/
```

That's it. The game will now see `my_first_mod` in the launcher's mod browser.

---

## Step 2 — Decode Vanilla Assets (Learn by Example)

Before creating custom content, decode a few vanilla scenes to understand the
format. The Ruby CLI converts binary `.scene` files to human-readable markup.

```bash
# Decode a vanilla scene to see what a real scene looks like
./bin/ruby_cli -d ~/.local/share/swordigo-desktop/assets/resources/forest_part1.scene \
    -o /tmp/decoded_scene.txt

# Look at the decoded scene
cat /tmp/decoded_scene.txt | head -100
```

You'll see markup like:
```
## FileRift decoded Swordigo file type: scene

Object{
    Identifier : 'Background'
    Component{
        ClassName : 'Model'
        Identifier : 1
        ModelComponent{
            YRotation : 0
            EmissionFactor : 0
            ...
        }
    }
    Component{
        ClassName : 'Background'
        Identifier : 101
        BackgroundComponent{
            TextureName : 'forest_background'
        }
    }
    Position{
        X : 2677.05005
        Y : 1809.54968
    }
    Depth : 1.72038269
    Rotation : 0
    Scaling : 1
    LocalAabb{ X : 0  Y : 0  Width : 0  Height : 0 }
    Hidden : 0
}
```

This is the native FileRift protobuf format decoded to text. Every scene is a
list of `Object{}` blocks, each containing Components.

---

## Step 3 — Create Your First Custom Scene

The easiest way to start is to copy a vanilla scene and modify it:

```bash
# Copy a simple vanilla scene as your starting point
cp ~/.local/share/swordigo-desktop/assets/resources/grass_part1.scene \
   ~/.local/share/swordigo-desktop/mods/my_first_mod/resources/my_level.scene

# Decode it to text so you can edit it
mkdir -p /tmp/my_mod_decode
cp ~/.local/share/swordigo-desktop/mods/my_first_mod/resources/my_level.scene \
   /tmp/my_mod_decode/
./bin/ruby_cli -d /tmp/my_mod_decode/ -o /tmp/my_mod_decoded/
```

Now edit the decoded scene:

```bash
nano /tmp/my_mod_decoded/my_level.scene
```

### What to change

1. **Background texture** — find the `BackgroundComponent` and change the texture:
   ```
   TextureName : 'cavesbackground2'    # dark cave background
   ```

2. **Spawn point** — find `spawn_default` and change its position:
   ```
   Identifier : 'spawn_default'
   ...
   Position{
       X : 0
       Y : 200
   }
   ```

3. **Add a custom platform** — copy an existing ground object block and change
   its Position to place it elsewhere. Each ground object has:
   - `GroundPolygon` (the collision shape)
   - `GroundMesh` (the renderable mesh)
   - `GroundMeshGenerator` (procedural detail)
   - `TextureMapping` ×2 (top + front textures)

4. **Remove monsters** — delete any `MonsterEntity` object blocks to make
   the level peaceful.

5. **Add a portal** — copy a `Portal` object block and change:
   ```
   DestinationSceneName : 'grass_part1'   # where this portal leads
   ```

### Recode your modified scene back to binary

```bash
# Recode the edited markup back to a binary .scene file
mkdir -p /tmp/my_mod_recode
cp /tmp/my_mod_decoded/my_level.scene /tmp/my_mod_recode/
./bin/ruby_cli -r /tmp/my_mod_recode/ -o /tmp/my_mod_recode_out/

# Copy the binary back to your mod
cp /tmp/my_mod_recode_out/my_level.scene \
   ~/.local/share/swordigo-desktop/mods/my_first_mod/resources/my_level.scene
```

---

## Step 4 — Add Custom Textures

The game loads `.pvr` (PowerVR compressed) textures. To add a custom texture:

1. **Create a PNG image** (power-of-2 dimensions recommended: 256×256, 512×512)

2. **Convert it to PVR** using the batch converter:
   ```bash
   # Place your PNG in a directory
   mkdir -p /tmp/my_textures
   cp my_custom_grass.png /tmp/my_textures/

   # Convert PNG → PVR (game format)
   ./bin/ruby_cli --batch /tmp/my_textures/ ~/.local/share/swordigo-desktop/mods/my_first_mod/resources/
   ```

3. **Reference it in your scene** — the texture name is the filename without
   the extension. If you named it `my_custom_grass.png`, the game will look
   for `my_custom_grass.tex.png` or `my_custom_grass.pvr`.

   In your scene file:
   ```
   TextureMappingComponent{
       TextureName : 'my_custom_grass'
       Scale : 250
       Offset{ X : 0  Y : 0 }
   }
   ```

---

## Step 5 — Lua Scripting (Game Logic)

Swordigo uses Lua 5.1 for all game logic. Scripts are embedded in scene
objects or in `.scl` (Script Library) files.

### Example: A simple trigger that prints a message

Create a ScriptArea object in your scene:

```
Object{
    Identifier : 'my_trigger'
    Component{
        ClassName : 'CollisionShape'
        Identifier : 101
        ShapeComponent{
            Rectangle{ X : -50  Y : -50  Width : 100  Height : 100 }
        }
        CollisionShapeComponent{
            IsGround : 0
            Collides : 1
            ReceivesDamage : 0
            MinDepth : -45
            MaxDepth : 45
            SpecialType : 3
            Enabled : 1
        }
    }
    Component{
        ClassName : 'Program'
        Identifier : 104
        ProgramComponent{
            OnActivate{
                String : $
local self = ...;
print("Hello from my mod!")
Program.Wait(1)
Character.ShowMessage("Welcome to my custom level!")
--
            }
        }
    }
    Position{ X : 0  Y : 100 }
    Depth : 0
    Rotation : 0
    Scaling : 1
    LocalAabb{ X : -50  Y : -50  Width : 100  Height : 100 }
    Hidden : 0
}
```

### Common Lua APIs

```lua
-- Player interaction
local player = Character.GetPlayer()
local hp = Character.GetHealth(player)

-- Items
Character.GiveItem(player, "item_healingpotion", 3)
local has_key = Character.HasItem(player, "item_key_yellow")

-- Scene management
Scene.Load("next_level")           -- load a different scene
Scene.SetFlag("boss_defeated", 1)  -- set a persistent flag
local flag = Scene.GetFlag("boss_defeated")

-- Camera
Camera.SetFocus(100, 200)
Camera.ResetFocus()

-- Effects
Effects.Portal("portal_effect")
Effects.Sound("snd_explosion")
```

See `docs/modding/02_quest_system.md` for the complete API reference.

---

## Step 6 — Create a Moving Platform

Add this object block to your scene:

```
Object{
    Identifier : 'moving_plat_1'
    Component{
        ClassName : 'PhysicsPlatform'
        Identifier : 101
        PhysicsPlatformComponent{
            MoveX : 0
            MoveY : 200        -- moves 200 units vertically
            Speed : 1.5        -- speed multiplier
            Delay : 0.0        -- seconds before first move
            PauseTime : 1.0    -- seconds at each end
        }
    }
    Component{
        ClassName : 'CollisionShape'
        Identifier : 102
        ShapeComponent{
            Rectangle{ X : -80  Y : -10  Width : 160  Height : 20 }
        }
        CollisionShapeComponent{
            IsGround : 1
            Collides : 1
            ReceivesDamage : 0
            MinDepth : -45
            MaxDepth : 45
            SpecialType : 0
            Enabled : 1
        }
    }
    Component{
        ClassName : 'Model'
        Identifier : 103
        ModelComponent{
            Name : 'platformwood0'
            YRotation : 0
            EmissionFactor : 0
            XRotation : 0
            ShatterColor{ R : 0  G : 0  B : 0  A : 1 }
            Origin{ X : 0  Y : 0  Z : 0 }
            Transparent : 0
            DiffuseColor{ R : 1  G : 1  B : 1  A : 1 }
        }
    }
    Position{ X : 500  Y : 0 }
    Depth : 0
    Rotation : 0
    Scaling : 1
    LocalAabb{ X : -80  Y : -10  Width : 160  Height : 20 }
    Hidden : 0
}
```

---

## Step 7 — Add a Breakable Pot

```
Object{
    TemplateName : 'pot'
    Identifier : 'pot_1'
    Component{
        ClassName : 'CollisionShape'
        Identifier : 101
        ShapeComponent{
            Rectangle{ X : -15  Y : -25  Width : 30  Height : 50 }
        }
        CollisionShapeComponent{
            IsGround : 0
            Collides : 1
            ReceivesDamage : 1
            MinDepth : -45
            MaxDepth : 45
            SpecialType : 0
            Enabled : 1
        }
    }
    Position{ X : 300  Y : -5 }
    Depth : 0
    Rotation : 0
    Scaling : 1
    LocalAabb{ X : -15  Y : -25  Width : 30  Height : 50 }
    Hidden : 0
}
```

When the player attacks it, the pot shatters and drops loot (configured in the
`pot` template in `game_common.scl`).

---

## Step 8 — Add a Collectible Health Nugget

```
Object{
    TemplateName : 'nugget_health'
    Identifier : 'health_1'
    Position{ X : 200  Y : 30 }
    Depth : 0
    Rotation : 0
    Scaling : 1
    LocalAabb{ X : -20  Y : -20  Width : 40  Height : 40 }
    Hidden : 0
}
```

The `nugget_health` template already has a `CollectableItem` component that
restores 1 health when touched.

---

## Step 9 — Add Ambient Lighting

Vanilla scenes use many point lights for atmosphere. Add some:

```
Object{
    Identifier : 'ambient_glow_1'
    Component{
        ClassName : 'Light'
        Identifier : 101
        LightComponent{
            Type : 3
            Intensity : 2.0
            Color{ R : 1  G : 0.85  B : 0.5  A : 1 }
            Radius : 350
        }
    }
    Component{
        ClassName : 'SimpleGlow'
        Identifier : 103
        SimpleGlowComponent{
            Color{ R : 1  G : 0.85  B : 0.5  A : 0.5 }
            Size : 40
        }
    }
    Position{ X : 200  Y : 50 }
    Depth : 0
    Rotation : 0
    Scaling : 1
    LocalAabb{ X : -30  Y : -30  Width : 60  Height : 60 }
    Hidden : 0
}
```

---

## Step 10 — Test Your Mod

1. **Launch the game** through the launcher
2. **Enable your mod** in the mod browser (click your mod name, click "Enable")
3. **Use the Scene Shifter** (SwordfareGUI) to teleport to your scene:
   - Press F5 to open the GUI
   - Find "Scene Shifter" section
   - Type `my_level` and click Load
4. **Or** set your scene as the starting scene by editing a save file

### Debug with the Lua console

Press backtick (`` ` ``) to open the ImGui console. Type:

```lua
-- Teleport to your scene
Scene.Load("my_level")

-- Check if your scene loaded
print(Scene.GetName())

-- Give yourself items for testing
local p = Character.GetPlayer()
Character.GiveItem(p, "item_broadsword", 1)
Character.GiveItem(p, "item_key_yellow", 3)
```

---

## Step 11 — Package Your Mod

When your mod works, you can zip it for distribution:

```bash
cd ~/.local/share/swordigo-desktop/mods/
zip -r my_first_mod.swdmod my_first_mod/
```

Others can install it by extracting the `.swdmod` into their `mods/` directory.

---

## Complete Scene Template

Here's a minimal but complete custom scene with all the elements above:

```text
## FileRift decoded Swordigo file type: scene

Object{
    Identifier : 'Background'
    Component{
        ClassName : 'Model'
        Identifier : 1
        ModelComponent{
            YRotation : 0
            EmissionFactor : 0
            XRotation : 0
            ShatterColor{ R : 0  G : 0  B : 0  A : 1 }
            Origin{ X : 0  Y : 0  Z : 0 }
            Transparent : 0
            DiffuseColor{ R : 1  G : 1  B : 1  A : 1 }
        }
    }
    Component{
        ClassName : 'Background'
        Identifier : 101
        BackgroundComponent{
            TextureName : 'forest_background'
        }
    }
    Position{ X : 2677  Y : 1809 }
    Depth : 1.72038269
    Rotation : 0
    Scaling : 1
    LocalAabb{ X : 0  Y : 0  Width : 0  Height : 0 }
    Hidden : 0
}

Object{
    Identifier : 'DirectionalLight'
    Component{
        ClassName : 'Light'
        Identifier : 101
        LightComponent{
            Type : 2
            Intensity : 3
            Color{ R : 1  G : 1  B : 1  A : 1 }
        }
    }
    Component{
        ClassName : 'Light'
        Identifier : 103
        LightComponent{
            Type : 1
            Intensity : 0.300000012
            Color{ R : 1  G : 1  B : 1  A : 1 }
        }
    }
    Component{
        ClassName : 'Light'
        Identifier : 105
        LightComponent{
            Type : 4
            Intensity : 0.400000006
            Color{ R : 0  G : 0  B : 0  A : 1 }
        }
    }
    Position{ X : 373  Y : 523 }
    Depth : 18.77
    Rotation : 0
    Scaling : 1
    LocalAabb{ X : -30  Y : -30  Width : 60  Height : 60 }
    Hidden : 0
}

Object{
    Identifier : 'spawn_default'
    Component{
        ClassName : 'SpawnPoint'
        Identifier : 101
        SpawnPointComponent{
            FacingDirection : 1
            SpawnOffset{ X : 0  Y : 0  Z : 0 }
        }
    }
    Position{ X : 0  Y : 200 }
    Depth : 0
    Rotation : 0
    Scaling : 1
    LocalAabb{ X : -30  Y : -30  Width : 60  Height : 60 }
    Hidden : 0
}

Object{
    Identifier : 'ground_main'
    Component{
        ClassName : 'CollisionShape'
        Identifier : 1
        ShapeComponent{
            Rectangle{ X : -500  Y : -80  Width : 1000  Height : 80 }
        }
        CollisionShapeComponent{
            IsGround : 1
            Collides : 1
            ReceivesDamage : 1
            MinDepth : -45
            MaxDepth : 45
            SpecialType : 0
            Enabled : 1
        }
    }
    Component{
        ClassName : 'GroundPolygon'
        Identifier : 105
        GroundPolygonComponent{
            Polygon{
                Vertex{ X : -500  Y : -80 }
                Vertex{ X : 500  Y : -80 }
                Vertex{ X : 500  Y : 0 }
                Vertex{ X : -500  Y : 0 }
                Convex : 0
                Closed : 1
            }
            Collides : 1
            MinDepth : -45
            MaxDepth : 45
            OnCollide{ }
        }
    }
    Component{
        ClassName : 'GroundMesh'
        Identifier : 107
        GroundMeshComponent{
            LocalAabb{ X : -510  Y : -90  Width : 1020  Height : 100 }
        }
    }
    Component{
        ClassName : 'GroundMeshGenerator'
        Identifier : 109
        GroundMeshGeneratorComponent{
            RandomSeed : 1291618994
            HorizNoise : 0
            SurfaceWidth : 120
            HatHeight : 25
        }
    }
    Component{
        ClassName : 'TextureMapping'
        Identifier : 111
        TextureMappingComponent{
            TextureName : 'forest_grass'
            Scale : 250
            Offset{ X : 0  Y : 0 }
        }
    }
    Component{
        ClassName : 'TextureMapping'
        Identifier : 113
        TextureMappingComponent{
            TextureName : 'forest_ground'
            Scale : 250
            Offset{ X : 0  Y : 0 }
        }
    }
    Position{ X : 0  Y : 0 }
    Depth : 0
    Rotation : 0
    Scaling : 1
    LocalAabb{ X : -510  Y : -90  Width : 1020  Height : 100 }
    Hidden : 0
}

Object{
    TemplateName : 'pot'
    Identifier : 'pot_1'
    Position{ X : 300  Y : -5 }
    Depth : 0
    Rotation : 0
    Scaling : 1
    LocalAabb{ X : -15  Y : -25  Width : 30  Height : 50 }
    Hidden : 0
}

Object{
    TemplateName : 'nugget_health'
    Identifier : 'health_1'
    Position{ X : -200  Y : 20 }
    Depth : 0
    Rotation : 0
    Scaling : 1
    LocalAabb{ X : -20  Y : -20  Width : 40  Height : 40 }
    Hidden : 0
}

Object{
    Identifier : 'torch_1'
    Component{
        ClassName : 'Light'
        Identifier : 101
        LightComponent{
            Type : 3
            Intensity : 2.0
            Color{ R : 1  G : 0.6  B : 0.3  A : 1 }
            Radius : 350
        }
    }
    Component{
        ClassName : 'SimpleGlow'
        Identifier : 103
        SimpleGlowComponent{
            Color{ R : 1  G : 0.6  B : 0.3  A : 0.5 }
            Size : 40
        }
    }
    Position{ X : -100  Y : 50 }
    Depth : 48
    Rotation : 0
    Scaling : 1
    LocalAabb{ X : -30  Y : -30  Width : 60  Height : 60 }
    Hidden : 0
}
```

---

## Useful Commands Cheat Sheet

```bash
# Decode a scene to read it
./bin/ruby_cli -d <input.scene> -o <output_dir/>

# Recode edited markup back to binary
./bin/ruby_cli -r <input_dir/> -o <output_dir/>

# Decode ALL game assets at once
./bin/ruby_cli -d ~/.local/share/swordigo-desktop/assets/resources/ -o decoded/

# Create a minimal scene from scratch
./bin/ruby_cli scene create my_scene.scene --level "MyLevel" --facing right

# Check world map
./bin/ruby_cli map summary worldmap.scmap
./bin/ruby_cli map list-nodes worldmap.scmap

# Batch convert textures
./bin/ruby_cli --batch <src_png_dir> <dst_pvr_dir>
```

---

## Next Steps

- Read the full [Scene Camera System](01_scene_camera_system.md) for advanced
  camera control (zoom, bounds, focus points)
- Read the [Quest System](02_quest_system.md) for item/flag/dialogue APIs
- Read the [Cutscene System](04_scene_cutscene_system.md) for cinematics
- Read the [FileRift API](05_modding_api_filerift.md) for the full protobuf
  schema reference

---

## Troubleshooting

**Scene doesn't load:**
- Check that the binary `.scene` file is in `mods/<mod_name>/resources/`
- Use the Lua console (backtick key) to check for errors
- Decode your scene back to text to verify the markup is valid

**Textures are black/missing:**
- Ensure the `.pvr` file is in the mod's `resources/` directory
- Check the texture name matches exactly (case-sensitive)
- Use `TextureName : 'texture_stem'` without file extension

**Portal doesn't work:**
- The `DestinationSceneName` must match a scene file name (without `.scene`)
- Ensure the destination scene exists in your mod or in vanilla assets
- The portal needs a `CollisionShape` with `SpecialType : 2`

**Lua script crashes:**
- Open the console (backtick) and check error messages
- Wrap risky code in `pcall()` for error handling
- Use `print()` liberally for debugging
