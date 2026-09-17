#pragma once
// game_scene_controller.h — `Caver::GameSceneController`, the level runtime.
//
// This is the piece that makes a `.scene` playable. Recovered from arm32_13:
//
//   InitWithScene          (0x313F1C) — camera 0.34907 rad fov, near 1, far 50/20000,
//                                      background layer direction vectors
//   SpawnHeroAt(id)        (0x3140B4) — ObjectWithIdentifier(id) else "spawn_default",
//                                      SpritePoint offset + facing, then CreateHeroObjectAt
//   CreateHeroObjectAt     (0x314258) — LibraryWithName("hiro").TemplateForName("hiro"),
//                                      identifier "hero", model from equipped armor,
//                                      HealthComponent with max = 2*ExperienceLevel + 4
//   AddHeroObjectToScene   (0x314470) — add to the scene, HeroEquipmentManager::Init,
//                                      equip weapon + armor, apply trinket to spells
//   GameControlButtonDown  (0x315314) — the input mapping (see game_control.h)
//   Update                 (0x31468C) — per-frame controller pass
//
// The hero is *not* driven by the monster AI: it is driven by the player, and its
// tuned numbers (NormalRunSpeed, JumpSpeed, NormalMaxJumpTime, FastRunSpeed …)
// live in the `hiro` template's CharControllerComponent. Nothing about how the
// hero moves is written down here — it is read out of the loaded archetype.

#include <memory>
#include <string>
#include <vector>

#include "ruby/caver/game/game_control.h"
#include "ruby/caver/game/game_data.h"
#include "ruby/caver/game/game_state.h"
#include "ruby/caver/game/player_profile.h"
#include "ruby/caver/collision.h"
#include "ruby/caver/library_manager.h"
#include "ruby/caver/runtime.h"
#include "tools/av_renderer.h"
#include "tools/scene_collision.h"
#include "tools/scene_loader.h"
#include "tools/scene_workspace.h"

namespace caver {
namespace game {

// The hero's tuned movement values, read from CharControllerComponent at spawn.
struct HeroTuning {
    float run_speed = 0.0f;          // CharControllerComponent.NormalRunSpeed (3)
    float fast_run_speed = 0.0f;     // NormalRunSpeed → FastRunSpeed (16)
    float jump_speed = 0.0f;         // JumpSpeed (4)
    float max_jump_time = 0.0f;      // NormalMaxJumpTime (5)
    float fast_max_jump_time = 0.0f; // FastMaxJumpTime (17)
    float gravity_magnitude = 0.0f;  // PhysicsObject.GravityMagnitude (3)
    bool  has_jump = false;
};

class GameSceneController {
public:
    GameSceneController();
    ~GameSceneController();

    GameSceneController(const GameSceneController&) = delete;
    GameSceneController& operator=(const GameSceneController&) = delete;

    // InitWithScene: attach the profile (for equipment/skills) and open the level.
    bool init_with_scene(PlayerProfile& profile, const std::string& level_name,
                         std::string* error = nullptr);

    // SpawnHeroAt(identifier): put the hero at the level's spawn point.
    bool spawn_hero_at(const std::string& identifier);

    // AddHeroObjectToScene: register the hero and equip it from the save.
    void add_hero_object_to_scene();

    // GameControlButtonDown / Up.
    void game_control_button_down(GameControlButton button);
    void game_control_button_up(GameControlButton button);

    // Update: one frame. `dt` is seconds.
    void update(float dt);

    // ── state for the renderer / debug shell ────────────────────────────────
    const std::string& level_name() const { return level_name_; }
    const std::string& level_title() const { return level_title_; }
    const av::SceneData& scene_data() const { return scene_; }
    // The recovered Caver collision world: one entry per CollisionShapeComponent
    // (plus GroundPolygonComponent edges and the hero's own shape).
    caver::CollisionWorld& collision_world() { return shapes_; }
    const caver::CollisionWorld& collision_world() const { return shapes_; }
    RuntimeScene& runtime() { return runtime_; }
    LibraryManager& libraries() { return libraries_; }
    ProgramHost* program_host() { return program_host_.get(); }

    const RuntimeObject* hero() const { return runtime_.hero(); }
    RuntimeObject* hero() { return runtime_.hero(); }
    const HeroTuning& hero_tuning() const { return tuning_; }
    float hero_radius() const { return hero_radius_; }
    // Where the hero's feet sit in the object's own space — the CollisionShape's
    // local min-Y (hiro: -34). The object's origin is NOT its feet: the physics
    // rests the SHAPE on the ground, so a renderer that draws the model at the
    // origin draws it 34 units too high (the "Hiro floats" bug). The model itself
    // is authored foot-anchored (hiro.POD's rest pose spans y 0.94..72.79).
    float hero_feet_offset() const { return hero_local_bounds_.y; }

    // The resolved archetype the hero was instantiated from
    // (`LibraryWithName("hiro").TemplateForName("hiro")`). The renderer draws
    // the hero from THIS — its ModelComponent names the pod, so equipment and
    // armor changes flow through with no model name written down anywhere.
    const av::SceneObject* hero_archetype() const {
        return hero_archetype_valid_ ? &hero_archetype_.object : nullptr;
    }
    float hero_template_scaling() const {
        return hero_archetype_valid_ ? hero_archetype_.scaling : 1.0f;
    }
    // The name the archetype is registered under in its ObjectLibrary.
    const std::string& hero_template_name() const { return hero_archetype_.name; }

    // ── the engine's camera ────────────────────────────────────────────────
    // `InitWithScene` calls Camera::SetPerspectiveProjection(0.34907, 1.0, 50.0,
    // 20000.0) — fov in radians, near 50, far 20000 — and then writes the
    // CameraController's focus offset for the device profile, which the binary
    // carries as two Vector3s: (0, 187, 1190) for the phone profile and
    // (0, 242, 1540) for the tablet profile (dword_314038/314040 and
    // 31403C/314044). The camera looks straight at the scene plane (the level's
    // XY, depth on Z), so the offset becomes an orbit pitch + distance.
    struct CameraFrame {
        float fov_degrees = 20.0f;    // 0.34907 rad
        float near_plane  = 50.0f;
        float far_plane   = 20000.0f;
        float offset[3]   = {0.0f, 187.0f, 1190.0f};
    };
    CameraFrame camera_frame() const;
    // The frame's av::Camera: focus on the controller's own camera point (which
    // tracks the hero), with the recovered offset applied as pitch/distance.
    av::Camera camera() const;

    float camera_x() const { return camera_[0]; }
    float camera_y() const { return camera_[1]; }

    const std::string& spawn_point_used() const { return spawn_point_used_; }
    const std::vector<std::string>& warnings() const { return runtime_.warnings(); }
    // Boot/equip notes the shell prints (the engine has no equivalent channel —
    // this is how OpenSwordigo reports what the real data decided).
    const std::vector<std::string>& notes() const { return notes_; }
    void note(const std::string& text) { notes_.push_back(text); }

    // Frame counters the shell prints.
    double clock() const { return runtime_.clock(); }
    size_t component_count() const { return runtime_.component_count(); }
    int    scripts_running() const;

private:
    void build_collision_world();
    void build_hero_collision();
    void install_ground_query();
    void read_hero_tuning();
    void apply_input_to_hero(float dt);
    void resolve_hero_against_level(float dt);
    // One pass of the collision loop restricted to the hero: refresh the shapes,
    // resolve the pairs, push the hero out of each contact it owns
    // (EntityComponent::HandleMessage). Returns true when it touched something.
    bool resolve_hero_contacts_once();
    // CharControllerComponent::Update's ground query: the vertical segment from
    // the character's AABB centre down to 30 units below its feet, resolved
    // through Caver::Scene::LineSegmentIntersectsGround. Writes the nearest hit.
    bool ground_probe(caver::Vec2* out_point, int* out_entry) const;
    void dump_ground_probe() const;

    // ── the level's terrain ────────────────────────────────────────────────
    // A level's walkable floor is its embedded `GroundMeshComponent` surfaces:
    // every object that carries one contributes a triangle soup
    // (`Caver::Mesh::LoadFromProtobufMessage`, 0x36510C) placed by
    // `swk::object_world_matrix` — the same transform the draw path uses, so
    // the hero stands exactly where the ground is drawn. Collision *shapes*
    // remain walls, platforms and triggers; the terrain is what the character
    // walks on.
    struct TerrainTriangle {
        caver::Vec2 v[3];
        float       z[3] = {0.0f, 0.0f, 0.0f};
        int         object_index = -1;
    };
    void build_terrain();
    // The highest terrain surface the vertical segment [from_y .. to_y] at `x`
    // crosses within the depth band. `from_y` is the probe's top, `to_y` its
    // bottom (from_y > to_y).
    bool terrain_probe(float x, float from_y, float to_y, float z, float* out_y) const;
    size_t terrain_triangle_count() const { return terrain_.size(); }
    // Snaps the hero onto that hit when it is landing or penetrating.
    void resolve_hero_ground(float dt);
    void record_collision_event(const caver::CollisionWorld::Contact& contact, bool hero_is_a);
    void camera_follow(float dt);

    PlayerProfile*  profile_ = nullptr;
    std::string     level_name_;
    std::string     level_title_;
    std::string     spawn_point_used_;

    av::SceneData      scene_;
    caver::CollisionWorld shapes_;
    // Owner transforms for the collision world, indexed by owner_index; the last
    // slot is the hero (its own CollisionShapeComponent drives its collisions).
    std::vector<caver::CollisionWorld::ObjectTransform> shape_transforms_;
    int  hero_slot_ = 0;
    caver::PhysicsBody hero_body_;
    // The hero's own authored shape bounds (relative to its origin), its depth
    // band, and its entry in the collision world: the three inputs the ground
    // probe is built from (CharControllerComponent::Update).
    caver::Rect hero_local_bounds_;
    // The authored depth band (MinDepth / MaxDepth, -15 / 15 for `hiro`) and the
    // rotation-folded band the ground query actually compares against.
    float       hero_min_depth_ = 0.0f;
    float       hero_max_depth_ = 0.0f;
    float       hero_world_min_depth_ = 0.0f;
    float       hero_world_max_depth_ = 0.0f;
    int         hero_entry_index_ = -1;
    std::vector<TerrainTriangle> terrain_;
    std::vector<caver::CollisionWorld::Contact> contacts_;
    bool collision_diag_done_ = false;
    int  trace_frames_ = 0;         // OPENSW_TRACE_GROUND diagnostic frame counter
    LibraryManager     libraries_;
    RuntimeScene       runtime_;
    std::unique_ptr<ProgramHost> program_host_;

    // The hero's resolved archetype (kept for the render path).
    av::SclTemplateEntry hero_archetype_;
    bool                 hero_archetype_valid_ = false;

    PlayerInput input_;
    HeroTuning  tuning_;

    float hero_radius_ = 0.0f;      // from the hero's CollisionShape
    // CharControllerComponent's jump state machine (this+81 = state, +82 = the
    // timer, +88 = JumpSpeed, +89 = the per-jump boost, +90 = MaxJumpTime).
    float jump_hold_ = 0.0f;
    float jump_boost_ = 0.0f;       // this+89: ground jump boost / 50 for the air jump
    bool  jump_held_ = false;
    bool  jump_air_ = false;        // this+85: the air jump was spent on this fall
    bool  grounded_ = true;

    // this+90: the rising window. Word 364 (normal) is read while the world is in
    // its normal state; word 372 (fast) is swapped in while the entity is in a
    // hurry (`*(char*)this + 312`). `hiro` authors 0.23 and 0.11.
    float jump_window() const {
        return tuning_.max_jump_time > 0.0f ? tuning_.max_jump_time : 0.23f;
    }
    float fast_jump_window() const {
        return tuning_.fast_max_jump_time > 0.0f ? tuning_.fast_max_jump_time : jump_window();
    }

    float camera_[2] = {0.0f, 0.0f};
    float camera_velocity_[2] = {0.0f, 0.0f};
    float camera_bounds_[2] = {32.0f, 36.0f};   // GameSceneController half extents
    std::vector<std::string> notes_;
};

} // namespace game
} // namespace caver
