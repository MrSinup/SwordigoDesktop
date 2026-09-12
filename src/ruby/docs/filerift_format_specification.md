# FileRift Asset Format: Technical Specification

## 1. Scope & Purpose
FileRift is the serialization and configuration format utilized by Swordigo for all scene blueprints, level placements, item definitions, and visual layouts. While stored on disk in production builds as binary Google Protocol Buffers (v2 Lite), development and modding authoring uses the human-readable FileRift text notation (`.scl`, `.scene`, `.gdata`, `.fr`).

---

## 2. File Classifications

| Extension | Formal Name | Primary Role | Root Scope |
| :--- | :--- | :--- | :--- |
| `.scl` | Scene Component Library | Reusable entity archetypes, object templates, and component definitions. | `scl` |
| `.scene` | Level Scene Definition | Concrete world instances, static meshes, lights, bounds, and placed actors. | `scene` |
| `.gdata` | Game Data Registry | Global game constants, item tables, skill curves, and player profile templates. | `gdata` |
| `.fr` | Generic FileRift Stream | Unstructured FileRift container supporting mixed object hierarchies. | `fr` |

---

## 3. Lexical Syntax & Grammar

### 3.1 Structural Blocks
Blocks represent nested Protocol Buffer message structures enclosed in braces `{ ... }`:
```filerift
Object {
    Identifier: 'hero'
    Scaling: 1.0
    Component {
        ClassName: 'Health'
        MaxHealth: 100
    }
}
```

### 3.2 Property Assignment
Properties can be defined with or without colon separators:
```filerift
# Standard key-value with colon
Identifier: 'dungeon_gate'
Depth: 0.0

# Without colon (concise syntax)
Position { X: 120.0, Y: 45.0 }
Scaling 1.5
```

### 3.3 Data Types
- **String**: Enclosed in single or double quotation marks (`'hero'`, `"florennum"`).
- **Integer**: Base-10 signed integer (`100`, `-5`).
- **Float**: Decimal floating-point numbers (`1.0`, `0.001`, `-45.2`).
- **Boolean**: `true` or `false`.
- **Vector2 / Vector3**: Nested struct with components (`X: 10.0, Y: 20.0, Z: 0.0`).
- **Color**: Hexadecimal or float channels (`R: 1.0, G: 0.5, B: 0.2, A: 1.0`).

### 3.4 Embedded Lua Chunks (`$ ... $end`)
Script properties contain multi-line Lua 5.1 code delimited by a starting dollar sign `$` and terminating `$end`:
```filerift
Component {
    ClassName: 'Program'
    Program: $
        local self, target = ...;
        if Character.HasItem("key_bronze") then
            DoorController.Open(self);
        end
    $end
}
```

### 3.5 External Macros (`$source[...]`)
Allows linking external standalone Lua script files during compilation:
```filerift
Program: $source[scripts/boss_ai.lua]
```

---

## 4. Protobuf Mapping & Tag IDs

FileRift properties correspond directly to Protobuf field tags and wire types:

| Scope | Field | Tag (Hex) | Wire Type | Description |
| :--- | :--- | :---: | :--- | :--- |
| `SceneObject` | `Identifier` | `0x0a` | Length-delimited (String) | Unique entity identifier string. |
| `SceneObject` | `Position` | `0x12` | Length-delimited (Message) | 3D coordinate vector (`Vector3`). |
| `SceneObject` | `Scaling` | `0x1d` | 32-bit (Float) | Scale factor multiplier. |
| `SceneObject` | `Rotation` | `0x25` | 32-bit (Float) | 2D plane z-axis rotation angle in radians. |
| `SceneObject` | `Depth` | `0x2d` | 32-bit (Float) | Z-buffer parallax rendering depth. |
| `SceneObject` | `Component` | `0x32` | Length-delimited (Message) | Attached ECS component definition. |
| `Component` | `ClassName` | `0x0a` | Length-delimited (String) | Exact engine component type name. |
| `Component` | `Identifier` | `0x12` | Length-delimited (String) | Component instance label. |
| `Component` | `Active` | `0x18` | Varint (Bool) | Active simulation state. |
| `Component` | `Exclusive` | `0x20` | Varint (Bool) | Prevents multiple instances on single entity. |

---

## 5. Typical Asset Structure Example

```filerift
# ═════════════════════════════════════════════════════════════════════════════
# Example .scl template asset
# ═════════════════════════════════════════════════════════════════════════════
Name: 'woodkeep_dungeon'
ImportedLibrary: 'game_common'

Template {
    Object {
        Identifier: 'spiked_trap'
        Scaling: 1.0
        
        Component {
            ClassName: 'Model'
            ModelName: 'trap_spikes'
        }
        
        Component {
            ClassName: 'CollisionShape'
            ShapeType: 'Box'
            Width: 64.0
            Height: 32.0
        }
        
        Component {
            ClassName: 'Damage'
            Damage: 30
            DamageType: 0 # PHYSICAL
            DamageOrigin: 0
        }
    }
}
```
