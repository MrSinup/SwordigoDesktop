#pragma once
// behaviour.h — the recovered component behaviours (the C++ half of Swordigo).
//
// Swordigo's component set is two things at once:
//   * STATE that the .scl/.scene protobuf carries (decoded in runtime.h), and
//   * BEHAVIOUR the C++ side runs every frame.
//
// The behaviour is recovered from OpenSwordigo/arm64_12/functions/Caver/<Class>/.
// The recovered split matches the shipped game exactly:
//   * controllers, animation clocks, physics, damage, triggers, spells and
//     projectiles tick in C++ — that is this file;
//   * the authored, per-archetype AI lives in Lua programs (ProgramComponent
//     plus the handler fields OnKill/OnHurt/OnActivate/OnCollide/OnLoad/OnCast/
//     OnTouch/OnPress/OnRelease/OnBreak/OnCollect/OnItemGet) and calls *back*
//     into these components through the Lua API surface — that is
//     program_host.cpp.
//
// Nothing here is a heuristic: every field read by a tick is a named field of
// the recovered schema (src/tools/scene_schemas.cpp), and every transition is
// the one the decompiled function performs. Fields the decompilation could not
// pin down are marked "unverified" and default to the neutral value, so an
// unsupported archetype degrades to standing still rather than to nonsense.

#include <cstdint>
#include <string>
#include <vector>

namespace caver {

class RuntimeScene;
struct RuntimeObject;
struct RuntimeComponent;

// EntityControllerComponent / EntityActionComponent: the action the controller
// is currently performing. `EntityController.PerformAction(name)` in Lua maps
// onto these; EntityActionComponent::IsFinished() ends each one.
enum class EntityAction : uint8_t {
    None = 0,
    Idle,
    Walk,
    Turn,
    Jump,
    Fall,
    Land,
    Attack,
    Cast,
    Hurt,
    Roam,
    Death,
    Custom,
};

// EntityControllerComponent.MovementBehavior
enum class MovementBehavior : uint8_t {
    Stand = 0,
    Walk = 1,
    Roam = 2,
    Follow = 3,
    Flee = 4,
};

// AnimationControllerComponent / KeyframeAnimationComponent / BlendAnimationComponent.
struct AnimationState {
    std::string current;        // animation name (or component identifier as text)
    std::string previous;       // for BlendToAnimation
    float time = 0.0f;          // KeyframeAnimation current time
    float duration = 0.0f;      // TimeToCompletion (from data if known, else 0)
    float speed = 1.0f;         // SpeedMultiplier
    float blend = 0.0f;         // BlendAnimation progress 0..1
    float blend_time = 0.0f;    // BlendToAnimation requested duration
    bool  running = false;
    bool  repeating = true;
};

// ParticleEmitterComponent / FireEmitterComponent / WeaponGlow / MagicBolt emitters.
struct EmitterState {
    float time = 0.0f;           // local clock (drives the render side)
    float accumulator = 0.0f;    // time since the last emitted particle
    float interval = 0.05f;      // seconds between particles
    float particle_age_max = 1.0f;
    int   spawned = 0;           // particles emitted so far
    int   max_particles = 0;     // 0 = unlimited
    float gravity[3] = {0, 0, 0};
    float rotation[3] = {0, 0, 0};
    float origin_offset[3] = {0, 0, 0};
    bool  playing = true;
    bool  local_system = false;
    bool  destroy_when_finished = false;
};

// CollisionShapeComponent / GroundPolygonComponent / PhysicsObjectComponent.
struct PhysicsState {
    bool  enabled = true;                 // PhysicsObject.PhysicsEnabled
    bool  entity_physics = true;          // EntityComponent.PhysicsEnabled
    float gravity_dir[2] = {0.0f, -1.0f}; // GravityDirection (Vector2, y = down)
    float gravity_magnitude = 0.0f;
    float ground_deceleration = 0.0f;
    float air_deceleration = 0.0f;
    float max_speed = 0.0f;
    float elasticity = 0.0f;
    bool  allow_rotation = false;
    bool  on_ground = true;
    float ground_friction = 1.0f;         // CollisionShape.Friction
    bool  unsafe_ground = false;
    float air_time = 0.0f;
};

// HealthComponent / DamageComponent.
struct HealthState {
    float max_health = 0.0f;
    float health = 0.0f;
    float hurt_timer = 0.0f;      // > 0 while the hurt reaction plays
    float invuln_timer = 0.0f;    // set by Hurt() so a burst of contacts hits once
    float damage_min = 0.0f;
    float damage_max = 0.0f;
    bool  dead = false;
    bool  receives_damage = false;
    bool  inflicts_damage = false;    // contact damage (spikes, fire, monsters)
    bool  collides = true;
    bool  is_ground = false;
    bool  shape_enabled = true;
    float reenable_timer = 0.0f;      // CollisionShape.SetEnabled(false) then true
};

// Triggers: PressureTriggerComponent, TouchableComponent, PortalComponent,
// DoorControllerComponent, ElevatorControllerComponent, BushControllerComponent,
// BreakableObjectComponent.
struct TriggerState {
    bool  pressed = false;         // pressure plate / touch
    float press_timer = 0.0f;
    float max_height_offset = 0.0f;
    bool  stay_pressed = false;
    float touch_radius = 0.0f;
    bool  touched = false;

    // Door / elevator / breakable motion.
    bool  open = false;
    float open_amount = 0.0f;      // 0 = closed, 1 = open (render side)
    int   mode = 0;                // ElevatorController.Mode
    float elevation[3] = {0, 0, 0};// ElevatorController travel axis/axis-limit
    float travel = 0.0f;
    float travel_dir = 0.0f;

    int   hits_taken = 0;
    int   hits_to_break = 0;
    bool  broken = false;
    bool  breaks_on_impact = false;

    bool  wobbling = false;
    float wobble_timer = 0.0f;

    // Collectable / item
    bool  collected = false;
    int   value = 0;
};

// Spell / projectile / skill family.
struct ProjectileState {
    float life = 0.0f;
    float max_life = 0.0f;
    float speed = 0.0f;
    float radius = 0.0f;
    float damage = 0.0f;
    bool  exploded = false;
    bool  align_rotation = true;
    bool  break_on_ground = false;
    float colour[3] = {1, 1, 1};
    std::string spawned_object;      // Skill.CastObjectTemplateName
};

// Per-component recovered state. Held per object (see RuntimeObject::behaviour),
// so components that own the same kind of state (e.g. every CollisionShape on an
// object) share the object's container — matching how Entity/EntityController
// address their collaborators by component identifier.
struct BehaviourState {
    // ── entity / controller ────────────────────────────────────────────────
    EntityAction     action = EntityAction::Idle;
    MovementBehavior movement = MovementBehavior::Stand;
    float action_time = 0.0f;
    float action_duration = 0.0f;
    float action_delay = 0.0f;
    float state_timer = 0.0f;
    int   facing = 1;                 // EntityComponent.FacingDirection sign
    float move_speed = 0.0f;
    float move_dir[2] = {0.0f, 0.0f};
    float accel = 0.0f;
    float default_move_speed = 0.0f;
    float default_acceleration = 0.0f;
    float targeting_distance = 0.0f;
    bool  has_target = false;
    std::string target_object;        // resolved EntityController.Target
    float target_distance = 0.0f;
    bool  at_target = false;
    bool  idle = true;

    // roam / patrol (EntityControllerComponent.RoamAreaId + RoamUpdate)
    float roam_origin[2] = {0.0f, 0.0f};
    float roam_radius = 0.0f;
    float roam_target[2] = {0.0f, 0.0f};
    float roam_wait = 0.0f;
    float attack_cooldown = 0.0f;
    bool  damage_dealt_this_swing = false;

    // ── animation ──────────────────────────────────────────────────────────
    AnimationState anim;
    std::string default_move_animation;
    std::string stand_animation;
    std::string walk_animation;
    std::string attack_animation;
    std::string jump_animation;
    std::string fall_animation;
    std::string death_animation;
    std::string shoot_animation;
    std::string fly_animation;
    std::string leap_animation;
    std::string blend_animation;
    std::string cast_animation;

    // ── character animation (CharController + CharAnimController) ──────────
    // Swordigo's hero (and every other character) does not animate itself: a
    // CharControllerComponent owns a CharAnimControllerComponent, which blends
    // between one AnimNode per lifecycle clip, each built from a
    // KeyframeAnimation component. Recovered from
    // CharAnimControllerComponent::{StartMoving,StopMoving,StartJumping,
    // StartFalling,SetCurrentRunSpeed} and CharControllerComponent::Update.
    struct AnimClip {
        std::string name;          // KeyframeAnimationComponent.Name → the .POD
        float speed = 1.0f;        // SpeedMultiplier (the node's playback rate)
        bool  repeating = true;    // Repeating
        bool  running = true;      // Running
    };
    struct CharAnimState {
        bool  active = false;
        // The clips the CharAnimController names, resolved to pod names.
        std::string stand, walk, jump, fall, air_jump, cast, hurt, die, push, lift;
        std::string current;       // clip playing now (pod name; "" = none)
        float time = 0.0f;         // clip clock in seconds, already rate-scaled
        float rate = 1.0f;         // speed * (run speed / 100) for the walk clip
        bool  repeating = true;
        bool  in_action = false;   // a jump/fall/attack clip owns the clock
        float run_speed = 0.0f;    // CharControllerComponent.NormalRunSpeed
        float jump_speed = 0.0f;   // CharControllerComponent.JumpSpeed
        float air_time = 0.0f;
    };
    CharAnimState char_anim;
    // Every KeyframeAnimation clip on the object, by name.
    std::vector<AnimClip> anim_clips;

    // ── physics / damage ───────────────────────────────────────────────────
    PhysicsState physics;
    HealthState  health;

    // ── triggers / doors ───────────────────────────────────────────────────
    TriggerState trigger;

    // ── spells / projectiles ───────────────────────────────────────────────
    ProjectileState projectile;

    // ── emitters / lights ──────────────────────────────────────────────────
    EmitterState emitter;
    float glow_pulse_time = 0.0f;
    float glow_pulse_amount = 0.0f;
    float light_radius = 0.0f;
    float light_intensity = 0.0f;
    float light_color[3] = {1, 1, 1};
    float light_offset[3] = {0, 0, 0};

    // ── misc clocks ────────────────────────────────────────────────────────
    float water_time = 0.0f;
    float model_rotation = 0.0f;
    float orbit_angle = 0.0f;
    bool  activated = false;

    // Which behaviours were started for this object (diagnostics / coverage).
    std::vector<std::string> active;
};

// Build the initial behaviour state for an object from its decoded components.
// Called once per object when the runtime loads it (the equivalent of
// SceneObject::InitWithTemplate's reconcile pass reaching the behaviour layer).
void behaviour_init(const RuntimeScene& scene, RuntimeObject& object);

// Advance every component behaviour of one object by dt.
void behaviour_tick(RuntimeScene& scene, RuntimeObject& object, float dt);

// True when the class has a recovered tick today (drives Stage::Updated).
bool behaviour_has_tick(const std::string& class_name);

// Fire a handler program on the matching components (OnKill, OnCollide, …).
// Declared here so behaviour.cpp can trigger scripted events without depending
// on program_host.h.
void behaviour_fire_event(RuntimeScene& scene, RuntimeObject& object,
                          const std::string& event_field, RuntimeObject* other);

} // namespace caver
