# CharacterState (`Caver::CharacterState`)

## Summary

`CharacterState` holds the player's persistent RPG stats and inventory. It is
**embedded in `GameState` at +0x10** (GameState ctor runs
`CharacterState::CharacterState(this+0x10)`), and the game reads it through
`GameState + 0x10` everywhere (e.g. `GameSceneController::Update` reads
level/XP at `GameState+0xA0`/`GameState+0x9C` = `CharacterState+0x90`/
`+0x8C`). It serializes to `Caver::Proto::CharacterState` via
`SaveToProtobufMessage` / `LoadFromProtobufMessage` — these two functions
are the authoritative field map.

Verified layout (64-bit) from the ctor (0x46D5B8), `SaveToProtobufMessage`
(0x46E044), `LoadFromProtobufMessage` (0x46D600), plus the consumers
`GameSceneController::Update` (0x4265E8), `ApplyLevelUp` (0x427080),
`GameViewController::LoadGameState` (0x42CA7C), and
`ExperienceBar::UpdateExperience` (0x48687C). **All three consumers agree:
level @ 0x90, XP @ 0x8C.**

## Struct layout (64-bit verified; 32-bit unverified)

| Offset (32-bit) | Offset (64-bit) | Size | Type | Field | Notes |
|---|---|---|---|---|---|
| 0x00 | 0x00 | 0x08 | `GameData*` | `GameData` | Used by `LoadFromProtobufMessage` (`GameData::ItemForName(*(GameData**)this, ...)`) and the XP curve. |
| 0x04 | 0x08 | 0x08 | `void*` | `GameDataRef` (shared_ptr control) | Zeroed in ctor; header's `_pad0[0x0c/0x18]` is wrong — real gap is just 0x08. |
| 0x0C | 0x10 | 0x08 | `void*` | `itemsBegin` | **std::set-style RB-tree** of item states (SaveToProtobuf walks tree links: left@+0, right@+8, parent@+16). Empty → begin=end=&itemsEnd (ctor: `itemsBegin = this+0x18`). |
| 0x14 | 0x18 | 0x08 | `void*` | `itemsEnd` | Zeroed when empty. |
| 0x1C | 0x20 | 0x08 | — | (capacity/other) | Zeroed in ctor. Unresolved. |
| 0x24 | 0x28 | 0x08 | `void*` | `skillsBegin` | **vector of 16-byte elements** (`shared_ptr<String>`-style; each element's first qword points at a String). **Header says 0x30/0x38 — wrong.** |
| 0x2C | 0x30 | 0x08 | `void*` | `skillsEnd` | |
| 0x34 | 0x38 | 0x08 | — | pad (0) | Unresolved. |
| 0x3C | 0x40 | 0x08 | `String*` | `currentSkill` | Nullable; name of the equipped skill (proto `current_skill`). |
| 0x44 | 0x48 | 0x08 | — | pad (0) | Unresolved. |
| 0x4C | 0x50 | 0x08 | `String*` | `weaponTrinket` | Nullable std::string* (proto `weapon_trinket`). |
| 0x54 | 0x58 | 0x08 | — | pad (0) | |
| 0x5C | 0x60 | 0x08 | `String*` | `armorTrinket` | (proto `armor_trinket`) |
| 0x64 | 0x68 | 0x08 | — | pad (0) | |
| 0x6C | 0x70 | 0x08 | `String*` | `skillTrinket` | (proto `skill_trinket`) |
| 0x74 | 0x78 | 0x08 | — | pad (0) | |
| 0x7C | 0x80 | 0x04 | `int` | `currentHealth` | Proto field 3. In GameState-relative terms: `GameState+0x90`. Set by LoadGameState to `2·healthLevel+4`. |
| 0x80 | 0x84 | 0x04 | `int` | `currentMana` | Proto field 4 (`GameState+0x94`); LoadGameState: `20·attackLevel+10`. |
| 0x84 | 0x88 | 0x04 | `int` | `coins` | Proto field 5 (`GameState+0x98`). |
| 0x88 | 0x8C | 0x04 | `int` | `experiencePoints` | Proto field 6 (`GameState+0x9C`). On load it is **clamped up** to `ExperiencePointsRequiredForLevel(level)` if the save value is below the curve (XP-for-current-level minimum). |
| 0x8C | 0x90 | 0x04 | `int` | `experienceLevel` | Proto field 7 (`GameState+0xA0`). Capped at 99 (GSC Update checks `level ≤ 98` before level-up; ExperienceBar treats 99 as max). |
| 0x90 | 0x94 | 0x04 | `int` | `healthLevel` | Proto field 48 (`GameState+0xA4`) — "health upgrades" spent. |
| 0x94 | 0x98 | 0x04 | `int` | `manaLevel` | Proto field 49 (`GameState+0xA8`). |
| 0x98 | 0x9C | 0x04 | `int` | `attackLevel` | Proto field 50 (`GameState+0xAC`). |
| 0x9C | 0xA0 | 0x10 | — | tail | Zeroed OWORD + default-float OWORD in ctor (0xA0–0xAF hold two more default floats). **Unresolved** — the ctor writes `xmmword_25A440` at 0x80 (health/mana/coins/XP defaults) and `xmmword_25A140` at 0x90 (level/upgrade defaults + 2 more floats at 0xA0). |

Total ≈ 0xB0. The header's `level@0x90, experience@0x94, health@0x98,
mana@0x9C` is **wrong**: level is at 0x90, XP at 0x8C, and 0x94/0x98/0x9C are
the three upgrade counters. The header's `_pad2[0x40/0x50]` before the stats
block should be `0x3C/0x40`.

Item-state element layout (from SaveToProtobufMessage's tree walk):
`{RB-tree links @0/8/16(+24 color), ... name(std::string*) @56, count(int) @72, ...}`.

## Exported functions

### CharacterState::ExperiencePointsRequiredForLevel(int)
- Mangled: `_ZN5Caver14CharacterState32ExperiencePointsRequiredForLevelEi` @ 0x46DF24
- Call sites: `GameSceneController::Update` (level-up check: `XP ≥ XPReq(level+1)`), `ApplyLevelUp`, `ExperienceBar::UpdateExperience`, `GameViewController::LoadGameState` (sets `GameState+0x9C` = XPReq(map-node level)).
- Behavior: `if (level < 2) return 0;` else sums over `l = level..3`:
  `round((8·l + 24) · round(((l+2)·mult(l-1)) / mult(l-1)))` where
  `mult = GameData::EntityHealthMultiplierAtLevel` — note the callee **does
  not actually read its GameData\* argument** (formula is level-only;
  `EntityHealthMultiplierAtLevel(GameData*,l)` = `max(((l-1)·0.5+1)·7.5 [halved for l<4, ·0.75 at 4], 1)`).
  The GSC call passes `GameState+0x10` (= `&CharacterState`, whose +0 is
  GameData) as the first arg — IDA shows `cs+0x10` because the caller is the
  GameState; everything lines up.
- Side effects: none (pure).
- Confidence: **verified in IDA + raw disasm** (call site 0x426798-0x4267A0).

### CharacterState::CharacterState()
- Mangled: `_ZN5Caver14CharacterStateC2Ev` @ 0x46D5B8
- Behavior: zeroes GameData(+0)/ref(+8), items empty (begin=end=&end), zeroes 0x20–0x77, then seeds the stats block defaults: 0x80-0x8F from `xmmword_25A440` and 0x90-0x9F from `xmmword_25A140` (default health/mana/coins/XP/level/upgrades — i.e. a fresh character).
- Confidence: **verified in IDA**.

### CharacterState::LoadFromProtobufMessage(Proto::CharacterState const&)
- Mangled: `_ZN5Caver14CharacterState23LoadFromProtobufMessageERKNS_5Proto14CharacterStateE` @ 0x46D600
- Behavior: proto fields 3,4,5 → health/mana/coins (+0x80/84/88); field 6 → XP (+0x8C) clamped up to `XPReq(level)`; field 7 → level (+0x90); fields 48/49/50 → upgrade counters; items/skills/trinkets rebuilt via `GameData::ItemForName` etc.
- Confidence: **verified in IDA**.

### CharacterState::SaveToProtobufMessage(Proto::CharacterState*) const
- Mangled: `_ZNK5Caver14CharacterState21SaveToProtobufMessageEPNS_5Proto14CharacterStateE` @ 0x46E044
- Behavior: inverse of Load; writes the 4-float OWORD at +0x80 (fields 3–6), field 7 from +0x90, fields 48/49/50 from +0x94/98/9C, then items (name+count), skills, currentSkill, weapon/armor/skill trinkets.
- Confidence: **verified in IDA**.

### Other exported helpers (verified addresses, inferred bodies)
`RemoveItem(Item*)` 0x46EBF4 · `HighestLevelItemOfType(ItemType)` 0x46EA54 ·
`AnyItemOfType(ItemType)` 0x46EF10 · `HasItemWithName(string const&)` 0x46ECE4 ·
`HasSkillWithName(string const&)` 0x46EFA0 · `WeaponDamageRange()` 0x46F3B8 ·
`SkillDamageRange()` 0x46F7E0 · `ArmorDamageMultiplier()` 0x46F688 ·
`BaseExperienceForKillingMonster(int,int,int)` 0x46E574 ·
`ExperiencePointsForKillingMonster(int,int)` 0x46E5AC ·
`TrinketBonusForArmors/Skills(shared_ptr<Item> const&)` 0x46FB44/0x46FE20 ·
`GetItemsOfType(ItemType, vector*)` 0x46EE44 · dtor 0x47418C (weak).

## Open questions / unresolved offsets

- **0xA0–0xAF**: two more default floats seeded by the ctor (`xmmword_25A140`
  lower half). Not written by Save/Load, no consumer found. Possibly
  "luck"/"spellpower" legacy stats. **Unresolved.**
- **0x20, 0x38, 0x48, 0x58, 0x68, 0x78**: zeroed qwords with no access
  found — likely vector capacity/tail slots or alignment padding.
- Item-state element layout beyond `name@56`/`count@72` is partial — the
  tree node prefix (left/right/parent/color) was read from the Save walk but
  the full `CharacterState::ItemState` struct (0x46DFD8 dtor frees two
  shared_ptr fields at +8/+12 of an inner object) isn't fully mapped.
- The proto field numbering above (3–7, 48–50) is from the **v1.4.13**
  binary; an older documented schema (filerift.cpp) lists
  `2=CurrentHealth, 4=CurrentMana, 5=CurrentCoins, 6=ExperiencePoints,
  7=ExperienceLevel` — fields 3/48/49/50 look renumbered in 1.4.13.

## Proposed SRE hooks

- **Direct stat editing is now safe**: health/mana/coins/XP/level/upgrades
  are plain ints at fixed offsets in the embedded CharacterState. Host can
  write `GameState + {0x90,0x94,0x98,0x9C,0xA0,0xA4,0xA8,0xAC}` — but better,
  export an accessor:
  - `sre_character_set_stats(hp, mana, coins, xp, level)` writing the five
    ints + re-clamping XP to the curve, exactly like `LoadGameState` does.
- **XP curve reuse**: `CharacterState_ExperiencePointsRequiredForLevel` is a
  clean exported pure function — SRE can use it to draw an XP-to-level table
  or to compute "XP needed for next level" for overlays.
- **Unsafe to expose raw**: `itemsBegin/itemsEnd` (RB-tree — inserting a raw
  node corrupts tree invariants and leaks), `skillsBegin/End` (vector of
  shared_ptr-ish entries — same). Any item add/remove must go through the
  real engine functions (`CharacterState::AddItem`-equivalent — not exported;
  use `GameViewController_AddItemToCharacter` / the shared_item path) or a
  new accessor that re-implements the tree insertion with the same allocator.
- `currentSkill`/trinket slots are plain `String*` — nullable; writing a
  valid engine-String pointer is OK, but allocating a String without
  `String_create` leaks; prefer an accessor.