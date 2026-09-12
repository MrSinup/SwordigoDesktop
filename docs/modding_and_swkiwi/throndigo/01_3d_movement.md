# Throndigo — 3D Movement Support

The mod turns the 2D side-scroller hero into a 3D camera + Z-axis
movement experiment, driven entirely from the hero template's `OnLoad`
Lua in `hiro.scl` (file `decoded/hiro.scl`, lines 6–488 of the decoded
markup).

All of this runs in the engine's Lua tied to the `hiro` template
instance (`hero = self`). It uses: `OverlayController` buttons,
`CameraController`, `GameController` input simulation, `ModelTransform
Controller` rotation, Z-axis `setPosition`/`setVelocity`, and a camera
renormalization loop.

## Boot sequence (OnLoad)

```lua
hero = self
OverlayController.RemoveAll()            -- clear any leftover buttons

CameraMode = 1
CameraController.SetOffset(500, 100, 0) -- camera pulled far to screen-plane
Program.Wait(0)
Camera.JumpToFocus()

local bg = Scene.CreateObject("background")  -- obj.scl: a quad that follows hero

CameraController.DisableOptimizations(true)  -- keep camera live every frame
for key, value in ipairs(Mega.FindAll()) do  -- wake every object in scene
    value:setAlwaysActive(true)
end
```

The background "follow" object (`obj.scl`): a `background` model scaled
0.5 that, every frame, repositions itself at the hero's position so the
visual backdrop always covers the camera. It also destroys any engine
`Background`/`Background_day`/`Background_night` objects when created.

## Virtual d-pad (oc.menu)

Four arrow buttons + a camera-direction toggle are created via the
`oc.menu` library (`oc.scl`):

| key | text | pos | onTouch / onHeld | effect |
|-----|------|-----|------------------|--------|
| left  | ← | (0.112, 0.75) | GameControlButtonDown(1/2), velX=−10, Z −10/+10 | move left |
| right | → | (0.232, 0.75) | GameControlButtonDown(1/2), velX=+10 or 0, Z +5/−5 | move right |
| front | ↑ | (0.168, 0.65) | GameControlButtonDown(1/2), rotate=0 | move forward |
| back  | ↓ | (0.168, 0.85) | GameControlButtonDown(1/2), rotate=0 | move backward |
| changedir | dirchange | (0.7, 0.7) | toggles CameraMode 1↔2, SetOffset(±500,100,0) | flip camera side |

Each button is created with `OverlayController.NewButton(text, x, y, w,
h)`; `oc.menu` wraps them with `onTouch` / `onHeld` / `onHeldFinish`
implemented as per-object poll threads (see `oc.scl`) polling
`btn:isPressed()` every 0.05 s.

### What the buttons actually do

Left/right blend **two** techniques at once:

1. **Simulated engine input** through
   `GameController.GameControlButtonDown(1/2)` + `...Up(1/2)` and
   `hero:setVelocity(Vector3.New(±10, vy, 0))` — the hero accelerates
   along X like a normal side-scroller walk.
2. **Z-axis translation** in `onHeld`: `hero:setPosition(hero:position()
   + v3(0,0,±10))` — the hero is shoved along depth every held frame
   (the direction flips with CameraMode), which is what produces the
   "3D" parallax walk.

`ModelTransformController.SetRotationAngle(hero, 90)` rotates the model
90° so the sprite faces the "forward" axis; `Entity.SetFacingDirection`
is used to flip facing.

Every held frame ends with `Camera.JumpToFocus()` so the camera tracks
immediately.

### Camera renormalizing loop

A non-blocking `async()` thread (from `async.scl`) spins every frame:

```lua
while true do
    Program.Wait(0)
    Camera.FocusAtPoint(hero:position())
    Camera.JumpToFocus()
end
```

Because the camera offset is huge (`±500, 100, 0`) and optimizations are
disabled, `FocusAtPoint` + `JumpToFocus` keep the hero centered in the
view.

## Camera mode toggle

`changedir` flips `CameraMode` 1↔2, choosing the camera offset sign
(`500,100,0` vs `-500,100,0`). All d-pad handlers check `CameraMode` to
pick which engine control button (1 or 2) and which Z-direction to move.

There is also a scrapped joystick (`bar` from `OverlayController.New
Button`, `makeMovable(true)`) whose `barfunc` demonstrates the intended
camera joystick (moved to settings position 0.7,0.5), plus supporting
globals (`barpressed`, `initialz`/`initialy`).

## Deep-dive notes

- `sub(:y())` / `hero:velocity():y()` read the existing Y velocity so X
  overrides don't stomp gravity; gravity/jumping still comes from the
  engine `CharControllerComponent`.
- `Vector3.New` is the engine's host-side vector constructor; the mod
  wraps it as `v3(...)`.
- `Mega.FindAll()` + `setAlwaysActive(true)` matters: with a shifted
  camera and Z movement, the engine's own culling would sleep distant
  objects; keeping everything active makes the 3D scene visually
  coherent.
- The mod calls `AnimationController.BlendToAnimation(hero, 105)` mostly
  commented out; running was left to the engine.
- `Math.clamp` is monkey-patched globally for later use.