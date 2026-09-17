#pragma once
// game_data.h — `Caver::GameData` and `Caver::Map`, the two static data tables
// the game loads once at profile creation.
//
// Where this comes from (arm32_13, 0x33BDB0 PlayerProfile::LoadGameStateFromProtobufMessage):
//
//   PathForResourceOfType("test",     "scmap") -> test.scmap    -> Proto::Map      -> Map::LoadFromProtobufMessage
//   PathForResourceOfType("gamedata", "gdata") -> gamedata.gdata -> Proto::GameData -> GameData::LoadFromProtobufMessage
//   new GameState(gamedata_shared_ptr)
//
// Note the resource names are the literal strings in that function: the *file*
// is `gamedata.gdata` but the lookup is ("gamedata", "gdata"), and the map file
// is literally `test.scmap`. Nothing here is invented.
//
// The field numbers below are the ones the engine's own generated symbols carry
// (`Caver::Proto::<Class>::k<Field>FieldNumber`); they were extracted from
// libswordigo.so with tools/extract_component_schema.py, not transcribed.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace caver {
namespace game {

// Proto::Item
struct ItemDef {
    int32_t     type = 0;
    std::string name;
    std::string title;
    std::string short_description;
    std::string description;
    bool        unique = false;
    int32_t     min_damage = 0;
    int32_t     max_damage = 0;
    int32_t     level = 0;
};

// Proto::Skill
struct SkillDef {
    std::string name;
    std::string title;
    std::string description;
    int32_t     mana_cost = 0;
    int32_t     min_damage = 0;
    int32_t     max_damage = 0;
};

// Proto::Quest
struct QuestDef {
    std::string name;
    std::string title;
    std::string follow_up_quest;
    std::string map_location;
};

// Proto::EntityClass
struct EntityClassDef {
    std::string name;
    std::string title;
    bool        level_hidden = false;
    bool        freezable = false;
    bool        stunnable = false;
    bool        grabbable = false;
    float       magic_resistance = 0.0f;
    float       physical_resistance = 0.0f;
};

// Proto::GuideTarget (GameData field 5) — the "guide target" marker the game
// points the player at (a level, and optionally an object inside it).
struct GuideTargetDef {
    int32_t     type = 0;
    std::string name;
    std::string level_name;
    std::string object_identifier;
    std::string carry_object_identifier;
    bool        show_only_after_scene_load = false;
    std::string portal_hint;
};

class GameData {
public:
    bool load_from_bytes(const std::string& bytes);

    const ItemDef*        item_for_name(const std::string& name) const;
    const SkillDef*       skill_for_name(const std::string& name) const;
    const QuestDef*       quest_for_name(const std::string& name) const;
    const EntityClassDef* entity_class_for_name(const std::string& name) const;

    // Identity hash of the parsed table, so a profile can note which gamedata
    // revision it was created against (the engine re-reads it every boot).
    uint64_t content_hash() const { return content_hash_; }

    const std::vector<ItemDef>&        items() const { return items_; }
    const std::vector<SkillDef>&       skills() const { return skills_; }
    const std::vector<QuestDef>&       quests() const { return quests_; }
    const std::vector<EntityClassDef>& entity_classes() const { return entity_classes_; }
    const std::vector<GuideTargetDef>& guide_targets() const { return guide_targets_; }

private:
    std::vector<ItemDef>        items_;
    std::vector<SkillDef>       skills_;
    std::vector<QuestDef>       quests_;
    std::vector<EntityClassDef> entity_classes_;
    std::vector<GuideTargetDef> guide_targets_;
    std::unordered_map<std::string, size_t> items_by_name_;
    std::unordered_map<std::string, size_t> skills_by_name_;
    std::unordered_map<std::string, size_t> quests_by_name_;
    std::unordered_map<std::string, size_t> entity_classes_by_name_;
    uint64_t content_hash_ = 0;
};

// Proto::MapNode_Portal
struct MapPortal {
    std::string destination_name;
    int32_t     direction = 0;
    int32_t     pass_direction = 0;
    bool        ignore_in_node_positioning = false;
};

// Proto::MapNode — one level on the world map.
struct MapNode {
    float       position[2] = {0.0f, 0.0f};
    std::string level_name;
    std::vector<MapPortal> portals;
    int32_t     type = 0;
    bool        hidden = false;
    int32_t     experience_level = 0;
    std::string music;
    bool        has_portal = false;
    int32_t     num_treasures = 0;
    std::string title;
    bool        ignore_in_statistics = false;
};

struct MapZone {
    std::string name;
    std::string title;
    std::vector<MapNode> nodes;
    int32_t     experience_level = 0;   // MapZone field 4 — applies to its nodes
    std::string music;                  // MapZone field 5
};

// Proto::Map (the .scmap world graph — 116 levels).
class GameMap {
public:
    bool load_from_bytes(const std::string& bytes);

    const std::vector<MapZone>& zones() const { return zones_; }
    const MapNode* node_for_level(const std::string& level_name) const;
    // Level titles come from the map node, which is what the in-game map shows
    // (`PlayerProfile::currentLevelTitle`).
    std::string title_for_level(const std::string& level_name) const;

private:
    std::vector<MapZone> zones_;
};

} // namespace game
} // namespace caver
