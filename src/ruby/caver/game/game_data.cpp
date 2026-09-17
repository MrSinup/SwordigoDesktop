#include "ruby/caver/game/game_data.h"

#include "platform/protobuf_reader.h"

namespace caver {
namespace game {
namespace {

// Field numbers are the engine's own (`Caver::Proto::<Class>::k<Field>FieldNumber`
// in libswordigo.so, extracted by tools/extract_component_schema.py). They are
// named here so the reader reads like the .proto it came from.
namespace GameDataF { constexpr int Item = 1, Skill = 2, Quest = 3, EntityClass = 4, GuideTarget = 5; }
namespace ItemF     { constexpr int Type = 1, Name = 2, Title = 3, ShortDescription = 4, Description = 5,
                                  Unique = 6, MinDamage = 7, MaxDamage = 8, Level = 9; }
namespace SkillF    { constexpr int Name = 1, Title = 2, Description = 3, ManaCost = 4, MinDamage = 5, MaxDamage = 6; }
namespace QuestF    { constexpr int Name = 1, Title = 2, FollowUp = 3, MapLocation = 4; }
namespace EntityClsF{ constexpr int Name = 1, Title = 2, LevelHidden = 3, Freezable = 4, Stunnable = 5,
                                  Grabbable = 6, MagicResistance = 7, PhysicalResistance = 8; }
namespace GuideF    { constexpr int Type = 1, Name = 2, LevelName = 3, ObjectIdentifier = 4,
                                  CarryObjectIdentifier = 5, ShowOnlyAfterSceneLoad = 6, PortalHint = 7; }
namespace MapF      { constexpr int Zone = 2; }
namespace MapZoneF  { constexpr int Name = 1, Title = 2, Node = 3, ExperienceLevel = 4, Music = 5; }
namespace MapNodeF  { constexpr int Position = 1, LevelName = 2, Portal = 3, Type = 4, Hidden = 5,
                                  ExperienceLevel = 6, Music = 7, HasPortal = 8, NumTreasures = 9,
                                  Title = 10, IgnoreInStatistics = 11; }
namespace PortalF   { constexpr int DestinationName = 1, Direction = 2, PassDirection = 3,
                                  IgnoreInNodePositioning = 4; }

uint64_t fnv1a(const std::string& bytes) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : bytes) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

// Proto::Vector2 is two fixed32 floats on tags 13/21 (field 1 wire I32, field 2
// wire I32) — the same layout the engine's Vector2 uses in scene payloads.
void read_vector2(const std::string& bytes, float out[2]) {
    try {
        proto::Reader reader(bytes);
        proto::Field field;
        while (reader.read_field(field)) {
            if (field.wire_type != proto::WIRE_I32) continue;
            if (field.field_number == 1) out[0] = field.float_val;
            else if (field.field_number == 2) out[1] = field.float_val;
        }
    } catch (...) {
        // A malformed Vector2 leaves the caller's defaults — it must not abort a boot.
    }
}

template <typename Fn>
void read_len_fields(const std::string& bytes, Fn fn) {
    try {
        proto::Reader reader(bytes);
        proto::Field field;
        while (reader.read_field(field))
            if (field.wire_type == proto::WIRE_LEN) fn(field.field_number, field.bytes_val);
    } catch (...) {
    }
}

template <typename Fn>
void read_varint_fields(const std::string& bytes, Fn fn) {
    try {
        proto::Reader reader(bytes);
        proto::Field field;
        while (reader.read_field(field))
            if (field.wire_type == proto::WIRE_VARINT) fn(field.field_number, field.varint_val);
    } catch (...) {
    }
}

} // namespace

bool GameData::load_from_bytes(const std::string& bytes) {
    items_.clear(); skills_.clear(); quests_.clear(); entity_classes_.clear(); guide_targets_.clear();
    items_by_name_.clear(); skills_by_name_.clear(); quests_by_name_.clear(); entity_classes_by_name_.clear();
    content_hash_ = fnv1a(bytes);

    read_len_fields(bytes, [this](int number, const std::string& body) {
        switch (number) {
            case GameDataF::Item: {
                ItemDef item;
                read_len_fields(body, [&](int f, const std::string& v) {
                    if (f == ItemF::Name) item.name = v;
                    else if (f == ItemF::Title) item.title = v;
                    else if (f == ItemF::ShortDescription) item.short_description = v;
                    else if (f == ItemF::Description) item.description = v;
                });
                read_varint_fields(body, [&](int f, uint64_t v) {
                    if (f == ItemF::Type) item.type = static_cast<int32_t>(v);
                    else if (f == ItemF::Unique) item.unique = v != 0;
                    else if (f == ItemF::MinDamage) item.min_damage = static_cast<int32_t>(v);
                    else if (f == ItemF::MaxDamage) item.max_damage = static_cast<int32_t>(v);
                    else if (f == ItemF::Level) item.level = static_cast<int32_t>(v);
                });
                items_by_name_.emplace(item.name, items_.size());
                items_.push_back(std::move(item));
                break;
            }
            case GameDataF::Skill: {
                SkillDef skill;
                read_len_fields(body, [&](int f, const std::string& v) {
                    if (f == SkillF::Name) skill.name = v;
                    else if (f == SkillF::Title) skill.title = v;
                    else if (f == SkillF::Description) skill.description = v;
                });
                read_varint_fields(body, [&](int f, uint64_t v) {
                    if (f == SkillF::ManaCost) skill.mana_cost = static_cast<int32_t>(v);
                    else if (f == SkillF::MinDamage) skill.min_damage = static_cast<int32_t>(v);
                    else if (f == SkillF::MaxDamage) skill.max_damage = static_cast<int32_t>(v);
                });
                skills_by_name_.emplace(skill.name, skills_.size());
                skills_.push_back(std::move(skill));
                break;
            }
            case GameDataF::Quest: {
                QuestDef quest;
                read_len_fields(body, [&](int f, const std::string& v) {
                    if (f == QuestF::Name) quest.name = v;
                    else if (f == QuestF::Title) quest.title = v;
                    else if (f == QuestF::FollowUp) quest.follow_up_quest = v;
                    else if (f == QuestF::MapLocation) quest.map_location = v;
                });
                quests_by_name_.emplace(quest.name, quests_.size());
                quests_.push_back(std::move(quest));
                break;
            }
            case GameDataF::EntityClass: {
                EntityClassDef entity;
                read_len_fields(body, [&](int f, const std::string& v) {
                    if (f == EntityClsF::Name) entity.name = v;
                    else if (f == EntityClsF::Title) entity.title = v;
                });
                read_varint_fields(body, [&](int f, uint64_t v) {
                    if (f == EntityClsF::LevelHidden) entity.level_hidden = v != 0;
                    else if (f == EntityClsF::Freezable) entity.freezable = v != 0;
                    else if (f == EntityClsF::Stunnable) entity.stunnable = v != 0;
                    else if (f == EntityClsF::Grabbable) entity.grabbable = v != 0;
                });
                entity_classes_by_name_.emplace(entity.name, entity_classes_.size());
                entity_classes_.push_back(std::move(entity));
                break;
            }
            case GameDataF::GuideTarget: {
                GuideTargetDef target;
                read_len_fields(body, [&](int f, const std::string& v) {
                    if (f == GuideF::Name) target.name = v;
                    else if (f == GuideF::LevelName) target.level_name = v;
                    else if (f == GuideF::ObjectIdentifier) target.object_identifier = v;
                    else if (f == GuideF::CarryObjectIdentifier) target.carry_object_identifier = v;
                    else if (f == GuideF::PortalHint) target.portal_hint = v;
                });
                read_varint_fields(body, [&](int f, uint64_t v) {
                    if (f == GuideF::Type) target.type = static_cast<int32_t>(v);
                    else if (f == GuideF::ShowOnlyAfterSceneLoad) target.show_only_after_scene_load = v != 0;
                });
                guide_targets_.push_back(std::move(target));
                break;
            }
            default:
                break;
        }
    });
    return !items_.empty() || !entity_classes_.empty();
}

const ItemDef* GameData::item_for_name(const std::string& name) const {
    auto it = items_by_name_.find(name);
    return it == items_by_name_.end() ? nullptr : &items_[it->second];
}

const SkillDef* GameData::skill_for_name(const std::string& name) const {
    auto it = skills_by_name_.find(name);
    return it == skills_by_name_.end() ? nullptr : &skills_[it->second];
}

const QuestDef* GameData::quest_for_name(const std::string& name) const {
    auto it = quests_by_name_.find(name);
    return it == quests_by_name_.end() ? nullptr : &quests_[it->second];
}

const EntityClassDef* GameData::entity_class_for_name(const std::string& name) const {
    auto it = entity_classes_by_name_.find(name);
    return it == entity_classes_by_name_.end() ? nullptr : &entity_classes_[it->second];
}

bool GameMap::load_from_bytes(const std::string& bytes) {
    zones_.clear();
    read_len_fields(bytes, [this](int number, const std::string& body) {
        if (number != MapF::Zone) return;
        MapZone zone;
        read_len_fields(body, [&](int f, const std::string& v) {
            if (f == MapZoneF::Name) zone.name = v;
            else if (f == MapZoneF::Title) zone.title = v;
            else if (f == MapZoneF::Music) zone.music = v;
            else if (f == MapZoneF::Node) {
                MapNode node;
                read_len_fields(v, [&](int nf, const std::string& nv) {
                    if (nf == MapNodeF::Position) read_vector2(nv, node.position);
                    else if (nf == MapNodeF::LevelName) node.level_name = nv;
                    else if (nf == MapNodeF::Music) node.music = nv;
                    else if (nf == MapNodeF::Title) node.title = nv;
                    else if (nf == MapNodeF::Portal) {
                        MapPortal portal;
                        read_len_fields(nv, [&](int pf, const std::string& pv) {
                            if (pf == PortalF::DestinationName) portal.destination_name = pv;
                        });
                        read_varint_fields(nv, [&](int pf, uint64_t pv) {
                            if (pf == PortalF::Direction) portal.direction = static_cast<int32_t>(pv);
                            else if (pf == PortalF::PassDirection) portal.pass_direction = static_cast<int32_t>(pv);
                            else if (pf == PortalF::IgnoreInNodePositioning) portal.ignore_in_node_positioning = pv != 0;
                        });
                        node.portals.push_back(std::move(portal));
                    }
                });
                read_varint_fields(v, [&](int nf, uint64_t nv) {
                    if (nf == MapNodeF::Type) node.type = static_cast<int32_t>(nv);
                    else if (nf == MapNodeF::Hidden) node.hidden = nv != 0;
                    else if (nf == MapNodeF::ExperienceLevel) node.experience_level = static_cast<int32_t>(nv);
                    else if (nf == MapNodeF::HasPortal) node.has_portal = nv != 0;
                    else if (nf == MapNodeF::NumTreasures) node.num_treasures = static_cast<int32_t>(nv);
                    else if (nf == MapNodeF::IgnoreInStatistics) node.ignore_in_statistics = nv != 0;
                });
                zone.nodes.push_back(std::move(node));
            }
        });
        read_varint_fields(body, [&](int f, uint64_t v) {
            if (f == MapZoneF::ExperienceLevel) zone.experience_level = static_cast<int32_t>(v);
        });
        zones_.push_back(std::move(zone));
    });
    return !zones_.empty();
}

const MapNode* GameMap::node_for_level(const std::string& level_name) const {
    for (const auto& zone : zones_)
        for (const auto& node : zone.nodes)
            if (node.level_name == level_name) return &node;
    return nullptr;
}

std::string GameMap::title_for_level(const std::string& level_name) const {
    const MapNode* node = node_for_level(level_name);
    return node ? node->title : std::string();
}

} // namespace game
} // namespace caver
