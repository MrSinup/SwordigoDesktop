# 04 — Scene Cutscene System

> Swordigo cutscenes are **Lua-driven coroutine sequences** — not pre-recorded
> animations. A `Program` component runs a coroutine that calls `Camera.*`,
> `Game.SetCinematicMode()`, `ShowQuestBubbles()`, `Game.FadeIn/Out()`, and
> `Program.Wait()` to orchestrate the sequence. The engine provides no
> dedicated "Cutscene" component — everything is assembled from the Lua API.

---

## 1. Cutscene Architecture

```
┌───────────────────────────────────────────────┐
│           ProgramComponent (coroutine)         │
│                                                │
│  1. Game.SetCinematicMode(true, false)          │
│     → Freeze hero controls, hide HUD           │
│                                                │
│  2. Scene.SetPaused(false)                      │
│     → Keep scene updating (for animations)     │
│                                                │
│  3. Camera.FocusAtShape(target)                 │
│     → Redirect camera                          │
│                                                │
│  4. Program.Wait(seconds)                       │
│     → Yield coroutine (sleep)                  │
│                                                │
│  5. ShowQuestBubbles(...) / HideTextBubble(...) │
│     → NPC dialogue                             │
│                                                │
│  6. Game.FadeOut() → Program.Wait() → FadeIn() │
│     → Scene transitions                        │
│                                                │
│  7. Camera.ResetFocus()                         │
│     → Restore hero tracking                    │
│                                                │
│  8. Game.SetCinematicMode(false, true)          │
│     → Unfreeze hero, show HUD                  │
└───────────────────────────────────────────────┘
```

The key insight: **cutscenes are just Lua scripts that yield**. The coroutine
suspends at each `Program.Wait()` call, allowing the game to render frames
while the script is "sleeping".

---

## 2. Core Cutscene API

### 2.1 Cinematic Mode

```lua
Game.SetCinematicMode(enable, showControls)
```
- `enable = true` → freeze hero, hide HUD, prevent input
- `enable = false` → restore hero controls, show HUD
- `showControls` → whether to show the controls overlay after unfreezing

Called **128 times** across all scenes — the most critical cutscene function.

### 2.2 Scene Pause

```lua
Scene.SetPaused(true/false)
```
Pauses/unpauses the entire scene (entities stop, physics stops, but rendering
continues). Used during teleport sequences to prevent the hero from falling
while being repositioned.

### 2.3 Fade Transitions

```lua
Game.FadeOut()     -- fade screen to black (70 calls)
Game.FadeIn()      -- fade screen from black (64 calls)
```

Typical pattern:
```lua
Game.FadeOut()
Program.Wait(0.5)      -- wait for fade to complete
-- ... reposition entities, change camera ...
Game.FadeIn()
Program.Wait(1.0)      -- wait for fade to complete
```

### 2.4 Screen Flash

```lua
Game.Flash()       -- brief white flash (36 calls)
```
Used for impact effects, item pickups, quest completions.

### 2.5 Coroutine Sleep

```lua
Program.Wait(seconds)
```
The **most-called function** in the entire game (1282 calls). Suspends the
coroutine for the given duration. The engine's `ProgramState::Update(dt)`
decrements `sleepTime` each frame; when it reaches zero, `lua_resume()` is
called to continue the script.

---

## 3. Dialogue System

### 3.1 Text Bubbles

```lua
bubble = ShowTextBubbles(id, position, animate, {line1, line2, ...})
```
- `id` — unique string identifier (for later `HideTextBubble`)
- `position` — `Vector3` world position for the bubble
- `animate` — `true` for typewriter effect
- Lines are displayed one at a time, advancing on tap/click

```lua
HideTextBubble(id)
```
Dismisses the text bubble. Called 112 times.

### 3.2 Quest Bubbles

```lua
bubble = ShowQuestBubbles(quest_id, id, position, animate, {lines...})
```
Same as `ShowTextBubbles` but associates the dialogue with a quest ID.
Used for quest-critical dialogue (29 calls).

### 3.3 NPC Dialogue Pattern

```lua
-- NPC has dialogue stored in Properties
local dialogue = Properties.GetProperty(npc, "dialogue")
if dialogue then
    ShowTextBubbles(npc:identifier() .. "_bubble",
        npc:position() + Vector3.New(0, 27, 0), true, {dialogue})
    HideTextBubble(npc:identifier() .. "_bubble")
end
```

---

## 4. Complete Cutscene Template

### 4.1 NPC Conversation Cutscene

```lua
local self, target = ...;

if target:identifier() == "hero" then
    -- 1. Enter cinematic mode
    Game.SetCinematicMode(true, false)
    Scene.SetPaused(false)
    
    -- 2. Position hero
    local hero = Scene.Find("hero")
    hero:setPosition(Scene.Find("questMarker"):position())
    hero:setVelocity(Vector3.New(0, 0, 0))
    Entity.SetFacingDirection(hero, 1)
    
    -- 3. Focus camera on NPC
    Camera.FocusAtShape(Scene.Find("king"),
        Rectangle.New(100, 100, 100, 100))
    
    -- 4. Wait for camera to settle
    Program.Wait(0.3)
    
    -- 5. Fade in
    Game.FadeIn()
    Program.Wait(1.0)
    
    -- 6. NPC dialogue
    ShowQuestBubbles("quest06_florennum_shard",
        self:identifier(),
        Scene.Find("king"):position() + Vector3.New(0, 10, 0),
        true, {
            "You! Where is your master?",
            "I need to speak to him. Now!"
        })
    HideTextBubble(self:identifier())
    
    -- 7. Scene transition
    Game.FadeOut()
    Program.Wait(1.0)
    Game.FadeIn()
    
    -- 8. More dialogue
    ShowQuestBubbles("quest06_florennum_shard",
        self:identifier() .. "_text2",
        Scene.Find("king"):position() + Vector3.New(0, 10, 0),
        true, {
            "He is dead? The Mageblade has been shattered?",
            "Then you must find all the shards.",
            "Now go!"
        })
    HideTextBubble(self:identifier() .. "_text2")
    
    -- 9. Restore camera
    Camera.ResetFocus()
    
    -- 10. Exit cinematic mode
    Game.SetCinematicMode(false, true)
    
    -- 11. Unlock door
    DoorController.Open(Scene.Find("jaildoor"))
    SoundLibrary.PlayEffect("unlock")
    
    -- 12. Update quest state
    Character.SetQuestCompleted("quest05_florennum")
    Character.AddQuest("quest06_florennum_shard")
end
```

### 4.2 Boss Intro Cutscene

```lua
-- OnEnter boss arena trigger
if target:identifier() == "hero" then
    Game.SetCinematicMode(true, false)
    Scene.SetPaused(false)
    
    -- Camera pan to boss
    Camera.FocusAtPoint(Scene.Find("boss"):position())
    Camera.JumpToFocus()
    Program.Wait(0.5)
    
    -- Boss roar
    SoundLibrary.PlayEffect("bossgrowl")
    Camera.Rumble()
    Program.Wait(1.0)
    
    -- Show boss health bar
    Game.Flash()
    
    -- Camera back to hero
    Camera.ResetFocus()
    Game.SetCinematicMode(false, true)
    
    -- Start boss music
    MusicPlayer.PlayMusic("boss", false, false)
end
```

### 4.3 Scene Transition Cutscene

```lua
-- Portal entry
if target:identifier() == "hero" then
    Game.FadeOut()
    Program.Wait(0.5)
    
    -- Teleport hero
    local spawn = Scene.Find("spawn_from_forest_part1")
    target:setPosition(spawn:position())
    target:setVelocity(Vector3.New(0, 0, 0))
    
    -- Change camera
    Camera.FocusAtShape(spawn)
    Camera.JumpToFocus()
    
    Game.FadeIn()
    Program.Wait(1.0)
    
    Camera.ResetFocus()
end
```

---

## 5. Scene-Level OnLoad Scripts

20 of 116 scenes have a root-level `OnLoad` script (protobuf root field 5).
These run once when the scene loads, before any player input.

### Common OnLoad Patterns

**Music setup:**
```lua
MusicPlayer.PlayMusic("forest", false, false)
```

**Day/night lighting:**
```lua
Scene.SetGroupHidden("DirectionalLight_night", true)
Scene.SetGroupHidden("DirectionalLight_day", false)
Scene.OverrideLights("DirectionalLight_day")
```

**Quest-state scene changes:**
```lua
if Character.IsQuestCompleted("quest05_florennum") then
    DoorController.Open(Scene.Find("castle_door"))
    Scene.SetGroupHidden("prison_bars", true)
end
```

**Intro camera:**
```lua
Camera.FocusAtPoint(Vector3.New(0, 200, 0))
Camera.JumpToFocus()
Program.Wait(2.0)
Camera.ResetFocus()
```

---

## 6. Animation During Cutscenes

Entities can still animate during cutscenes (scene is not fully paused):

```lua
-- Play hurt animation on hero
AnimationController.BlendToAnimation(target, 13, 0.1)

-- Set hero velocity for knockback
target:setVelocity(Vector3.New(-70, 220, 0))

-- Animate door opening
TransformController.TranslateBy(door, Vector3.New(0, 150, 0), 2)

-- Rotate object
ModelTransformController.SetRotationSpeed(obj, 720)

-- Scale object
TransformController.ScaleTo(obj, Vector3.New(0, 0, 0), 1.0)
```

---

## 7. Audio During Cutscenes

```lua
MusicPlayer.PlayMusic("bosskill", false, false)  -- start music
MusicPlayer.FadeOut(2.0)                          -- fade out over 2s
SoundLibrary.PlayEffect("unlock")                 -- play sound effect
SoundLibrary.PlayEffect("bossgrowl")
SoundLibrary.PlayEffect("item_get")
```

---

## 8. Program Coroutine Model (Technical)

### How Program.Wait() Works

The `ProgramComponent` contains a `ProgramState` with a Lua coroutine:

```cpp
struct ProgramState {
    lua_State *L;           // Lua state (coroutine)
    SceneObject *sceneObject;
    bool isSuspended;
    float sleepTime;        // decremented each frame
    bool active;
    bool paused;
    bool completed;
    float speedScaling;
};
```

Each frame, `ProgramState::Update(dt)` is called:
1. If `isSuspended`: decrement `sleepTime` by `dt`
2. If `sleepTime <= 0`: call `lua_resume()` to continue the coroutine
3. The coroutine yields when it calls `Program.Wait()`

This means:
- `Program.Wait(1.0)` suspends for ~60 frames at 60fps
- Multiple Program components run concurrently (each has its own coroutine)
- `Scene.SetPaused(true)` freezes all Program components
- `Game.SetCinematicMode(true)` only freezes the hero, not Programs

---

## 9. Modding Notes

- **Always call `Game.SetCinematicMode(false, true)` at the end** — leaving
  cinematic mode on freezes the hero permanently.

- **Always call `Camera.ResetFocus()`** — leaving the camera focused on a
  non-hero entity makes the game unplayable.

- **Use `Program.Wait()` between state changes** — without waits, the engine
  can't render intermediate frames (fades won't animate, cameras won't pan).

- **FadeOut/FadeIn need waits** — `Game.FadeOut()` starts the fade but doesn't
  block. You must `Program.Wait(0.5)` for the fade to complete.

- **Multiple Program components run in parallel** — a boss AI script and a
  cutscene script can run simultaneously. Use `Scene.SetPaused()` if you need
  exclusive control.

- **Dialogue blocks until dismissed** — `ShowTextBubbles()` returns immediately
  but the bubble stays on screen until `HideTextBubble()` is called.
  Plan your dialogue flow accordingly.
