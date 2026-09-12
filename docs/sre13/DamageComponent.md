# DamageComponent

## Summary

`Caver::DamageComponent` is the engine's damage-dealing half: it defines a
damage range, type, and special type, and queues `DamageImpact` records
(source, victim, point, impulse, amount) against the owner's objects. Impacts
are delivered by `DeliverImpact`, which fans `HandleMessage` calls out to the
victim (message 14) and the attacker's objects (message 13) so every listener
(health, physics knockback, hit-sparks, sounds) reacts.

Header: none yet — see `Component.md` for the shared base.

## Struct layout

Verified from the ARM64 v1.4.13 ctor (`0x361B38`), `LoadFromProtobufMessage`
(`0x361C1C`) and `AddPotentialImpact` (`0x363004`). 64-bit ABI.

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x68 | 24 | `std::vector<DamageImpact>` | `potentialImpacts` | `{begin@0x68, end@0x70, cap@0x78}`. Pushed by `AddPotentialImpact` when not immediately delivering. |
| 0x80 | 24 | `std::vector<DamageImpact>` | `deliveredImpacts` | `{begin@0x80, end@0x88, cap@0x90}`. Used when the component delivers impacts synchronously. |
| 0x98 | 1 | `bool` | `deliverAsync` | Load writes +0x98 from proto byte (`Message+40`). **Inferred** — gates which vector is used. |
| 0x9C | 4 | `int` | `damageMin` | Load: `this+39`. |
| 0xA0 | 4 | `int` | `damageMax` | Load: `this+40` (proto `max - min`). |
| 0xA4 | 4 | `int` | `damageMin2` | Load: `this+41` = same min. |
| 0xA8 | 4 | `int` | `damageMax2` | Load: `this+42` = same max. |
| 0xAC | 4 | `int` | `damageType` | Load: `this+43` via `DamageTypeFromProtobufValue`. |
| 0xB0 | 4 | `int` | `specialDamageType` | Load: `this+44` via `SpecialDamageTypeFromProtobufValue`. |
| 0xB4 | 1 | `bool` | `enabled` | Default 1 (ctor +0xB4). |
| 0xB8 | 8 | `void*` | (unknown) | Zeroed (this+23). |
| 0xC0 | 4 | `float` | `damageMultiplier` | Default 1.0f (ctor `this+24`); Load: `this+48` from proto field. |
| 0xC4 | 4 | `float` | (unknown) | Load: `this+49` from proto (`Message+36`). |
| 0xC8 | 1 | `bool` | `isProjectile` | Load: +0xC8 from proto byte (`Message+28`). Ctor WORD `0x100` at 0xC8 = `{0x00, 0x01}`. |
| 0xC9 | 1 | `bool` | `hasDeliveredImpacts` | Default 1. `AddPotentialImpact` checks +0xC9 to decide whether to push into `deliveredImpacts` (0x80). |
| 0xCC | 4 | — | (end) | Total ≈ 0xCC. |

### DamageImpact (heap record, 0x40 bytes)
`{ source `SceneObject*` @0x00, victim `intrusive_ptr<SceneObject>` @0x08,
point `Vector2` @0x10, impulse `Vector2` @0x18, amount `float`/int @0x20,
flags @0x24, payload @0x28 (16 bytes) }` — written in `AddPotentialImpact`.

## Vtable (at 0x61DE80)

Overrides only (base layout in `Component.md`):

| Slot | Offset | Target |
|---|---|---|
| 0 | +0x00 | `~DamageComponent()` (0x36D4D4→0x36D4D4) |
| 1 | +0x08 | `~DamageComponent()` (0x36D4EC) |
| 2 | +0x10 | `Clone()` |
| 4 | +0x20 | `LoadFromProtobufMessage` (0x361C1C) |
| 5 | +0x28 | `SaveToProtobufMessage` (0x361CD8) |
| 6 | +0x30 | `ShouldSave()` |
| 8 | +0x40 | `Prepare()` (0x362E94) |
| 14–18 | +0x70.. | IBindable accessors (0x361E98–0x362E8C) |
| 25 | +0xC8 | `Update(float)` — not overridden (base `Component::Update`) |
| 26 | +0xD0 | `HandleMessage(int, void*)` |

## Exported functions

### DamageComponent::AddPotentialImpact(...)
- Mangled: `_ZN5Caver15DamageComponent17AddPotentialImpact...` (0x363004).
- Behavior: builds a `DamageImpact` (source = this's owner, victim/point/impulse from args, with a squared-distance check against the attacker's position at owner+0x80). Pushes into `potentialImpacts` (0x68), or — when the caller asks to deliver now and +0xC9 is set — into `deliveredImpacts` (0x80). Manages the victim's intrusive refcount.
- Confidence: **verified**.

### DamageComponent::DeliverImpact(DamageImpact*)
- Mangled: `_ZN5Caver15DamageComponent14DeliverImpact...` (0x363270).
- Behavior: `victim->HandleMessage(14, impact)`; `owner(+0x28)->HandleMessage(13, impact)`; if `owner+0x30` non-null, `HandleMessage(13, impact)` on it too.
- Confidence: **verified**.

### DamageComponent::LoadFromProtobufMessage
- Mangled: `_ZN5Caver15DamageComponent23LoadFromProtobufMessageERKNS_5Proto9ComponentE` (0x361C1C).
- Behavior: fills the min/max/type/special-type/flag fields above via the proto extension; converts enums through `DamageTypeFromProtobufValue`/`SpecialDamageTypeFromProtobufValue`.
- Confidence: **verified**.

## Open questions

- `IsImpactBlocked` (0x3631F0) and the second `damageMin2/Max2` copies are unexamined; the ± pair may be "physical vs magical" channels.
- Which message constants 13/14 map to in the `HandleMessage` enum (13 = damage taken, 14 = impact) — inferred from call sites.

## Proposed SRE hooks

- `DamageComponent_AddPotentialImpact`/`DeliverImpact` are the correct, side-effect-complete way to make the hero hurt things — do not write the vectors directly.
- For spawn/attack scripting expose the existing binded-property accessors (`SetValueForBindedProperty`) to tweak damageMin/Max at runtime — they route through the real setters.
- **Do not** touch the `std::vector` headers at 0x68/0x80 from outside: the vectors own heap memory and the engine iterates them per frame.