# 03 — Graphy Scene Backend + Visual Timeline Scripting UX

This is the point of the project. Everything else exists to make this correct.

Scope: the **spatial** graph. `SceneObject`s placed at real `Position`/`Depth`, each
referencing a template or defined inline. It hosts a **motion editor**, not a logic graph.
`02-…` §7 defines the `IGraphBackend` seam this plugs into.

---

## 1. What the user actually does (end-to-end, one pass)

1. Open `town_shop.scene`. The canvas shows objects at their real world coordinates,
   pan/zoom, with `Bounds` as the visible rect and `Group`s as collapsible clusters.
2. Right-click the `item1` object → a menu lists **only the hook slots that exist on that
   object or its components**: `OnLoad` (from the object) and `ProgramComponent.Program`.
   The menu is generated from the decoded file, never hard-coded (`01-…` §3.2).
3. Pick `ProgramComponent.Program`. Graphy:
   - resolves the object's model (`ProgramComponent` has none → falls back to the object's
     `Model` component, else a generic marker glyph),
   - spawns a **ghost** — a real, live instance of that model — in an isolated preview
     canvas, seeded at the object's real `Position`/`Depth`/`Rotation`/`Scaling`,
   - opens the **timeline** at `t = 0` with tracks pre-created for the properties that
     are legal to animate on this object.
4. The user drags the ghost. A **keyframe** lands on each touched track at the playhead
   (auto-key), with the easing selected from the toolbar.
5. The user scrubs to `t = 0.8`, drags again → second keyframe. Graphy draws the
   interpolation between them live on the ghost and as a curve in the track lane.
6. The user marks `t ∈ [0.8, 2.0)` as a **loop region**.
7. The user drops an **action marker** at `t = 0.0`: `CreateShopItem(self, "healingpotion", 50)`.
8. The user hits **Compile & Bind**. Graphy writes `.rbsrc` (or compiles it straight to
   bytecode, `05-…`) and injects into the live game (`06-…`) or back into the asset.
9. Later, the user re-opens the same object. Because the `.rbsrc` retains provenance
   (`object_id`, `hook`, `template`), the recording is restored — the ghost re-seeds, the
   keyframes reappear, the loop region is still there. **This is the property that makes
   the tool a tool and not an exporter.**

Steps 2–9 are the whole feature. Below, each is specified.

---

## 2. Scene backend: building the spatial graph

### 2.1 Additional inputs beyond `02-…`

| Source | Use |
| --- | --- |
| `Scene.Bounds(3)` | initial viewport; the visible world rect |
| `Scene.Group(4)` | clusters; `Locked(6)` prevents ghost dragging of members; `ObjectIdentifier(2)` is a repeated name list using `#N` suffixes (`01-…` §6.6) |
| `Scene.ObjectLibrary(2)` | template resolution + `ImportedLibrary` cross-file dependency edges |
| `SceneObject.Position(4)/Depth(5)/Rotation(6)/Scaling(7)` | world placement |
| `SceneObject.LocalAabb(8)` | node footprint in world space (rect, not min/max) |
| `Scene.OnLoad(5)` | a scene-level hook (fires before objects; see §7.3) |

### 2.2 Template resolution

`TemplateName` on a scene object must be classified (`01-…` §6.5):

```cpp
enum class TemplateRef { Absent, Placeholder, Resolved, Unresolved };

TemplateRef resolve_template(const QString& name, const ObjectLibrary& lib) {
    if (name.isEmpty())                 return TemplateRef::Absent;      // town_shop item1
    if (name == "Template 1")           return TemplateRef::Placeholder; // 90% of town_shop
    if (lib.has_template(name))         return TemplateRef::Resolved;    // item2 -> shop_item
    // follow ImportedLibrary, then resolve cross-file, else:
    return TemplateRef::Unresolved;
}
```

- `Placeholder` → node glyph is a plain rounded rect, and the component list comes **only**
  from the inline `Component` blocks.
- `Resolved` → the component list is `template.components ⊕ inline.components`, with
  inline **overriding** matching `(ClassName, Identifier)` pairs. This is the "semi-linked
  prefab" semantics every scene format ends up with, and it is required to render `item2`
  correctly.
- `Absent` → fully inline (this is `item1`, the `.rbsrc` example target).
- `Unresolved` → `SCL005` warning badge, still renders.

### 2.3 World layout

Nodes are placed by `should-be-world` position, not by graph layout:

```
node_centre_world = ( Position.X, Position.Y + Depth * k_depth_skew )
```

`k_depth_skew` is an editor preference (default `0.35`), used only for *visual* separation
of depth planes — it never affects the value written back. Additional aids:

- `LocalAabb` controls the node body size at `zoom = 1`, so a 60×60 background and a
  20×20 marker are visibly different sizes, matching the game's own collision semantics.
- A **depth ruler** on the right edge, with one lane per distinct `Depth` value (this is
  a 2.5-D scene; there is no `Position.Z`, `01-…` §5.3). Dragging a node into another lane
  changes `Depth` and emits a `Depth` keyframe if recording.
- Groups render as `CommentFrame`s with a tint and a `Locked` padlock; a locked group's
  members reject ghost drags (but still accept keyframe edits).
- Template edges (`Resolved` templates) draw as a dim `GroupEdge` from the instance node
  to the template node — useful, and impossible in the current converter.

---

## 3. Hook slots: the entry point

### 3.1 Hook inventory (measured, not assumed)

The complete set — **21 slots on 17 owner classes** (`01-…` §3.2, derived from
`libswordigo.so`):

| Owner | Field | Tag | Notes |
| --- | --- | --- | --- |
| `Scene` | `OnLoad` | 5 | scene-level; runs before objects |
| `SceneObject` | `OnLoad` | 10 | per-object; the "action marker at t=0" carrier |
| `SceneObjectGroup` | `OnLoad` | 4 | group-level |
| `ObjectLibrary` | `Program` | 5 | library-level |
| `PropertiesComponent` | `OnLoad` | 1 | |
| `CollisionShapeComponent` | `OnCollide` | 9 | receives `self, target, normal, groundCollision` |
| `CollisionShapeComponent` | `OnCollisionEnd` | 10 | |
| `CollisionShapeComponent` | `OnReceiveDamage` | 12 | |
| `GroundPolygonComponent` | `OnCollide` | 6 | |
| `CollectableItemComponent` | `OnCollect` | 3 | |
| `MonsterEntityComponent` | `OnKill` / `OnHurt` | 1 / 2 | |
| `HeroEntityComponent` | `OnItemGet` | 1 | |
| `EntityActionComponent` | `OnActivate` | 1 | |
| `PressureTriggerComponent` | `OnPress` / `OnRelease` | 2 / 3 | |
| `TouchableComponent` | `OnTouch` | 2 | |
| `AttackComponent` | `OnAttack` | 11 | |
| `BreakableObjectComponent` | `OnBreak` | 4 | |
| `SpellComponent` | `OnCast` | 1 | |
| `ProgramComponent` | `Program` | 2 | gated by `ExecuteOnce(1)`/`Enabled(3)`/`Trigger(4)` |

The hook chip appears on the component node (`02-…` §6.1) for exactly the hooks present
in the decoded file, plus one *add* affordance ("+ hook") that creates an empty `Program`
block. Chip state: `absent` (grey), `present` (red, has a `String`), `bound` (green, has
a `.rbsrc` provenance record), `dirty` (amber, `.rbsrc` newer than the asset).

### 3.2 Hook argument signatures

Hooks are Lua varargs. The first arguments are known from observed scripts:

| Hook | Implicit args |
| --- | --- |
| `OnLoad` | `self` |
| `OnCollide` | `self, target, normal, groundCollision` |
| `OnCollisionEnd` | `self, target` |
| `OnReceiveDamage` | `self, damage, source` (inferred) |
| `OnCollect` | `self, collector` (inferred) |
| `OnKill` / `OnHurt` | `self, target` (inferred) |
| `OnTouch` | `self, toucher` (inferred) |
| `OnPress` / `OnRelease` | `self` (inferred) |

The compiler emits `local self = ...;` for hooks whose only binding is `self`, and
`local self, target, normal, groundCollision = ...;` for `OnCollide` — matching
`hiro.scl`'s verbatim preamble. **Marked inferred entries must be confirmed by reading the
engine's Lua binding table before the compiler relies on them** (`05-…` §7).

---

## 4. The ghost: live preview of the real model

### 4.1 Spawn

Clicking a hook chip does:

```
GhostSpec spawn_from(object, hook):
    model_name = first(  object.components
                           .where(class == Model || class == Particle || class == Sprite)
                           .map(payload.Name) )
              ?? template_graphic(object.template_name)
              ?? GENERIC_MARKER
    return GhostSpec {
        model_name,
        seed_transform = TRS(object.Position, object.Depth,
                             object.Rotation, object.Scaling),
        source_object  = object.identifier,
        hook,
        lua_preamble   = hook_signature(hook),     # §3.2
    }
```

Spawn is **non-authoritative**: the ghost never mutates the scene asset. It is an extra
renderable in an isolated preview, and it is destroyed when the timeline closes unless the
user explicitly "bakes to asset".

### 4.2 Preview canvas

- Reuse `src/ruby/viewport/viewport_3d_widget.{h,cpp}` (`Viewport3DWidget : QOpenGLWidget`
  with `RubyGizmo`, `RubyPicking`, `CameraBoundsGizmo`) rather than a new GL context. The
  ghost renders through the same model path the game does
  (`src/ruby/caver/game/game_renderer.cpp` binds `model.materials[i].diffuse_texture_index`
  → `model.textures[...]`), so what the user drags is *the actual asset*.
- The preview camera is seeded from the ghost's seed transform and framed on its
  `LocalAabb`, with an orthographic toggle (the game is 2.5-D — an orthographic view is
  usually what the user wants).
- The preview shows a **ghost trail**: previous keyframe poses at 35% opacity, the
  interpolated current pose solid, and next pose at 35%. This is the single highest-value
  affordance for a motion editor and neither reference engine shows it by default.
- A **ground grid** at `Depth = 0` and a **world-space crosshair** at the seed origin make
  absolute vs relative motion legible.

### 4.3 Dragging semantics

| Input | Effect |
| --- | --- |
| Drag body | translate X/Y; writes `position` track |
| Drag vertical handle (or `R` modifier) | `Depth`; writes `depth` track |
| Drag rotation ring (or `E`) | `rotation` (radians); writes `rotation` track |
| Drag corner (or `S`) | uniform `scaling`; writes `scaling` track |
| `Shift` while dragging | axis lock |
| `Ctrl` while dragging | snap to 1 world unit (else free) |
| `Alt` + drag a keyframe | break handle, edit easing |

Auto-key is **on by default** (Unreal Sequencer's default for a recording pass, and the
only behaviour that makes scrub-drag-drop feel like a motion editor). With auto-key off,
dragging moves the ghost's *offset* (a preview-only nudge, exactly like Unreal's
`ISequencer` "preview" mode) and a `K` press commits.

### 4.4 The scrub/record contract

Borrowed directly from `ISequencer` (`08-…` §2.1):

```
begin_scrub():                       # SButtonUp on the timeline or first mouse-move of a drag
    OnBeginScrubbing()               # stops the game-loop evaluation, starts a transaction
    evaluate_paused = true
per_frame():
    SetLocalTime(t, SnapTimeMode::STM_None, /*bEvaluate=*/false)
    apply_tracks_to_ghost(t)         # pure function of the timeline
end_scrub():
    OnEndScrubbing()                 # one undo entry, resume evaluation
```

The `bEvaluate = false` batching matters: without it, a 60 Hz drag would re-apply the
whole track set (and, in Scene mode, re-tick any live preview) on every mouse-move.
`SetLocalTime(t, snap, false)` then `apply_tracks_to_ghost(t)` is the correct decomposition.

---

## 5. Timeline data model

### 5.1 Structures

```cpp
namespace ruby::graph::timeline {

using Seconds = double;

enum class PropertyKind {          // what a track drives
    PositionX, PositionY, Depth,   // 2.5-D transform  (01-§5.3)
    Rotation, Scaling,
    BoolField, IntField, FloatField,   // arbitrary payload field on the component
    AssetName,                         // swap a TextureName/Name
    Visibility,
};

enum class Interp { Step, Linear, Cubic, EaseIn, EaseOut, EaseInOut };
enum class Extrapolation { Clamp, Loop, PingPong };

struct Key {
    Seconds time = 0.0;
    variant<double, int, bool, QString> value;
    Interp interp = Interp::Linear;
    // tangent handles, in value-time space; mirrored unless broken
    double in_slope = 0.0, out_slope = 0.0;
    bool handle_broken = false;
    bool operator<(const Key& k) const { return time < k.time; }
};

struct Track {
    PropertyKind kind;
    QString target_field;                        // e.g. "Transparency", "MaxParticles"
    int target_component_id = 0;                 // component-scoped, like refs (02-§5.1)
    std::vector<Key> keys;                       // sorted by time, invariant
    Extrapolation extrapolation = Extrapolation::Clamp;
    bool muted = false;
    Seconds quantization = 0.0;                  // 0 = continuous
};

struct LoopRegion {
    Seconds begin = 0.0, end = 0.0;
    bool infinite = true;
    int repeats = 0;                             // used when infinite == false
};

struct ActionMarker {                            // Godot Animation::MarkerKey analogue
    Seconds time = 0.0;
    QString fn;                                  // "CreateShopItem"
    QString receiver;                            // "self" | "Scene" | "<object id>"
    std::vector<Argument> args;                  // typed, so codegen is exact
    bool once = true;                            // ExecuteOnce-style
    QString comment;
    bool enabled = true;
};

struct Argument {
    enum class Kind { Number, String, Bool, SelfRef, ObjectRef, ComponentRef } kind;
    double number = 0.0;
    QString text;
};

struct Recording {                               // == the .rbsrc document (04-…)
    QString provenance_object;                   // scene object identifier (with #N)
    QString provenance_hook;                     // "OnCollide"
    QString provenance_owner_class;              // "CollisionShape"
    int     provenance_owner_id = 0;
    QString provenance_template;                 // resolved template, or empty
    QString source_asset;                        // town_shop.scene
    std::vector<Track> tracks;
    std::vector<LoopRegion> loops;
    std::vector<ActionMarker> markers;
    Seconds duration = 0.0;
    double  frame_quantum = 1.0 / 60.0;          // codegen tick size (05-§4)
};

} // namespace
```

### 5.2 Invariants

1. `track.keys` is sorted and de-duplicated by time (`|Δt| < 1e-6` → replace, and the
   *new* value wins).
2. A `Recording` is **self-describing**: it names the object and hook it was recorded
   against, but nothing binds it there permanently (`01-…` §5 of `00-…`: reusability).
3. Values are stored in **world units and seconds**, never in frames. Frame snapping is a
   view concern (`frame_quantum` at codegen only).
4. Loop regions may not overlap. Creating an overlapping region truncates the previous.
5. An `ActionMarker` with `once = true` compiles to a call guarded so it fires exactly once
   per activation — matching `ProgramComponent.ExecuteOnce` and the `item1` pattern.
6. Every track records `target_component_id`, because `Identifier` is object-scoped
   (`02-…` §5.1) and a `.rbsrc` must survive being rebound to a differently-numbered
   object. On bind, the compiler re-resolves by `ClassName` + ordinal, not by raw id.

### 5.3 Track set offered for a given target

Tracks are generated from the target's actual schema so the user cannot author nonsense:

| Target | Transform tracks | Field tracks | Marker namespace |
| --- | --- | --- | --- |
| `SceneObject` / `ProgramComponent` | PosX, PosY, Depth, Rotation, Scaling, Visibility | `ProgramComponent.Enabled`, `ExecuteOnce`, `Trigger` | world + object fns |
| `CollisionShapeComponent` | Pos/Depth/Rot/Scale (via owner) | `Collides`, `ReceivesDamage`, `MinDepth`, `MaxDepth`, `Enabled`, `SpecialType`, `UnsafeGround`, `Friction` | `CollisionShape.*` |
| `PhysicsObjectComponent` | as owner | `PhysicsEnabled`, `GravityMagnitude`, `MaxSpeed`, `AirDeceleration`, `GroundDeceleration`, `AllowRotation` | `PhysicsObject.*` |
| `ModelComponent` | as owner | `EmissionFactor`, `Transparent`, `YRotation`, `XRotation`, `DiffuseColor`, `ShatterColor`, `Name` (AssetName) | `Model.*` |
| `ParticleEmitterComponent` | as owner | `MaxParticles`, `DestroyWhenFinished`, `LocalSystem`, `ParticleEmitter.Parameter[n]` (indexed) | `ParticleEmitter.*` |
| `SimpleGlowComponent` | as owner | `PulseTime`, `PulseAmount`, `Size`, `NumSegments`, `Depth` | `SimpleGlow.*` |
| `LightComponent` | as owner | `Intensity`, `Radius`, `Type`, `Color` | `Light.*` |
| `CollectableItemComponent` | as owner | `RequiresPickup`, `Type`, `Value` | `CreateShopItem` etc. |

`ParticleEmitter.Parameter[n]` is a **repeated float** (14 instances on one `blackhole`
emitter, `01-…` §6.4), so the indexed form is required or the user will edit the wrong
channel. This is a concrete case where a naive graph/timeline model loses data.

---

## 6. Timeline panel

Layout is a direct analogue of Godot's `AnimationTrackEditor` (`08-…` §2.2):

```
┌ TimelinePanel (VBox) ───────────────────────────────────────────────────────┐
│ ┌ toolbar ────────────────────────────────────────────────────────────────┐ │
│ │ [▶/⏸] [⏮] [◼]  t = 0.850 s  │ snap 1/60 ▾ │ interp Linear ▾ │ [auto-key]│ │
│ │ [add track ▾] [add marker ▾] [loop region] │ zoom ─────●─────           │ │
│ └─────────────────────────────────────────────────────────────────────────┘ │
│ ┌ Ruler (AnimationTimelineEdit analogue, Range) ──────────────────────────┐ │
│ │ 0.0      0.5      1.0      1.5      2.0            [loop][loop]         │ │
│ └─────────────────────────────────────────────────────────────────────────┘ │
│ ┌ TrackArea (VBox of TrackEdit rows, inside a ScrollContainer) ───────────┐ │
│ │ ▸ Object: item1                       [lock][mute]                      │ │
│ │   Position X   ◆───────────◇────────────────╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌         │ │
│ │   Position Y   ◆───────────◇                                            │ │
│ │   Depth        ◆           ◇                                            │ │
│ │   Rotation     ◆───────────◇                                            │ │
│ │   Scaling      ◆───────────────────────────────────                     │ │
│ │ ▸ Markers      ▲CreateShopItem(self,"healingpotion",50)                 │ │
│ └─────────────────────────────────────────────────────────────────────────┘ │
│ ┌ CurveArea (optional, AnimationBezierTrackEdit analogue) ────────────────┐ │
│ │ selected track's value-vs-time curve with draggable tangent handles     │ │
│ └─────────────────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────────────────┘
```

Notes:

- **Reuse `src/ruby/panels/animation_control_bar.{h,cpp}`.** It already exposes
  `set_frame_count(int)`, `set_frame(float)`, `frameChanged(float)`,
  `playingChanged(bool)` with a `QSlider` + `QTimer` transport and was explicitly written
  to be "deliberately independent from the viewport so it can be docked, floated, or
  reused by future scene and animation editors". This is that future editor. Upgrade it
  rather than forking it: add `set_duration(Seconds)`, `set_hook_label(QString)`,
  `set_markers(...)`, and switch its internal unit from frames to seconds with a
  `frame_quantum` display option.
- The **marker lane** is a dedicated row (Godot's `AnimationMarkerEdit : Control`).
  Markers are drag-editable in time, click-to-edit arguments, and show a chip glyph on
  the ruler.
- **Loop regions** are drawn on the ruler as brackets with a repeat count; dragging an
  edge resizes. Unreal calls this a "sub-sequence / looping section"; ours is a
  compile-time concept (`05-…` §4.5) rather than a runtime one, because `.rbsrc` must
  compile to `for`/`Program.Wait`.
- **Selection model**: `SelectedKey{int track, int key}` with `KeyInfo{float pos}` — the
  same shape as Godot's (`animation_track_editor.h:760,766`). Multi-select, box-select,
  group drag with `moving_selection_offset`, and a `RBMap<SelectedKey, KeyInfo>`.
- **Clipboard**: track clipboard and key clipboard (Godot has both,
  `animation_track_editor.h:866,885`). Copying keys between tracks of compatible
  `PropertyKind` is a huge authoring accelerator.

---

## 7. Action markers

### 7.1 Catalog

Markers are typed calls. The catalog is generated from the engine's Lua API surface, not
typed by the user (the project already dumps this: `lua_api_dump.txt`,
`tools/ruby_cli.cpp`'s `lua_api_dump`, and `sre_mini_api.c`).

Observed/known entries:

| Function | Args | Evidence |
| --- | --- | --- |
| `CreateShopItem` | `(self, item_name, price, [unlock_flag])` | `town_shop.scene` item1/item2/item3 |
| `Program.Wait` | `(seconds)` | everywhere; **emitted by codegen, never user-placed** |
| `Program.Execute` | `(self, program_index)` | `rlsw.scl` portal script (`Program.Execute(self, 3)`) |
| `SoundLibrary.PlayEffect` | `(name)` | `credits.scene` (`SoundLibrary.PlayEffect("insect_die2")`) |
| `PhysicsObject.SetEnabled` | `(obj, bool)` | `rlsw.scl` deep-scaling script |
| `TransformController.ScaleTo` | `(obj, scale, duration)` | `rlsw.scl` portal script |
| `CollisionShape.NewThread` | `(fn, obj)` | `rlsw.scl` `ObjectHasPhysics` |
| `CollisionShape.SetEnabled` | `(self, id, bool)` | `graphy.cpp` demo graph |
| `PhysicsObject.IsEnabled` | `(obj)` | `rlsw.scl` |
| `Scene.CreateObject` | `(template, name, parent)` | `rlsw.scl` portal effect |
| `Character.HasItem` / `HasSceneFlag` / `AddSceneFlag` | | `rlsw.scl` |
| `Scene.OverrideLights` | | `rlsw.scl` blackhole |
| `self:setScaling` / `self:scaling` / `self:setPosition` / `self:position` / `self:destroy` / `self:identifier` | | everywhere |

The marker picker searches this catalog with typed argument widgets per signature, so the
user never types Lua.

### 7.2 The two real patterns, resolved

**Pattern A — "single action at t = 0, no motion"** (`town_shop.scene` `item1`, verbatim):

```lua
local self = ...;

CreateShopItem(self, "healingpotion", 50);
```

Recorded as: `duration = 0`, no tracks, one `ActionMarker{ t=0, fn="CreateShopItem",
receiver="self", args=[String "healingpotion", Number 50], once=true }`. Compiled back to
exactly that text (`05-…` §4.7).

**Pattern B — "keyframe/scrub-to-loop", not hand-authored control flow**
(`rlsw.scl` `blackhole` `OnLoad`, verbatim):

```lua
local self = ...;
self.friendly = self.friendly or "neutral"
self.exper = self.exper or 700
…
local scale = self:scaling()
self:setScaling(0.01)

for map = 1, (1e+200^2) do
    if (map % 10) == 0 then
        Program.Wait(0.0001)
    end
    if self.exper > 0 then
        self.exper = self.exper - 1
        self:setScaling(self:scaling() + (1/100))
    end
end
```

Read as a recording: `scaling` starts at `0.01`, ramps by `+0.01` per tick, for
`exper = 700` ticks, with `Program.Wait(0.0001)` every 10th tick. **The wait cadence is
strided, not per-tick:** only `700 / 10 = 70` waits occur, so the elapsed time is
`70 × 0.0001 = 0.007 s` (this is easy to get wrong by a factor of 100 — the `% 10` guard
is load-bearing). So the recording is **a linear keyframe run from `0.01` at `t = 0` to
`7.01` at `t = 0.007 s`**, plus a saturating loop guard on a *data* counter (`exper`)
rather than a time counter. Faithful re-emission therefore needs a `wait_stride` knob
(`04-…` §3), otherwise the compiler would emit 700 yields instead of 70 and change the
object's time-slice cost.

So the honest recording is:

```yaml
tracks:
  - property: Scaling
    keys: [ {t: 0.000, v: 0.01, interp: Linear},
            {t: 0.007, v: 7.01, interp: Linear} ]
loops:
  - { begin: 0.0, end: 0.007, infinite: true }
markers: []
state:
  counter_exper: { init: 700, per_tick: -1, gate: "> 0" }   # see §7.3
wait_stride: 10           # Program.Wait every 10th tick, q = 1e-5  =>  1e-4 per wait
```

**This is the inversion the brief asks for**: rather than sampling a curve into N frames,
we read the existing loop as a curve, and the compiler re-emits a `for`/`Program.Wait`
tick loop from it (`05-…` §4.8). The `blackhole` file is the regression fixture for that
inversion — parse it, produce the recording, compile the recording, and require the
compiled result to be behaviourally equivalent (same scale trajectory, same tick count).

### 7.3 Data-state (the part keyframes cannot express)

`blackhole`'s loop is bounded by `self.exper`, not by time. A pure keyframe model cannot
express that, and silently converting it to a time bound would *change the film*.
Therefore a `Recording` may carry a small **`state` block**: named counters/handles with
`init`, `per_tick` delta, and a `gate` expression, compiled to the same `if self.exper > 0
then … end` shape. This is original — neither Unreal's `FMovieSceneChannel` nor Godot's
`Animation::Track` has a notion of a *data* bound on an animation loop, because neither
targets a coroutine language where that is the idiomatic authoring style.

`.rbsrc` serialises `state` as first-class (see `04-…` §4.5), so it round-trips.

---

## 8. Live preview rendering

**Two preview surfaces, deliberately distinct:**

1. **In-canvas mini-preview**: the ghost is drawn as a 2-D projection inside the Graphy
   canvas at its world position, using the model's `LocalAabb` silhouette plus a small
   sprite thumbnail. Cheap, always visible, and shows the *scene* relationship.
2. **Isolated 3-D preview** (the "preview canvas" of §4.2): a dockable `Viewport3DWidget`
   showing the ghost alone, at high fidelity, with trails and grid.

Both are driven by one `GhostInstance`:

```cpp
struct GhostInstance {
    QString model_name;
    TransformTRS seed, current;              // current = f(t) from tracks
    std::vector<TransformTRS> key_poses;     // for trails
    Seconds t = 0.0;
    // Filled by apply_tracks_to_ghost():
    bool  field_bool[8];  int field_int[8];  double field_float[8];
    QString field_asset[4];
};
```

`apply_tracks_to_ghost(t)` is **pure**: it evaluates `Recording.tracks` at `t` and writes
into the ghost; it never touches the asset, the scene, or the game. That purity is what
makes `bEvaluate = false` scrubbing cheap and makes the ghost reproducible frame-for-frame
from a `.rbsrc` alone (a property that is testable, see §10).

For previewing against the *live* scene, `06-…` §5 defines an optional read-only "sample
the live object's transform" channel so a ghost can be seeded from a running game instead
of from the asset.

---

## 9. Undo/redo, persistence, provenance

- One `QUndoStack` for the timeline. Every mutating operation is a command:
  `AddKeyCommand`, `MoveKeysCommand`, `SetInterpCommand`, `AddMarkerCommand`,
  `EditLoopRegionCommand`, `BindRecordingCommand`. A scrub is a single command via
  `begin_scrub`/`end_scrub` (§4.4).
- `.rbsrc` file per recording, default path
  `<assets>/rbsrc/<scene>/<object>__<hook>.rbsrc`, with the provenance block inside so
  moving the file does not lose the binding (`04-…` §3).
- The graph node keeps a `rbsrc_path` attribute per hook chip, so reopening the scene
  restores chips as `bound` without scanning the directory.
- Rebind with a different target: Graphy re-resolves `target_component_id` by
  `ClassName` + ordinal and re-runs codegen; the `.rbsrc` is never mutated by a rebind.

---

## 10. Acceptance criteria and tests

| # | Criterion |
| --- | --- |
| B1 | Opening `town_shop.scene` produces hook chips for exactly `item1`/`item2`/`item3`'s `ProgramComponent.Program` and `itemcheck`'s hooks; no chip for a hook absent from the file |
| B2 | `item1` + "assign new `.rbsrc`" + one `CreateShopItem` marker + Compile produces the identical `String` body as the shipping asset (after normalising the CRLF preamble) |
| B3 | Parsing `rlsw.scl`'s `blackhole` `OnLoad` yields a 2-key `Scaling` track spanning `[0.0, 0.007]`, a `state.counter_exper` block with `init 700`, and `wait_stride 10`; recompiling it reproduces the same trajectory and the same 70 yield points |
| B4 | Dragging a ghost with auto-key produces exactly one key per touched track per drag (not one per mouse-move) |
| B5 | `apply_tracks_to_ghost(t)` is deterministic: same `.rbsrc`, same `t` → identical `GhostInstance` |
| B6 | Loop-region edges are only draggable at edges; overlapping creation truncates the previous region |
| B7 | A `.rbsrc` recorded on `item1` rebinds to `item2` with a different component numbering and compiles to correct Lua |
| B8 | Non-transform tracks (`MaxParticles`, `EmissionFactor`, `ParticleEmitter.Parameter[7]`) round-trip through `.rbsrc` without loss |
| B9 | Locked groups reject ghost drags but accept keyframe edits |
| B10 | `TemplateRef::Placeholder` objects render inline components only; `Resolved` objects render `template ⊕ inline` with inline winning |

---

## 11. Original design decisions (flagged, with justification)

1. **Ghost trails + previous/next pose ghosting.** Neither reference engine ships this by
   default at the object level; both require a Curve Editor to see motion. For a 2.5-D
   scene with 85 objects, pose trails in the preview are the difference between usable and
   unusable. *Original.*
2. **`state` block for data-bounded loops (§7.3).** Required to represent `blackhole`
   faithfully. *Original.*
3. **Loop region as a *compile-time* concept.** `Program.Wait` in a `for` is the engine's
   own idiom; a runtime loop construct does not exist in the script API we observed.
   Modelling the loop region as codegen input (rather than a runtime playback setting, as
   Unreal does) is what makes `.rbsrc` compile to idiomatic Swordigo. *Original.*
4. **Track generation constrained by schema.** Unreal lets you key almost any UPROPERTY;
   we restrict to the fields the target actually has, because the target is a decoded
   binary and unkeyable fields are a false promise. *Original, and a direct consequence of
   working over a decoded asset rather than in-editor objects.*
5. **`target_component_id` resolved by class + ordinal on bind.** Necessary because
   `Component.Identifier` is object-scoped and reused (`02-…` §5.1). *Original.*
