# GameState (`Caver::GameState`)

## Summary

`GameState` is the in-memory save-game model: the embedded
`CharacterState`, per-level and per-quest state maps, the
`StateProperties` bag, the current level/spawn/map-node strings, and the
quest-text log. It is constructed with
`GameState(boost::shared_ptr<GameData> const&)` (0x47402C) and owned by
`PlayerProfile` (+0xE0 shared_ptr) and `GameViewController` (+0x98
shared_ptr); the `GameSceneController` holds a raw `GameState*` at +0x08.
It serializes to `Caver::Proto::GameState` and is the object a save/load
round-trip operates on.

⚠ **`GameState::Clear` does not exist in v1.4.13.** The header
(`GameState.h`) and `hooks/GameViewController.c` declare
`GameState_Clear` resolving `_ZN5Caver9GameState5ClearEv`, but that symbol
is **absent from the dynamic symbol table** (the only `GameState::Clear`
in the binary is `Caver::Proto::GameState::Clear`, the protobuf message,
`_ZN5Caver5Proto9GameState5ClearEv`). Calling the SRE resolver for it
returns NULL → crash if invoked. It was presumably an inlined/static
method or removed in 1.4.13. **Remove the DL_SYMBOL or reimplement it.**

The header's layout is misaligned: it assumed CharacterState is 0x88 bytes
(ending at 0x98); the real embedded CharacterState is ~0xB0 bytes
(0x10–0xBF), so every field after it shifts.

## Struct layout (64-bit verified; 32-bit unverified)

| Offset (32-bit) | Offset (64-bit) | Size | Type | Field | Notes |
|---|---|---|---|---|---|
| 0x00 | 0x00 | 0x08 | `GameData*` | `GameData` | shared_ptr ptr (ctor). |
| 0x08 | 0x08 | 0x08 | `void*` | `GameDataRef` | shared_ptr control. |
| 0x10 | 0x10 | ~0xB0 | `CharacterState` | `CharacterState` | **Embedded** (ctor runs `CharacterState(this+0x10)`). Key absolute offsets: `+0x90` health, `+0x94` mana, `+0x98` coins, `+0x9C` XP, `+0xA0` level, `+0xA4/0xA8/0xAC` upgrade counters (see CharacterState.md). |
| 0xC8 | 0xB0 | 0x18 | `std::map` | `levels` | `std::map<string, shared_ptr<LevelState>>`: begin@0xB0, end-node@0xB8, size@0xC0 (ctor: begin=&end trick). |
| 0xE0 | 0xC8 | 0x18 | `std::map` | `quests` | `std::map<string, shared_ptr<QuestState>>`: begin@0xC8, end@0xD0, size@0xD8. **Header's `nodesBegin/End` at 0xC8/0xD0 is this map.** |
| 0xF8 | 0xE0 | — | `StateProperties` | `stateProperties` | **Embedded** (ctor + Load call `StateProperties` ctor/Load on this+0xE0). **Header modeled it as a pointer — wrong.** |
| 0x110 | 0xF8 | 0x18 | `std::string` | `currentLevelName` | Proto field 10; read by LoadGameState for `StateForLevelWithName`. |
| 0x128 | 0x110 | 0x18 | `std::string` | `currentSpawnPoint` | Proto field 11; defaults "town_part1"/"spawn_from_town_herohouse" for new games. |
| 0x140 | 0x128 | 0x18 | `std::string` | `currentMapNodeName` | Proto field 12; defaults "m_town_herohouse". |
| 0x158 | 0x140 | 0x10 | `shared_ptr<MapNode>` | `currentMapNode` | ptr@0x140, control@0x148; `Map::NodeForName` result. |
| 0x168 | 0x150 | 0x18 | `std::string` | `str_proto14` | Proto field 14. Unresolved purpose. |
| 0x180 | 0x168 | 0x18 | `std::string` | `str_proto15` | Proto field 15. Unresolved. |
| 0x198 | 0x180 | 0x18 | `std::string` | `str_proto24` | Proto field 24. Unresolved. |
| 0x1B0 | 0x198 | 0x02 | `uint16` | `u16_proto25` | Proto field 25-ish (written from proto offset 200). Unresolved. |
| 0x1B8 | 0x1A0 | 0x18 | `std::string` | `str_proto26` | Proto field 26. |
| 0x1D0 | 0x1B8 | 0x18 | `std::string` | `str_proto27` | Proto field 27. |
| 0x1E8 | 0x1D0 | 0x18 | `std::vector<shared_ptr<QuestText>>` | `questTexts` | begin@0x1D0, end@0x1D8, cap@0x1E0. |
| 0x200 | 0x1E8 | 0x04 | `int` | `i_proto70` | Proto field 70. Unresolved. |

Total size ≈ 0x1F0.

## Exported functions

### GameState::GameState(shared_ptr<GameData> const&)
- Mangled: `_ZN5Caver9GameStateC2ERKN5boost10shared_ptrINS_8GameDataEEE` @ 0x47402C
- Behavior: stores GameData shared_ptr, constructs embedded CharacterState, initializes the two maps (begin=&end), StateProperties, zeroes the strings/vectors, and copies the GameData pointer into `CharacterState+0x00`.
- Confidence: **verified in IDA**.

### GameState::LoadFromProtobufMessage(Proto::GameState const&)
- Mangled: `_ZN5Caver9GameState23LoadFromProtobufMessageERKNS_5Proto9GameStateE` @ 0x474458
- Behavior: `CharacterState::LoadFromProtobufMessage(this+0x10, proto.character_state)`; rebuilds `levels` map (+0xB0) from proto field 8; `quests` (+0xC8) from field 34; `StateProperties` (+0xE0) from field 23; the seven strings; `currentMapNode` via `Map::NodeForName`; `questTexts` vector (+0x1D0) from field 58; and `+0x1E8` from proto field 70.
- Confidence: **verified in IDA**.

### GameState::SaveToProtobufMessage(Proto::GameState*) const
- Mangled: `_ZNK5Caver9GameState21SaveToProtobufMessageEPNS_5Proto9GameStateE` @ 0x474E7C — inverse of Load.
- Confidence: **verified in IDA** (address; body parallel to Load).

### GameState::AllNodesVisited()
- Mangled: `_ZN5Caver9GameState15AllNodesVisitedEv` @ 0x475D08 — checks every `LevelState`'s `visited` flag; used by the map screen for "100%" display.
- Confidence: **verified address; body inferred**.

### GameState::PercentCompleted()
- Mangled: `_ZN5Caver9GameState16PercentCompletedEv` @ 0x475FD8 — ratio of visited nodes × treasures found.
- Confidence: **verified address; body inferred**.

### GameState::CurrentLevelState(bool) / StateForLevelWithName(string const&, bool) / StateForQuestWithName / AddStateForQuestWithName / GetAllCompletedQuests / GetAllQuestsInProgress / CurrentGuideTarget / GuideTargetCompleted
- Addresses 0x475644 / 0x475430 / 0x475674 / 0x4756B0 / 0x475974 / 0x475B38 / 0x476710 / 0x476474 — map accessors + guide-target logic. Verified addresses.

## Open questions / unresolved offsets

- The header's `GameData@0, GameDataRef@0x08, CharacterState@0x10` are right;
  everything after (nodesBegin/nodesEnd, StateProperties pointer) is wrong —
  replaced by maps/embedded StateProperties/strings as tabled above.
- The 32-bit column is unverified.
- `GameState_Clear` DL_SYMBOL resolves to **NULL** in v1.4.13 (see Summary).

## Proposed SRE hooks

- **Save-state tooling**: `GameState` is the serialization root — SRE can
  snapshot/restore by calling the exported `SaveToProtobufMessage`/
  `LoadFromProtobufMessage` through a host-side protobuf encoder, or by
  memcpy'ing the struct while the game is paused (`updatesDisabled` on GSC
  or `Scene::SetPaused`). Struct memcpy is safe *only* if the heap-owning
  members (maps/strings/vectors) are re-built via Load — raw memcpy of the
  full struct double-frees. **Needs accessor functions, not raw exposure.**
- `sre_gamestate_snapshot()` / `sre_gamestate_restore()`: serialize the
  GameState through the real proto functions into a host buffer, and restore
  through `LoadFromProtobufMessage` — the engine-supported path.
- **Level-state mutation**: `StateForLevelWithName(gs, name, true)` returns
  the `LevelState*` (creates if missing) — SRE can set `visited`/treasure
  counters through it for "mark level complete" cheats.
- Fix `GameViewController.c`: drop the `GameState_Clear` resolver (or
  implement `Clear` semantics as `LoadFromProtobufMessage(default)`).
- **Unsafe**: `levels`/`quests` maps and `questTexts` vector — raw
  insertion corrupts the RB-tree/vector; go through the accessors above.