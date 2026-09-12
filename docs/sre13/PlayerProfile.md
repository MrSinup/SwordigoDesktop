# PlayerProfile

## Summary

`Caver::PlayerProfile` is the on-disk save slot: identifier, display name, the
`DateTime` of the last play, level/percent/time-played stats, the currently
equipped weapon/armor/trinket names, a `std::map<string,int>` of named counters
(e.g. collectible/quest flags), a `GameState*` snapshot, and a "loaded" flag.
It is what `ProfileManager`/`ProfileSelectionViewController` list and what
`GameViewController` loads into the live `GameState` when a save is selected.

Header: `src/sre/sre13/caver/PlayerProfile.h`.

## Struct layout

Verified from the ARM64 v1.4.13 ctor (`0x47B9D8`), `SaveToProtobufMessage`
(`0x47C5E0`) and `LoadFromProtobufMessage` (`0x47CBA8`). 64-bit ABI. The header's
guess (`Identifier` at 0x18, `lastPlayedTime` at 0x40) is **off**: the real
layout is below.

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x00 | 24 | — | (unknown) | Three QWORDs zeroed in ctor. Possibly a `ProfileStats` sub-object or unused base. |
| 0x18 | 24 | `std::string` | `identifier` | Proto field 1 (`identifier`). SSO string. |
| 0x30 | 24 | `std::string` | `name` | Proto field 2 (`name`). |
| 0x48 | 16 | `DateTime` | `lastPlayedTime` | Proto field 3. `DateTime::Now()` result stored at +0x48 (8-byte double timestamp). |
| 0x50 | 24 | `std::string` | `currentLevelTitle` | Proto field 4. |
| 0x68 | 4 | `int` | `level` | Proto field 5. Default `1` (ctor sets `this+0x68 = 1`). |
| 0x6C | 4 | `float` | `percentCompleted` | Proto field 6. `GameState::PercentCompleted()` cached here at save time. |
| 0x70 | 8 | `double` | `timePlayed` | Proto field 7 (seconds). |
| 0x78 | 24 | `std::string` | `equippedWeaponName` | Proto field 8. |
| 0x90 | 24 | `std::string` | `equippedArmorName` | Proto field 9. |
| 0xA8 | 24 | `std::string` | `weaponTrinketName` | Proto field 10. |
| 0xC0 | 24 | `std::string` | `armorTrinketName` | Proto field 11. |
| 0xD8 | 1 | `bool` | `loaded` | Set to 1 at the end of `LoadFromProtobufMessage`. |
| 0xD9 | 7 | — | (pad/unknown) | — |
| 0xE0 | 16 | — | (unknown) | Two OWORDs zeroed in ctor; overlapping the `GameState` ptr region below. |
| 0xF0 | 8 | `GameState*` | `gameState` | **Real offset (header guessed `countersBegin`).** `GameState*` snapshot; `SaveToProtobufMessage` reads `this+0xF0` and calls `GameState::SaveToProtobufMessage`; `LoadFromProtobufMessage(a3=true)` calls `LoadGameStateFromProtobufMessage` → sets it. |
| 0xF8 | 8 | — | (unknown) | Zeroed in ctor. |
| 0x100 | 24 | `std::map<std::string,int>` | `counters` | **Real offset (header guessed `countersBegin`/`countersEnd` pair).** libc++ red-black tree header: `begin_node` at +0x100 (`= &this+0x108` when empty), size +0x108, root +0x110. Iterated in `SaveToProtobufMessage`; filled via `__emplace_unique_key_args` in Load (counter value written to node+0x38). |
| 0x118 | 1 | `bool` | `someFlag` | Proto field 12 (byte at proto+0x98, has-bit 0x1000). Meaning unresolved — likely "uses cheats" or "new profile". |
| 0x119+ | — | — | (end) | Total ≥ 0x119. |

The header's `_pad1[0x20/0x28]` + `lastPlayedTime` at 0x40/0x50 guess: real
`lastPlayedTime` is at **0x48** (after the `name` string). The header's
`countersBegin`/`countersEnd` at 0x88/0x90 do not exist as separate fields — the
counters are a single `std::map` at **0x100**.

## Object graph & lifecycle

- Constructed empty by `ProfileManager`; filled by `InitWithIdentifier` +
  `Load`/`LoadFromPath` (or `LoadFromProtobufMessage`).
- `gameState` (+0xF0) is a raw `GameState*` owned by the profile (allocated when
  a save is loaded); `GameViewController::LoadGameState` copies its stats into
  the live game.
- `Save(bool)` writes the protobuf to `LocalFilePath()` (documents dir + `Filename()`,
  e.g. `<id>.gplayer`); `DeleteProfile` removes the file.
- Counters are merged/upserted on load (`__emplace_unique_key_args`, value stored
  at node+0x38); `ValueForCounter`/`SetValueForCounter`/`IncreaseCounterValue`
  are the accessors SRE resolves in `PlayerProfile.h`.

## Exported functions

### PlayerProfile::PlayerProfile()
- Mangled: `_ZN5Caver13PlayerProfileC2Ev` (0x47B9D8).
- Behavior: zeroes everything, sets `level = 1` (+0x68), `counters` map header
  (`begin_node = &this+0x108`), `loaded = 0` (+0x118).
- Confidence: **verified**.

### PlayerProfile::SaveToProtobufMessage(Proto::PlayerProfile*)
- Mangled: `_ZN5Caver13PlayerProfile21SaveToProtobufMessageEPNS_5Proto13PlayerProfileE` (0x47C5E0).
- Behavior: bails if `gameState` (+0xF0) is null. Writes: identifier (+0x18), name
  (+0x30), `lastPlayedTime` (refreshed with `DateTime::Now()`), `currentLevelTitle`
  (+0x50), `level` (+0x68, read from `GameState+0xA0`), `percentCompleted` (+0x6C,
  recomputed via `GameState::PercentCompleted`), `timePlayed` (+0x70), the four
  equipped-item names (+0x78/0x90/0xA8/0xC0), the counters map (+0x100), the
  `someFlag` byte (+0x118), and the full `GameState` via
  `GameState::SaveToProtobufMessage(gameState, proto->game_state)`.
- Side effects: allocates protobuf message objects; reads live `GameState`.
- Confidence: **verified**.

### PlayerProfile::LoadFromProtobufMessage(Proto::PlayerProfile const&, bool loadGameState)
- Mangled: `_ZN5Caver13PlayerProfile23LoadFromProtobufMessageERKNS_5Proto13PlayerProfileEb` (0x47CBA8).
- Behavior: copies name, DateTime, level title, level, percent, time played, the
  four equipment names, upserts every counter into the map (+0x100), copies
  `someFlag` (+0x118), and — when the flag is set — calls
  `LoadGameStateFromProtobufMessage` (populating `gameState` at +0xF0). Finally
  sets `loaded` (+0xD8).
- Confidence: **verified**.

### PlayerProfile::Load() / LoadGameState() / Save(bool) / DeleteProfile()
- `Load` (0x47C3AC): reads `LocalFilePath()`, parses the `PlayerProfile` proto,
  calls `LoadFromProtobufMessage`.
- `LoadGameState` (0x47C540): loads just the `GameState` sub-message.
- `Save` (0x47C1EC): `SaveToProtobufMessage` then writes the file.
- `DeleteProfile` (0x47C318): removes the save file.
- Confidence: **verified** (symbols + callee lists; bodies read for Save/Load paths).

### ProfileStats helpers
`currentLevelTitle`, `equippedWeaponName`, `equippedArmorName`,
`weaponTrinketName`, `armorTrinketName` (0x46105C–0x4617AC, weak) derive display
strings from the underlying `GameState`/item data — they are the functions the
menu UI calls. `FormattedTimePlayed(double, bool)` (0x47CED0) formats the
`timePlayed` double.

## Open questions

- The 24 bytes at +0x00 (and the OWORDs at +0xE0) have no recovered access.
- `someFlag` (+0x118) meaning — set/cleared only through the proto field 12 byte.
- The exact `DateTime` layout (only the 8-byte timestamp at +0x48 is confirmed).

## Proposed SRE hooks

- `PlayerProfile` is the cleanest save-editing surface: expose
  `profile_get_id()`, `PlayerProfile_ValueForCounter`, and
  `PlayerProfile_SetValueForCounter` (already in the header) plus a new
  `PlayerProfile_LoadFromPath` (already declared) — all safe because they go
  through the map/accessors.
- For cheat/tooling builds, a `PlayerProfile_SetLevel(profile, int)` accessor
  (writes +0x68) is safe — but must also update the linked `GameState+0xA0` or
  the save/load cycle will disagree; expose a pair that writes both.
- **Do not** mutate the counters map internals (+0x100 header) directly; always
  go through `SetValueForCounter` so the red-black tree stays balanced.
- `timePlayed` (+0x70) is a plain double — trivially safe to read/write via an
  accessor, and useful for time-attack tooling.