# 03 — Lighting System

> Swordigo lighting is object-based: every scene has exactly **one** `Light` object (named
> `DirectionalLight`, or `DirectionalLight_day`/`DirectionalLight_night` in `town_part1`)
> that carries **three** `LightComponent`s. Point lights can be added as extra `Light`
> objects (e.g. `obj11#4`, `obj8#14` in `florennum_part1`/`grove_part1`).

---

## 1. The `Light` object anatomy (Observed)

Full decoded structure from `thecave_part1.scene`:

```
Object{
    Identifier : 'DirectionalLight'
    Component{
        ClassName : 'Light'   Identifier : 101
        LightComponent{ Type : 2  Intensity : 2  Color{ R:1 G:1 B:1 A:1 } }
    }
    Component{
        ClassName : 'Light'   Identifier : 103
        LightComponent{ Type : 1  Intensity : 0.300000012  Color{ R:1 G:1 B:1 A:1 } }
    }
    Component{
        ClassName : 'Light'   Identifier : 105
        LightComponent{ Type : 4  Intensity : 1  Color{ R:0 G:0 B:0 A:1 } }
    }
    Position{ X : 974.746826  Y : 834.802612 }
    Depth : 620.097656
    Rotation : 0   Scaling : 1
    LocalAabb{ X:-30 Y:-30 Width:60 Height:60 }
    Hidden : 0
}
```

Variants observed:
- `hero.scene`: Type 2 I0.6 + Type 1 I0.1 (dim studio setup).
- `town_part1`: two objects — `DirectionalLight_day` and `DirectionalLight_night`
  (day/night cycle swap).

---

## 2. `LightComponent` fields (Observed schema)

| Field # | Name | Type | Notes |
|---------|------|------|-------|
| 8 | `Type` | int | 1/2/3/4 — see below |
| 21 | `Intensity` | float | see statistics |
| 26 | `Color` | FloatColor (R,G,B,A) | note: Type 4 uses **black** |
| 37 | `LinearAttenuation` | float | rarely set |
| 45 | `QuadraticAttenuation` | float | rarely set |
| 50 | `Offset` | Vector3 | rarely set |
| 61 | `Radius` | float | rarely set |

---

## 3. Light types — statistics across all 118 scenes (Observed)

| Type | Intensity | Count | Interpretation (Inferred) |
|------|-----------|-------|---------------------------|
| **3** | 2.0 | 145 | **Main daylight / directional** (the standard key light) |
| **2** | 2.0 / 3.0 / 1.5 / 1.2 | 51+24+11+11 | **Key light** (sun/window), varying strength |
| **4** | 1.0 / 0.7 / 0.3 | 49+19+12 | **Black fill** — a negative/shadow light (Color is black!) |
| **1** | 0.3 / 0.5 / 0.2 | 48+23+23 | **Ambient light** |

**Observations:**
- Type 3 + Intensity 2 is the universal "standard day" light (145 occurrences).
- Type 4's `Color` is **black** — in the shader this acts as a subtractive/occlusion light.
  **Inferred:** Type 4 = shadow/contrast light used to darken cave interiors.
- Ambient (Type 1) is always a *small* intensity (0.2–0.5).
- Every scene's single `DirectionalLight` object = the canonical triple `{Type 2, Type 1, Type 4}`.

### Outdoor vs indoor (Inferred from the data)

- Outdoor scenes stick to the canonical triple (identical values across
  `fire_part1`, `grass_part1`, `grove_part1`, …).
- Indoor/cave scenes add extra `Light` objects with `Position` + `Depth 30` at key spots
  (`florennum_cave1` has 4 Light objects; `florennum_part1` has 4; `grove_part1` has 3).
- `hero.scene` uses dimmed intensities (a "studio" look).

---

## 4. `LightTemplate` — the Scene Creator deliverable

### 4.1 Default directional light (canonical triple)

```
Object{
    Identifier : 'DirectionalLight'
    Component{ ClassName:'Light' Identifier:101  LightComponent{ Type:2 Intensity:2   Color{1,1,1,1} } }
    Component{ ClassName:'Light' Identifier:103  LightComponent{ Type:1 Intensity:0.3  Color{1,1,1,1} } }
    Component{ ClassName:'Light' Identifier:105  LightComponent{ Type:4 Intensity:1    Color{0,0,0,1} } }
    Position{ X:0  Y:0 }
    Depth : 620.097656
    Rotation : 0   Scaling : 1
    LocalAabb{ X:-30 Y:-30 Width:60 Height:60 }
    Hidden : 0
}
```

UI-editable properties:
- `Intensity` of each of the three components (day key / ambient / shadow fill).
- `Color` of each.
- `Position` (usually at the level's center).
- Identifier (`DirectionalLight` vs `DirectionalLight_day`/`_night` for day/night scenes).

### 4.2 Point light (for interiors/caves)

```
Object{
    Identifier : 'obj1#4'      # unique name
    Component{ ClassName:'Light' Identifier:101
               LightComponent{ Type:3 Intensity:2 Color{1,1,1,1} Radius:?? } }   # Radius/attenuation Unknown
    Position{ X:...  Y:... }
    Depth : 30
    Rotation : 0  Scaling : 1  Hidden : 0
}
```

**Unknown:** the exact role of `Radius`/attenuation fields and the engine's falloff curve
for Type 3 point lights — flag as UNKNOWN in the UI tooltip until the shader is studied.

### 4.3 Evidence notes

- Component IDs 101/103/105 are the local IDs inside the light object (must stay unique
  per object, can reuse the same triple).
- `Depth` for lights is large (620) — keep for the directional light.
