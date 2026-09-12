#include "ruby/caver/component.h"

#include <algorithm>
#include <cstring>

#include "ruby/caver/behaviour.h"
#include "ruby/caver/visual.h"

namespace caver {
namespace {

// Stage shorthands. Parsed is implied by every row (the schema is complete for
// all 80 classes); the interesting columns are Updated and Rendered.
constexpr Stage kParsed = Stage::Parsed | Stage::Instantiated;
constexpr Stage kUpdate = Stage::Parsed | Stage::Instantiated | Stage::Updated;
constexpr Stage kRender = Stage::Parsed | Stage::Instantiated | Stage::Rendered;
constexpr Stage kBoth   = Stage::Parsed | Stage::Instantiated | Stage::Updated | Stage::Rendered;

// Recovered from the `Component` schema in scene_schemas.cpp (payload tag >> 3).
// Keep payload_field verbatim — the gaps are the game's own numbering.
// `recover_dir` is the matching directory under
// OpenSwordigo/arm64_12/functions/Caver/ holding the per-function decompilation.
const ComponentType kTypes[] = {
    {"SpriteComponent",                         "Sprite",                         802, kRender, false},
    {"ModelComponent",                          "Model",                          810, kBoth,   false},
    {"KeyframeAnimationComponent",              "KeyframeAnimation",              818, kUpdate, false},
    {"BlendAnimationComponent",                 "BlendAnimation",                 826, kParsed, false},
    {"ModelTransformControllerComponent",       "ModelTransformController",       834, kUpdate, true},
    {"GroundPolygonComponent",                  "GroundPolygon",                  882, kRender, false},
    {"GroundMeshComponent",                     "GroundMesh",                     890, kRender, false},
    {"GroundMeshGeneratorComponent",            "GroundMeshGenerator",            898, kParsed, true},
    {"TextureMappingComponent",                 "TextureMapping",                 906, kParsed, false},
    {"WaterMeshComponent",                      "WaterMesh",                      914, kBoth,   false},
    {"ShapeComponent",                          "Shape",                          962, kRender, false},
    {"CollisionShapeComponent",                 "CollisionShape",                 970, kRender, false},
    {"DamageComponent",                         "Damage",                         978, kParsed, false},
    {"HealthComponent",                         "Health",                         986, kParsed, false},
    {"BoneControlledCollisionShapeComponent",   "BoneControlledCollisionShape",   994, kParsed, false},
    {"ObjectLinkControllerComponent",           "ObjectLinkController",          1002, kParsed, true},
    {"LightComponent",                          "Light",                         1042, kBoth,   false},
    {"ShadowComponent",                         "Shadow",                        1050, kRender, false},
    {"SoundEffectComponent",                    "SoundEffect",                   1122, kParsed, false},
    {"AnimationControllerComponent",            "AnimationController",           1194, kUpdate, false},
    {"CharAnimControllerComponent",             "CharAnimController",            1202, kParsed, false},
    {"CharControllerComponent",                 "CharController",                1210, kParsed, true},
    {"EntityComponent",                         "Entity",                        1218, kParsed, false},
    {"BushControllerComponent",                 "BushController",                1226, kParsed, true},
    {"ElevatorControllerComponent",             "ElevatorController",            1234, kParsed, true},
    {"PressureTriggerComponent",                "PressureTrigger",               1242, kParsed, true},
    {"DoorControllerComponent",                 "DoorController",                1250, kParsed, true},
    {"ProgramComponent",                        "Program",                       1258, kUpdate, false},
    {"MonsterEntityComponent",                  "MonsterEntity",                 1266, kParsed, true},
    {"PhysicsObjectComponent",                  "PhysicsObject",                 1274, kParsed, false},
    {"BreakableObjectComponent",                "BreakableObject",               1282, kParsed, true},
    {"EntityControllerComponent",               "EntityController",              1290, kParsed, true},
    {"EntityActionComponent",                   "EntityAction",                  1298, kParsed, true},
    {"PhysicsPlatformComponent",                "PhysicsPlatform",               1306, kParsed, false},
    {"EntityInfoComponent",                     "EntityInfo",                    1314, kParsed, false},
    {"HeroEntityComponent",                     "HeroEntity",                    1322, kParsed, true},
    {"BackgroundComponent",                     "Background",                    1602, kRender, false},
    {"PropertiesComponent",                     "Properties",                    1682, kParsed, false},
    {"ParticleEmitterComponent",                "ParticleEmitter",               2002, kBoth,   false},
    {"ParticleComponent",                       "Particle",                      2010, kRender, false},
    {"FireEmitterComponent",                    "FireEmitter",                   2026, kBoth,   false},
    {"SimpleGlowComponent",                     "SimpleGlow",                    2034, kBoth,   false},
    {"ParticleObjectComponent",                 "ParticleObject",                2042, kRender, false},
    {"OrbitControllerComponent",                "OrbitController",               2050, kUpdate, true},
    {"MonsterControllerComponent",              "MonsterController",             2418, kParsed, true},
    {"WalkingMonsterControllerComponent",       "WalkingMonsterController",      2426, kParsed, true},
    {"ChargingMonsterControllerComponent",      "ChargingMonsterController",     2434, kParsed, true},
    {"SnappingMonsterControllerComponent",      "SnappingMonsterController",     2442, kParsed, true},
    {"AttackComponent",                         "Attack",                        2450, kParsed, true},
    {"LeapingMonsterControllerComponent",       "LeapingMonsterController",      2458, kParsed, true},
    {"SkellyMonsterControllerComponent",        "SkellyMonsterController",       2466, kParsed, true},
    {"StaticMonsterControllerComponent",        "StaticMonsterController",       2474, kParsed, true},
    {"ShootingMonsterControllerComponent",      "ShootingMonsterController",     2482, kParsed, true},
    {"BatMonsterControllerComponent",           "BatMonsterController",          2490, kParsed, true},
    {"BouncingMonsterControllerComponent",      "BouncingMonsterController",     2498, kParsed, true},
    {"MonsterDeathControllerComponent",         "MonsterDeathController",        2506, kParsed, true},
    {"GenericMonsterControllerComponent",       "GenericMonsterController",      2514, kParsed, true},
    {"SwingableWeaponComponent",                "SwingableWeapon",               3202, kParsed, false},
    {"SwingableWeaponControllerComponent",      "SwingableWeaponController",     3210, kParsed, true},
    {"SwingComponent",                          "Swing",                         3218, kParsed, false},
    {"WeaponGlowComponent",                     "WeaponGlow",                    3226, kRender, false},
    {"WeaponTrailComponent",                    "WeaponTrail",                   3234, kRender, false},
    {"PortalComponent",                         "Portal",                        4002, kParsed, true},
    {"SpawnPointComponent",                     "SpawnPoint",                    4010, kParsed, false},
    {"CollectableItemComponent",                "CollectableItem",               4018, kParsed, true},
    {"TouchableComponent",                      "Touchable",                     4034, kParsed, true},
    {"ItemDropComponent",                       "ItemDrop",                      4042, kParsed, true},
    {"OverlayTextComponent",                    "OverlayText",                   4050, kRender, false},
    {"PortalEffectComponent",                   "PortalEffect",                  4058, kRender, false},
    {"MagicBoltComponent",                      "MagicBolt",                     4402, kParsed, true},
    {"MagicExplosionComponent",                 "MagicExplosion",                4410, kParsed, true},
    {"SkillComponent",                          "Skill",                         4418, kParsed, true},
    {"MagicSpellCastComponent",                 "MagicSpellCast",                4426, kParsed, true},
    {"FireBreathComponent",                     "FireBreath",                    4434, kParsed, true},
    {"ProjectileControllerComponent",           "ProjectileController",          4442, kParsed, true},
    {"MagicBombComponent",                      "MagicBomb",                     4450, kParsed, true},
    {"MagicHookshotComponent",                  "MagicHookshot",                 4458, kParsed, true},
    {"SpellComponent",                          "Spell",                         4466, kParsed, true},
};

// One payload field can carry more than one ClassName string in shipping data:
// the shape component is written as either "Shape" or "UtilityShape" (the
// corpus uses UtilityShape for collision helpers, Shape for pure geometry).
struct Alias { const char* short_name; const char* class_name; };
const Alias kAliases[] = {
    {"UtilityShape", "ShapeComponent"},
    {"CollisionShape", "CollisionShapeComponent"},
    {"Program", "ProgramComponent"},
    {"Glow", "SimpleGlowComponent"},
    {"TextureMapping", "TextureMappingComponent"},
};

constexpr size_t kTypeCount = sizeof(kTypes) / sizeof(kTypes[0]);

} // namespace

const std::vector<ComponentType>& component_types() {
    // The stage column is derived from the code that actually implements each
    // stage (behaviour.cpp for Update, visual.cpp for Render), so the coverage
    // report can never claim more recovery than exists.
    static const std::vector<ComponentType> table = [] {
        std::vector<ComponentType> rows(kTypes, kTypes + kTypeCount);
        for (ComponentType& row : rows) {
            if (behaviour_has_tick(row.class_name) || behaviour_has_tick(row.short_name))
                row.stages = row.stages | Stage::Updated;
            if (visual_has_item(row.class_name) || visual_has_item(row.short_name))
                row.stages = row.stages | Stage::Rendered;
        }
        return rows;
    }();
    return table;
}

const ComponentType* component_type_by_class(const std::string& class_name) {
    for (const auto& t : kTypes)
        if (class_name == t.class_name) return &t;
    for (const auto& a : kAliases)
        if (class_name == a.short_name) return component_type_by_class(a.class_name);
    return nullptr;
}

const ComponentType* component_type_by_short(const std::string& short_name) {
    for (const auto& t : kTypes)
        if (short_name == t.short_name) return &t;
    for (const auto& a : kAliases)
        if (short_name == a.short_name) return component_type_by_class(a.class_name);
    return nullptr;
}

const ComponentType* component_type_by_field(uint32_t payload_field) {
    for (const auto& t : kTypes)
        if (payload_field == t.payload_field) return &t;
    return nullptr;
}

CoverageReport coverage() {
    CoverageReport report;
    for (const auto& t : kTypes) {
        ++report.total;
        if (has(t.stages, Stage::Parsed))       ++report.parsed;
        if (has(t.stages, Stage::Instantiated)) ++report.instantiated;
        if (has(t.stages, Stage::Updated))      ++report.updated;
        if (has(t.stages, Stage::Rendered))     ++report.rendered;
        if (t.controller)                       ++report.controllers;
    }
    return report;
}

std::string short_name_for_class(const std::string& class_name) {
    const ComponentType* t = component_type_by_class(class_name);
    if (t) return t->short_name;
    static const char kSuffix[] = "Component";
    if (class_name.size() > sizeof(kSuffix) - 1 &&
        class_name.compare(class_name.size() - (sizeof(kSuffix) - 1), sizeof(kSuffix) - 1, kSuffix) == 0)
        return class_name.substr(0, class_name.size() - (sizeof(kSuffix) - 1));
    return class_name;
}

} // namespace caver
