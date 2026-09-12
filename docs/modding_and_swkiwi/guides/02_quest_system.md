# 02 — Quest System

> Swordigo's quest system is **character-side** — quests live in the save data
> (snapshot.bin), managed by `Character.*` Lua APIs. Scene scripts add, check,
> and complete quests via these APIs. There is no dedicated Quest component in
> the scene file format — quests are pure state flags on the character.

---

## 1. Quest Lifecycle

```
Character.AddQuest("quest_id")
    │
    ▼
Character.HasQuest("quest_id")        → true
Character.IsQuestInProgress("quest_id") → true
Character.IsQuestCompleted("quest_id") → false
    │
    ▼ (player completes objectives)
    │
Character.SetQuestCompleted("quest_id")
    │
    ▼
Character.IsQuestCompleted("quest_id") → true
Character.IsQuestInProgress("quest_id") → false
```

Quests are persistent — stored in `snapshot.bin` and survive scene transitions.
The save system writes them as part of the character state blob.

---

## 2. Complete Quest API

### 2.1 Quest Management

```lua
Character.AddQuest(quest_id)
```
Adds a quest to the character's active quest log. The quest_id is a string
like `"quest06_florennum_shard"`. Called 24 times across all scenes.

```lua
Character.HasQuest(quest_id) → boolean
```
Returns true if the quest has been added (active or completed). Called 8 times.

```lua
Character.IsQuestInProgress(quest_id) → boolean
```
Returns true if the quest is active but not yet completed. Called 12 times.

```lua
Character.IsQuestCompleted(quest_id) → boolean
```
Returns true if the quest has been turned in / completed. Called 18 times.

```lua
Character.SetQuestCompleted(quest_id)
```
Marks a quest as complete. This is **irreversible** — once completed, it stays
completed. Called 22 times.

### 2.2 Scene Flags (Scene-Local State)

Scene flags are **per-scene, persistent** booleans stored in the save data.
They allow scripts to remember state across scene visits.

```lua
Character.HasSceneFlag(flag_id) → boolean
```
Check if a scene flag is set. Called 41 times — the most-used state API.

```lua
Character.AddSceneFlag(flag_id)
```
Set a scene flag to true. Called 34 times.

```lua
Character.SetSceneFlag(flag_id, value)
```
Set a scene flag to a specific value. Called 2 times.

### 2.3 Global Flags

```lua
Character.HasFlag(flag_id) → boolean
```
Check a global (cross-scene) flag. Called 10 times.

```lua
Character.AddFlag(flag_id)
```
Set a global flag. Called 6 times.

### 2.4 Item Management

```lua
Character.HasItem(item_id) → boolean
```
Check if the character owns an item. Called 40 times — used for gate checks.

```lua
Character.AddItem(item_id)
```
Give an item to the character. Called 6 times.

```lua
Character.RemoveItem(item_id)
```
Remove an item from the character. Called 14 times.

```lua
Character.HasSkill(skill_id) → boolean
```
Check if the character has a skill (e.g., `"fireball"`, `"hookshot"`). Called 2 times.

```lua
Character.AddSkill(skill_id)
```
Grant a skill to the character. Called 4 times.

### 2.5 Currency

```lua
Character.NumCoins() → integer
```
Get current soul shard (coin) count. Called 4 times.

```lua
Character.SetNumCoins(amount)
```
Set coin count. Called 4 times.

```lua
Character.AddCoins(amount)
```
Add coins (negative to subtract).

---

## 3. All Quest IDs (Observed)

Extracted from all 116 decoded scene files:

| Quest ID | Description | Scene(s) |
|----------|-------------|----------|
| `quest01_find_master` | Find the master (tutorial) | town_woods1, town_woods_end |
| `quest02_village_elder` | Speak to village elder | town_elderhouse |
| `quest03_woodkeep` | Woodkeep quest | town_woods_end |
| `quest04_village_elder2` | Return to elder | town_elderhouse |
| `quest05_florennum` | Florennum questline | florennum_jail_part1, florennum_jail_part2 |
| `quest06_florennum_shard` | Mageblade shard quest | florennum_jail_part1, thecave_* |
| `quest07_go_to_king` | Go to the king | florennum_tower1, florennum_towertop |
| `quest071_fire` | Fire shard | fire_partBoss, fire_* |
| `quest072_ice` | Ice shard | snowy_part1, snowy_part2 |
| `quest09_assemble` | Assemble Mageblade | thecave_part15 |
| `quest10_death` | Final boss | theend |

### Quest Progression Flow

```
quest01_find_master
    → quest02_village_elder
    → quest03_woodkeep
    → quest04_village_elder2
    → quest05_florennum
    → quest06_florennum_shard
    → quest07_go_to_king
    → quest071_fire (fire shard)
    → quest072_ice (ice shard)
    → quest09_assemble (assemble Mageblade)
    → quest10_death (final boss)
```

---

## 4. Quest Trigger Patterns (Observed)

### Pattern 1: NPC Dialogue → Quest Grant
```lua
-- In NPC collision trigger
if target:identifier() == "hero" then
    Game.SetCinematicMode(true, false)
    Scene.SetPaused(false)
    
    local npc = Scene.Find("elder")
    ShowQuestBubbles("quest02_village_elder", self:identifier(),
        npc:position() + Vector3.New(0, 10, 0), true, {
        "The dark forces have taken our master!",
        "You must find him before it's too late."
    })
    HideTextBubble(self:identifier())
    
    Character.AddQuest("quest02_village_elder")
    Game.ShowNotification("New Quest: Find the Elder")
    
    Game.SetCinematicMode(false, true)
end
```

### Pattern 2: Objective Completion → Quest Update
```lua
-- In item collect trigger
if target:identifier() == "hero" then
    Character.AddItem("mageblade_shard_fire")
    Character.RemoveItem("quest_key_fire")
    
    ShowTextBubbles(self:identifier() .. "_text",
        self:position() + Vector3.New(0, 20, 0), true, {
        "You found the Fire Shard!"
    })
    HideTextBubble(self:identifier() .. "_text")
    
    -- Check if all shards collected
    if Character.HasItem("mageblade_shard_fire") and
       Character.HasItem("mageblade_shard_ice") then
        Character.SetQuestCompleted("quest06_florennum_shard")
        Character.AddQuest("quest09_assemble")
    end
end
```

### Pattern 3: Gate Check (Quest-Gated Door)
```lua
-- In door trigger
if target:identifier() == "hero" then
    if Character.IsQuestCompleted("quest05_florennum") then
        DoorController.Open(Scene.Find("jaildoor"))
        SoundLibrary.PlayEffect("unlock")
        CollisionShape.SetEnabled(Scene.Find("trigger3"), 101, false)
    else
        ShowTextBubbles("gate_text", self:position(), true, {
            "The door is locked. You need a key."
        })
        HideTextBubble("gate_text")
    end
end
```

### Pattern 4: Shop System
```lua
-- In shop Program component
CreateShopItem(self, "healingpotion", 50)
CreateShopItem(self, "ironsword", 80)
CreateShopItem(self, "firetrinket", 150, "trinket1")

-- Purchase handler
if Character.NumCoins() >= price then
    Character.SetNumCoins(Character.NumCoins() - price)
    Character.AddItem(item_id)
    SoundLibrary.PlayEffect("item_get")
    Game.ShowNotification("Purchased: " .. Game.TitleForItem(item_id))
end
```

---

## 5. Dialogue System

Swordigo uses two dialogue APIs:

```lua
ShowTextBubbles(id, position, animate, {line1, line2, ...}) → bubbleObject
```
Generic text bubbles for NPCs, notices, etc. Called 67 times.

```lua
ShowQuestBubbles(quest_id, id, position, animate, {line1, line2, ...}) → bubbleObject
```
Quest-specific dialogue — automatically associated with a quest for tracking.
Called 29 times.

```lua
HideTextBubble(id)
```
Dismiss a text bubble. Called 112 times.

### Dialogue Properties

```lua
Properties.GetProperty(self, "dialogue_key")
Properties.SetProperty(self, "dialogue_key", value)
```
Used to store dialogue state (e.g., whether NPC has already spoken a line).

---

## 6. Game Notification API

```lua
Game.ShowNotification(text)
```
Display a popup notification (e.g., "New Quest", "Item Get"). Called 18 times.

```lua
Game.TitleForItem(item_id) → string
```
Get the display name for an item. Called 14 times — used in notifications.

```lua
Game.IncCounter(counter_id)
```
Increment a game counter (e.g., enemies killed, secrets found). Called 34 times.

```lua
Game.Flash()
```
Screen flash effect (used on item get, quest complete). Called 36 times.

---

## 7. Save System (Quest Persistence)

All quest state is stored in `snapshot.bin` — the main save data blob.
The save system uses Google Play Games Snapshots on Android, and local file
I/O on desktop.

### Save Data Flow

```
Native C++ → JNI call → Java Snapshot API → Google Play Games
                                                    ↓
Native C++ ← JNI callback ← Java reads bytes ← snapshotLoaded()
```

### Desktop Implementation

```cpp
// jni_bridge.cpp
case 0x13250001: // loadSnapshot
    // Reads g_save_dir + "/snapshot.bin" from disk
    // Sets g_snapshot_load_pending = true
    
case 0x13250002: // saveSnapshot
    // Writes byte[] to g_save_dir + "/snapshot.bin"
    
case 0x13250003: // deleteSnapshot
    // Removes g_save_dir + "/snapshot.bin"
```

### Save File Location

```
~/.local/share/swordigo-desktop/save/
├── snapshot.bin              (main game state)
├── Documents/
│   └── <profile>.gplayer    (profile data)
├── controls_arm64.ini       (input config)
└── settings.bin             (game settings)
```

---

## 8. Modding Notes

- **Quest IDs are strings** — you can add custom quests with any ID.
  Use a prefix like `"mod_myquest_"` to avoid collisions.

- **Scene flags are per-scene** — use them for scene-local state that
  persists across visits (e.g., "has this chest been opened?").

- **Global flags cross scenes** — use them for world-wide state
  (e.g., "has the player defeated the fire boss?").

- **`Character.HasSceneFlag` is the most-used state check (41 calls)** —
  it's the primary mechanism for remembering what happened in a scene.

- **Items and skills are permanent** — once added, they persist in the
  save file. Plan item progression carefully.

- **`SetQuestCompleted` is irreversible** — once a quest is marked complete,
  there's no undo. Make sure the player has finished all objectives.
