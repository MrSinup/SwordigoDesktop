# 02 — Scene Bounds & Camera

> The two systems the research prompt flagged as most critical. **SceneBounds is a real,
> REQUIRED root-level field** — it is *not* rendered by FileRift's text decoder, which is
> why it never shows up in decoded dumps. Camera, by contrast, is **not stored in scene
> files at all** — it is an engine-side singleton driven by Lua.

---

## 1. `Bounds` — the root "SceneBounds" field

### 1.1 Where it lives

- Root protobuf **field 3** of the scene message (length-delimited, 20 bytes).
- Stored by the Ruby SDK as `SceneData::bounds` (raw bytes) — preserved verbatim on save.
- **Observed in 118/118 shipped scenes.** It is REQUIRED.

### 1.2 Wire format (Observed)

A 20-byte sub-message containing exactly four `fixed32` fields:

| Tag byte | Protobuf field | Value |
|----------|----------------|-------|
| `0x0d` | 1 | `X` (left edge, world units) |
| `0x15` | 2 | `Y` (bottom edge) |
| `0x1d` | 3 | `Width` |
| `0x25` | 4 | `Height` |

All values are little-endian IEEE-754 floats.

Example raw bytes (`plains_part1.scene`):
```
0d 00 00 af c4 15 00 80 a2 c4 1d 00 f8 27 46 25 00 c0 0f 45
```
→ `X=-1400, Y=-1300, W=10750, H=2300`.

### 1.3 Real-world values (Observed, sample)

| Scene | X | Y | W | H |
|-------|---|---|---|---|
| `fire_part1` | -3500 | -500 | 5500 | 2000 |
| `florennum_part1` | -3000 | -1000 | 3200 | 3000 |
| `plains_part1` | -1400 | -1300 | 10750 | 2300 |
| `town`-style rooms (`florennum_shop`) | -450 | -300 | 1750 | 1000 |
| `plains_house1` | -450 | -100 | 1450 | 800 |
| `grass_house` | -450 | -200 | 2850 | 900 |
| `icecastle_partBoss` | -2000 | -1500 | 5000 | 4500 |
| `thecave_part1` | -2900 | -1200 | 2900 | 2200 |
| `hero.scene` / `menu.scene` | -3500 | -500 | 5500 | 2000 |
| `new_level.scene` (hand-made) | -160 | -24 | 320 | 240 |

**Patterns (Inferred):**
- Outdoor arenas: X ≈ -3500, W ≈ 5000–11000.
- Small interiors/rooms: W ≈ 1500–3000.
- The bounds always comfortably contain the level geometry and the `spawn_default` point.
- `new_level.scene` bounds match its ground object's `LocalAabb` exactly — evidence that
  bounds are *authored* to fit the playable area.

### 1.4 Why the Scene Creator must generate it

The research prompt's symptom — *"Camera.ResetFocus doesn't work in a new scene"* — is
explained by this field. A scene without meaningful bounds has nothing for the camera to
constrain/focus against. **"New Scene" must create bounds automatically** (e.g. from the
ground objects' AABB, padded), as one of the first steps alongside scene root + player spawn.

### 1.5 Ruby implementation note

To emit bounds in Ruby: `proto::Writer` with four fixed32 float fields (tags 3, 11, 19, 27
after the enclosing field-3 tag `0x1a 0x14`). This is a small, well-understood addition to
`scene_loader`/the scene-save path.

---

## 2. Camera — engine-side, script-driven

### 2.1 Camera is NOT a scene object (Observed)

- Zero `Camera*` classes appear in the object/component census of any scene.
- All 262 `Camera` mentions in scene files are Lua calls inside `Program`/`EntityAction`
  bytecode.

### 2.2 The Lua camera API (Observed census across 118 scenes)

| Call | Count | Meaning (Inferred from decompiled `CameraController`) |
|------|-------|-------|
| `Camera.ResetFocus()` | 262 | Return camera to default follow behavior (used on spawn / scene load) |
| `Camera.FocusAtShape(obj, rect?)` | 206 | Focus on an object (with optional screen-space rectangle) |
| `Camera.Rumble()` | 112 | Screen shake |
| `Camera.FollowShape(obj, rect?)` | 62 | Follow an object with optional constraints |
| `Camera.FocusAtPoint(pos)` | 60 | Pan camera to a world point |
| `Camera.JumpToFocus()` | 30 | Snap immediately to focus target |
| `Camera.IsPointVisible(pos)` | 10 | Frustum test |
| `Camera.FollowObject(obj)` | 2 | Follow an object |

### 2.3 Decompiled `CameraController` surface (Observed, arm32)

Functions present in `Caver::CameraController`:
`Update`, `FocusAtShape`, `FocusAtPoint`, `FocusAtRectangle`, `ResetFocus`,
`StopFollowing`, `FollowObject`, `FollowShape`, `GotoTargetImmediately`, `Rumble`,
`RegisterProgramLibrary`.

Core `Caver::Camera` math surface: `SetAspectRatio`, `SetPerspectiveProjection`,
`EvaluateViewMatrix`, `VisibleAreaSizeAtDistance`, `MinDistanceForVisibleAreaSize`,
`AABBOnZPlane`, `AABBForZRange`, `ScreenPointFromWorldPosition`,
`WorldPositionFromScreenPoint`, `RayFromScreenPoint`, `ForwardDirection`.

**Inferred camera model:** a perspective camera with a focus/follow target;
`FocusAt*` sets a focus target + optional constraint rectangle, `FollowShape` attaches to
an object, `ResetFocus` restores default (hero-follow + bounds clamping).

### 2.4 How scenes use it (Observed examples)

- `florennum_tower1` scripts: `Camera.Rumble()` inside looped programs (tower shake),
  `FocusAtShape`/`FollowShape` for set-piece framing.
- `fire_partBoss`: `Camera.Rumble()` bursts on the boss's shockwave attack.
- Spawn sequences: `Camera.ResetFocus()` is called at scene entry so the camera re-engages
  the hero and the scene bounds.

---

## 3. `CameraTemplate` — the Scene Creator deliverable

Because camera is engine-side, the *scene-side* camera configuration is:

1. **Bounds** (root field 3) — the camera's hard constraint box.
2. **`spawn_default` position** — the initial camera focus point.
3. **Optional Lua** (scene `OnLoad` or a `Program` object) for custom framing:
   - `Camera.ResetFocus()` — default (follow hero, clamp to bounds)
   - `Camera.FocusAtPoint(Vector3.New(x, y, 0))` — fixed framing for a set-piece
   - `Camera.FollowShape(Scene.Find("name"), Rectangle.New(...))` — constrained follow
   - `Camera.JumpToFocus()` — snap to the focus target immediately

### Template instantiation rules (Inferred, evidence-backed)

| Property | Default | Source |
|----------|---------|--------|
| Bounds X/Y/W/H | AABB of ground objects + 200px margin | observed authoring pattern |
| Initial focus | `spawn_default` position | `hero.scene`/`menu.scene` behavior |
| Default behavior | `Camera.ResetFocus()` in OnLoad | observed in 262 call sites |
| Follow constraint | none (full-bounds clamp) | standard scenes |

> **Pitfall to avoid (documented in the research prompt):** a new scene with tiny/zero
> bounds will make `Camera.ResetFocus()` appear broken. Always generate bounds *before*
> wiring camera scripts.
