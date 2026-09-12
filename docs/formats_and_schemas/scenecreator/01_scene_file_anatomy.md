# 01 — Scene File Anatomy

> What a `.scene` file actually is, top to bottom. All structure below is **Observed**
> from decoded real scenes unless tagged otherwise.

---

## 1. Root protobuf message

A scene file is a single protobuf message. The root fields (from `src/tools/scene_loader.cpp`
header comment + raw byte analysis):

| Root field | Wire type | Content | Notes |
|-----------|-----------|---------|-------|
| **1** | length-delimited | `Object` records (repeated) | Every scene object. 44–194 objects per shipped scene. |
| **2** | length-delimited | `ObjectLibrary` (raw bytes) | Embedded object templates/libraries. Present in most scenes (plains_part1 has 1). |
| **3** | length-delimited | **`Bounds`** | **Always present (118/118 scenes).** A 20-byte sub-message of four `fixed32` floats: `X, Y, Width, Height`. See [02_scene_bounds_and_camera.md](02_scene_bounds_and_camera.md). |
| **4** | length-delimited | `Group` (raw bytes) | Object groups (e.g. `town_part1` groups). Present but rarely needed. |
| **5** | length-delimited | `OnLoad` (Lua) | Scene-level load script. Present in **20/118** scenes (e.g. `town_part1`, `plains_part2`, `icecastle_part1`, `town_herohouse`). See [07_lua_scripting_api.md](07_lua_scripting_api.md). |

**Observed:** `probe_root` on `plains_part1.scene` → `root field 1 count 194, field 2 count 1, field 3 count 1, field 4 count 1`. No field 5 (no OnLoad).

---

## 2. The `Object` record

Every scene object is an `Object{ ... }` block. The canonical structure (observed in
literally every scene):

```
Object{
    TemplateName : 'hiro'          # OPTIONAL — library/template reference (resolved at load)
    Identifier   : 'hero'          # REQUIRED — unique object name in the scene
    Component{ ... }               # REQUIRED — one or more (ClassName + payload)
    Component{ ... }
    Position{
        X : 2134.82739             # REQUIRED — world X (float)
        Y : 522.848511             # REQUIRED — world Y (float)
    }
    Depth : 0                      # REQUIRED — Z ("depth"): -45..45 typical for collision,
                                   #            620 for the DirectionalLight, ~1.72 for Background
    Rotation : 0                   # REQUIRED — radians around Z (0, 3.14, 4.70, …)
    Scaling : 1                    # REQUIRED — uniform scale
    LocalAabb{
        X : -30                    # OPTIONAL-ish — render bounds
        Y : -30
        Width : 60
        Height : 60
    }
    Hidden : 0                     # REQUIRED — 0/1
}
```

### Field-by-field notes

- **`TemplateName`** — set when the object is an *instance* of a library template
  (`hiro`, `beetle_wasteland`, `dire_cavelurker`, `SceneObject`…). When present the loader
  resolves the template's components from the embedded `ObjectLibrary` (root field 2).
  **Observed:** `beetle_wasteland1` has only `TemplateName : 'beetle_wasteland'` + transform —
  all behavior comes from the library.
- **`Identifier`** — unique per scene. Naming conventions observed:
  - `obj1`, `obj3#15`, `obj10#4` — editor-generated "anonymous" objects (base + `#N` suffix).
  - `spawn_default`, `spawn_from_<scenename>`, `Background`, `DirectionalLight`,
    `questheromarker`, `ground1`, `boss`, `trigger`, `shape…`.
- **`Depth`** — the Z axis. Ground collision uses `MinDepth -45 / MaxDepth 45`; the
  `DirectionalLight` sits at `Depth ≈ 620`; `Background` at `Depth ≈ 1.72`.
- **`Rotation`** — radians. 0 = facing right (in Swordigo's convention), π ≈ facing left.

---

## 3. The `Component` record

```
Component{
    ClassName : 'SpawnPoint'       # REQUIRED — component type
    Identifier : 101               # REQUIRED — LOCAL component id, unique within the object
    ParentComponentIdentifier : 100  # OPTIONAL — links child components (e.g. CollisionShape → GroundPolygon)
    <ComponentSpecificPayload>     # e.g. SpawnPointComponent{ FacingDirection : 1, ... }
}
```

- **Local component IDs** are per-object, not global. The same object can carry
  `101, 103, 105` (the `DirectionalLight`) or `980, 981, 982, 983, 984, 985` (a GMG ground object).
- `ParentComponentIdentifier` creates child/parent relations — e.g. a `CollisionShape`
  (id 983) parented to a `GroundPolygon` (id 980) so it inherits its polygon.

---

## 4. What every scene contains (the invariants)

Decoded inventory across all categories (see the census in [05_component_catalog.md]):

| Element | Frequency | Status |
|---------|-----------|--------|
| `Background` object (`BackgroundComponent{ TextureName }`) | 1 per scene | **REQUIRED** for visual background |
| `DirectionalLight` object (3× `LightComponent`) | 1 per scene | **REQUIRED** for lighting |
| `spawn_default` (`SpawnPointComponent`) | 1 per scene | **REQUIRED** for the player spawn |
| `spawn_from_<scene>` spawn points | 1 per incoming portal | **COMMON** — needed for transitions |
| `GroundPolygon` objects (floor/collision) | 9–66 per scene | **REQUIRED** for walkable ground |
| `CollisionShape` objects | 2–13 per scene | **REQUIRED** for walls/obstacles |
| `Model` objects | 0–67 per scene | **COMMON** (decor, entities) |
| `Bounds` (root field 3) | 118/118 | **REQUIRED** |
| `ObjectLibrary` (root field 2) | most scenes | **COMMON** |
| `OnLoad` (root field 5) | 20/118 | **OPTIONAL** |

### Observed example — `thecave_part1.scene` (smallest shipped scene, 44 objects)

1. `Background` — `cavesbackground2`
2. `DirectionalLight` — 3 LightComponents (Type 2 I2, Type 1 I0.3, Type 4 I1)
3. `beetle_wasteland1` — template instance (library-only components)
4. `cavelurker1` — template instance (`dire_cavelurker`, Rotation 3.15)
5. `obj1` — `GroundPolygon` (polygon of 10+ vertices, `Collides: 1`, MinDepth/MaxDepth)
6. … more ground polygons, `CollisionShape`s, spawn points (`spawn_default`,
   `spawn_from_thecave_part2`, `spawn_from_wasteland_part4`), `ItemDrop`.

---

## 5. Transforms & coordinates

- Swordigo is effectively **2.5D**: `Position{X,Y}` in world space + `Depth` (Z) for
  rendering layers and collision depth ranges.
- **Observed depths:** Background ≈ 1.72 (layer behind everything), DirectionalLight ≈ 620
  (lighting layer), gameplay objects at 0 with collision `MinDepth -45 / MaxDepth 45`.
- Ground polygons carry `MinDepth`/`MaxDepth` (usually -45/45) which define the depth band
  the hero can stand on.

---

## 6. Save/round-trip pipeline (Ruby SDK)

`scene_loader.cpp` pipeline:
1. **Load:** parse root field 1 → `SceneObject`s; preserve fields 2–5 as raw bytes
   (`object_libraries`, `bounds`, `groups`, `onload_scripts`) so **nothing is lost** on save.
2. Compute a scene AABB from object positions (used for editor camera framing).
3. **Save:** re-emit every `SceneObject` via `proto::Writer`, then write the preserved
   raw sections verbatim.

> **Implication for the Scene Creator:** Ruby already round-trips `Bounds` and `OnLoad`
> byte-faithfully. Creating a *new* scene only requires emitting field 1 objects + field 3
> bounds; fields 2/4/5 are optional.

---

## 7. Known unknowns

- **Unknown:** the exact wire format of root field 2 (`ObjectLibrary`) and field 4 (`Group`)
  — they are preserved raw by Ruby and not yet parsed.
- **Unknown:** whether `Bounds` is read by the vanilla engine for camera clamping, or only
  by tooling. The camera's constraint behavior is inferred (see [02](02_scene_bounds_and_camera.md)).
