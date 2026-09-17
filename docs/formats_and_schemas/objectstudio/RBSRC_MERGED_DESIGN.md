# rbsrc — the merged design (behaviour + structure + timeline)

> **Status:** design + first implementation slice. Supersedes the `rbsrc` sections
> of `OBJECTSTUDIO_AND_RBSRC.md` and the timeline/rbsrc sections of
> `docs/03-graphy-scene-timeline-backend.md` / `docs/04-rbsrc-format-spec.md`,
> which described two different formats under the same name.
> **UI target:** Ruby GG (`src/ruby/`, Qt6). The legacy ImGui shell is not a
> target for any of this.
> **Ground truth:** `src/tools/lua_api_table.h` (generated) — see §2.

---

## 0. The problem this document settles

The name `.rbsrc` was invented twice, independently:

| | Source | `rbsrc` is | Strength |
|---|---|---|---|
| **A** | `OBJECTSTUDIO_AND_RBSRC.md` (11 Sep) | a **Lua 5.1 superset**: `on load(self) … end` blocks plus an `archetype` structure tier with `->` wiring | ids become compiler-generated; scripts are real Lua, so existing content adopts it unchanged |
| **B** | the timeline/visual-scripter brief | a **recording of a scrubbed ghost**: keyframes `(time, property, value, easing)`, loop regions, action markers `(fn, args, time)`, provenance | a modder *without Lua* can animate by dragging, and the result is re-openable for editing |

They are the same idea at different altitudes. A is "what the object **is**"; B is
"what the object **does over time**". Both compile to the same artifact — a Lua
chunk plus its `Program.Bytes` bytecode — and both need the same three services
(id allocation, API knowledge, byte-exact write-back). Two formats would mean two
parsers, two validators, two write-backs, and a user who has to guess which one
holds their behaviour.

**Decision: one format, three tiers, one compiler.** The timeline is a tier of
`.rbsrc`, not a sibling format. A keyframe track is sugar that the compiler
*lowers* into the Lua loop the game already expects, and because the lowering is
reversible, a compiled script can be reopened as a timeline. That reversibility
is the whole reason the timeline is worth building in the editor rather than
being a one-way export.

---

## 1. What an object's behaviour actually is (measured, not assumed)

From `hiro.scl`, `rlsw.scl`, `credits.scene`, `town_shop.scene` and the corpus
census in `CAVER_BACKEND_STATUS.md`:

* A `Program` is `String` (source, field 1) + `Bytes` (compiled Lua 5.1, field 2)
  + `Name` (field 3). **The engine only runs `Bytes`**: `Caver::Program::LoadIntoState`
  tests field 2 and feeds it to `luaL_loadbuffer`. Field 1 is authoring metadata.
  So `String` and `Bytes` must never be allowed to disagree — that is the bug
  class `scene_set_program_source()` already exists to prevent.
* Handlers hang off **21 slots on 17 owner classes**, not off one
  `ProgramComponent`. `SceneObject.OnLoad` is tag 10 of the object;
  `CollisionShapeComponent.OnCollide` is tag 9 of the collision shape. A scripter
  that only looks for `ProgramComponent` misses most of them.
* The chunk receives its arguments through `...`, one set per hook — e.g.
  `self, target, normal, groundCollision` for `OnCollide`, `self` alone for
  `OnLoad`. This is why a timeline must be *bound to a hook*: the binding
  determines both the owning component/field to write and the names available in
  the preamble.
* `Bytes` is a **32-bit** Lua 5.1 dump stream (`sizeof(size_t) == 4`,
  `lua_Number = double`, little-endian) even in the 64-bit builds, with a
  `1b 4c 75 61 51 00 01 04 04 04 08 00` header. A stock host `luac5.1` emits
  `size_t = 8` and is not merely header-incompatible — it is structurally
  unreadable. The compiler must therefore emit the dump itself (see §5).

---

## 2. The real Lua API, extracted mechanically

The old reconstruction in `src/tools/scene_lua.cpp::register_api()` covers ~60
hand-written functions and is a *subset*. The engine's actual surface is
recoverable exactly, because every class that exposes an API has a
`RegisterLibrary()` / `RegisterProgramLibrary()` method that builds the namespace
string and passes a static `Caver::LibFunction const*` table:

```c
sub_28B2E0((int)v5, "CollisionShape");                            // namespace
Caver::ProgramState::RegisterLibrary(v4 + 24, v5, &off_43E6B4);   // table
```

`Caver::LibFunction` is `{ const char* name; void* fn; }` — 8 bytes on ARM32 — so
reading that table out of the matching binary enumerates the namespace exactly.
`RegisterClass` is the second shape and takes **two** tables (class constructors
and the instance metatable), which is how `Vector3` publishes `New` separately
from `x`/`y`/`z`/`__add`.

```
tools/extract_lua_api.py                    # the tree, * = mutating
tools/extract_lua_api.py --header > src/tools/lua_api_table.h
```

Result against the shipped **v1.4.13 armeabi-v7a** build
(`~/.local/share/swordigo-desktop/engine/v1.4.13/`), located via the IDA dump in
`OpenSwordigo/arm32_13`:

**37 namespaces · 227 entries** — 205 namespace functions, 3 class constructors,
19 instance members.

```
AnimationController(2)  Camera(9)      Character(20)   CharController(4)
CollectableItem(6)      CollisionShape(3) Damage(2)    DoorController(2)
ElevatorController(2)   Entity(5)      EntityController(25) Game(16)
Health(8)               ItemDrop(5)    KeyframeAnimation(6) Light(?) 
Math(4)                 ModelTransformController(4) MusicPlayer(3)
ObjectLinkController(4) OverlayText(1) ParticleEmitter(5) PhysicsObject(6)
Portal(2)               Program(5)     Properties(2)   Rectangle(1+5)
Scene(7)                SceneObject(26) Skill(4)       SnappingMonsterController(1)
SoundLibrary(1)         Spell(2)       TextBubble(3)   Touchable(1)
TransformController(7)  Vector3(2+13)
```

Three findings that change the spec:

1. **`Character` (20 functions) and `Game` (16) are the story/cinematic API** —
   `Character.AddQuest`, `AddItem`, `SetNumCoins`, `RegisterTreasure`,
   `Game.SetCinematicMode`, `ShowNotification`, `FadeIn/FadeOut`, `EnterPortal`.
   None of these appeared in the hand-written reconstruction. A cutscene timeline
   without them is not possible, and they are exactly what an action marker wants.
2. **`EntityController` has 25 functions** — the AI surface (`SetMoveSpeed`,
   `PerformAction`, `Target`, `SetMovementBehavior`, `IdleTime`, `RoamBounds`).
   This is the vocabulary of the behaviour tier.
3. **`SceneObject` has 26**, including the `__index`/`__newindex` pair, which means
   `self:position()` and property-style `self.position` are both routed through
   the same table. The timeline's property tracks should compile to the *setter*
   functions (`setPosition`, `setScaling`, `setRotation`, `setDepth`,
   `setHidden`, `setVelocity`), all of which exist.

**Caveat, recorded deliberately:** the table extraction captures what the game
publishes, not argument arity or types. Arity lives in each function body as
`luaL_checknumber(L, 2)` / `lua_gettop` usage and is a separate, mechanical pass
(§8 M1). Until then the validator is *name-accurate and arity-permissive*: it
catches typos and invented functions, not wrong argument counts. Additional
metamethods set outside `RegisterClass` (e.g. `__eq`, `__div`) may also exist —
`Vector3` shows `__add`/`__sub`/`__mul` in the class table and nothing else, which
is suspicious enough to flag rather than assert.

---

## 3. The merged format

One file, three tiers, shared grammar. Order is free; the compiler is a
multi-pass over the same AST.

```lua
-- fire_spirit.rbsrc                     version 1

archetype fire_spirit                    -- library unit (structure tier)
  scaling 1

  model   "firespray"   as body          -- component declaration, id derived
  glow    as halo       { color #3b5a33, size 27 }
  light   as fire       { type 3, color #9eef00, radius 250 }
  emitter as fume       { max 40 }
    light     -> fire                    -- LightId, resolved from the schema
    particles -> particle.fume           -- ParticleEmitterId

  -- ── timeline tier ──────────────────────────────────────────────────────
  timeline halo
    hook   OnLoad                        -- which slot this recording binds to
    step   0.02                          -- generated tick
    track  scaling                       -- property of the timeline's target
      key  0.000  0.10  linear
      key  1.200  2.00  ease-out
    track  position
      key  0.000  (0, 0, 0)
      key  0.600  (0, 68, -95)
    marker 0.000  Character.SetNumCoins(3)
    marker 0.500  SoundLibrary.PlayEffect("enemy_die")
    loop   0.000 .. 1.200  repeat forever

  -- ── behaviour tier ─────────────────────────────────────────────────────
  on load(self)
    Program.Wait(2.0)
    local spray = Scene.CreateObject("firespray", "fspr", self)
    spray:setRotation(self:rotation())
  end
```

Relationship to ObjectStudio's original proposal: `on <event>(self)` survives
verbatim (§0 table A), and the `archetype`/`->` structure tier survives verbatim.
What is new is `timeline`, which is the only construct that ObjectStudio could not
express and the only one that needs a *visual* editor.

### 3.1 Timeline semantics

| Field | Meaning |
|---|---|
| `hook` | which of the 21 slots the compiled chunk is written into; also selects the `...` preamble |
| `target` | `self` (default) or a component slug in this archetype; a track may also target `child.<slug>` for a spawned object |
| `step` | tick granularity of the generated loop. Default 1/60 s; `blackhole`'s authored cadence is respected when re-opened |
| `track <property>` | `position` \| `rotation` \| `scaling` \| `depth` \| `hidden` \| `velocity`, plus `param.<n>` for `ParticleEmitter.SetParameterAtIndex` |
| `key t v easing` | easing ∈ `linear`, `ease-in`, `ease-out`, `ease-in-out`, `step`, or a bezier handedness triple |
| `marker t call(args)` | a discrete call at a timestamp; `call` is validated against §2 (`mutating` entries are the intended ones) |
| `loop a .. b repeat N` | loop region; `repeat forever` wraps the generated body in `while true do … end` |
| `wait-stride n` | emit `Program.Wait` only every *n*-th tick |

`wait-stride` is not decoration. The `blackhole` object in `rlsw.scl` is a bounded
`for map = 1, N do … end` loop of ~700 iterations that sets `self:scaling()` every
tick but calls `Program.Wait` only occasionally; 700 ticks with a wait on each
would total 0.07 s, whereas the authored stride makes the growth take the intended
time. A timeline that ignores stride compiles to Lua that runs **100× too fast** —
a silent, plausible-looking failure. Recording stride in the format is what makes
the `blackhole` pattern round-trippable at all.

---

## 4. Lowering: timeline tier → Lua

Each track lowers to an interpolated setter call inside one tick loop; each marker
lowers to a tick comparison. The whole timeline is one chunk.

```lua
-- rbsrc v1 · fire_spirit · timeline "halo" · hook OnLoad
-- generated: 61 ticks @ 0.0200s (1.2000s), loop 0.0000..1.2000 forever
local self = ...

local function __ease_out(t) return 1.0 - (1.0 - t) * (1.0 - t) end
local function __seg(t, t0, t1) return math.max(0.0, math.min(1.0, (t - t0) / (t1 - t0))) end

while true do
  for __frame = 0, 60 do
    local __t = __frame * 0.02

    if __t <= 1.2 then
      SceneObject.setScaling(self, 0.1 + (2.0 - 0.1) * __ease_out(__seg(__t, 0.0, 1.2)))
    end

    if __t >= 0.0 and __t < 0.02 then Character.SetNumCoins(3) end
    if __t >= 0.5 and __t < 0.52 then SoundLibrary.PlayEffect("enemy_die") end

    Program.Wait(0.02)
  end
end
```

Design rules the emitter must hold:

1. **`self` comes from `...`**, via the hook's parameter table — never assumed.
2. **Every literal goes through the API validator** (§2) so a typo is a compile
   error at author time rather than a runtime `attempt to call nil` in-game.
3. **Markers are edge-gated** (`__t >= t and __t < t + step`) so a marker fires
   exactly once even though the loop body passes over it — the discrete analogue
   of the `blackhole`/`item1` distinction.
4. **Vector tracks interpolate component-wise** using `Vector3.New` plus the
   instance members confirmed in §2 (`:x()`, `:y()`, `:z()`), or emit scalar
   locals and one construction at the end when the track is dense.
5. **Scaling is the `blackhole` special case**: a pure incremental `for` loop with
   no interpolation table, when the recording is a monotonic ramp — this is a
   recognised *shape*, not a degenerate case, and emitting the authored idiom
   keeps the diff against shipped content small.
6. **`String` always receives generated Lua**, so the file stays self-describing
   even when the timeline is the source of truth.
7. The `.rbsrc` sidecar keeps the keyframes; `String` keeps the lowering. The two
   are reconciled by the provenance block, so re-opening edits the timeline, not
   the generated loop.

---

## 5. Compile pipeline (unchanged shape, now with a real front end)

```
.rbsrc ──parse──▶ AST (3 tiers)
         │
         ├─ resolve   slugs → component ids; marker calls → API table (§2)
         ├─ validate  unknown namespace/function → error; unknown field → raw
         ├─ lower     timeline → tick loop; markers → edge gates; structure → ids
         ├─ emit Lua  behaviour bodies verbatim + generated prelude
         ├─ compile   luaL_loadbuffer + 32-bit lua_dump → Program.Bytes
         └─ write     scl_* / scene_* byte-preserving writers → .scl / .scene
```

The bytecode step is the one place where a stock toolchain cannot be used: the
dump must be the **32-bit** stream described in §1. `filerift::compile_lua_to_bytecode`
already links the vendored Lua 5.1 compiler (`src/sre/base/lua/src`), which is the
right host for a purpose-built `lua51_dump32`; `Caver::Program::LoadIntoState`
in the game is the reference for what it must accept.

---

## 6. The visual scripter in Ruby GG (Qt6)

Graphy's canvas is already mounted as the central widget of `ruby_main_window`
(`ruby_main_window.cpp:286`, sources in `RUBY_GG_SOURCES`). The scripter adds a
second workspace beside it, reusing that renderer rather than a new one:

```
src/ruby/script/
  script_workspace.{h,cpp}      // QSplitter: outliner | viewport+timeline | inspector
  ghost_view.{h,cpp}            // QOpenGLWidget: isolated preview of ONE object,
                                //   seeded at its real scene transform
  timeline_widget.{h,cpp}       // QWidget: track lanes, keyframe handles, playhead,
                                //   loop region handles, marker pins
  track_lane.{h,cpp}            // one property track: keys, easing glyphs, drag
  action_marker_bar.{h,cpp}     // discrete calls, argument editor popup
  program_slot_bar.{h,cpp}      // the 21 hook slots as buttons over the object
  rbsrc_bridge.{h,cpp}          // Qt ⇄ rbsrc::Document; dirty/undo integration
```

Interaction model, and why it differs from a Blueprint graph on purpose:

* Clicking a hook slot on an object spawns the **ghost** — a real model instance in
  `GhostView`, at the object's real `Position`/`Depth`/`Rotation`/`Scaling`.
* Scrubbing the timeline drives the ghost through the same interpolation the
  compiler will emit, so **what you scrub is what compiles**. The preview and the
  code generator read one interpolation function, not two implementations.
* Dragging the gizmo in `GhostView` while a track is armed **records a keyframe**
  at the playhead (the Unreal Sequencer "record" affordance).
* Loop handles and marker pins are first-class draggable objects, matching §3.1 —
  not text the user types.
* Undo goes on the existing per-scene `QUndoStack` via the snapshot command from
  parity task 1.2, so timeline edits are undoable like every other edit.

Right-click an object node in Graphy → **Assign `.rbsrc`** → pick a file → the
compiler re-targets `self` to that object and writes the hook block. One
`.rbsrc` is reusable across objects because it is parametrised by whatever `self`
binds to, which is the property that makes the recording format worth more than a
one-off export.

---

## 7. Live injection

Unchanged in intent from the master plan, and more tractable here than in a normal
modding context because SRE already replaces Swordigo's interpreter at the ELF
level. v1 does **object-level hot rebind**: cancel the object's `ProgramState`,
install the freshly compiled closure, re-`Execute` from the current local time.
Full hot-swap of a coroutine suspended inside `Program.Wait` is explicitly out of
scope for v1. The existing TCP Lua console on `EnginePod` is the transport; no
second emulator, matching the `CAVER_BACKEND_STATUS.md` §2 rule.

---

## 8. Milestones

| # | Deliverable | Acceptance |
|---|---|---|
| **M1** ✅ | `tools/extract_lua_api.py` + generated `lua_api_table.h` | 37 namespaces / 227 entries, regenerable, checked against the shipped 1.4.13 ARM32 binary |
| **M2** | Arity + argument types per API function | parse `luaL_check*`/`lua_gettop` out of the decomp; validator rejects wrong arity |
| **M3** | `rbsrc::Document` + timeline tier + Lua lowering + validator | `blackhole` and `item1` patterns round-trip text → AST → Lua → AST |
| **M4** | Structure tier compile (`archetype`, `->` wiring, id allocation) | fork `crypt_torch`, save, reload shows new ids only |
| **M5** | Bytecode: 32-bit `lua_dump` + write-back into `Program{String,Bytes}` | `Bytes` loads in the game and in `caver::program_host` |
| **M6** | Scripter UI: hook slots, ghost, timeline lanes, marker bar | animate a `blackhole`-shaped ramp by dragging only |
| **M7** | Live rebind over the TCP console | edit a hook, see it take effect without a scene reload |

M3 is the load-bearing milestone: the lowering rules in §4 are what the UI
records into, and getting them wrong is invisible until a script runs at the wrong
speed in-game.

---

## 9. Open questions

1. **Hook parameter tables.** Deriving each of the 21 hooks' `...` preamble from
   the decomp of its `Trigger` call site is required before the generated prelude
   is trustworthy for anything but `OnLoad`.
2. **`ProgramComponent.Trigger` enum values** — still unresolved, and it selects
   which hook a generic `Program` block is.
3. **Non-track properties.** `ParticleEmitter.SetParameterAtIndex` is indexed;
   modelling `param.<n>` as a track needs the `Type → [Parameter]` name table.
4. **`String` vs sidecar authority** when both exist and disagree (ObjectStudio
   §3.5 recommendation is sidecar-authoritative; unchanged).
5. **`Label` as slug storage** — ObjectStudio's own open question, still open.
