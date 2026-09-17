#pragma once
// game_state.h — `Caver::GameState`, i.e. a save file.
//
// A profile's GameState is `<profile>.gstate` (Proto::GameState). The template a
// new profile is created from is `newplayer.gstate`, which is 38 bytes:
//
//   field 1: (empty CharacterState)      -> a level-0 hero: 0 XP, no items
//   field 3: "town_herohouse"            -> CurrentLevel
//   field 4: "spawn_default"             -> CurrentSpawnPoint
//   field 9: "map"                       -> SelectedMenuTab
//
// So "boot the game as a fresh hero in the town" is not a special case in the
// code: it is exactly what reading newplayer.gstate produces.
//
// Equipment is derived, not stored. `CharacterState::EquippedWeapon()` is
// `HighestLevelItemOfType(ItemType::Weapon)` and `EquippedArmor()` is the same
// with `ItemType::Armor` (both literals read off arm32_13 0x314568 / 0x314464):
// the hero always wields the best *owned* item of that type, and AddItem
// re-derives it on every pickup.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ruby/caver/game/game_data.h"

namespace caver {
namespace game {

// Proto::Item.Type — only the two the engine compares against directly are
// pinned by evidence; the rest come from the item table itself.
enum class ItemType : int32_t {
    Unknown = 0,
    Weapon  = 1,   // CharacterState::EquippedWeapon -> HighestLevelItemOfType(1)
    Armor   = 2,   // CharacterState::EquippedArmor  -> HighestLevelItemOfType(2)
};

// Proto::CharacterState_ItemState
struct ItemState {
    std::string name;
    int32_t     count = 0;
};

// Proto::CharacterState (GameState field 1)
struct CharacterState {
    int32_t current_health = 0;
    int32_t current_mana = 0;
    int32_t current_coins = 0;
    int32_t experience_points = 0;
    int32_t experience_level = 0;
    std::vector<ItemState> inventory;      // field 11 Item[]
    std::vector<ItemState> skills;         // field 15 Skill[]
    std::string current_skill;             // field 16
    std::string equipped_weapon_name;      // field 12 (kept; the engine re-derives it)
    std::string equipped_armor_name;       // field 13
    std::string weapon_trinket;            // field 17
    std::string armor_trinket;             // field 18
    std::string skill_trinket;             // field 19
    int32_t health_attribute = 0;          // field 20
    int32_t attack_attribute = 0;          // field 21
    int32_t magic_attribute = 0;           // field 22

    // `HighestLevelItemOfType`: the highest-Level owned item of that type, which
    // is what the hero is wielding. Null when the hero owns nothing of the type.
    const ItemDef* highest_level_item_of_type(const GameData& data, ItemType type) const;
    const ItemDef* equipped_weapon(const GameData& data) const;
    const ItemDef* equipped_armor(const GameData& data) const;

    bool has_item(const std::string& name) const;
    int  item_count(const std::string& name) const;
};

// Proto::LevelState (GameState field 2, repeated)
struct LevelState {
    std::string level_name;
    bool        visited = false;
    int32_t     num_treasures = 0;
    int32_t     treasures_found = 0;
    std::string properties;      // field 3, preserved verbatim
};

// Proto::QuestState (GameState field 7, repeated)
struct QuestState {
    std::string quest_name;
    bool        completed = false;
};

class GameState {
public:
    bool load_from_bytes(const std::string& bytes);

    CharacterState& character() { return character_; }
    const CharacterState& character() const { return character_; }

    const std::string& current_level() const { return current_level_; }
    const std::string& current_spawn_point() const { return current_spawn_point_; }
    const std::string& current_map_node_name() const { return current_map_node_name_; }
    const std::string& selected_menu_tab() const { return selected_menu_tab_; }
    const std::string& previous_portal_level() const { return previous_portal_level_; }
    const std::string& carried_object_template() const { return carried_object_template_; }
    const std::string& carried_object_identifier() const { return carried_object_identifier_; }

    void set_current_level(const std::string& level, const std::string& spawn_point);
    void set_current_map_node(const std::string& node_name) { current_map_node_name_ = node_name; }

    // `StateForLevelWithName` — the state row for a level, created on first visit
    // the way the engine does (a missing row is not an error).
    LevelState&       state_for_level(const std::string& level_name);
    const LevelState* state_for_level_or_null(const std::string& level_name) const;

    // `StateForQuestWithName` / `AddStateForQuestWithName`.
    QuestState&       state_for_quest(const std::string& quest_name);

    const std::vector<LevelState>& levels() const { return levels_; }
    const std::vector<QuestState>& quests() const { return quests_; }

    // Counters (PlayerProfile::ValueForCounter / IncreaseCounterValue).
    int  counter(const std::string& key) const;
    void set_counter(const std::string& key, int value);
    void increase_counter(const std::string& key, int delta = 1);

    // `AllNodesVisited` / `PercentCompleted` operate on the map, so they are
    // reported here as raw counts and left to the caller to divide.
    size_t visited_level_count() const;

private:
    CharacterState character_;
    std::vector<LevelState> levels_;
    std::vector<QuestState> quests_;
    std::string current_level_;
    std::string current_spawn_point_;
    std::string current_map_node_name_;
    std::string selected_menu_tab_;
    std::string previous_portal_level_;
    std::string carried_object_template_;
    std::string carried_object_identifier_;
    std::unordered_map<std::string, int> counters_;
};

} // namespace game
} // namespace caver
