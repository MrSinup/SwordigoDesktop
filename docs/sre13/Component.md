# Component (`Caver::Component`)

## Summary

`Component` is the base class of the engine's component system — every
behaviour attached to a `SceneObject` (sprite, physics, health, AI, …)
derives from it (269 RTTI types in `OpenSwordigo/caver_rtti.txt`). It is a
virtual class (vtable at +0, secondary interface vtable at +0x10) whose
instances are heap-allocated and owned by their `SceneObject` (refcounted
via intrusive_ptr: the ComponentCollection keeps `intrusive_ptr<Component>`).
The header's size comment ("sizeof should be 0x28, 0x48") refers to an
older/32-bit estimate and is inconsistent with the header's own fields; the
real 64-bit size is ≥ 0x68 (outlets list at the tail).

The component bindings system (property grid + outlet connections — the
entire game is data-driven) lives here: `ValueForBindedProperty`,
`SetValueForBindedProperty`, `PerformBindedAction`, `AutoConnectOutlets`,
`outletBindingsRoot/outletBindingsCount`.

## Struct layout

64-bit verified pieces (LoadFromProtobufMessage 0x348AC0, ctor-family weak
functions, vtable scan); 32-bit column unverified — **guess** where marked.

| Offset (32-bit) | Offset (64-bit) | Size | Type | Field | Notes |
|---|---|---|---|---|---|
| 0x00 | 0x00 | 0x08 | `void*` | `vtable` | Base vtable (see below). |
| 0x04 | 0x08 | 0x08 | — | `_pad0` | Header's `archSplit(0x04,0x08)`; unresolved. |
| 0x08 | 0x10 | 0x08 | `void*` | `vtable2` | Secondary interface vtable pointer. |
| 0x10 | 0x18 | 0x08 | `void*` | `dat` | A data pointer; set by `LoadFromProtobufMessage`? No — see 0x20. **Unresolved.** |
| 0x18 | 0x20 | 0x08 | `uint64` | `identifier` | Written as a whole qword by `LoadFromProtobufMessage` (`*(this+0x20) = proto[6]`) — so the header's `flags`+`flags2` ints are really **one 64-bit identifier/type word**. |
| 0x20 | 0x28 | 0x08 | `SceneObject*` | `object` | Owning SceneObject (set at attach time). |
| 0x28 | 0x30 | 0x08 | — | `_pad1` | |
| 0x30 | 0x38 | 0x18 | `String` | `label` | 24-byte engine String. `LoadFromProtobufMessage`: `std::string::operator=(this+0x38, proto[7])`. Header's "needs further testing" note resolved: offset 0x38 verified. |
| 0x48 | 0x50 | 0x08 | `void*` | `outletBindingsRoot` | Root of the outlet/binding list (per header; not directly verified — **inferred**). |
| 0x4C | 0x58 | 0x08 | — | `_pad2` | |
| 0x54 | 0x60 | 0x08 | `long` | `outletBindingsCount` | (per header; **inferred**). |

The 32-bit layout (0x28 size claim) is **not** confirmed anywhere; treat the
whole 32-bit column as guess.

## Vtable

Base vtable found at **0x61BFD8** in v1.4.13, verified by applying
relocations to the `.data.rel.ro` image (RELATIVE + ABS64) and cross-checked
against real call sites: `SceneObject::LoadFromProtobufMessage` calls the new
component's vtable+0x20 (Load), `SceneObject::UpdateBounds` calls vtable+0xD8
(HasBounds), +0xE0 (localAABB), +0xE8 (minDepth), +0xF0 (maxDepth),
`SceneObject::SetScene` calls vtable+0xD0 (HandleMessage with message 1/2 =
attached/detached). **33 slots**, RTTI anchor (typeinfo) at +0x110.

> ⚠️ Correction: an earlier draft of this table was misaligned by 8 slots
> (it started at 0x61C018, 0x40 bytes into the real vtable, and labeled
> `Prepare` as slot 0). The table below is the verified layout; the relative
> order of slots 8–31 is what the old table showed, shifted by +0x40.

| Slot | Offset | Target | Signature (demangled) |
|---|---|---|---|
| 0 | +0x00 | 0x32F3B4 | `Component::~Component()` (incomplete-object dtor) |
| 1 | +0x08 | 0x32F3B8 | `Component::~Component()` (deleting dtor) |
| 2 | +0x10 | 0x32F3BC | `Component::Clone() const` |
| 3 | +0x18 | 0x327164 | `Component::InitWithComponent(Component const*)` |
| 4 | +0x20 | 0x348AC0 | `Component::LoadFromProtobufMessage(Proto::Component const&)` |
| 5 | +0x28 | 0x348AE0 | `Component::SaveToProtobufMessage(Proto::Component*) const` |
| 6 | +0x30 | 0x327168 | `Component::ShouldSave()` |
| 7 | +0x38 | 0x32716C | `Component::Process(vector<intrusive_ptr<Component>>&)` |
| 8 | +0x40 | 0x32F3C0 | `Component::Prepare()` |
| 9 | +0x48 | 0x32718C | `Component::FinishLoad()` |
| 10 | +0x50 | 0x327190 | `Component::SetDefaultValues()` |
| 11 | +0x58 | 0x348D80 | `Component::AutoConnectOutlets()` |
| 12 | +0x60 | 0x327194 | `Component::ComponentValuesChanged(Component*)` |
| 13 | +0x68 | 0x348C90 | `Component::UpdateBindingDependencies()` |
| 14 | +0x70 | 0x349B30 | `Component::GetBindings(vector<Binding>*)` |
| 15 | +0x78 | 0x349B34 | `Component::GetEnumValuesForBindedProperty(int, vector<BindingValue>*)` |
| 16 | +0x80 | 0x3495A4 | `Component::ValueForBindedProperty(int)` |
| 17 | +0x88 | 0x34986C | `Component::SetValueForBindedProperty(int, BindingValue const&)` |
| 18 | +0x90 | 0x32E35C | `Component::PerformBindedAction(int)` |
| 19 | +0x98 | 0x3323B8 | `Component::componentCategories() const` |
| 20 | +0xA0 | 0x349B30 | `Component::ImplementsInterface(long)` |
| 21 | +0xA8 | 0x348D58 | `Component::RegisterInterfaces(ComponentManager*)` |
| 22 | +0xB0 | 0x327230 | `Component::ShouldBeUpdatedInEditor() const` |
| 23 | +0xB8 | 0x32C100 | `Component::RequiresUpdate() const` |
| 24 | +0xC0 | 0x327240 | `Component::UpdateWhenPaused() const` |
| 25 | +0xC8 | 0x32C108 | `Component::Update(float)` |
| 26 | +0xD0 | 0x32D510 | `Component::HandleMessage(int, void*)` |
| 27 | +0xD8 | 0x32D518 | `Component::HasBounds() const` |
| 28 | +0xE0 | 0x32D520 | `Component::localAABB() const` |
| 29 | +0xE8 | 0x32D528 | `Component::minDepth() const` |
| 30 | +0xF0 | 0x32D530 | `Component::maxDepth() const` |
| 31 | +0xF8 | 0x338174 | `Component::IsVisualComponent() const` |
| 32 | +0x100 | — | (33rd virtual — `__cxa_pure_virtual` in base; overridden by some components) |
| — | +0x108 | — | vcall/vbase offset (`0xfffffffffffffff0`) |
| — | +0x110 | — | `typeinfo for Caver::Component` (RTTI anchor) |

Notes: slots are read as `(*(vt + 8))` for the intrusive release
(`SceneObject`-style refcount dtor at vtable+8), matching the `FollowObject`
release pattern. Concrete components override subsets of these slots; the
relocation-applied dumps for the 15 documented components (see the per-
component docs) show which slots each class overrides (e.g. `HealthComponent`
overrides Clone/Load/Save/ShouldSave/Prepare/GetBindings/ValueFor/SetValue/
PerformAction/categories/ImplementsInterface/RegisterInterfaces/Update).
The secondary vtable (interface at `vtable2`, +0x10) follows the RTTI anchor
and holds the same bindings methods as `_ZThn16_` thunks (adjusting `this` by
-0x10 for the `IBindable` base); a third vtable block (`_ZThn24_` thunks for
`UpdateWhenPaused`/`Update`, offset -0x18) follows at +0x138+. The header's
`vtable2`/`dat` pair reflects this multiple inheritance.

## Exported functions (selected)

- `Component::LoadFromProtobufMessage(Proto::Component const&)` @ 0x348AC0:
  writes `identifier` (+0x20, proto field 6) and `label` (+0x38, proto
  field 7). **Verified in IDA.**
- `Component::SaveToProtobufMessage(Proto::Component*) const` @ 0x348AE0:
  inverse. **Verified in IDA.**
- `Component::RegisterInterface(ComponentManager*, long)` @ 0x348D5C ·
  `RegisterInterfaces` @ 0x348D58 · `AutoConnectOutlets` @ 0x348D80 ·
  `UpdateBindingDependencies` @ 0x348C90 · `BindingForOutlet(int)` @ 0x349028 ·
  `NewProgramStateForProgram(Program const&)` @ 0x349ABC ·
  `ComponentWithIdentifier(int)` @ 0x349AAC — binding/outlet machinery.
  **Verified addresses, bodies inferred.**
- `component_fetch(SceneObject*, const char*)` (SRE helper, not engine):
  resolves a component by name on a SceneObject (see `hooks/Component.c`).

## Open questions / unresolved offsets

- 0x08 (`_pad0`), 0x18 (`dat`): no access found in the examined functions.
- `outletBindingsRoot/Count` (+0x50/+0x60): header-annotated only.
- The exact 32-bit layout and the claimed 0x28 size need re-verification
  against the armeabi-v7a binary.

## Proposed SRE hooks

- **Live component introspection is the highest-value surface**: expose
  `sre_component_list(obj)` (identifier+label+class name via RTTI) and
  `sre_component_get/set_binding(comp, bindingId, value)` that call the
  real `ValueForBindedProperty`/`SetValueForBindedProperty` through the
  vtable — this is how a scene-editor property grid should talk to the game.
- **Safe**: `label` is a normal String — a host can read it for UI. Writing
  requires `String_create` first.
- **Unsafe to expose raw**: `outletBindingsRoot`/`outletBindingsCount` —
  internal linked-list state; mutating breaks `AutoConnectOutlets` and the
  serializer. Use `RegisterInterface`/`AutoConnectOutlets` instead.
- Component creation: `operator new(0x48)` + ctor (per the header comment)
  is only valid for the base class; every concrete component has its own
  size. Never `operator new` a component from host code without knowing the
  concrete type.