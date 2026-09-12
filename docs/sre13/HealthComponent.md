# HealthComponent

## Summary

`Caver::HealthComponent` gives a `SceneObject` a health pool: current health,
maximum health, and the death/damage flags that drive hit feedback. Damage is
applied through `TakeDamage(int)` (called by the damage-delivery path), which
clamps health at zero and raises the "dead" flag. It also carries a
regen-style float pair (one value from the scene file, one default) whose
exact runtime use is unresolved in this pass.

Header: none yet — see `Component.md` for the shared base (`0x00`–`0x67`).

## Struct layout

Verified from the ARM64 v1.4.13 ctor (`0x36B304`), `LoadFromProtobufMessage`
(`0x36B3C0`) and `TakeDamage` (`0x36BEA0`). 64-bit ABI. All offsets are
`Component`-relative (base ends at `0x67`).

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x68 | 8 | `void*` | (unknown) | Zeroed in ctor. |
| 0x70 | 1 | `bool` | `isDead` | **Verified:** `TakeDamage` sets +0x70 = 1 when health reaches 0. |
| 0x74 | 4 | `int` | `health` | Load writes `this+29` from proto field 1. |
| 0x78 | 4 | `int` | `currentHealth` | **Verified:** `TakeDamage` mutates `this+30`: `h = max(0, h - dmg)`. |
| 0x7C | 4 | `int` | `initialHealth` | Load writes `this+31` = same proto value (start-of-frame health). |
| 0x80 | 4 | `int` | `maxHealth` | Load writes `this+32` from proto field 2 (only if ≤ 1). |
| 0x84 | 4 | `float` | `regenPerSecond` | Load writes a float here from proto field 3 (`Message+24 → +12`). **Inferred.** |
| 0x88 | 4 | `float` | `= 50.0f` | Default 50.0 (ctor `0x42480000`). Meaning unresolved. |
| 0x8C | 4 | — | (unknown) | High half of the ctor's QWORD write. |
| 0x90 | 4 | `float` | `= 1.0f` | Default 1.0 (ctor `this+36`). Multiplier of some kind. **Inferred.** |
| 0x94 | 8 | `void*` | (unknown) | Zeroed. |
| 0x9C | 8 | `void*` | (unknown) | Zeroed. |
| 0xA4 | 8 | `void*` | (unknown) | Zeroed. |
| 0xB0 | 8 | `void*` | (unknown) | Zeroed. |
| 0xB8 | 1 | `bool` | `tookDamage` | **Verified:** `TakeDamage` sets +0xB8 = 1 on every hit (frame flag for hit feedback). |
| 0xBC | 4 | — | (end) | Total ≈ 0xC0. |

Header-style discrepancy note: none exists yet for this class; the base
`Component` fields (vtable at +0x00, refcount +0x08, second/third vtable
pointers +0x10/+0x18, owner `SceneObject*` at **+0x28**) are shared by every
component — see `Component.md`.

## Vtable (at 0x61E920)

Only slots that differ from the base `Component` vtable are listed; slots not
listed inherit the base implementation (see `Component.md` for the 30+ slot
base layout — note the base vtable is larger than the 10 slots previously
documented; slots 27–30 are `HasBounds`/`localAABB`/`minDepth`/`maxDepth`).

| Slot | Offset | Target | Notes |
|---|---|---|---|
| 0 | +0x00 | `~HealthComponent()` (0x36CEC4) | Incomplete-object dtor. |
| 1 | +0x08 | `~HealthComponent()` (0x36CFAC) | Deleting dtor. |
| 2 | +0x10 | `HealthComponent::Clone()` (0x36CFD0) | — |
| 4 | +0x20 | `HealthComponent::LoadFromProtobufMessage` (0x36B3C0) | — |
| 5 | +0x28 | `HealthComponent::SaveToProtobufMessage` (0x36B43C) | — |
| 6 | +0x30 | `ShouldSave()` (0x36D038) | — |
| 8 | +0x40 | `Prepare()` (0x36BCE0) | — |
| 14–18 | +0x70.. | IBindable accessors (0x36B5A8–0x36BCD8) | GetBindings/Value/SetValue/PerformBindedAction. |
| 25 | +0xC8 | `Update(float)` (0x36D0D8 → HealthComponent::Update at 0x36CFD8?) | Verified override exists (see dump). |

## Exported functions

### HealthComponent::TakeDamage(int)
- Mangled: `_ZN5Caver15HealthComponent10TakeDamageEi` (0x36BEA0).
- Behavior: ignores `dmg < 1`; sets `tookDamage` (+0xB8); `currentHealth (+0x78) = max(0, currentHealth - dmg)`; if health hits 0 sets `isDead` (+0x70).
- Side effects: mutates two flags and health; callers (0x60F20) drive death/feedback off the flags.
- Confidence: **verified**.

### HealthComponent::LoadFromProtobufMessage(Proto::Component const&)
- Mangled: `_ZN5Caver15HealthComponent23LoadFromProtobufMessageERKNS_5Proto9ComponentE` (0x36B3C0).
- Behavior: base load; reads the `HealthComponent` extension and sets +0x74/+0x78/+0x7C (health ×3), +0x80 (maxHealth), +0x84 (float).
- Confidence: **verified**.

## Open questions

- The three health copies (+0x74/+0x78/+0x7C) — likely `max → current → start` bookkeeping; only +0x78 is mutated.
- Meaning of the 50.0f default at +0x88 and 1.0f at +0x90.
- `Update` override body not read this pass.

## Proposed SRE hooks

- `HealthComponent_TakeDamage(comp, int)` (the existing hook target) is the safe damage path — always use it over writing +0x78 directly so the death flag stays consistent.
- For god-mode/cheat tooling expose `HealthComponent_SetHealth(comp, int)` as a wrapper that writes +0x78 and clears `isDead` when > 0 (raw writes risk desyncing the three copies).
- Reading `isDead` (+0x70) / `tookDamage` (+0xB8) is safe and useful for kill-cam/stat tooling.