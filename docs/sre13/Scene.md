# Scene

## Summary

`Caver::Scene` is the engine's world container: it owns the embedded script
`ProgramState`, the `ObjectLibrary` (asset/object templates), two `SceneGrid`s
(one for collision/ground, one for visual/gameplay objects), the `LightOverlay`,
two `ComponentManager`s, the ordered sets of `SceneObject`s and
`SceneObjectGroup`s, and the scene's `Camera` reference. `GameSceneController`
(see `GameSceneController.md`) holds a `shared_ptr<Scene>` and drives
`Scene::Update`/`Draw` each frame.

Header: `src/sre/sre13/caver/Scene.h`.

## Struct layout

Verified from the ARM64 v1.4.13 ctor (`0x4F5940`), `SetPaused` (`0x4FB518`),
`SetBounds` (`0x4F7340`), `FinishLoad` (`0x4F70B0`), `RegisterObject` (`0x4F7F9C`),
`Update` (`0x4F81AC`). 64-bit ABI only (no 32-bit offsets attempted — the object
is ~0x400 bytes and the 32-bit layout is not derivable from this binary).

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x00 | 8 | `void*` | `vtable` | `off_6336A0` (see Vtable). |
| 0x08 | 1 | `bool` | `finishedLoading` | Set to 1 at the top of `FinishLoad`. |
| 0x09 | 1 | `bool` | `executeSceneScript` | If set, `FinishLoad` runs the scene `Program` in a child state. |
| 0x0A | 6 | — | (unknown) | Not touched by recovered functions. |
| 0x10 | 4 | `int` | `= 256` | Set to 256 in the ctor; no access found elsewhere. Possibly a light/particle budget. Unresolved. |
| 0x14 | 4 | — | (unknown) | Unresolved. |
| 0x18 | 4 | — | (unknown) | Zeroed in ctor. |
| 0x20 | 4 | `int` | `pauseCount` | **Real offset (header guessed 0x18).** `SetPaused(true)` increments, `SetPaused(false)` decrements. |
| 0x28 | 0x60 | `ProgramState` | `programState` | **Real offset (header guessed 0x20).** Embedded root Lua state; `ProgramState::ProgramState(this+0x28, NULL)` in ctor. |
| 0x88 | 16 | `shared_ptr<Program>` | `sceneProgram` | Zeroed in ctor; `FinishLoad` reads `this+0x88` and runs it in a child state if non-null. |
| 0x98 | 16 | `Rectangle` | `bounds` | `SetBounds` writes 16 bytes here (x, y, width, height); width/height read at +0xA0/+0xA4 and defaulted to `xmmword_259F10` if < 0.01. |
| 0xA8 | 16 | `shared_ptr<ObjectLibrary>` | `objectLibrary` | **Real offset (header guessed 0xA0).** Reset via `boost::shared_ptr<ObjectLibrary>::reset(this+0xA8)`; `DefaultComponents::RegisterAll` wired through it. |
| 0xB8 | 24 | `std::set<SceneObject*>`-like | `objects` | 24-byte red-black tree header (begin-node sentinel at `&this+0xC0`); `FinishLoad` walks it calling `SceneObject::FinishLoad` on each. Node payload read at node+0x38. |
| 0xD0 | 24 | `std::set<SceneObjectGroup*>`-like | `groups` | Same shape, sentinel at `&this+0xD8`; `FinishLoad` walks it calling `SceneObjectGroup::FinishLoad`. |
| 0xE8 | 24 | tree header | `objectsWaitingForActivation` | Self-referential sentinel (`this+0xE8 = &this+0xE8`); `RegisterObjectWaitingForActivation`/`ActivateObject` use it. Inferred. |
| 0x110 | 24 | tree header | (unknown) | Sentinel at `&this+0x118`. Inferred. |
| 0x128 | 24 | tree header | (unknown) | Sentinel at `&this+0x128`. Inferred. |
| 0x140 | 24 | tree header | (unknown) | Sentinel at `&this+0x140`. Inferred. |
| 0x158 | 4 | `int` | `= -1` | Set to -1 in ctor (`this+0x1C8`... see note below). |
| 0x1D0 | ? | `LightOverlay` | `lightOverlay` | Constructed inline at `this+0x1D0`. |
| 0x298 | 4 | `int` | (unknown) | Zeroed in ctor. |
| 0x2B8 | 8 | `void*` | (unknown) | Zeroed in ctor. |
| 0x2D8 | 0x40 | `SceneGrid` | `collisionGrid` | Two layers added in ctor: `(-300,300,250,250)` and `(-3000,-300,1000,1000)`; `SetBounds` propagates the scene rect here. |
| 0x318 | 0x40 | `SceneGrid` | `gameplayGrid` | One layer `(0,0,150,150)`; also receives `SetBounds`. |
| 0x358 | ? | — | (unknown) | Zeroed OWORDs at 0x358..0x37F and byte at 0x37C. |
| 0x37C | 1 | `bool` | (unknown) | Zeroed in ctor. |

Notes on discrepancies vs. the header:
- `pauseCount` is at **0x20**, not 0x18; the embedded `ProgramState` is at **0x28**, not 0x20.
- `objectLibrary` is at **0xA8**, not 0xA0 (16-byte `shared_ptr`).
- The header's `Camera` pointer at `_pad3` could not be pinned: `Scene` references
  the camera through `Camera::AABBForZRange` in `Update`, but the camera field is
  set by the scene view (see `SceneView`/`GameSceneView`), not the ctor. Likely a
  `shared_ptr<Camera>` somewhere in the 0x1C8–0x2B8 region; **unresolved**.

## Vtable (at 0x6336A0)

| Slot | Offset | Target | Notes |
|---|---|---|---|
| 0 | +0x00 | `Scene::~Scene()` (0x4F60D4) | Incomplete-object dtor. |
| 1 | +0x08 | `Scene::~Scene()` (0x4F64A4) | Deleting dtor. |
| 2 | +0x10 | `Scene::RegisterObject(SceneObject*)` (0x4F7F9C) | Called via `SetScene` on a newly attached object. |
| 3 | +0x18 | `Scene::Update(float)` (0x4F81AC) | Per-frame tick. |
| 4 | +0x20 | `Scene::Draw(RenderingContext*, Matrix4 const&)` (0x4F905C) | Draws the scene. |
| 5 | +0x28 | `Scene::AddObject(intrusive_ptr<SceneObject> const&)` (0x4F73A4) | Called through vtable from object attach paths. |
| 6 | +0x30 | `Scene::RemoveObject(intrusive_ptr<SceneObject> const&, bool)` (0x4F7BEC) | — |
| 7 | +0x38 | `Scene::SetBounds(Rectangle const&)` (0x4F7340) | Called through vtable; direct call from GVC load path. |

Slots beyond +0x38 are RTTI data, not virtuals (the blob at 0x6336A0 continues
into typeinfo structures).

## Exported functions

### Scene::Scene()
- Mangled: `_ZN5Caver5SceneC2Ev` (0x4F5940).
- Call sites: `GameSceneController` (scene creation during level load).
- Behavior: sets vtable, `pauseCount=0`, builds the embedded `ProgramState`, the
  four tree containers, two `ComponentManager`s (+0x158? no — +0x158 is `-1`;
  the managers are at **+0x158 and +0x188**, both 0x30 bytes, per ctor:
  `ComponentManager::ComponentManager(this+0x158)` and `(this+0x188)` — see note
  below), the `LightOverlay` at +0x1D0, creates the `ObjectLibrary` shared_ptr,
  registers all default components, and `ManageInterface`s ~20 component
  interfaces (HeroEntity, Portal, Monster, Background, IUpdateable, Light,
  ShadowVolume, Shadow, GroundMesh, WaterMesh, Sprite, Model, WeaponTrail,
  ParticleEmitter, Glow, MagicExplosion, Health, Overlay, Touchable, TextBubble,
  DimensionObject). Adds SceneGrid layers and registers the Lua libraries
  (`Scene::RegisterLibrary`, Sound/Music/SceneObjectLib) into the embedded state.
- Side effects: allocates the ObjectLibrary and a 0x80-byte library container.
- Confidence: **verified**.

> Correction: the two `ComponentManager`s are constructed at `this+0x158` and
> `this+0x188` (0x30 bytes each, ending right at the `LightOverlay` at 0x1D0).
> The `= -1` write at `this+0x1C8` belongs to the LightOverlay ctor region.
> The table above is best-effort; treat 0x158–0x1CF as "two ComponentManagers +
> LightOverlay" rather than individually named fields.

### Scene::SetPaused(bool)
- Mangled: `_ZN5Caver5Scene9SetPausedEb` (0x4FB518).
- Behavior: `pauseCount += (paused ? 1 : -1)`. Reference-counted so nested pause
  requests balance out.
- Confidence: **verified**.

### Scene::SetBounds(Rectangle const&)
- Mangled: `_ZN5Caver5Scene9SetBoundsERKNS_9RectangleE` (0x4F7340).
- Behavior: copies the rectangle to `this+0x98`; if width or height < 0.01,
  replaces with a default rect (`xmmword_259F10`); propagates to both SceneGrids.
- Confidence: **verified**.

### Scene::FinishLoad()
- Mangled: `_ZN5Caver5Scene10FinishLoadEv` (0x4F70B0).
- Behavior: sets `finishedLoading` (byte +0x08); `ObjectLibrary::LoadAllPrograms`
  into the embedded state; if `sceneProgram` (+0x88) exists and
  `executeSceneScript` (byte +0x09) is set, `CreateChildState` → `LoadProgram` →
  `Execute(0)`; then `SceneObject::FinishLoad` on every object and
  `SceneObjectGroup::FinishLoad` on every group (walking the +0xB8/+0xD0 trees).
- Confidence: **verified**.

### Scene::RegisterObject(SceneObject*)
- Mangled: `_ZN5Caver5Scene14RegisterObjectEPNS_11SceneObjectE` (0x4F7F9C).
- Behavior: calls `SceneObject::RegisterComponentInterfaces` on the first
  ComponentManager (this+0x158); if the object's byte at +0x100 is set, also on
  the second manager (this+0x188).
- Confidence: **verified**.

### Scene::Update(float)
- Mangled: `_ZN5Caver5Scene6UpdateEf` (0x4F81AC).
- Behavior: the per-frame driver: collision pair bookkeeping, camera-view-area
  updates (`SceneGrid::UpdateVisibleAreasWithCamera`), culling, `ProgramState::Update`
  on the embedded state, `SceneObject::Update` for active objects, plus
  collision resolution via `CollisionPairSet`/`CollisionShape`.
- Confidence: **verified** (function confirmed; inner details summarized).

### Scene::ObjectWithIdentifier(String*)
- Mangled: `_ZN5Caver5Scene20ObjectWithIdentifierERKNSt6__ndk112basic_string...E` (0x4F6D78).
- Behavior: searches the object set for an object whose identifier string matches.
- Confidence: **inferred** (symbol + callers only).

## Open questions

- The `Camera` field offset is unresolved (header `_pad3`); the camera is wired
  up outside the ctor (scene-view/controller code).
- Three of the four tree containers at 0xE8–0x168 have no recovered accessor;
  names are inferred from the ctor's self-referential sentinel pattern and the
  existence of `RegisterObjectWaitingForActivation`/`ActivateObject`.
- The `int = 256` at +0x10 is unexplained.
- Exact size of the object (≥ 0x380) and the region 0x1C8–0x2D8 between
  LightOverlay and the grids is only partially mapped.

## Proposed SRE hooks

- `g_sre_scene` (raw `Scene*`) is already implicitly available to SRE via
  `GameSceneController`; add `Scene_SetPaused`/`Scene_GetPaused` accessors
  (the pauseCount semantics are reference-counted — expose the count, don't
  write a boolean).
- `Scene_ObjectWithIdentifier` is the safe, supported way to find objects by
  name — prefer it over walking `objects` directly. Walking the +0xB8 tree is
  safe for *read* only; insertion/removal must go through `Scene_AddObject`/
  `Scene_RemoveObject` (vtable slots 5/6) so the grid and component managers
  stay consistent.
- A `Scene_SpawnObjectFromTemplate(Scene*, String* templateId, Vector2* pos)`
  wrapper (via `ObjectLibrary` + `AddObject`) would give SRE level-injection
  without touching internal containers.
- `Scene_SetBounds` is safe to call directly (pure data write + grid update);
  it is the sanctioned way to resize the playable world.