# SceneObject

## Summary

`Caver::SceneObject` is the base class of everything that lives in a `Scene`:
sprites, models, the hero, monsters, particles, doors, and so on. It is a
ref-counted (`boost::intrusive_ptr`, refcount at +0x08), bindable object with a
primary vtable (10 virtuals: dtor, clone, the IBindable property/action
interface, `AddComponent`) and a secondary `IBindable` vtable at +0x10. It owns
a `Scene*` back-pointer (+0x20), a name string (+0x50), a position Vector3
(+0x80), a Z rotation (+0x90), a uniform scale (+0x9C), a local AABB (+0xA4),
an `instanceScaling` float (+0x40), an optional script `shared_ptr<Program>`
(+0x68), a component vector (+0xD0), group membership, and per-instance flags
(+0x18, +0x100, +0x103, +0x104, ...).

Header: `src/sre/sre13/caver/SceneObject.h`.

## Struct layout

Verified from the ARM64 v1.4.13 ctor (`0x4FFDD4`), `LoadFromProtobufMessage`
(`0x501088`), `SetScene` (`0x500C60`), `AddComponent` (`0x500514`),
`setPosition` (`0x5036A8`), `setLocalAABB` (`0x503950`), `WorldMatrix`
(`0x503F28`), `UpdateBounds` (`0x5037CC`), `SetIdentifier` (`0x502CE0`).
64-bit ABI.

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x00 | 8 | `void*` | `vtable` | `off_6339F8` (see Vtable). |
| 0x08 | 4 | `int` | `refCount` | `boost::intrusive_ptr` refcount; `++`/`--` at +8 everywhere. Header correct. |
| 0x0C | 4 | — | (unknown) | Not written by ctor. |
| 0x10 | 8 | `void*` | `vtable2` | Secondary `IBindable` vtable (`&off_633A58`). Header correct. |
| 0x18 | 2 | `uint16` | `= 1` | WORD `1` at 0x18. Enabled/active flag. |
| 0x1A | 6 | — | (unknown) | — |
| 0x20 | 8 | `Scene*` | `scene` | **Real offset (header guessed 0x18).** Set by `SetScene`; read by `SetScene` early-out. |
| 0x28 | 8 | `void*` | (unknown) | Zeroed (ctor OWORD at 0x20..0x2F). Header guessed `parent` here — unconfirmed. |
| 0x30 | 8 | `void*` | (unknown) | Zeroed (ctor OWORD at 0x30..0x3F). |
| 0x38 | 8 | `void*` | (unknown) | Zeroed (same OWORD as 0x30). |
| 0x40 | 4 | `float` | `instanceScaling` | **Real offset (header guessed 0x28).** Default `1.0f`; `SetInstanceScaling` writes here; proto field 14. |
| 0x44 | 1 | `bool` | (unknown) | Zeroed in ctor. |
| 0x45 | 3 | — | (unknown) | — |
| 0x48 | 8 | `void*` | (unknown) | Zeroed (ctor OWORD at 0x48..0x57). |
| 0x50 | 24 | `std::string` | `name` | **Real offset (header guessed 0x30 `Identifier`).** Proto field 1 (`name`); `LoadFromProtobufMessage` copies into +0x50; `SetIdentifier` assigns here (SSO bytes at +0x51, cap at +0x58, data ptr at +0x60). |
| 0x68 | 16 | `shared_ptr<Program>` | `program` | **Real offset (header `_pad4` region).** Optional script; loaded from proto field 17 (0x38-byte `Program` allocation + `Program::LoadFromProtobufMessage`). |
| 0x78 | 8 | `void*` | (unknown) | Zeroed. |
| 0x80 | 4 | `float` | `position.x` | **Real offset (header guessed 0x38/0x50 `Position` Vector3).** `setPosition` writes x@+0x80, y@+0x84 (the +0x80 qword copy in Load is the proto's Vector2 position sub-message, x+y in one qword). |
| 0x84 | 4 | `float` | `position.y` | See above. |
| 0x88 | 4 | `float` | `position.z` | Proto field 24; `WorldMatrix` passes it to `Matrix4::PreTranslate` (z). |
| 0x8C | 4 | — | (unknown) | Zeroed (ctor OWORD at 0x88..0x97). |
| 0x90 | 4 | `float` | `rotation` | Z rotation (radians). Proto field 25; `WorldMatrix` builds `Matrix4::RotationZ(float[36])`. |
| 0x94 | 4 | — | (unknown) | Zeroed in ctor. Possibly rotation.x/rotation.y (unused in 2D). |
| 0x98 | 4 | `float` | (unknown) | Ctor writes NEON D1 = **1.0f**. Not consumed by `WorldMatrix`. Possibly scale.y or a z-scale. |
| 0x9C | 4 | `float` | `scale` | Uniform scale, default **1.0f**; `WorldMatrix` builds diag(scale, scale, scale, 1.0) from float[39]. |
| 0xA0 | 4 | — | (unknown) | Zeroed in ctor. |
| 0xA4 | 16 | `Rectangle` | `localAABB` | **Real offset (header guessed 0x50/0x60).** x,y,w,h; ctor default `xmmword_25A3F0` = (−10, −10, 5, 5); proto field 15; `setLocalAABB`/`UpdateBounds` use it. |
| 0xB4 | 8 | — | (unknown) | Zeroed (qword at 0xB4). |
| 0xBC | 8 | — | (unknown) | Zeroed (qword at 0xBC). |
| 0xC4 | 1 | `bool` | `boundsDirty` | Set to 1 by `setPosition`/`setLocalAABB` when the value actually changes and no bounds update is queued; cleared by `UpdateBounds`→`setLocalAABB` path. |
| 0xC5 | 3 | — | (unknown) | — |
| 0xC8 | 4 | `float` | `minDepth` | Read/written by `UpdateBounds` (float[50]); clamped to component `minDepth` (vtable+0xE8). |
| 0xCC | 4 | `float` | `maxDepth` | Same (float[51], vtable+0xF0). |
| 0xD0 | 24 | `std::vector<Component*>` | `components` | **Real offset.** `{begin@0xD0, end@0xD8, cap@0xE0}`; `SetScene` walks it calling each component's vtable+0xD0 (SetScene, arg 1/2). `RemoveAllComponents` clears it. |
| 0xE8 | 8 | `void*` | (unknown) | Zeroed. |
| 0xF0 | 8 | `void*` | (unknown) | Zeroed. |
| 0xF8 | 1 | `bool` | (unknown) | Zeroed in ctor. |
| 0xFC | 4 | `int` | (unknown) | Zeroed (DWORD at 0xFC). |
| 0x100 | 1 | `bool` | `isVisualObject` | Read by `Scene::RegisterObject` (byte +0x100 decides whether to register in the second ComponentManager). Zeroed by ctor (DWORD at 0xFF). **Inferred.** |
| 0x101 | 2 | — | (unknown) | Zeroed (DWORD at 0xFF overlaps; see ctor `*(_DWORD*)(this+255)=0`). |
| 0x103 | 1 | `bool` | `alwaysActive` | `SetAlwaysActive(bool)`; default `1` in ctor (WORD `1` at 0x103–0x104). |
| 0x104 | 1 | `bool` | `isHidden` | Proto field 16 (`is_hidden`); `SceneObject_isHidden` reads it. Ctor defaults the whole WORD at 0x103 to 1, so this starts "hidden" and is overwritten on load. **Inferred.** |
| 0x105 | 2 | — | (unknown) | — |
| 0x108 | 8 | `void*` | (list node) | `= 0`; with +0x110 forms an intrusive-list node (`0x110 = this` — self-referential; group membership hook). |
| 0x110 | 8 | `void*` | (list node) | `= this` in ctor. `SceneObjectGroup` list hook. |
| 0x118 | 8 | `void*` | (unknown) | Zeroed (byte at 0x118). |
| 0x11C | 8 | `void*` | (unknown) | Zeroed (qword at 0x11C). |
| 0x124 | 8 | `void*` | (unknown) | Zeroed (qword at 0x124). |
| 0x12C | 4 | — | (end) | Total ≥ 0x12C. |

Header discrepancies: `Scene` is at **0x20** (not 0x18); `Scaling` at **0x40**
(not 0x28); `Identifier` string at **0x50** (not 0x30); `Position` is a Vector3
at **0x80** (not a Vector3 at 0x38/0x50 — and not a Vector2 at 0x88 as earlier
recovered docs claimed: `setPosition` writes +0x80/+0x84, `WorldMatrix`
PreTranslates x@+0x80, y@+0x84, z@+0x88); `Rotation` float at **0x90**;
`Scale` float at **0x9C**; `LocalAABB` at **0xA4**; `Hidden` at **0x104** (not
0xB8-ish). The components vector at 0xD0 and flags at 0x100+ were entirely
missing from the header.

## Vtable (primary at 0x6339F8, 10 slots)

| Slot | Offset | Target | Notes |
|---|---|---|---|
| 0 | +0x00 | `SceneObject::~SceneObject()` (0x4FFE7C) | Incomplete-object dtor. |
| 1 | +0x08 | `SceneObject::~SceneObject()` (0x5001C8) | Deleting dtor — the refcount-release path (`AddComponent`, `FinishLoad`, `SetScene` call vtable+8 when refcount hits 0). |
| 2 | +0x10 | `SceneObject::Clone() const` (0x500E88) | Deep copy. |
| 3 | +0x18 | `SceneObject::UpdateBindingDependencies()` (0x505174) | IBindable. |
| 4 | +0x20 | `SceneObject::GetBindings(std::vector<Binding>*)` (0x501B48) | IBindable. |
| 5 | +0x28 | `SceneObject::GetEnumValuesForBindedProperty(int, std::vector<BindingValue>*)` (0x501F70) | IBindable. |
| 6 | +0x30 | `SceneObject::ValueForBindedProperty(int)` (0x5024C8) | IBindable. |
| 7 | +0x38 | `SceneObject::SetValueForBindedProperty(int, BindingValue const&)` (0x502814) | IBindable. |
| 8 | +0x40 | `SceneObject::PerformBindedAction(int)` (0x503128) | IBindable. |
| 9 | +0x48 | `SceneObject::AddComponent(intrusive_ptr<Component> const&)` (0x503FD8) | Called through vtable+0x48 from `LoadFromProtobufMessage`/`AddComponent`. |

The secondary vtable at **0x633A58** (stored at object+0x10) holds the same six
IBindable methods as `_ZThn16_` thunks (adjusting `this` by -0x10 for the
`IBindable` base). The vtable blob continues with `SceneObjectGroup`'s vtable
and RTTI structures.

## Exported functions

### SceneObject::SceneObject()
- Mangled: `_ZN5Caver11SceneObjectC2Ev` (0x4FFDD4).
- Behavior: sets both vtable pointers, `refCount=0`, `instanceScaling=1.0f`
  (+0x40), `alwaysActive=1`/`isHidden=1` (WORD at 0x103), zeroes the strings/
  vectors/AABB, sets localAABB default (−10,−10,5,5) and the +0x98/+0x9C pair
  to 1.0 (NEON D1), sets the +0x110 self-referential group hook.
- Confidence: **verified**.

### SceneObject::SetScene(Scene*)
- Mangled: `_ZN5Caver11SceneObject8SetSceneEPNS_5SceneE` (0x500C60).
- Behavior: if the scene changed: for a non-null scene — stores it at +0x20,
  calls each component's SetScene (vtable+0xD0, arg 1), calls
  `scene->vtable+0x10` (`RegisterObject`), checks for a component needing
  "1" registration (vtable+0xD0 with arg 1), sets byte +0x19, `UpdateBounds`.
  For a null scene — calls each component's SetScene (arg 2), `RemoveFromAllGroups`,
  `SceneObjectLib::PerformCleanupForSceneObject`, clears byte +0x19 and +0x20.
- Side effects: registers/unregisters in the scene, mutates components.
- Confidence: **verified**.

### SceneObject::AddComponent(Component*)
- Mangled: `_ZN5Caver11SceneObject12AddComponentEPNS_9ComponentE` (0x500514).
- Behavior: bumps the component's refcount (+8), calls the virtual
  `AddComponent` (vtable+0x48) with an intrusive_ptr, then releases the
  temporary ref (may delete via the component's vtable+8).
- Confidence: **verified**.

### SceneObject::LoadFromProtobufMessage(Proto::SceneObject const&)
- Mangled: `_ZN5Caver11SceneObject23LoadFromProtobufMessageERKNS_5Proto11SceneObjectE` (0x501088).
- Behavior: name (+0x50); for each proto component — `ComponentFactory::sharedManager`
  → `NewComponentWithClassName` → component's vtable+0x20 (LoadFromProtobuf) →
  add via vtable+0x48; position x/y (+0x80, copied as one qword from the proto's
  Vector2 sub-message), position.z (+0x88, proto field 24), rotation (+0x90,
  proto field 25), `SetInstanceScaling` (+0x40); localAABB (+0xA4); `isHidden`
  (+0x104); optional program (+0x68) with `Program::LoadFromProtobufMessage`.
- Confidence: **verified**.

### Other exported functions
`setPosition(Vector2)` (0x5036A8, weak — writes +0x80/+0x84 and sets
`boundsDirty`@+0xC4, queueing `Scene::RegisterObjectForWorldBoundsUpdate`),
`setLocalAABB(Rectangle)` (0x503950, weak), `UpdateBounds()` (0x5037CC — walks
the components vector, unions component localAABBs (vtable+0xE0) and clamps
min/maxDepth (vtable+0xE8/0xF0), then `setLocalAABB`),
`WorldPointFromLocalPoint`/`LocalPointFromWorldPoint` (0x503BF8/0x503C84),
`VelocityAtWorldPoint` (0x503E28), `ContainsPoint` (0x503D1C),
`HandleMessage(int, void*)` (0x5035B0), `RemoveComponentWithIdentifier(int)`
(0x504538), `RegisterComponentInterfaces(ComponentManager*)` (0x504A68),
`Clone()` (0x500E88), `InitWithTemplate`/`UpdateFromTemplate`/`Templatize`/
`UnlinkFromTemplate` (0x5001EC–0x500A08), `RemoveAllComponents` (0x500DBC),
`SetInstanceScaling(float)` (0x5005E0), `SetAlwaysActive(bool)` (0x505084),
`RegisterForWorldBoundsUpdate()` (0x503B2C), and the IBindable set.
`isHidden()` (0x33FE4C, weak) reads byte +0x104 (see note).

## Open questions

- `isHidden` semantics: the ctor defaults +0x104 to 1 (hidden?) and the load
  overwrites it from the proto — the "hidden by default until told otherwise"
  reading is likely a not-yet-loaded state. Needs a call-site check of
  `isHidden()`.
- The `parent` field (header guess) is unconfirmed; no recovered function
  writes +0x28.
- What occupies +0x8C/+0x94 (zeroed) and +0x98 (defaults to 1.0f): candidates
  are rotation.y/rotation.z and a second scale (or velocity) — none are read by
  the recovered `WorldMatrix`, which uses only rotation@+0x90 and scale@+0x9C.
- The earlier recovered-doc claim of a `velocity` Vector2 at +0x98 (from
  `xmmword_25A3F0`) was wrong: the ctor writes NEON D1 (1.0, 1.0) there and
  `xmmword_25A3F0` (the −10,−10,5,5 AABB) only lands at +0xA4.
- Group membership nodes at +0x108/+0x110 and the exact shape of
  `RemoveFromAllGroups` weren't fully traced.

## Proposed SRE hooks

- `SceneObject_SetInstanceScaling`, `SceneObject_setPosition`,
  `SceneObject_setLocalAABB`, `SceneObject_UpdateBounds`,
  `SceneObject_SetAlwaysActive`, `SceneObject_AddComponent` are all already
  declared in the header and are safe (they route through the real methods).
- A `SceneObject_SetHidden(SceneObject*, bool)` accessor (writes +0x104 via
  the same path `isHidden` reads) would let SRE hide/show any object — the
  natural building block for debug visualizers.
- Teleporting the hero is best done with `setPosition` on the hero
  `SceneObject` + `UpdateBounds` (the GSC already exposes `g_sre_hero`).
- A `SceneObject_WorldMatrix(SceneObject*, float out[16])` export would give
  scripts the exact transform (position@0x80/rotation@0x90/scale@0x9C) without
  reimplementing the matrix math.
- **Do not** write the components vector (+0xD0) directly — always use
  `AddComponent`/`RemoveAllComponents` so the refcount and scene registration
  stay consistent.