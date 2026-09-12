# PortalComponent (`Caver::PortalComponent`)

## Summary

`PortalComponent` is the level-transition object: when the player's shape
touches the portal's trigger shape, the game switches scenes. It holds a
`ComponentOutlet<CollisionShapeComponent>` (`"triggerShape"`), two target
strings, and an activation flag. The two strings are the level-transition
destination — almost certainly `targetScene` and `targetSpawnObject` (which
`GameSceneController` consumes when the portal fires).

Allocation size: **0xC0** (`operator new(0xC0)` in `Create`). Vtable at
**0x61A720**.

## Struct layout

64-bit verified from ctor (`0x33ACAC`), `LoadFromProtobufMessage`
(`0x33AEBC`), `Create`.

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x00 | 0x68 | — | `Component` base | See Component.md. |
| 0x68 | 24 | `ComponentOutlet<CollisionShapeComponent>` | `triggerShape` | Vtable `off_61A8E0` (dumped: `ComponentOutlet<CollisionShapeComponent>` with `Interface`/`identifier`/`setIdentifier`/`target`/`ConnectTo` slots). Bound by ctor as outlet `"triggerShape"`. id@+0x70, target@+0x78. |
| 0x80 | 24 | `std::string` | `targetScene` | Load: `std::string::operator=` from proto field 2 (msg+16). **Inferred: destination scene name.** |
| 0x98 | 24 | `std::string` | `targetSpawn` | Load: from proto field 3 (msg+24). **Inferred: spawn object/portal id.** |
| 0xB0 | 1 | `bool` | `active` | Load from proto byte (msg+32). Ctor zeroes. **Inferred: enabled/active flag.** |
| 0xB1 | 15 | — | (end) | Size 0xC0. |

## Vtable (0x61A720)

33-slot `Component` layout; overridden: dtor, `Clone`, `Load`/`Save`,
`ShouldSave`, `Prepare` (+0x40), bindings (+0x70..+0x90),
`componentCategories` (+0x98), `ImplementsInterface`/`RegisterInterfaces`
(+0xA0/+0xA8), `HandleMessage` (+0xD0). `Update` stays base (portals react
to contact via the scene, not per-frame logic).

## Exported functions

### PortalComponent::Create()
- Mangled: `_ZN5Caver15PortalComponent6CreateEv`
- Behavior: `operator new(0xC0)` + ctor. Size anchor.
- Confidence: **verified**.

### PortalComponent::LoadFromProtobufMessage(Proto::Component const&)
- Mangled: `_ZN5Caver15PortalComponent23LoadFromProtobufMessageERKNS_5Proto9ComponentE` (0x33AEBC).
- Behavior: `Component::LoadFromProtobufMessage`, then the triggerShape
  outlet id (+0x70, with target release at +0x78 when it changes), the two
  destination strings (+0x80/+0x98), and the active byte (+0xB0).
- Side effects: releases a previously-connected trigger shape.
- Confidence: **verified** (offsets); string roles inferred.

## Open questions / unresolved offsets

- Exact string semantics: is +0x98 a spawn *object id* or a second scene
  name (e.g. the "back" destination for two-way portals)? Needs the caller
  in `GameSceneController`'s portal-fire path.
- Whether +0xB0 gates the transition or is written by the transition.

## Proposed SRE hooks

- `PortalComponent_GetDestination(comp)` — read +0x80/+0x98 for a level-map
  debug overlay (draw arrows between portals).
- `PortalComponent_SetDestination(comp, scene, spawn)` — repoint a portal at
  runtime to force a custom level flow (the foundation of a level-select
  menu or sequence-break testing).
- **Safe**: string writes need String helpers; the outlet at +0x68 must keep
  its target consistent with the scene's shape registration.