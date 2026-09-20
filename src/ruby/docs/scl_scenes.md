# Understanding SCL Scenes & Level Hierarchy

## 1. SCL vs SCENE: The Dual Architecture

Swordigo divides level and entity construction into two complementary asset formats:

```text
┌─────────────────────────┐          Instantiates          ┌─────────────────────────┐
│     Entity Template     │ ─────────────────────────────> │       World Scene       │
│         (.scl)          │                                │        (.scene)         │
│                         │                                │                         │
│ Defines components,     │                                │ Places concrete actor   │
│ default health, meshes, │                                │ instances at (X, Y, Z)  │
│ and behavior scripts    │                                │ with custom parameters  │
└─────────────────────────┘                                └─────────────────────────┘
```

- **`.scl` (Scene Component Library)**: The blueprint catalog. Defines entity archetypes (e.g. `goblin_spear`, `torch_lantern`, `checkpoint_shrine`) with all their internal components.
- **`.scene` (Level Scene)**: The physical world map. Instantiates blueprints into the level, establishes ground meshes, camera bounds, dynamic spawn triggers, and background layers.

---

## 2. Anatomy of an SCL Entity Template

Every archetype in `.scl` is encapsulated in a `Template` container:

```filerift
Name : 'dungeon_props'
Template {
    Object {
        Identifier : 'treasure_chest'
        Component {
            ClassName : 'CollectableItem'
            Identifier : 1
            CollectableItemComponent {
                Type : 2
                Value : 50
                ItemName : 'shard_large'
                RequiresPickup : 1
            }
        }
        Component {
            ClassName : 'Model'
            Identifier : 101
            ModelComponent {
                Name : 'chest'
                EmissionFactor : 1.2
                Origin { X : 0, Y : 0, Z : 0 }
            }
        }
    }
}
```

### Component Roles:
1. **Identifier**: Numeric component ID unique within this object.
2. **ClassName**: Ties into the native C++ engine reflection registry (`Health`, `Model`, `Physics`, `Program`).
3. **Payload**: Strongly-typed fields parsed directly from Protocol Buffers.

---

## 3. World Scene Hierarchy & Spatial Math

In `.scene` files, each placed actor establishes spatial transform properties:

```filerift
Object {
    TemplateName : 'chest_gold'
    Identifier : 'chest_room1_secret'
    Position {
        X : 1240.5
        Y : -320.0
    }
    Depth : 0.0
    Rotation : 0.0
    Scaling : 1.0
    LocalAabb {
        X : -40.0
        Y : 0.0
        Width : 80.0
        Height : 60.0
    }
    Hidden : 0
}
```

### Spatial Parameters:
- **`Position { X, Y }`**: 2D primary world coordinate. In Swordigo, the camera navigates on the XY plane.
- **`Depth`**: Z-axis layer offset (typically `-5.0` to `5.0`). Controls parallax layers and rendering order.
- **`LocalAabb`**: Axis-Aligned Bounding Box relative to position origin. Used for rapid camera frustum culling. If an entity moves outside its `LocalAabb`, it may pop out of existence when the camera pans away!
- **`Hidden`**: When set to `1`, disables all rendering while keeping physics and triggers active.

---

## 4. Protobuf Binary Serialization

While modders edit clean FileRift text syntax, Swordigo's APK expects compiled binary Protocol Buffers.

### The FileRift Pipeline:
```text
FileRift Text (.scene / .scl)
              │
              ▼ [recode_markup in ruby_tools_bridge]
Google Protobuf v2 Lite Binary
              │
              ▼
Swordigo APK (assets/data/...)
```

Ruby GG provides instant binary compilation. In the **New File Wizard** or via **FileRift Recode**, toggle *Compile Binary Protobuf* to produce game-ready assets instantly!
