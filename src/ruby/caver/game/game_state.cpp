#include "ruby/caver/game/game_state.h"

#include <algorithm>

#include "platform/protobuf_reader.h"

namespace caver {
namespace game {
namespace {

// GameState / CharacterState / LevelState / QuestState field numbers, taken from
// the engine's own `k<Field>FieldNumber` symbols.
namespace GSF { constexpr int CharacterState = 1, LevelState = 2, CurrentLevel = 3,
                       CurrentSpawnPoint = 4, CurrentMapNodeName = 5, QuestState = 7,
                       Properties = 8, SelectedMenuTab = 9, CarriedObjectTemplate = 10,
                       CarriedObjectIdentifier = 11, PreviousPortalLevel = 13; }
namespace CSF { constexpr int CurrentHealth = 2, CurrentMana = 4, CurrentCoins = 5,
                       ExperiencePoints = 6, ExperienceLevel = 7, Item = 11, EquippedWeapon = 12,
                       EquippedArmor = 13, Skill = 15, CurrentSkill = 16, WeaponTrinket = 17,
                       ArmorTrinket = 18, SkillTrinket = 19, HealthAttribute = 20,
                       AttackAttribute = 21, MagicAttribute = 22; }
namespace LSF { constexpr int LevelName = 1, Visited = 2, Properties = 3,
                       NumTreasures = 4, TreasuresFound = 5; }
namespace QSF { constexpr int QuestName = 1, Completed = 2; }
namespace ISF { constexpr int Name = 1, Count = 2; }

template <typename Fn>
void read_len(const std::string& bytes, Fn fn) {
    try {
        proto::Reader reader(bytes);
        proto::Field field;
        while (reader.read_field(field))
            if (field.wire_type == proto::WIRE_LEN) fn(field.field_number, field.bytes_val);
    } catch (...) {
    }
}

template <typename Fn>
void read_varint(const std::string& bytes, Fn fn) {
    try {
        proto::Reader reader(bytes);
        proto::Field field;
        while (reader.read_field(field))
            if (field.wire_type == proto::WIRE_VARINT) fn(field.field_number, field.varint_val);
    } catch (...) {
    }
}

ItemState parse_item_state(const std::string& bytes) {
    ItemState state;
    read_len(bytes, [&](int f, const std::string& v) { if (f == ISF::Name) state.name = v; });
    read_varint(bytes, [&](int f, uint64_t v) { if (f == ISF::Count) state.count = static_cast<int32_t>(v); });
    return state;
}

void parse_character_state(const std::string& bytes, CharacterState& out) {
    read_len(bytes, [&](int f, const std::string& v) {
        switch (f) {
            case CSF::Item:  out.inventory.push_back(parse_item_state(v)); break;
            case CSF::Skill: out.skills.push_back(parse_item_state(v)); break;
            case CSF::EquippedWeapon: out.equipped_weapon_name = v; break;
            case CSF::EquippedArmor:  out.equipped_armor_name = v; break;
            case CSF::CurrentSkill:   out.current_skill = v; break;
            case CSF::WeaponTrinket:  out.weapon_trinket = v; break;
            case CSF::ArmorTrinket:   out.armor_trinket = v; break;
            case CSF::SkillTrinket:   out.skill_trinket = v; break;
            default: break;
        }
    });
    read_varint(bytes, [&](int f, uint64_t v) {
        switch (f) {
            case CSF::CurrentHealth:     out.current_health = static_cast<int32_t>(v); break;
            case CSF::CurrentMana:       out.current_mana = static_cast<int32_t>(v); break;
            case CSF::CurrentCoins:      out.current_coins = static_cast<int32_t>(v); break;
            case CSF::ExperiencePoints:  out.experience_points = static_cast<int32_t>(v); break;
            case CSF::ExperienceLevel:   out.experience_level = static_cast<int32_t>(v); break;
            case CSF::HealthAttribute:   out.health_attribute = static_cast<int32_t>(v); break;
            case CSF::AttackAttribute:   out.attack_attribute = static_cast<int32_t>(v); break;
            case CSF::MagicAttribute:    out.magic_attribute = static_cast<int32_t>(v); break;
            default: break;
        }
    });
}

} // namespace

const ItemDef* CharacterState::highest_level_item_of_type(const GameData& data, ItemType type) const {
    const ItemDef* best = nullptr;
    for (const auto& owned : inventory) {
        const ItemDef* item = data.item_for_name(owned.name);
        if (!item || item->type != static_cast<int32_t>(type)) continue;
        if (!best || item->level > best->level) best = item;
    }
    return best;
}

const ItemDef* CharacterState::equipped_weapon(const GameData& data) const {
    return highest_level_item_of_type(data, ItemType::Weapon);
}

const ItemDef* CharacterState::equipped_armor(const GameData& data) const {
    return highest_level_item_of_type(data, ItemType::Armor);
}

bool CharacterState::has_item(const std::string& name) const {
    for (const auto& item : inventory)
        if (item.name == name) return true;
    return false;
}

int CharacterState::item_count(const std::string& name) const {
    for (const auto& item : inventory)
        if (item.name == name) return item.count;
    return 0;
}

bool GameState::load_from_bytes(const std::string& bytes) {
    character_ = CharacterState{};
    levels_.clear();
    quests_.clear();
    counters_.clear();

    read_len(bytes, [this](int f, const std::string& v) {
        switch (f) {
            case GSF::CharacterState: parse_character_state(v, character_); break;
            case GSF::LevelState: {
                LevelState level;
                read_len(v, [&](int lf, const std::string& lv) {
                    if (lf == LSF::LevelName) level.level_name = lv;
                    else if (lf == LSF::Properties) level.properties = lv;
                });
                read_varint(v, [&](int lf, uint64_t lv) {
                    if (lf == LSF::Visited) level.visited = lv != 0;
                    else if (lf == LSF::NumTreasures) level.num_treasures = static_cast<int32_t>(lv);
                    else if (lf == LSF::TreasuresFound) level.treasures_found = static_cast<int32_t>(lv);
                });
                levels_.push_back(std::move(level));
                break;
            }
            case GSF::QuestState: {
                QuestState quest;
                read_len(v, [&](int qf, const std::string& qv) { if (qf == QSF::QuestName) quest.quest_name = qv; });
                read_varint(v, [&](int qf, uint64_t qv) { if (qf == QSF::Completed) quest.completed = qv != 0; });
                quests_.push_back(std::move(quest));
                break;
            }
            case GSF::CurrentLevel: current_level_ = v; break;
            case GSF::CurrentSpawnPoint: current_spawn_point_ = v; break;
            case GSF::CurrentMapNodeName: current_map_node_name_ = v; break;
            case GSF::SelectedMenuTab: selected_menu_tab_ = v; break;
            case GSF::PreviousPortalLevel: previous_portal_level_ = v; break;
            case GSF::CarriedObjectTemplate: carried_object_template_ = v; break;
            case GSF::CarriedObjectIdentifier: carried_object_identifier_ = v; break;
            case GSF::Properties: {
                // Properties are counters (PlayerProfile::ValueForCounter); the row
                // format is a name/count pair, the same shape as ItemState.
                ItemState property = parse_item_state(v);
                if (!property.name.empty()) counters_[property.name] = property.count;
                break;
            }
            default: break;
        }
    });
    return !current_level_.empty();
}

void GameState::set_current_level(const std::string& level, const std::string& spawn_point) {
    current_level_ = level;
    current_spawn_point_ = spawn_point;
    state_for_level(level).visited = true;
}

LevelState& GameState::state_for_level(const std::string& level_name) {
    for (auto& level : levels_)
        if (level.level_name == level_name) return level;
    LevelState created;
    created.level_name = level_name;
    levels_.push_back(std::move(created));
    return levels_.back();
}

const LevelState* GameState::state_for_level_or_null(const std::string& level_name) const {
    for (const auto& level : levels_)
        if (level.level_name == level_name) return &level;
    return nullptr;
}

QuestState& GameState::state_for_quest(const std::string& quest_name) {
    for (auto& quest : quests_)
        if (quest.quest_name == quest_name) return quest;
    QuestState created;
    created.quest_name = quest_name;
    quests_.push_back(std::move(created));
    return quests_.back();
}

int GameState::counter(const std::string& key) const {
    auto it = counters_.find(key);
    return it == counters_.end() ? 0 : it->second;
}

void GameState::set_counter(const std::string& key, int value) {
    counters_[key] = value;
}

void GameState::increase_counter(const std::string& key, int delta) {
    counters_[key] += delta;
}

size_t GameState::visited_level_count() const {
    size_t visited = 0;
    for (const auto& level : levels_)
        if (level.visited) ++visited;
    return visited;
}

} // namespace game
} // namespace caver
