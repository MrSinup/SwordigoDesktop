# 01 — Scene Camera System

> The camera in Swordigo is **NOT a scene object** — it is an engine-side singleton
> (`Caver::CameraController`) driven entirely by Lua scripts. Scene files influence it
> only through the `Bounds` field (root protobuf field 3) and Lua API calls inside
> `Program`/`EntityAction` scripts.

---

## 1. Camera Architecture

```
┌──────────────────────────────────────────────┐
│            Caver::CameraController            │
│                                               │
│  State:                                       │
│    focus_target    → SceneObject* (default: hero) │
│    focus_point     → Vector3 (world coords)  │
│    follow_target   → SceneObject*            │
│    follow_rect     → Rectangle (constraint) │
│    shake_intensity → float                   │
│    bounds          → Rectangle (from scene)  │
│                                               │
│  Per-frame:                                   │
│    1. If follow_target → track it             │
│    2. Else if focus_target → smooth to it    │
│    3. Else if focus_point → pan to it        │
│    4. Clamp to Bounds                         │
│    5. Apply shake                             │
│    6. Compute view matrix                     │
└──────────────────────────────────────────────┘
```

The camera always starts by tracking the hero entity. Scripts override this
temporarily for cutscenes, then restore via `Camera.ResetFocus()`.

---

## 2. Scene Bounds (Root Field 3)

Every scene **requires** a `Bounds` field — without it, `Camera.ResetFocus()`
has nothing to clamp against and the camera may fly offscreen.

### Wire format

Root protobuf field 3, length-delimited, 20 bytes:
```
4 × fixed32 (little-endian IEEE-754 floats):
  Tag 0x0d → X      (left edge, world units)
  Tag 0x15 → Y      (bottom edge)
  Tag 0x1d → Width  (right edge = X + Width)
  Tag 0x25 → Height (top edge = Y + Height)
```

### Observed values

| Scene Type | X | Y | Width | Height | Example |
|-----------|---|---|-------|--------|---------|
| Large outdoor | -3500 | -1300 | 10750 | 2300 | plains_part1 |
| Medium arena | -3500 | -500 | 5500 | 2000 | fire_part1 |
| Small interior | -450 | -300 | 1750 | 1000 | florennum_shop |
| Tiny room | -450 | -100 | 1450 | 800 | plains_house1 |
| Boss arena | -2000 | -1500 | 5000 | 4500 | icecastle_partBoss |
| Cave | -2900 | -1200 | 2900 | 2200 | thecave_part1 |

### Auto-generation rule

When creating a new scene, generate Bounds from the AABB of all ground objects
(padded by ~200px on each side):

```lua
-- Pseudocode for scene creator
local minX, minY, maxX, maxY = compute_ground_aabb()
Bounds = { X: minX-200, Y: minY-200, Width: (maxX-minX)+400, Height: (maxY-minY)+400 }
```

---

## 3. Camera Lua API (Complete)

### 3.1 Focus Control

```lua
Camera.ResetFocus()
```
Restores the camera to its default behavior: smoothly track the hero entity,
clamped to scene bounds. **Called 146 times across all scenes** — always at
scene entry and after cutscenes.

```lua
Camera.FocusAtShape(sceneObject)
Camera.FocusAtShape(sceneObject, Rectangle.New(x, y, w, h))
```
Sets the focus target to an entity. The optional Rectangle defines a
**screen-space constraint** — the camera keeps the entity within this rect
rather than centering on it. Used 126 times.

```lua
Camera.FocusAtPoint(Vector3.New(x, y, 0))
```
Pans the camera to a fixed world point. Used 24 times for cinematic pans.

```lua
Camera.JumpToFocus()
```
Snaps the camera instantly to the current focus target (no interpolation).
Used 24 times — typically after repositioning the hero during cutscenes.

### 3.2 Follow Mode

```lua
Camera.FollowShape(sceneObject)
Camera.FollowShape(sceneObject, Rectangle.New(x, y, w, h))
```
Attaches the camera to track an entity's movement in real-time. The optional
Rectangle constrains how far the camera can drift from center. Used 44 times.

```lua
Camera.FollowObject(sceneObject)
```
Simpler follow — tracks entity without constraint rectangle. Used 2 times.

```lua
Camera.StopFollowing()
```
Detaches the camera from any follow target.

### 3.3 Effects

```lua
Camera.Rumble()
Camera.Rumble(intensity, duration)
```
Screen shake effect. Used 40 times — boss attacks, tower earthquakes, impacts.

### 3.4 Queries

```lua
Camera.IsPointVisible(Vector3.New(x, y, 0))
```
Returns true if the point is within the camera's visible frustum. Used 10 times.

### 3.5 Decompiled CameraController Functions

From IDA/Ghidra decompilation of the ARM64 binary:

| Function | Purpose |
|----------|---------|
| `CameraController::Update(float dt)` | Per-frame update — applies follow, focus, bounds, shake |
| `CameraController::FocusAtShape(SceneObject*)` | Set focus target |
| `CameraController::FocusAtShape(SceneObject*, Rectangle)` | Set focus target + constraint |
| `CameraController::FocusAtPoint(Vector3)` | Set fixed focus point |
| `CameraController::ResetFocus()` | Restore hero tracking |
| `CameraController::FollowShape(SceneObject*)` | Attach follow |
| `CameraController::FollowShape(SceneObject*, Rectangle)` | Attach follow + constraint |
| `CameraController::FollowObject(SceneObject*)` | Simple follow |
| `CameraController::GotoTargetImmediately()` | Snap to focus |
| `CameraController::Rumble()` | Trigger shake |
| `CameraController::StopFollowing()` | Detach follow |

---

## 4. Camera Math (Decompiled)

The underlying `Caver::Camera` class provides:

| Function | Purpose |
|----------|---------|
| `SetAspectRatio(float)` | Set viewport aspect ratio |
| `SetPerspectiveProjection(float fov, float near, float far)` | Configure projection |
| `EvaluateViewMatrix()` | Compute the view matrix for rendering |
| `VisibleAreaSizeAtDistance(float z)` | How much world is visible at depth z |
| `MinDistanceForVisibleAreaSize(Vector2 size)` | Minimum zoom for given area |
| `AABBOnZPlane(Rectangle bounds, float z)` | Project AABB to screen at depth z |
| `ScreenPointFromWorldPosition(Vector3 pos)` | World → screen coords |
| `WorldPositionFromScreenPoint(Vector2 pt, float z)` | Screen → world coords |
| `RayFromScreenPoint(Vector2 pt)` | Unproject screen point to ray |
| `ForwardDirection()` | Camera forward vector |

### Perspective Projection

The camera uses a **perspective projection** (not orthographic), which is why
objects appear smaller in the background. The projection matrix from decoded
scenes shows:
```
Proj row0: 3.195  0.000  0.000  0.000    (X scale)
Proj row1: 0.000  5.671  0.000  0.000    (Y scale)
Proj row2: 0.000  0.000 -1.005 -100.251  (Z/near-far)
Proj row3: 0.000  0.000 -1.000  0.000    (perspective divide)
```

---

## 5. Common Camera Patterns (Observed)

### Pattern 1: Scene Entry (most common)
```lua
-- In OnLoad or spawn trigger
Camera.ResetFocus()  -- track hero, clamp to bounds
```

### Pattern 2: Cutscene Pan
```lua
Game.SetCinematicMode(true, false)   -- freeze hero
Scene.SetPaused(false)
Camera.FocusAtShape(Scene.Find("king"), Rectangle.New(100, 100, 100, 100))
Program.Wait(2.0)
Camera.ResetFocus()
Game.SetCinematicMode(false, true)   -- unfreeze hero
```

### Pattern 3: Boss Arena Lock
```lua
-- On collide with boss zone
Camera.FocusAtShape(self, Rectangle.New(100, 100, 100, 100))
-- On collision end
Camera.ResetFocus()
```

### Pattern 4: Earthquake/Tower Shake
```lua
while true do
    Camera.Rumble()
    Program.Wait(0.5)
end
```

### Pattern 5: Teleport Pan
```lua
Game.FadeOut()
Program.Wait(0.5)
target:setPosition(Scene.Find("questMarker"):position())
Camera.FocusAtShape(Scene.Find("focusArea2"))
Camera.JumpToFocus()
Game.FadeIn()
Program.Wait(1.0)
Camera.ResetFocus()
```

---

## 6. Depth/Z-Layer System

Swordigo is 2.5D — `Position{X,Y}` is the world plane, `Depth` is the Z layer:

| Depth Value | Layer | Notes |
|------------|-------|-------|
| 1.72 | Background | Far behind everything |
| 0 | Gameplay | Hero, enemies, ground collision |
| 620 | DirectionalLight | Lighting layer |
| -45..45 | Collision band | MinDepth/MaxDepth for ground collision |
| 15 | Effects | Particle emitters, glows |

The camera renders all visible depth layers simultaneously — depth is for
**rendering order and collision filtering**, not for camera distance.

---

## 7. Modding Notes

- **Never forget Bounds** — a new scene without proper Bounds will make the camera
  appear broken. Always compute Bounds from ground geometry.

- **Always call `Camera.ResetFocus()`** after any cutscene — failing to do so leaves
  the camera locked on a fixed point, making the game unplayable.

- **`Camera.FocusAtShape` with Rectangle** is for "cinematic framing" — the camera
  tracks the entity but keeps it off-center within the rectangle.

- **`Camera.FollowShape` is for dynamic tracking** — use when the focus target moves
  unpredictably (e.g., a flying boss, a moving platform).

- The camera view is **perspective**, not orthographic — objects further from the
  camera appear smaller. Plan your scene layout accordingly.
