#include "ruby/caver/game/game_scene_controller.h"

#include <cmath>
#include <cstdio>
#include <filesystem>

#include "ruby/caver/program_host.h"

namespace fs = std::filesystem;

namespace caver {
namespace game {
namespace {

// The hero template the engine instantiates, straight from
// GameSceneController::CreateHeroObjectAt: LibraryWithName("hiro") then
// TemplateForName("hiro"), with the object identifier "hero".
constexpr const char* kHeroLibrary  = "hiro";
constexpr const char* kHeroTemplate = "hiro";
constexpr const char* kHeroObjectIdentifier = "hero";

// SpawnHeroAt falls back to this identifier when the requested one is empty or
// missing (the literal in the function is "spawn_default").
constexpr const char* kDefaultSpawnPoint = "spawn_default";

// InitWithScene (0x313F1C): Camera::SetPerspectiveProjection(0.34907, 1.0, 50.0, 20000.0)
// — radians, aspect, near, far. The four dwords it then stores into the
// CameraController are the focus offset per device profile; type 0 (the phone
// build) reads dword_314038/314040 = 1190.0/187.0 and type 1 (tablet) reads
// dword_31403C/314044 = 1540.0/242.0.
constexpr float kCameraFovRadians     = 0.34907f;
constexpr float kCameraNear           = 50.0f;
constexpr float kCameraFar            = 20000.0f;
constexpr float kCameraOffsetPhoneY   = 187.0f;
constexpr float kCameraOffsetPhoneZ   = 1190.0f;
constexpr float kCameraOffsetTabletY  = 242.0f;
constexpr float kCameraOffsetTabletZ  = 1540.0f;
constexpr bool  kCameraFocusOffsetTablet = false;   // device type 0 by default

// CreateHeroObjectAt: HealthComponent max health = 2 * ExperienceLevel + 4.
int hero_max_health(int experience_level) { return 2 * experience_level + 4; }

// `hiro` authors no PhysicsObjectComponent at all (its components are
// CharController, CollisionShape, HeroEntity, animation/weapon/particle parts),
// so the hero's gravity is the `Caver::PhysicsObjectState` constructor
// (0x204534) installs, read field by field:
//   +40  max speed        = 1143930880 = 680.0
//   +60  drag coefficient = 0
//   +64..+76 rotation     = (0, 1, 1, 0)
//   +96 / +100            = -1082130432 / 1145569280 = -1.0 / 808.0  (gravity pair)
//   +104 useGravity       = 1
// `PhysicsObjectState::UpdateObjectState` (0x2048C0) then builds the per-step
// acceleration from the 2x2 at +64..+76 rotated onto the vector at +80/+84 and
// integrates `position += (velocity + groundVelocity) * dt`, so the engine's
// working unit is 808 units/s^2 straight down with a 680 unit/s speed clamp.
constexpr float   kEntityGravityMagnitude = 808.0f;   // constructor +100 = 1145569280
constexpr float   kPhysicsMaxSpeed         = 680.0f;   // constructor +40  = 1143930880
constexpr caver::Vec2 kEntityGravityDirection{0.0f, -1.0f};

// CharControllerComponent::Update's own speed/acceleration table, decoded from
// the immediates it stores into entity+104 (target horizontal speed, physics+44)
// and entity+108 (acceleration, physics+48):
//   ground jump / on solid ground  1153138688 = 1520.0
//   airborne                        1145569280 =  800.0
//   pushed (entity+201 set)         1148846080 = 1032.0
//   hurt                            1133903872 =  300.0
//   aimed at a slow walk            110.0
// ``hiro``'s own run speed is 230 and PhysicsObjectState's max speed is 680, so
// the acceleration model is a linear ease at 1520 u/s^2 while the feet are on a
// solid surface.
constexpr float kGroundAcceleration = 1520.0f;
constexpr float kAirAcceleration    = 800.0f;
// The air jump's flat boost: `this[89] = 1112014848` = 50.0 (LABEL_191).
constexpr float kAirJumpBoost      = 50.0f;

float field_or(const RuntimeComponent* component, const char* name, float fallback) {
    if (!component) return fallback;
    const RuntimeField* field = component->field(name);
    if (!field) return fallback;
    if (field->wire == proto::WIRE_VARINT) return static_cast<float>(field->varint_value);
    return field->float_value;
}

const RuntimeComponent* find_component(const RuntimeObject* object, const std::string& class_name) {
    if (!object) return nullptr;
    for (const auto& component : object->components)
        if (component.is(class_name)) return &component;
    return nullptr;
}

// av::'s parsed collision shape -> the engine's CollisionShapeComponent. This is
// the only bridge between the loader and the recovered Caver collision: the
// loader names the geometry, collision.cpp decides what it does.
bool shape_from_data(const av::CollisionShapeData& shape, caver::CollisionShapeComponent& out) {
    switch (shape.type) {
        case av::COLL_RECT:
            out.shape_type = caver::kShapeRectangle;
            out.rect = caver::Rect{shape.rect[0], shape.rect[1], shape.rect[2], shape.rect[3]};
            break;
        case av::COLL_CIRCLE:
            out.shape_type = caver::kShapeCircle;
            out.circle.center = caver::Vec2{shape.circle_center[0], shape.circle_center[1]};
            out.circle.radius = shape.circle_radius;
            break;
        case av::COLL_POLYGON:
            out.shape_type = caver::kShapePolygon;
            for (size_t i = 0; i + 1 < shape.polygon_points.size(); i += 2)
                out.polygon.vertices.push_back(
                    caver::Vec2{shape.polygon_points[i], shape.polygon_points[i + 1]});
            if (out.polygon.vertices.size() < 3) return false;
            break;
        default:
            return false;
    }
    out.enabled         = shape.enabled;
    out.collides        = shape.collides;
    out.is_ground       = shape.is_ground;
    out.unsafe_ground   = shape.unsafe_ground;
    out.receives_damage = shape.receives_damage;
    out.inflicts_damage = shape.inflicts_damage;
    out.bone_controlled = shape.bone_controlled;
    out.friction        = shape.friction;
    out.min_depth       = shape.min_depth;
    out.max_depth       = shape.max_depth;
    out.special_type    = shape.special_type;
    return true;
}

// ShapeComponent payload -> geometry. The payload is the ShapeComponent
// submessage the file carried on its slot; its geometry lives in the nested
// Rectangle / Circle / Polygon message, so the fields are read two levels down
// exactly as the binary's ShapeComponent::LoadFromProtobufMessage does
// (0x29CACC: `v5 << 31` = rectangle set, `v5 & 2` = circle set, `v5 & 4` =
// polygon set).
//
// Field numbers, confirmed against the shipped bytes:
//   hiro's shape payload = 0a14 0d 000000c1 15 000008c2 1d 00008041 25 00006042
//   i.e. field 1 (LEN, 20 bytes) = Rectangle{1:X -8, 2:Y -34, 3:Width 16,
//   4:Height 56}. The schema's "Tag 10 / 18 / 26" are FileRift TAG values (raw
//   protobuf keys), i.e. field 1 wire 2, field 2 wire 2, field 3 wire 2 — the
//   same trap `CollisionShapeComponent`'s Tag 24/32/53/61/88 hide (fields
//   3/4/6/7/11).
bool shape_from_payload(const RuntimePayload& payload, caver::CollisionShapeComponent& out) {
    bool any = false;
    try {
        proto::Reader reader(payload.bytes);
        proto::Field field;
        while (reader.read_field(field)) {
            if (field.wire_type != proto::WIRE_LEN) continue;
            if (field.field_number == 1) {    // Rectangle (Tag 10)
                float v[4] = {0, 0, 0, 0};
                proto::Reader nested(field.bytes_val);
                proto::Field inner;
                while (nested.read_field(inner)) {
                    if (inner.wire_type == proto::WIRE_I32 && inner.field_number >= 1 &&
                        inner.field_number <= 4)
                        v[inner.field_number - 1] = inner.float_val;
                }
                out.shape_type = caver::kShapeRectangle;
                out.rect = caver::Rect{v[0], v[1], v[2], v[3]};
                any = true;
            } else if (field.field_number == 2) {   // Circle (Tag 18)
                proto::Reader nested(field.bytes_val);
                proto::Field inner;
                float center[2] = {0, 0};
                float radius = 0.0f;
                while (nested.read_field(inner)) {
                    if (inner.wire_type == proto::WIRE_LEN && inner.field_number == 1) {
                        proto::Reader v2(inner.bytes_val);
                        proto::Field c;
                        while (v2.read_field(c))
                            if (c.wire_type == proto::WIRE_I32 && c.field_number <= 2)
                                center[c.field_number - 1] = c.float_val;
                    } else if (inner.wire_type == proto::WIRE_I32 && inner.field_number == 2) {
                        radius = inner.float_val;
                    }
                }
                out.shape_type = caver::kShapeCircle;
                out.circle.center = caver::Vec2{center[0], center[1]};
                out.circle.radius = radius;
                any = true;
            } else if (field.field_number == 3) {   // Polygon (Tag 26)
                proto::Reader nested(field.bytes_val);
                proto::Field inner;
                while (nested.read_field(inner)) {
                    if (inner.wire_type == proto::WIRE_LEN && inner.field_number == 1) {   // Vertex
                        proto::Reader v2(inner.bytes_val);
                        proto::Field c;
                        caver::Vec2 vertex;
                        while (v2.read_field(c))
                            if (c.wire_type == proto::WIRE_I32 && c.field_number <= 2)
                                (c.field_number == 1 ? vertex.x : vertex.y) = c.float_val;
                        out.polygon.vertices.push_back(vertex);
                    }
                }
                if (out.polygon.vertices.size() >= 3) {
                    out.shape_type = caver::kShapePolygon;
                    any = true;
                }
            }
        }
    } catch (...) {
        return false;
    }
    return any;
}

} // namespace

GameSceneController::GameSceneController() = default;
GameSceneController::~GameSceneController() = default;

bool GameSceneController::init_with_scene(PlayerProfile& profile, const std::string& level_name,
                                          std::string* error) {
    profile_ = &profile;
    level_name_ = level_name;
    spawn_point_used_.clear();
    input_ = PlayerInput{};

    // `PathForResourceOfType(level, "scene")` -> <resources>/<level>.scene
    const std::string scene_path = PlayerProfile::resource_path(level_name, "scene");
    std::error_code ec;
    if (!fs::exists(scene_path, ec)) {
        if (error) *error = "level scene not found: " + scene_path;
        return false;
    }

    scene_ = av::scene_load(scene_path);
    if (scene_.objects.empty()) {
        if (error) *error = "level scene parsed to zero objects: " + scene_path;
        return false;
    }

    level_title_ = profile.map().title_for_level(level_name);
    if (level_title_.empty()) level_title_ = level_name;

    // The scene's own geometry is the collision world, built from the real
    // CollisionShapeComponent / GroundPolygonComponent payloads and resolved with
    // the recovered Caver algorithms (see collision.h) — not the editor's wall
    // segments.
    build_collision_world();

    // The level's ObjectLibraries: the ones embedded in the .scene itself, plus
    // the ones it imports from disk. This is the SCL runtime the level needs for
    // Scene.CreateObject, item drops, spell spawns and weapon creation — the hero
    // itself is instantiated through it below.
    libraries_.clear();
    libraries_.add_search_path(fs::path(scene_path).parent_path().string());
    if (!scene_.filepath.empty())
        libraries_.add_search_path(fs::path(scene_.filepath).parent_path().string());
    for (const auto& embedded : scene_.object_libraries)
        libraries_.load_bytes(embedded, std::string(), scene_path, nullptr);
    for (const auto& path : scene_.imported_library_paths)
        libraries_.load_file(path, nullptr);

    // The hiro library is not always imported by a scene, but CreateHeroObjectAt
    // looks it up by name regardless, so make sure it is present the same way the
    // engine's own resource loader would find it.
    if (!libraries_.has(kHeroLibrary)) {
        bool ok = false;
        libraries_.load_by_name(kHeroLibrary, nullptr);
        (void)ok;
    }

    runtime_.clear();
    runtime_.set_library_manager(&libraries_);
    if (!runtime_.load(scene_)) {
        if (error) *error = "runtime refused the scene: " + scene_path;
        return false;
    }

    // The Lua 5.1 host: the level's programs (Program.String/Bytes plus every
    // handler field) run against the runtime from this point on.
    program_host_ = std::make_unique<ProgramHost>();
    std::string host_error;
    if (!program_host_->init(&host_error)) {
        if (error) *error = "Lua host init failed: " + host_error;
        program_host_.reset();
        return false;
    }
    runtime_.set_program_host(program_host_.get());

    // The runtime loaded the objects before the host existed, so start the
    // level's keep-active programs now. This is where a scene's OnLoad scripts
    // and every object's `while true do … Program.Wait` loop begin, i.e. the
    // level's own behaviour actually starts running.
    program_host_->start(runtime_);

    return true;
}

bool GameSceneController::spawn_hero_at(const std::string& identifier) {
    // Scene::ObjectWithIdentifier(identifier), falling back to "spawn_default".
    std::string wanted = identifier.empty() ? kDefaultSpawnPoint : identifier;
    const av::SceneObject* spawn_object = nullptr;
    for (const auto& object : scene_.objects) {
        if (object.name == wanted) { spawn_object = &object; break; }
    }
    if (!spawn_object) {
        wanted = kDefaultSpawnPoint;
        for (const auto& object : scene_.objects) {
            if (object.name == wanted) { spawn_object = &object; break; }
        }
    }
    if (!spawn_object) {
        note("no spawn point in " + level_name_ + " (looked for '" + identifier +
             "' and '" + std::string(kDefaultSpawnPoint) + "')");
        return false;
    }
    spawn_point_used_ = spawn_object->name;

    // The hero starts at the spawn object's own position plus its
    // SpawnPointComponent offset — exactly the arithmetic in SpawnHeroAt.
    const float hero_position[3] = {
        spawn_object->pos_x + spawn_object->spawn_offset[0],
        spawn_object->pos_y + spawn_object->spawn_offset[1],
        spawn_object->pos_z + spawn_object->spawn_offset[2],
    };

    // Keep the archetype the hero is instantiated from: the render path draws
    // the hero out of its own ModelComponent, so nothing about what Hiro looks
    // like (including armor-swapped models) is written down in the shell.
    av::SclTemplateEntry entry;
    if (libraries_.find_template(kHeroTemplate, &entry)) {
        hero_archetype_ = entry;
        hero_archetype_valid_ = true;
    } else {
        hero_archetype_valid_ = false;
        note(std::string("hero archetype '") + kHeroTemplate + "' not in any loaded library");
    }

    RuntimeObject* hero = runtime_.spawn(kHeroTemplate, kHeroObjectIdentifier, hero_position);
    if (!hero) return false;

    runtime_.set_hero(kHeroObjectIdentifier);
    read_hero_tuning();
    add_hero_object_to_scene();
    build_hero_collision();
    install_ground_query();

    camera_[0] = hero->pos[0];
    camera_[1] = hero->pos[1];
    camera_velocity_[0] = 0.0f;
    camera_velocity_[1] = 0.0f;
    return true;
}

void GameSceneController::add_hero_object_to_scene() {
    RuntimeObject* hero = runtime_.hero();
    if (!hero) return;

    // HeroEquipmentManager::Init + EquipItem: the hero's model comes from the
    // equipped armor, and the weapon is whatever HighestLevelItemOfType(Weapon)
    // resolves to in the save. A fresh profile owns nothing, so the hiro
    // template's own ModelComponent stands.
    if (profile_) {
        const CharacterState& character = profile_->state().character();
        if (const ItemDef* armor = character.equipped_armor(profile_->data()))
            note("equipped armor: " + armor->name);
        else
            note("equipped armor: none (fresh profile owns nothing)");
        if (const ItemDef* weapon = character.equipped_weapon(profile_->data()))
            note("equipped weapon: " + weapon->name);
        else
            note("equipped weapon: none");

        // CreateHeroObjectAt installs a HealthComponent whose maximum is derived
        // from the save's ExperienceLevel.
        const int max_health = hero_max_health(character.experience_level);
        hero->behaviour.health.max_health = static_cast<float>(max_health);
        hero->behaviour.health.health = character.current_health > 0
                                            ? static_cast<float>(character.current_health)
                                            : static_cast<float>(max_health);
        note("max health: " + std::to_string(max_health));
    }
    note("hero added to scene");
}

void GameSceneController::read_hero_tuning() {
    const RuntimeObject* hero = runtime_.hero();
    const RuntimeComponent* controller = find_component(hero, "CharController");
    tuning_ = HeroTuning{};
    if (!controller) return;

    // CharControllerComponent fields, read by name out of the loaded archetype.
    tuning_.run_speed          = field_or(controller, "NormalRunSpeed", 0.0f);
    tuning_.fast_run_speed     = field_or(controller, "FastRunSpeed", tuning_.run_speed);
    tuning_.jump_speed         = field_or(controller, "JumpSpeed", 0.0f);
    tuning_.max_jump_time      = field_or(controller, "NormalMaxJumpTime", 0.0f);
    tuning_.fast_max_jump_time = field_or(controller, "FastMaxJumpTime", tuning_.max_jump_time);
    tuning_.has_jump           = tuning_.jump_speed > 0.0f;

    // The hero's PhysicsObjectComponent supplies its gravity. It is reported even
    // when zero, because "the archetype leaves gravity unset" and "the recovered
    // physics ignored it" are different bugs and this distinguishes them.
    if (const RuntimeComponent* body = find_component(hero, "PhysicsObject"))
        tuning_.gravity_magnitude = field_or(body, "GravityMagnitude", 0.0f);
}

void GameSceneController::build_hero_collision() {
    // The hero fights and stands on its OWN CollisionShape, the one the `hiro`
    // archetype authors — the same list every other object carries. It is added
    // to the collision world on the hero's slot, so the level's real geometry is
    // what stops it, not a synthesised circle.
    // The hero's shape is read from the object the ObjectLibrary actually spawned
    // (`LibraryWithName("hiro").TemplateForName("hiro")` -> identifier "hero"),
    // whose component payloads are decoded. A bare SCL template carries class
    // names but no payload bytes, so parsing the archetype itself yields a shape
    // with no geometry — the spawned object is the authoritative copy.
    hero_radius_ = 0.0f;
    const int hero_index = hero_slot_;
    size_t hero_shapes = 0;
    const RuntimeObject* hero = runtime_.hero();
    const RuntimeComponent* shape_component = find_component(hero, "CollisionShape");
    if (!shape_component) shape_component = find_component(hero, "Shape");
    if (shape_component) {
        for (const RuntimePayload& payload : shape_component->payloads) {
        }
        // ShapeComponent geometry slots: field 10 Rectangle, 18 Circle, 26 Polygon
        // (`scene_collision.h` documents the same numbers from the binary).
        for (const RuntimePayload& payload : shape_component->payloads) {
            caver::CollisionShapeComponent component;
            component.owner_index = hero_index;
            component.owner = "hero";
            component.identifier = "hero";
            if (!shape_from_payload(payload, component)) continue;
            // The hero's own shape is its body, never the level's floor.
            component.is_ground = false;
            component.object_depth = hero ? hero->pos[2] : 0.0f;
            if (component.shape_type == caver::kShapeRectangle)
                hero_radius_ = component.rect.w * 0.5f;
            else if (component.shape_type == caver::kShapeCircle)
                hero_radius_ = component.circle.radius;
            // The ground probe CharControllerComponent::Update sweeps is built
            // from the object's own AABB and depth band, so keep both here.
            const caver::Rect local = component.world_shape().bounds();
            if (local.w > 0.0f && local.h > 0.0f) hero_local_bounds_ = local;
            shapes_.add(std::move(component));
            hero_entry_index_ = static_cast<int>(shapes_.entries().size()) - 1;
            ++hero_shapes;
        }
        // Behaviour flags off the CollisionShapeComponent slot. The depth band
        // matters as much as the geometry: `hiro` authors MinDepth -15 / MaxDepth
        // 15, and Scene::LineSegmentIntersectsGround only accepts a candidate
        // whose own band overlaps the query's — a zero band matches nothing but a
        // zero band, so the character never found a floor.
        hero_min_depth_ = field_or(shape_component, "MinDepth", 0.0f);
        hero_max_depth_ = field_or(shape_component, "MaxDepth", 0.0f);
        hero_world_min_depth_ = hero_min_depth_;
        hero_world_max_depth_ = hero_max_depth_;
        for (size_t i = 0; i < shapes_.entries().size(); ++i) {
            if (shapes_.entries()[i].component.owner_index != hero_index) continue;
            caver::CollisionShapeComponent& component = shapes_.mutable_entries()[i].component;
            component.collides = field_or(shape_component, "Collides", 1.0f) != 0.0f;
            component.enabled = field_or(shape_component, "Enabled", 1.0f) != 0.0f;
            component.receives_damage = field_or(shape_component, "ReceivesDamage", 0.0f) != 0.0f;
            component.inflicts_damage = field_or(shape_component, "InflictsDamage", 0.0f) != 0.0f;
            component.unsafe_ground = field_or(shape_component, "UnsafeGround", 0.0f) != 0.0f;
            component.friction = field_or(shape_component, "Friction", 0.0f);
            component.min_depth = hero_min_depth_;
            component.max_depth = hero_max_depth_;
            component.world_min_depth = hero_min_depth_;
            component.world_max_depth = hero_max_depth_;
        }
    }
    if (hero_radius_ <= 0.0f) hero_radius_ = 8.0f;   // hiro's authored half-width
    note("hero collision: " + std::to_string(hero_shapes) + " shapes from the spawned object, "
         "radius " + std::to_string(hero_radius_) + ", band [" +
         std::to_string(hero_world_min_depth_) + "," + std::to_string(hero_world_max_depth_) +
         "] (authored [" + std::to_string(hero_min_depth_) + "," +
         std::to_string(hero_max_depth_) + "])");

    // The hero's body state (PhysicsObjectComponent's own numbers).
    hero_body_ = caver::PhysicsBody{};
    hero_body_.radius = hero_radius_;
    if (tuning_.gravity_magnitude > 0.0f) {
        // The archetype (or a script) authored gravity: PhysicsObjectComponent
        // stores it already scaled by 800.
        hero_body_.gravity_magnitude = tuning_.gravity_magnitude;
        hero_body_.gravity_direction = caver::Vec2{0.0f, -1.0f};
    } else {
        // PhysicsObjectState's own defaults, straight from its constructor.
        hero_body_.gravity_magnitude = kEntityGravityMagnitude;
        hero_body_.gravity_direction = kEntityGravityDirection;
    }
    hero_body_.use_gravity = true;
    if (const RuntimeObject* hero = runtime_.hero()) {
        hero_body_.position = caver::Vec2{hero->pos[0], hero->pos[1]};
        hero_body_.velocity = caver::Vec2{hero->vel[0], hero->vel[1]};
    }

}

void GameSceneController::build_collision_world() {
    shapes_.clear();
    shape_transforms_.clear();
    contacts_.clear();
    shape_transforms_.resize(scene_.objects.size() + 1);   // +1 = the hero slot
    hero_slot_ = static_cast<int>(scene_.objects.size());

    size_t from_components = 0;
    size_t from_ground = 0;
    size_t ground_flags = 0;
    size_t boundary_loops = 0;
    for (size_t i = 0; i < scene_.objects.size(); ++i) {
        const av::SceneObject& object = scene_.objects[i];
        const av::CollisionData data = av::collision_parse(object);
        for (const av::CollisionShapeData& shape : data.shapes) {
            caver::CollisionShapeComponent component;
            component.owner_index = static_cast<int>(i);
            component.owner = object.name;
            component.identifier = object.name;
            component.object_depth = object.pos_z;   // SceneObject::Depth (tag 5)
            if (!shape_from_data(shape, component)) continue;
            if (component.is_ground) ++ground_flags;
            // A level-boundary loop (one polygon spanning the whole area) is
            // ground, never a wall: a character inside it is contained, not
            // penetrating, and resolving that depth launches it out of the map.
            // The ground query still sees it.
            const caver::Rect shape_bounds = component.world_shape().bounds();
            if (component.shape_type == caver::kShapePolygon &&
                (shape_bounds.w > 2000.0f || shape_bounds.h > 2000.0f)) {
                component.is_wall = false;
                ++boundary_loops;
            }
            shapes_.add(std::move(component));
            ++from_components;
        }
        // GroundPolygonComponent edges: walkable surfaces in their own right
        // (GroundPolygonComponent field 10/18, collides + friction + depth band).
        for (const av::CollisionData::GroundPolygon& polygon : data.ground_polygons) {
            caver::CollisionShapeComponent component;
            component.owner_index = static_cast<int>(i);
            component.owner = object.name;
            component.identifier = object.name;
            component.object_depth = object.pos_z;   // SceneObject::Depth (tag 5)
            component.shape_type = caver::kShapePolygon;
            for (size_t k = 0; k + 1 < polygon.points.size(); k += 2)
                component.polygon.vertices.push_back(caver::Vec2{polygon.points[k], polygon.points[k + 1]});
            if (component.polygon.vertices.size() < 3) continue;
            component.collides = polygon.collides;
            component.is_ground = true;
            component.unsafe_ground = polygon.unsafe_ground;
            component.friction = polygon.friction;
            component.min_depth = polygon.min_depth;
            component.max_depth = polygon.max_depth;
            shapes_.add(std::move(component));
            ++from_ground;
        }
    }

    note("collision: " + std::to_string(from_components) + " shape components (" +
         std::to_string(ground_flags) + " ground), " + std::to_string(from_ground) +
         " ground polygons, " + std::to_string(boundary_loops) + " boundary loops, " +
         std::to_string(shapes_.shape_count()) + " entries");

    // The walkable terrain: a level's floor is the GroundMesh surfaces it
    // carries, so the character's ground query runs against these triangles
    // (see build_terrain / terrain_probe). Collision shapes stay the walls.
    build_terrain();
}

void GameSceneController::install_ground_query() {
    // The runtime's ground query exists for NPC physics; it works on world Y, so
    // it asks the collision world for the surface at the hero's feet. It is the
    // same query the hero resolve uses, kept in one place.
    RuntimeScene* scene = &runtime_;
    runtime_.set_ground_query([scene](float x, float z) {
        // Without a controller reference here, fall back to the object's current
        // height: callers that need a precise probe use ground_surface().
        const RuntimeObject* hero = scene->hero();
        return hero ? hero->pos[1] : 0.0f;
    });
}

void GameSceneController::game_control_button_down(GameControlButton button) {
    input_.button_down(button);
}

void GameSceneController::game_control_button_up(GameControlButton button) {
    input_.button_up(button);
}

void GameSceneController::apply_input_to_hero(float dt) {
    RuntimeObject* hero = runtime_.hero();
    if (!hero) return;

    BehaviourState& b = hero->behaviour;

    // Horizontal: the buttons are an axis, and the speed comes from the
    // archetype's CharControllerComponent.
    const float speed = tuning_.run_speed > 0.0f ? tuning_.run_speed : b.move_speed;
    hero->vel[0] = input_.move_axis * speed;
    if (input_.move_axis != 0.0f) {
        const int facing = input_.move_axis < 0.0f ? -1 : 1;
        hero->facing = facing;
        b.facing = facing;
        if (b.action != EntityAction::Attack && b.action != EntityAction::Cast)
            b.action = EntityAction::Walk;
    } else if (b.action == EntityAction::Walk) {
        b.action = EntityAction::Idle;
    }

    // ── The jump, as CharControllerComponent::Update (0x2AB9E0) runs it ─────
    //
    // The engine's jump is not a one-shot impulse. Its state machine (this+81)
    // reaches state 3 with a timer (+82) and a window (+90 = NormalMaxJumpTime),
    // and *every frame* the window is open it writes
    //
    //     entity+112 = JumpSpeed (+88) + boost (+89)     // physics+52
    //     entity+116 = 1                                 // physics+56
    //
    // PhysicsObjectState::UpdateSpeedComponents then takes the forced branch
    // (`if (*(this+56)) this[21] = this[13]`) and replaces the vertical velocity
    // outright, so gravity does not touch it while the jump is live:
    //
    //     ``if (v51) *(float *)(entity + 112) = v48``
    //
    // The window closes when the timer passes MaxJumpTime, or when the button is
    // released past the first 20% of it (``v49 <= v50 * 0.2`` keeps a tap worth at
    // least a fifth of the arc). That constant-velocity rise is the whole reason
    // Swordigo's jump clears ~96 units instead of the ~39 a single 250 u/s
    // impulse reaches under the same 808 u/s^2 gravity.
    //
    // The air jump is its own branch (LABEL_191): the boost becomes a flat 50,
    // the timer *starts* at 30% of the window, and it may only be spent once per
    // airborne period (+85).
    const bool held = input_.jump_down;
    const bool grounded = b.physics.on_ground || hero->grounded;
    if (grounded) jump_air_ = false;

    if ((input_.jump_pressed || (held && !jump_held_))) {
        if (grounded && tuning_.has_jump) {
            jump_held_ = true;
            jump_hold_ = 0.0f;                 // the ground jump starts at 0
            jump_boost_ = hero_body_.ground_velocity.y * 0.5f;   // entity+92 * 0.5
            b.physics.on_ground = false;
            hero->grounded = false;
        } else if (!grounded && tuning_.has_jump && !jump_air_ && !jump_held_) {
            jump_held_ = true;
            jump_air_ = true;
            jump_hold_ = fast_jump_window() * 0.3f;   // this[82] = this[90] * 0.3
            jump_boost_ = kAirJumpBoost;              // this[89] = 50.0
        }
    }
    if (std::getenv("OPENSW_TRACE_JUMP") && input_.jump_down) {
        std::fprintf(stderr,
                     "opensw: [jump] pressed=%d down=%d grounded=%d has=%d jump_held=%d t=%.2f "
                     "boost=%.1f pos_y=%.1f vel_y=%.1f\n",
                     input_.jump_pressed ? 1 : 0, input_.jump_down ? 1 : 0, grounded ? 1 : 0,
                     tuning_.has_jump ? 1 : 0, jump_held_ ? 1 : 0, static_cast<double>(jump_hold_),
                     static_cast<double>(jump_boost_), static_cast<double>(hero->pos[1]),
                     static_cast<double>(hero->vel[1]));
    }
    if (jump_held_) {
        const float window = jump_window();
        if (jump_hold_ > window || (!held && jump_hold_ > window * 0.2f)) {
            jump_held_ = false;                    // this+81 = 0: gravity takes over
            jump_boost_ = 0.0f;
        } else {
            // The forced vertical velocity, written every live frame.
            hero->vel[1] = tuning_.jump_speed + jump_boost_;
            jump_hold_ += dt;
        }
    }

    // Attack / cast are action states in the recovered EntityAction machine; the
    // swing and spell objects themselves are created by SwingComponent /
    // SkillComponent, which the C++ behaviour layer drives from these actions.
    if (input_.attack_down && b.action != EntityAction::Attack && b.action != EntityAction::Cast)
        b.action = EntityAction::Attack;
    if (input_.cast_down && b.action != EntityAction::Cast && profile_) {
        if (!profile_->state().character().current_skill.empty())
            b.action = EntityAction::Cast;
    }
    grounded_ = grounded;
}

GameSceneController::CameraFrame GameSceneController::camera_frame() const {
    CameraFrame frame;
    frame.fov_degrees = kCameraFovRadians * 180.0f / 3.14159265358979323846f;   // 0.34907 rad
    frame.near_plane  = kCameraNear;    // 50.0
    frame.far_plane   = kCameraFar;     // 20000.0
    // Device profile: the phone build is the one whose constants the decompiled
    // InitWithScene writes for device type 0 — the same profile the desktop port
    // targets.
    frame.offset[0] = 0.0f;
    frame.offset[1] = kCameraFocusOffsetTablet ? kCameraOffsetTabletY : kCameraOffsetPhoneY;
    frame.offset[2] = kCameraFocusOffsetTablet ? kCameraOffsetTabletZ : kCameraOffsetPhoneZ;
    return frame;
}

av::Camera GameSceneController::camera() const {
    const CameraFrame frame = camera_frame();

    // CameraController::FocusAtPoint sets position = focus + offset and looks at
    // the focus, so the offset is exactly an orbit pitch (atan of its Y over its
    // Z) at that distance. The level's playfield is the scene's XY plane with Z
    // as depth, which is why yaw stays 0.
    const float dy = frame.offset[1];
    const float dz = frame.offset[2];
    const float distance = std::sqrt(dy * dy + dz * dz);

    av::Camera camera;
    camera.fov        = frame.fov_degrees;
    camera.near_plane = frame.near_plane;
    camera.far_plane  = frame.far_plane;
    camera.distance   = distance;
    camera.pitch      = std::atan2(dy, dz) * 180.0f / 3.14159265358979323846f;
    camera.yaw        = 0.0f;
    camera.target[0]  = camera_[0];
    camera.target[1]  = camera_[1];
    camera.target[2]  = 0.0f;
    return camera;
}

void GameSceneController::camera_follow(float dt) {
    RuntimeObject* hero = runtime_.hero();
    if (!hero) return;
    // The controller keeps the camera on the hero with the half extents it was
    // initialised with (GameSceneController::SpawnHeroAt sets them from the device
    // profile). A critically damped follow keeps the same framing without a snap.
    const float target_x = hero->pos[0];
    const float target_y = hero->pos[1];
    const float k = 12.0f;
    camera_velocity_[0] += (target_x - camera_[0]) * k * dt;
    camera_velocity_[1] += (target_y - camera_[1]) * k * dt;
    camera_velocity_[0] *= (1.0f - std::min(1.0f, k * dt * 0.5f));
    camera_velocity_[1] *= (1.0f - std::min(1.0f, k * dt * 0.5f));
    camera_[0] += camera_velocity_[0] * dt;
    camera_[1] += camera_velocity_[1] * dt;
}

void GameSceneController::update(float dt) {
    apply_input_to_hero(dt);
    camera_follow(dt);

    runtime_.update(dt);

    resolve_hero_against_level(dt);

    // The jump's edge flag lives exactly one frame (the button transition), like
    // the engine's touch handling, which sets the pending-jump state for the tick
    // the touch began and clears it on the next.
    input_.jump_pressed = false;
}

void GameSceneController::resolve_hero_against_level(float dt) {
    // The recovered component physics integrates the hero's velocity but knows
    // nothing about the player: in the engine, horizontal travel and the jump
    // window belong to CharControllerComponent, and the walls belong to
    // CollisionShape. Both are resolved here, against the level's real geometry.
    RuntimeObject* hero = runtime_.hero();
    if (!hero) return;

    // ── Horizontal: PhysicsObjectState::UpdateSpeedComponents' eased axis ────
    //
    // The engine never teleports the body sideways. CharController writes the
    // *target* speed into physics+44 (entity+104) and an acceleration into
    // physics+48 (entity+108), and UpdateSpeedComponents walks the current
    // velocity toward the target at `accel * ramp * dt`:
    //
    //     if (fabsf(v16 - this[11]) <= v22 * dt) v24 = this[11];
    //     else v24 = v16 + (v22 * (v21 - v16 > 0 ? 1 : -1)) * dt;
    //
    // with ramp (this[9]) climbing 0.5/s to a cap of 1. Slamming the position
    // instead (the previous behaviour) is what made the character read as
    // "glitched": it had no inertia to absorb the collision pushes.
    const float target_vx = input_.move_axis *
                            (tuning_.run_speed > 0.0f ? tuning_.run_speed : hero->behaviour.move_speed);
    const float accel = hero->grounded ? kGroundAcceleration : kAirAcceleration;
    const float vx = hero->vel[0];
    if (std::fabs(target_vx - vx) <= accel * dt) {
        hero->vel[0] = target_vx;
    } else {
        hero->vel[0] = vx + (target_vx > vx ? accel : -accel) * dt;
    }

    // ── Vertical: gravity, unless the jump currently owns the axis ───────────
    //
    // PhysicsObjectState::UpdateSpeedComponents takes the forced branch while
    // physics+56 is set (`this[21] = this[13]`), which CharController raises for
    // every frame of a live jump. Outside it the two gravity components
    // (+92/+96, default (0, -1)) scaled by the magnitude (+100, default 808)
    // accumulate into the velocity.
    if (!jump_held_) {
        const float gravity = hero_body_.gravity_magnitude * std::fabs(hero_body_.gravity_direction.y);
        if (gravity > 0.0f) hero->vel[1] -= gravity * dt;
    }
    // PhysicsObjectState::UpdateObjectState clamps before it integrates (`v9 =
    // this[10]` = 680).
    if (hero->vel[1] > kPhysicsMaxSpeed) hero->vel[1] = kPhysicsMaxSpeed;
    if (hero->vel[1] < -kPhysicsMaxSpeed) hero->vel[1] = -kPhysicsMaxSpeed;
    hero->pos[0] += hero->vel[0] * dt;
    hero->pos[1] += hero->vel[1] * dt;

    // Everything that stops the hero — floors, walls, slopes, platforms — is the
    // recovered Caver collision loop: hero shape vs the level's
    // CollisionShapeComponents and GroundPolygonComponents, resolved through
    // `CollidesWithShape` and `PhysicsObjectState::HandleGroundCollision`.
    const bool was_grounded = hero->grounded;
    hero->grounded = false;
    hero->behaviour.physics.on_ground = false;
    for (int iteration = 0; iteration < 4; ++iteration) {
        const bool touched = resolve_hero_contacts_once();
        if (!touched) break;
    }
    resolve_hero_ground(dt);
    if (hero->grounded && !was_grounded && hero->vel[1] <= 0.0f) {
        hero->behaviour.action = EntityAction::Land;
        hero->behaviour.action_time = 0.0f;
    }

    // The action state machine. In the engine this is CharControllerComponent::
    // Update; the hero has no EntityController (that is the monster AI), so the
    // locomotion state is decided here: committed states (a swing, a cast) run to
    // completion, everything else follows movement and grounding.
    BehaviourState& state = hero->behaviour;
    if (state.action == EntityAction::Attack || state.action == EntityAction::Cast) {
        state.action_time += dt;
        if (state.action_time >= (state.action_duration > 0.0f ? state.action_duration : 0.30f)) {
            state.action = EntityAction::Idle;
            state.action_time = 0.0f;
        }
    } else if (!hero->grounded) {
        state.action = hero->vel[1] > 0.0f ? EntityAction::Jump : EntityAction::Fall;
    } else if (state.action == EntityAction::Land && state.action_time < 0.12f) {
        state.action_time += dt;
    } else if (input_.move_axis != 0.0f) {
        state.action = EntityAction::Walk;
    } else {
        state.action = EntityAction::Idle;
    }
}

bool GameSceneController::ground_probe(caver::Vec2* out_point, int* out_entry) const {
    // CharControllerComponent::Update builds the query exactly like this:
    //
    //   segment.p0 = (aabb.x + aabb.w * 0.5, aabb.y + aabb.h * 0.5)
    //   segment.p1 = (segment.p0.x, segment.p0.y - (aabb.h * 0.5 + 30))
    //   Scene::LineSegmentIntersectsGround(segment, depth + minDepth, depth + maxDepth)
    //
    // which is the character's AABB centre down to 30 units below its feet. The
    // nearest hit is the surface it stands on (or can jump from).
    const RuntimeObject* hero = runtime_.hero();
    if (!hero || hero_local_bounds_.w <= 0.0f || hero_local_bounds_.h <= 0.0f) return false;
    const float cx = hero->pos[0] + hero_local_bounds_.x + hero_local_bounds_.w * 0.5f;
    const float cy = hero->pos[1] + hero_local_bounds_.y + hero_local_bounds_.h * 0.5f;
    const float drop = hero_local_bounds_.h * 0.5f + 30.0f;
    // The object's world depth band, exactly as CharControllerComponent::Update
    // passes it to Scene::LineSegmentIntersectsGround (0x3769D4):
    //     sceneObject->rotation + WorldMinDepth .. rotation + WorldMaxDepth
    // The shape's own WorldMin/MaxDepth already fold in the rotation once, so the
    // query band is the object's authored Min/MaxDepth scaled by the object's
    // scaling — for `hiro` that is the -15 / 15 its CollisionShape authors.
    const float query_min = hero_world_min_depth_;
    const float query_max = hero_world_max_depth_;
    const int skip = hero_entry_index_;
    bool found = false;
    float best_d2 = 3.4e38f;
    caver::Vec2 best{0.0f, 0.0f};
    int best_entry = -1;
    const auto& entries = shapes_.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        if (static_cast<int>(i) == skip) continue;   // never the hero's own body
        const caver::CollisionShapeComponent& component = entries[i].component;
        if (!component.collides || !component.enabled) continue;
        if (!component.depth_band_overlaps(query_min, query_max)) continue;
        const caver::CollisionShape shape = component.world_shape();
        caver::Vec2 point;
        if (!caver::shape_intersects_line_segment(shape, caver::Vec2{cx, cy},
                                                  caver::Vec2{cx, cy - drop}, &point, nullptr, nullptr))
            continue;
        const float dx = point.x - cx, dy = point.y - cy;
        const float d2 = dx * dx + dy * dy;
        if (d2 < best_d2) {
            best_d2 = d2;
            best = point;
            best_entry = static_cast<int>(i);
            found = true;
        }
    }
    if (!found) return false;
    if (out_point) *out_point = best;
    if (out_entry) *out_entry = best_entry;
    return true;
}

void GameSceneController::build_terrain() {
    terrain_.clear();
    float z_min = 1e30f, z_max = -1e30f;
    for (size_t i = 0; i < scene_.objects.size(); ++i) {
        const av::SceneObject& object = scene_.objects[i];
        if (object.ground_meshes.empty()) continue;
        float matrix[16];
        swk::object_world_matrix(object, matrix);
        for (const av::PODMesh& mesh : object.ground_meshes) {
            const size_t vertex_count = static_cast<size_t>(mesh.num_vertices);
            if (vertex_count == 0 || mesh.positions.size() < vertex_count * 3) continue;
            auto place = [&](uint32_t index, caver::Vec2* out, float* out_z) {
                if (index >= vertex_count) return false;
                const float x = mesh.positions[index * 3 + 0];
                const float y = mesh.positions[index * 3 + 1];
                const float z = mesh.positions[index * 3 + 2];
                out->x = matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12];
                out->y = matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13];
                *out_z = matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14];
                return true;
            };
            for (size_t k = 0; k + 2 < mesh.indices.size(); k += 3) {
                TerrainTriangle tri;
                bool ok = true;
                for (int c = 0; c < 3; ++c)
                    ok = ok && place(mesh.indices[k + static_cast<size_t>(c)], &tri.v[c], &tri.z[c]);
                if (!ok) continue;
                tri.object_index = static_cast<int>(i);
                z_min = std::min(z_min, std::min(tri.z[0], std::min(tri.z[1], tri.z[2])));
                z_max = std::max(z_max, std::max(tri.z[0], std::max(tri.z[1], tri.z[2])));
                terrain_.push_back(tri);
            }
        }
    }
    if (terrain_.empty()) {
        note("terrain: no GroundMesh surfaces in this level");
        return;
    }
    note("terrain: " + std::to_string(terrain_.size()) + " triangles from " +
         std::to_string(scene_.objects.size()) + " objects, depth " +
         std::to_string(z_min) + ".." + std::to_string(z_max));
}

bool GameSceneController::terrain_probe(float x, float from_y, float to_y, float z,
                                        float* out_y) const {
    // A vertical scanline through each terrain triangle: where it crosses the
    // triangle, the TOP of the crossed span is the surface the character lands
    // on. The depth band keeps a far-background sheet from catching a character
    // standing in front of it (the level's depth span is ±255 around the
    // object's own Depth; ±300 covers every shipped level without letting the
    // backdrop win).
    bool found = false;
    float best = -1e30f;
    for (const TerrainTriangle& tri : terrain_) {
        if (tri.z[0] > z + 300.0f && tri.z[1] > z + 300.0f && tri.z[2] > z + 300.0f) continue;
        if (tri.z[0] < z - 300.0f && tri.z[1] < z - 300.0f && tri.z[2] < z - 300.0f) continue;
        float ymin = 1e30f, ymax = -1e30f;
        for (int i = 0; i < 3; ++i) {
            const caver::Vec2& p = tri.v[i];
            const caver::Vec2& q = tri.v[(i + 1) % 3];
            if (std::fabs(q.x - p.x) < 1e-6f) {
                if (std::fabs(x - p.x) < 1e-6f) {
                    ymin = std::min(ymin, std::min(p.y, q.y));
                    ymax = std::max(ymax, std::max(p.y, q.y));
                }
                continue;
            }
            const float t = (x - p.x) / (q.x - p.x);
            if (t < 0.0f || t > 1.0f) continue;
            const float y = p.y + t * (q.y - p.y);
            ymin = std::min(ymin, y);
            ymax = std::max(ymax, y);
        }
        if (ymin > ymax) continue;      // the scanline misses this triangle
        if (to_y > ymax) continue;      // the probe ends above the surface
        const float surface = std::min(from_y, ymax);
        if (surface < ymin) continue;   // the probe passes entirely above it
        if (surface > best) {
            best = surface;
            found = true;
        }
    }
    if (!found) return false;
    if (out_y) *out_y = best;
    return true;
}

void GameSceneController::dump_ground_probe() const {
    // Temporary: every entry whose world AABB spans the hero's x, with the
    // recovered probe's verdict for each. Answers "is there a floor under the
    // spawn point, and what is it made of" without a debugger.
    const RuntimeObject* hero = runtime_.hero();
    if (!hero) return;
    const float hx = hero->pos[0];
    const float hy = hero->pos[1];
    const float feet = hy + hero_local_bounds_.y;
    const float cx = hx + hero_local_bounds_.x + hero_local_bounds_.w * 0.5f;
    const float cy = hy + hero_local_bounds_.y + hero_local_bounds_.h * 0.5f;
    const float drop = hero_local_bounds_.h * 0.5f + 30.0f;
    std::fprintf(stderr, "opensw: [probe] hero x=%.1f y=%.1f feet=%.1f bounds(%.1f,%.1f %.1fx%.1f) depth=%.2f band[%.2f,%.2f]\n",
                 static_cast<double>(hx), static_cast<double>(hy), static_cast<double>(feet),
                 static_cast<double>(hero_local_bounds_.x), static_cast<double>(hero_local_bounds_.y),
                 static_cast<double>(hero_local_bounds_.w), static_cast<double>(hero_local_bounds_.h),
                 static_cast<double>(hero->pos[2]), static_cast<double>(hero_world_min_depth_),
                 static_cast<double>(hero_world_max_depth_));
    int spans = 0;
    const auto& entries = shapes_.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const caver::CollisionShapeComponent& c = entries[i].component;
        const caver::Rect& r = c.world_aabb;
        // A 400-unit window: the spawn point sits on a GroundPolygon strip only a
        // few tens of units wide, and the entry that owns it must be visible here.
        if (hx < r.x - 400.0f || hx > r.x + r.w + 400.0f) continue;
        ++spans;
        caver::Vec2 point;
        const caver::CollisionShape shape = c.world_shape();
        const bool hit = caver::shape_intersects_line_segment(shape, caver::Vec2{cx, cy},
                                                              caver::Vec2{cx, cy - drop}, &point,
                                                              nullptr, nullptr);
        if (spans <= 24)
            std::fprintf(stderr,
                         "opensw: [probe] #%zu '%s' type %d y[%.1f..%.1f] depth %.1f band[%.1f,%.1f] "
                         "enabled=%d collides=%d ground=%d -> hit=%d at y=%.1f\n",
                         i, c.owner.c_str(), static_cast<int>(c.shape_type),
                         static_cast<double>(r.y), static_cast<double>(r.y + r.h),
                         static_cast<double>(c.object_depth), static_cast<double>(c.world_min_depth),
                         static_cast<double>(c.world_max_depth), c.enabled ? 1 : 0, c.collides ? 1 : 0,
                         c.is_ground ? 1 : 0, hit ? 1 : 0, static_cast<double>(point.y));
        // A polygon's own numbers, plus the transform the world applied to it:
        // the only way to tell a parse error from a transform error.
        if (c.shape_type == caver::kShapePolygon && spans <= 24) {
            std::fprintf(stderr,
                         "opensw: [probe]     verts=%zu pos=(%.1f,%.1f) rot=%.3f scale=%.3f "
                         "first=(%.1f,%.1f)(%.1f,%.1f)(%.1f,%.1f) bounds=(%.1f,%.1f %.1fx%.1f)\n",
                         c.polygon.vertices.size(), static_cast<double>(c.world_position.x),
                         static_cast<double>(c.world_position.y), static_cast<double>(c.world_rotation),
                         static_cast<double>(c.world_scale),
                         c.polygon.vertices.size() > 0 ? static_cast<double>(c.polygon.vertices[0].x) : 0.0,
                         c.polygon.vertices.size() > 0 ? static_cast<double>(c.polygon.vertices[0].y) : 0.0,
                         c.polygon.vertices.size() > 1 ? static_cast<double>(c.polygon.vertices[1].x) : 0.0,
                         c.polygon.vertices.size() > 1 ? static_cast<double>(c.polygon.vertices[1].y) : 0.0,
                         c.polygon.vertices.size() > 2 ? static_cast<double>(c.polygon.vertices[2].x) : 0.0,
                         c.polygon.vertices.size() > 2 ? static_cast<double>(c.polygon.vertices[2].y) : 0.0,
                         static_cast<double>(c.local_bounds.x), static_cast<double>(c.local_bounds.y),
                         static_cast<double>(c.local_bounds.w), static_cast<double>(c.local_bounds.h));
        }
    }
    std::fprintf(stderr, "opensw: [probe] %d of %zu entries span x=%.1f\n", spans,
                 entries.size(), static_cast<double>(hx));

    // Ground geometry in the level: embedded GroundMesh surfaces (what a level
    // really is built from) and every entry within 2000 units of the hero's x.
    size_t mesh_objects = 0, mesh_triangles = 0, mesh_vertices = 0;
    float nearest_ground_y = 1e30f;
    std::string nearest_ground_owner;
    for (const av::SceneObject& object : scene_.objects) {
        if (object.ground_meshes.empty()) continue;
        ++mesh_objects;
        for (const av::PODMesh& mesh : object.ground_meshes) {
            mesh_vertices += static_cast<size_t>(mesh.num_vertices);
            mesh_triangles += mesh.indices.size() / 3;
        }
    }
    std::fprintf(stderr, "opensw: [probe] %zu objects carry GroundMesh (%zu vertices, %zu triangles)\n",
                 mesh_objects, mesh_vertices, mesh_triangles);
    for (size_t i = 0; i < entries.size(); ++i) {
        const caver::CollisionShapeComponent& c = entries[i].component;
        const caver::Rect& r = c.world_aabb;
        if (hx < r.x - 2000.0f || hx > r.x + r.w + 2000.0f) continue;
        if (!c.is_ground) continue;
        const float d = std::fabs((r.y + r.h * 0.5f) - feet);
        if (d < nearest_ground_y) {
            nearest_ground_y = d;
            nearest_ground_owner = c.owner;
        }
    }
    std::fprintf(stderr, "opensw: [probe] nearest ground entry within 2000 units: '%s' at |dy|=%.1f\n",
                 nearest_ground_owner.c_str(), static_cast<double>(nearest_ground_y));
}

void GameSceneController::resolve_hero_ground(float dt) {
    // The character's footing is the engine's ground query, not the pair loop:
    // a level's walkable floor is the CollisionShape/GroundPolygon geometry the
    // probe sweeps, and this is where `grounded` comes from. Miss it and the
    // hero falls through the level. The pair loop still handles walls.
    RuntimeObject* hero = runtime_.hero();
    if (!hero) return;
    // The authored collision is the authority — it is what the engine queries.
    // The GroundMesh scanline is a fallback for a level that ships a drawable
    // floor with no collision shapes at all; it can only ever be a last resort,
    // because a mesh has no notion of which of its surfaces is walkable and a
    // side wall answers the scanline exactly like a floor does.
    caver::Vec2 hit{0.0f, 0.0f};
    int entry = -1;
    bool have_hit = false;
    caver::Vec2 shape_hit;
    int shape_entry = -1;
    if (ground_probe(&shape_hit, &shape_entry)) {
        hit = shape_hit;
        entry = shape_entry;
        have_hit = true;
    } else {
        const float cx = hero->pos[0] + hero_local_bounds_.x + hero_local_bounds_.w * 0.5f;
        const float top = hero->pos[1] + hero_local_bounds_.y + hero_local_bounds_.h * 0.5f;
        const float bottom = hero->pos[1] + hero_local_bounds_.y - 30.0f;
        float surface = 0.0f;
        if (terrain_probe(cx, top, bottom, hero->pos[2], &surface)) {
            hit = caver::Vec2{cx, surface};
            have_hit = true;
        }
    }
    if (std::getenv("OPENSW_TRACE_GROUND") && trace_frames_ < 40) {
        ++trace_frames_;
        std::fprintf(stderr,
                     "opensw: [ground] f=%d pos=(%.1f,%.1f) vel=%.1f terrain_hit=%d shape_hit=%d entry=%d surface=%.1f\n",
                     trace_frames_, static_cast<double>(hero->pos[0]),
                     static_cast<double>(hero->pos[1]), static_cast<double>(hero->vel[1]),
                     have_hit ? 1 : 0, shape_entry >= 0 ? 1 : 0, shape_entry,
                     static_cast<double>(hit.y));
    }
    if (!have_hit) return;

    // The object origin sits `local_bounds.y` above its feet.
    const float feet = hero->pos[1] + hero_local_bounds_.y;
    const float next_feet = feet + hero->vel[1] * dt;
    const bool penetrating = feet < hit.y - 0.01f;
    const bool landing = hero->vel[1] <= 0.0f && next_feet <= hit.y + 0.01f;
    if (!penetrating && !landing) return;

    hero->pos[1] = hit.y - hero_local_bounds_.y;
    hero->vel[1] = 0.0f;
    hero->grounded = true;
    hero->behaviour.physics.on_ground = true;
    hero_body_.position = caver::Vec2{hero->pos[0], hero->pos[1]};
    hero_body_.velocity = caver::Vec2{hero->vel[0], 0.0f};
    hero_body_.on_ground = true;
    hero_body_.ground_shape = entry;
}

bool GameSceneController::resolve_hero_contacts_once() {
    // One pass of Caver::Scene::Update's collision half, restricted to the hero:
    // refresh every shape's world transform, resolve the pairs, then push the
    // hero out of each contact it owns (EntityComponent::HandleMessage).
    RuntimeObject* hero = runtime_.hero();
    if (!hero || shapes_.shape_count() == 0) return false;

    // The hero's body mirrors the runtime object so one collision pass owns the
    // whole state change (the engine's PhysicsObjectState is the authority).
    hero_body_.radius = hero_radius_;
    hero_body_.use_gravity = true;
    hero_body_.position = caver::Vec2{hero->pos[0], hero->pos[1]};
    hero_body_.velocity = caver::Vec2{hero->vel[0], hero->vel[1]};
    hero_body_.on_ground = hero->grounded;

    // Owner transforms: the level's objects first, the hero on its own slot.
    const auto& states = runtime_.render_state();
    for (size_t i = 0; i < states.size() && static_cast<int>(i) < hero_slot_; ++i) {
        const ObjectRenderState& state = states[i];
        caver::CollisionWorld::ObjectTransform& transform = shape_transforms_[i];
        transform.position = caver::Vec2{state.pos[0], state.pos[1]};
        transform.rotation = state.rot_y;
        transform.scale    = state.scale;
        transform.flip     = state.facing < 0;
        transform.hidden   = state.hidden;
    }
    {
        caver::CollisionWorld::ObjectTransform& transform =
            shape_transforms_[static_cast<size_t>(hero_slot_)];
        transform.position = caver::Vec2{hero->pos[0], hero->pos[1]};
        transform.rotation = 0.0f;
        transform.scale    = 1.0f;
        transform.flip     = hero->facing < 0;
        transform.velocity = caver::Vec2{hero->vel[0], hero->vel[1]};
        transform.hidden   = false;
    }

    shapes_.update(shape_transforms_, contacts_);

    if (!collision_diag_done_) {
        // One-shot diagnostic: what the hero's own shape looks like, and how much
        // of the level is even a candidate for it. A zero here means the world is
        // empty around the spawn point; a large number with no contacts means the
        // narrowphase is not seeing the overlap.
        collision_diag_done_ = true;
        // The probe dump is a development aid (it prints every shape under the
        // spawn point); it stays behind an env var so a normal boot is quiet.
        if (std::getenv("OPENSW_DEBUG_GROUND")) dump_ground_probe();
        int hero_entry = -1;
        for (size_t i = 0; i < shapes_.entries().size(); ++i)
            if (shapes_.entries()[i].component.owner_index == hero_slot_) hero_entry = static_cast<int>(i);
        if (hero_entry >= 0 && std::getenv("OPENSW_DEBUG_GROUND")) {
            const caver::CollisionShapeComponent& component =
                shapes_.entries()[static_cast<size_t>(hero_entry)].component;
            const caver::Rect aabb = component.world_aabb;
            int candidates = 0;
            for (const auto& entry : shapes_.entries()) {
                const caver::Rect& other = entry.component.world_aabb;
                if (aabb.x + aabb.w <= other.x || other.x + other.w <= aabb.x) continue;
                if (aabb.y + aabb.h <= other.y || other.y + other.h <= aabb.y) continue;
                ++candidates;
            }
            std::fprintf(stderr,
                         "opensw: hero shape type %d aabb(%.1f,%.1f %.1fx%.1f) candidates %d of %zu, "
                         "terrain %zu triangles\n",
                         static_cast<int>(component.shape_type),
                         static_cast<double>(aabb.x), static_cast<double>(aabb.y),
                         static_cast<double>(aabb.w), static_cast<double>(aabb.h), candidates,
                         shapes_.entries().size(), terrain_.size());
        }
    }

    bool touched = false;
    const auto& entries = shapes_.entries();
    for (const caver::CollisionWorld::Contact& contact : contacts_) {
        const bool hero_is_a = contact.a >= 0 && static_cast<size_t>(contact.a) < entries.size() &&
                               entries[static_cast<size_t>(contact.a)].component.owner_index == hero_slot_;
        const bool hero_is_b = contact.b >= 0 && static_cast<size_t>(contact.b) < entries.size() &&
                               entries[static_cast<size_t>(contact.b)].component.owner_index == hero_slot_;
        if (!hero_is_a && !hero_is_b) continue;

        caver::CollisionInfo info = contact.info;
        if (hero_is_b) {
            // The world resolved the level's shape as the mover; flip the contact
            // so the hero is the one being pushed out.
            info.normal.x = -info.normal.x;
            info.normal.y = -info.normal.y;
            std::swap(info.velocity, info.other_velocity);
        }
        // PhysicsObjectState::HandleGroundCollision (0x2BB278) resolves it, with
        // its own guards: the separating test (`dot(normal, velocity) > depth`),
        // the `position += normal * depth` push and the tangential velocity fold.
        // The previous clamped 6-unit push was an invention that made the
        // character buoyant — it held him above the surface on every frame while
        // the real push-out would have seated him on it.
        if (!hero_body_.handle_ground_collision(info, hero_local_bounds_.w)) continue;
        hero->pos[0] = hero_body_.position.x;
        hero->pos[1] = hero_body_.position.y;
        hero->vel[0] = hero_body_.velocity.x;
        hero->vel[1] = hero_body_.velocity.y;
        if (hero_body_.on_ground) {
            hero->grounded = true;
            hero->behaviour.physics.on_ground = true;
            // isOnSolidGround / isOnSteepGround (0x2AC58C / 0x2AC5C8): the floor
            // is solid when the contact normal is steeper than 0.7 in Y. The
            // steep case is what the CharController slides on instead of walking.
            hero->behaviour.physics.unsafe_ground = hero_body_.on_steep_ground;
        }
        touched = true;
        record_collision_event(contact, hero_is_a);
    }
    return touched;
}

void GameSceneController::record_collision_event(const caver::CollisionWorld::Contact& contact,
                                                 bool hero_is_a) {
    // Scene::Update sends message 7 (`OnCollide`) to both objects, and message 9
    // when the contact is ground. The runtime records it the same way so scripts
    // and the studio see exactly what the engine would have delivered.
    const auto& entries = shapes_.entries();
    const int hero_entry = hero_is_a ? contact.a : contact.b;
    const int other_entry = hero_is_a ? contact.b : contact.a;
    if (hero_entry < 0 || other_entry < 0 || static_cast<size_t>(other_entry) >= entries.size())
        return;
    SceneEvent event;
    event.event = contact.ground ? "OnGroundCollide" : "OnCollide";
    event.object = "hero";
    event.other = entries[static_cast<size_t>(other_entry)].component.owner;
    event.handler = "CollisionShape";
    runtime_.record_event(event);
}

int GameSceneController::scripts_running() const {
    return program_host_ ? program_host_->running_scripts() : 0;
}

} // namespace game
} // namespace caver
