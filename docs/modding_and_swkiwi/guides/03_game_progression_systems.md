# 03 — Game Progression Systems

> Swordigo's progression is built on interconnected systems: the world map
> (.scmap) defines level connectivity, save data tracks character state, and
> Lua scripts gate access via quest/item/flag checks. This document covers
> the world map, save system, character stats, and progression mechanics.

---

## 1. World Map (.scmap)

The world map is a protobuf file that defines zones, nodes, portals, and
travel paths between scenes.

### 1.1 Ruby CLI Commands

```bash
# Summary of zones, nodes, portals
ruby_cli map summary worldmap.scmap

# Full decoded markup
ruby_cli map decode worldmap.scmap

# Validate for broken/orphan/duplicate issues
ruby_cli map validate worldmap.scmap

# BFS travel path between two scenes
ruby_cli map path worldmap.scmap "town_part1" "fire_part1"

# List all nodes with zone/level/title/type
ruby_cli map list-nodes worldmap.scmap
```

### 1.2 Map Structure (Observed)

```
WorldMap {
    Zone[]           → named regions (forest, cave, town, etc.)
    Node[]           → individual scenes within zones
    Portal[]         → connections between nodes
    TravelPath[]     → precomputed paths
}
```

Each Node references a scene file (e.g., `town_part1.scene`) and belongs to
a Zone. Portals define bidirectional connections with spawn point mapping.

### 1.3 Zone/Level Hierarchy

| Zone | Scenes | Description |
|------|--------|-------------|
| **Town** | town_part1..6, town_elderhouse, town_healerhouse, town_shop, town_herohouse | Starting village |
| **Forest** | forest_part1..3, forest_cave0..1, grove_part1..3 | First outdoor area |
| **Wasteland** | wasteland_part1..4 | Desert region |
| **Cave** | thecave_part1..15, thecave_crypt1 | Underground labyrinth |
| **Florennum** | florennum_part1..2, florennum_jail_*, florennum_tower* | Castle city |
| **Fire** | fire_part1..5, fire_partBoss | Fire realm |
| **Snowy** | snowy_part1..2, snowy_cave1..2 | Ice region |
| **Graveyard** | beyond_graveyard | Late-game area |
| **End** | theend | Final boss |

---

## 2. Character Stats

### 2.1 Health System

```lua
Health.CurrentHealth(entity) → integer
Health.MaxHealth(entity) → integer
Health.CurrentPercent(entity) → float (0.0 - 1.0)
```

Health is stored per-entity as a `HealthComponent`:
```
HealthComponent {
    MaxHealth : 100        (typical for hero)
    HEALTH_TYPE : 0        (0 = normal, 1 = boss, etc.)
    BarOffset{ X:0 Y:40 Z:0 }  (UI bar position)
}
```

### 2.2 Mana System

```lua
Health.SetCurrentMana(entity, amount)
Health.MaxMana(entity) → integer
```

Mana is used for spells and abilities. Regenerates over time or via items.

### 2.3 Coins (Soul Shards)

```lua
Character.NumCoins() → integer
Character.SetNumCoins(amount)
Character.AddCoins(amount)     -- (inferred from usage)
```

Soul shards are the in-game currency, used at shops. The coin limit is
patched to 9999 in SRE desktop mode.

### 2.4 Skills

Skills are unlocked progressively and grant new abilities:

| Skill | Effect | When Obtained |
|-------|--------|---------------|
| `fireball` | Ranged magic attack | After fire shard quest |
| `hookshot` | Grapple to surfaces | Cave exploration |
| `doublejump` | Second air jump | Mid-game |
| `walljump` | Climb walls | Late-game |

```lua
Character.HasSkill("fireball") → boolean
Character.AddSkill("fireball")
```

---

## 3. Item System

### 3.1 Item Categories

**Weapons:**
- `ironsword` — basic melee
- `broadsword` — upgraded melee
- `firetrinket` — fire enchantment
- `iceshield` — ice defense

**Consumables:**
- `healingpotion` — restore health
- `manapotion` — restore mana

**Quest Items:**
- `mageblade_shard_fire` — fire shard
- `mageblade_shard_ice` — ice shard
- `quest_key_*` — various keys

**Equipment:**
- Armor pieces (chest, legs, etc.)

### 3.2 Item Drop System

Items drop from enemies via `ItemDropComponent`:
```
ItemDropComponent {
    ItemId : "healingpotion"
    DropChance : 0.3
    DropCount : 1
}
```

### 3.3 Shop System

Shops use `CreateShopItem(self, item_id, price [, alternative_id])`:
```lua
-- Slot 1: fire trinket if player doesn't have it, else healing potion
if CreateShopItem(self, "firetrinket", 150, "trinket1") then
else
    CreateShopItem(self, "healingpotion", 50)
end
```

---

## 4. Progression Flow

### 4.1 Main Quest Line

```
START
  │
  ├─ Tutorial: Find Master (quest01)
  │    └─ Learn controls, basic combat
  │
  ├─ Village Elder (quest02)
  │    └─ Get first quest from elder
  │
  ├─ Woodkeep (quest03)
  │    └─ Explore forest, find woodkeep
  │
  ├─ Return to Elder (quest04)
  │    └─ Report findings, get new direction
  │
  ├─ Florennum (quest05)
  │    └─ Travel to castle city
  │    └─ Meet the King
  │
  ├─ Mageblade Shards (quest06)
  │    ├─ Fire Shard (quest071) → Fire Realm
  │    └─ Ice Shard (quest072) → Snowy Region
  │
  ├─ Assemble Mageblade (quest09)
  │    └─ Combine shards at the cave
  │
  └─ Final Boss (quest10)
       └─ theend scene
```

### 4.2 Gating Mechanisms

**Quest gates:**
```lua
if Character.IsQuestCompleted("quest05_florennum") then
    DoorController.Open(Scene.Find("castle_door"))
end
```

**Item gates:**
```lua
if Character.HasItem("mageblade_shard_fire") then
    -- Allow access to fire temple
end
```

**Flag gates:**
```lua
if Character.HasSceneFlag("boss_defeated") then
    -- Change NPC dialogue, open new area
end
```

**Skill gates:**
```lua
if Character.HasSkill("hookshot") then
    -- Allow grapple-only passages
end
```

---

## 5. Scene Groups

Scenes can be organized into groups that can be shown/hidden:

```lua
Scene.SetGroupHidden("group_name", true)   -- hide group
Scene.SetGroupHidden("group_name", false)  -- show group
```

Used for:
- Day/night scene variants
- Story-progressed scene changes
- Boss arena state changes

---

## 6. Hardmode

The game has a hardmode flag that increases difficulty:

```cpp
// Detected in SRE boot
[SRE] Hardmode flag: 0x20bae60
```

Hardmode is toggled from the game's settings menu and affects:
- Enemy damage multipliers
- Health drop rates
- Boss behavior

---

## 7. Achievements

The game tracks achievements via the Caver engine's achievement system.
The SRE hooks into this to provide desktop-compatible achievement popups:

```cpp
// SRE achievement popup
[SRE] Achievement popup active (pending=0x20bb200)
```

---

## 8. Modding Notes

- **Custom quests** — Add new quest IDs via `Character.AddQuest()`.
  Use unique prefixes to avoid collisions with vanilla quests.

- **Custom items** — Items are referenced by string ID. Add new items
  by adding models and configuring `CollectableItemComponent`.

- **Custom skills** — Skills are checked via `Character.HasSkill()`.
  Add new skills and hook them into the `CharControllerComponent`.

- **Progression gates** — Use `Character.HasSceneFlag()` for scene-local
  gates, `Character.HasFlag()` for world-wide gates.

- **Save compatibility** — Custom state should use prefixed flag/quest IDs
  (e.g., `"mod_myquest_step1"`) to avoid overwriting vanilla state.

- **Shop balancing** — `CreateShopItem()` prices are in soul shards.
  Vanilla prices range from 50 (healing potion) to 500 (rare items).
