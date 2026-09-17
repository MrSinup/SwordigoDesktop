#pragma once
// collision.h — Caver's collision, recovered.
//
// This is NOT the editor's wall-segment subsystem. It is a transcription of the
// engine's own collision types and algorithms, read off the decompilation:
//
//   Caver::CollisionShape::CollidesWithShape        0x28EFB8  (arm32_13)
//   Caver::CircleIntersectsCircle                   0x3973E0
//   Caver::CircleIntersectsRectangle                0x397190
//   Caver::CircleIntersectsPolygon                  0x396E3C
//   Caver::RectangleIntersectsRectangle             0x396CA4
//   Caver::RectangleIntersectsPolygon               0x396964
//   Caver::RectangleIntersectsConvexPolygon         0x3967AC
//   Caver::LineSegmentIntersects{Circle,Rectangle,Polygon}
//   Caver::CollisionShape::IntersectsWithLineSegment 0x28F450
//   Caver::CollisionShape::NearestPointInShape       0x28F528
//   Caver::Scene::Update                             0x374E40  (the pair loop)
//   Caver::CollisionPairSet::RegisterCollision       0x28E8B4
//   Caver::CollisionShapeComponent::{SetDefaultShapeOfType, UpdateCollisionShape,
//                                    SetUpdatedShape, UpdateWorldAABB}
//   Caver::PhysicsObjectState::HandleGroundCollision 0x2BB278
//   Caver::PhysicsObjectState::AdjustGroundCollisionVector 0x2BB1AC
//   Caver::EntityComponent::HandleMessage            0x2B1058  (msgs 7 and 9)
//
// Shape types are exactly the engine's: 1 = rectangle, 2 = circle, 3 = polygon
// (`Shape::Bounds` 0x39B070 switches on 1/2/3). Everything is 2D in the scene's
// X-Y plane; Z is only a gate (the component's MinDepth/MaxDepth band).
//
// Convention for contact normals: `CollisionInfo::normal` points FROM the other
// shape TOWARD the shape being pushed out, so the engine's resolution
// `position += normal * depth` (PhysicsObjectState::HandleGroundCollision) moves
// the mover out of the surface. The engine writes that normal in the primitives
// and negates it for the circle-as-first-argument cases; we take the direction
// from the primitive's own output.

#include <cstdint>
#include <string>
#include <vector>

namespace caver {

// ─────────────────────────────────────────────────────────────────────────────
// Geometry (Caver::Vector2 / Rectangle / Circle / Polygon)
// ─────────────────────────────────────────────────────────────────────────────

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

// Caver::Rectangle — {x, y, width, height} in local space, per
// ShapeComponent field 10 and Rectangle's own protobuf field numbers.
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    float center_x() const { return x + w * 0.5f; }
    float center_y() const { return y + h * 0.5f; }
};

// Caver::Circle — center + radius (radius at +8, per CircleIntersectsCircle).
struct Circle {
    Vec2  center;
    float radius = 0.0f;
};

// Caver::Polygon — the engine stores {count, vertex array, closed flag}
// (CircleIntersectsPolygon reads `*poly` as the vertex count and +17 as the
// closed flag, stepping 8 bytes per vertex).
struct Polygon {
    std::vector<Vec2> vertices;
    bool closed = true;

    // Edge count walked by the engine's polygon loops:
    //   count = num_vertices + (closed ? 0 : -1)
    int edge_count() const {
        const int n = static_cast<int>(vertices.size());
        return closed ? n : (n > 0 ? n - 1 : 0);
    }
};

// Caver::Shape::CollisionType (Shape::Bounds switches on these).
enum ShapeType : int {
    kShapeNone      = 0,
    kShapeRectangle = 1,
    kShapeCircle    = 2,
    kShapePolygon   = 3,
};

// ─────────────────────────────────────────────────────────────────────────────
// Contact
// ─────────────────────────────────────────────────────────────────────────────

// Caver::CollisionInfo — 37 bytes written by CollidesWithShape:
//   +0 position  +8 velocity  +16 other velocity  +24 normal  +32 depth  +36 valid
struct CollisionInfo {
    Vec2  position;                 // world contact point
    Vec2  velocity;                 // the mover's velocity at contact
    Vec2  other_velocity;           // the other shape's velocity
    Vec2  normal;                   // unit, points out of the surface into the mover
    float depth = 0.0f;             // penetration depth along `normal`
    bool  valid = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// CollisionShape — one shape in world space
// ─────────────────────────────────────────────────────────────────────────────
//
// The engine's CollisionShape carries the live transform: world position
// (Vector2 at +12), the ShapeComponent offset (+20), scale (+24), the
// horizontal-flip byte (+28), velocity (+32/+36) and the world-AABB x range
// (+40/+44) that CollidesWithShape early-outs on. `ShapeComponent`'s own shape
// (the local, authored geometry) is kept separately below so an editor can
// still show the authored rect while the world shape carries the transform.
struct CollisionShape {
    ShapeType type = kShapeNone;
    Rect      rect;
    Circle    circle;
    Polygon   polygon;

    Vec2  position;                 // world position of the shape's origin
    Vec2  offset;                   // ShapeComponent offset (local translation)
    float rotation = 0.0f;          // radians
    float scale    = 1.0f;
    bool  flip     = false;
    Vec2  velocity;

    // World AABB (the engine keeps it on the component; the x range is what
    // CollidesWithShape tests first).
    float aabb_min_x = 0.0f;
    float aabb_max_x = 0.0f;

    // The shape's own bounds in local space (`Shape::Bounds`).
    Rect bounds() const;
    // The shape's bounds after the transform — the component's world AABB.
    Rect world_bounds() const;
    // True when the x ranges cannot overlap (CollidesWithShape's first test).
    bool x_ranges_apart(const CollisionShape& other) const;
};

// ─────────────────────────────────────────────────────────────────────────────
// The recovered primitives (each one is a named function in the binary)
// ─────────────────────────────────────────────────────────────────────────────

// out = point * scale + offset  (Caver::Vector2::Transformed)
Vec2 transformed(const Vec2& point, float scale, const Vec2& offset);
// out = (world - offset) / scale  (Caver::Vector2::InverseTransformed)
Vec2 inverse_transformed(const Vec2& world, float scale, const Vec2& offset);
// Rotate about the origin (Caver::Vector2::Rotated).
Vec2 rotated(const Vec2& v, float radians);
// Caver::RangesOverlap(a_min, a_max, b_min, b_max, &out_overlap_centre)
bool ranges_overlap(float a_min, float a_max, float b_min, float b_max, float* out_centre);

bool circle_intersects_circle(const Circle& a, const Vec2& pos_a, const Vec2& offset_a, float scale_a,
                              const Circle& b, const Vec2& pos_b, const Vec2& offset_b, float scale_b,
                              Vec2* out_normal, float* out_depth);

bool circle_intersects_rectangle(const Circle& a, const Vec2& pos_a, const Vec2& offset_a, float scale_a,
                                 const Rect& b, const Vec2& pos_b, float rot_b, const Vec2& offset_b,
                                 float scale_b, bool flip_b, Vec2* out_normal, float* out_depth);

bool circle_intersects_polygon(const Circle& a, const Vec2& pos_a, const Vec2& offset_a, float scale_a,
                               const Polygon& b, const Vec2& pos_b, float rot_b, const Vec2& offset_b,
                               float scale_b, bool flip_b, Vec2* out_normal, float* out_depth);

// Separating-axis minimum translation between two oriented rectangles.
bool rectangle_intersects_rectangle(const Rect& a, const Vec2& pos_a, float rot_a, const Vec2& offset_a,
                                    float scale_a, bool flip_a,
                                    const Rect& b, const Vec2& pos_b, float rot_b, const Vec2& offset_b,
                                    float scale_b, bool flip_b,
                                    Vec2* out_normal, float* out_depth);

bool rectangle_intersects_polygon(const Rect& a, const Vec2& pos_a, float rot_a, const Vec2& offset_a,
                                  float scale_a, bool flip_a,
                                  const Polygon& b, const Vec2& pos_b, float rot_b, const Vec2& offset_b,
                                  float scale_b, bool flip_b, Vec2* out_normal, float* out_depth);

// Caver::LineSegmentIntersectsRectangle / Circle / Polygon.
bool line_segment_intersects_rectangle(const Vec2& p0, const Vec2& p1, const Rect& r,
                                       const Vec2& pos, float rot, float scale, float* out_t);
bool line_segment_intersects_circle(const Vec2& p0, const Vec2& p1, const Vec2& center, float radius,
                                    float* out_t);
bool line_segment_intersects_polygon(const Vec2& p0, const Vec2& p1, const Polygon& poly,
                                     const Vec2& pos, float rot, float scale, float* out_t);
// Caver::LineSegmentsIntersect.
bool line_segments_intersect(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1,
                             float* out_t, Vec2* out_point);

// Caver::CollisionShape::CollidesWithShape(other, scale, info)
bool collides_with(const CollisionShape& a, const CollisionShape& b, float other_scale,
                   CollisionInfo* out_info);

// Caver::CollisionShape::IntersectsWithLineSegment / NearestPointInShape.
bool shape_intersects_line_segment(const CollisionShape& shape, const Vec2& p0, const Vec2& p1,
                                   Vec2* out_point, Vec2* out_another, int* out_flag);
Vec2 nearest_point_in_shape(const CollisionShape& shape, const Vec2& world_point);

// ─────────────────────────────────────────────────────────────────────────────
// CollisionShapeComponent — the component semantics, minus the SceneObject wiring
// ─────────────────────────────────────────────────────────────────────────────
//
// Field meanings recovered from the component's own functions:
//   shape (authored)  -> +56      / updated shape -> +156 (collisionShape())
//   bounds            -> +220     / world AABB    -> +236
//   enabled           -> +252     / is-ground     -> +255
//   collides          -> +254     / unsafe ground -> +255 mirror
//   friction          -> +264     / min/max depth -> +212/+216
struct CollisionShapeComponent {
    // The authored local shape (ShapeComponent's shape, inherited by every
    // CollisionShapeComponent).
    ShapeType shape_type = kShapeNone;
    Rect      rect;
    Circle    circle;
    Polygon   polygon;
    Vec2      offset;                 // ShapeComponent offset (local translation)

    bool  enabled        = true;
    bool  collides       = true;
    // A *wall* is a shape the pair loop may resolve against. A level-boundary
    // polygon (a single loop spanning the whole area, thousands of units across)
    // is not a wall: a character inside it is contained, not penetrating, and
    // resolving that depth launches the character out of the level. Those keep
    // `collides` (so the ground query still sees them) but are not walls.
    bool  is_wall        = true;
    bool  is_ground      = false;
    bool  unsafe_ground  = false;
    bool  receives_damage = false;
    bool  inflicts_damage = false;
    bool  bone_controlled = false;
    float friction       = 0.0f;
    float min_depth      = 0.0f;      // engine: MinDepth, the authored depth band
    float max_depth      = 0.0f;      // engine: MaxDepth
    // CollisionShapeComponent::WorldMinDepth / WorldMaxDepth (+196 / +200):
    // "object rotation + Min/MaxDepth * object scale". The shape's own depth
    // band is *these*, not position.z — the scene's Z is the camera axis and the
    // band is a rotation+scale fold the engine recomputes every time the shape
    // takes the owner transform (see update_collision_shape).
    float world_min_depth = 0.0f;
    float world_max_depth = 0.0f;
    int   special_type   = 0;
    // SceneObject::Depth — the ground query compares the object's depth band
    // (`depth + min_depth .. depth + max_depth`) against the probe's range
    // (Scene::LineSegmentIntersectsGround, 0x3769D4).
    float object_depth   = 0.0f;
    std::string identifier;
    std::string owner;                // owning SceneObject's identifier

    // Live transform, filled from the owning SceneObject each frame
    // (CollisionShapeComponent::UpdateCollisionShape).
    Vec2  world_position;
    float world_rotation = 0.0f;
    float world_scale    = 1.0f;
    bool  world_flip     = false;
    Vec2  world_velocity;

    // Set by CollisionShapeComponent::SetUpdatedShape / UpdateWorldAABB.
    Rect  local_bounds;
    Rect  world_aabb;

    int owner_index = -1;             // index into the world's object list

    // The component's live shape (Caver::CollisionShapeComponent::collisionShape
    // returns the *updated* shape, not the authored one).
    CollisionShape world_shape() const;
    // SetDefaultShapeOfType: type 1 takes the LocalAabb as a rectangle, type 2 a
    // circle centred on the object origin whose radius is the largest absolute
    // LocalAabb extent (0x290658).
    void set_default_shape_of_type(int type, const Rect& local_aabb);
    // UpdateCollisionShape (0x290444): copy the object transform onto the shape.
    void update_collision_shape(const Vec2& object_pos, float object_rot, float object_scale,
                                bool object_flip, const Vec2& object_velocity);
    // Scene::LineSegmentIntersectsGround's band test, over the shape's own
    // world depth range:
    //   objectDepth-agnostic in the engine: it compares this shape's
    //   rotation-folded depth band against the query's band.
    bool depth_band_overlaps(float query_min, float query_max) const {
        return world_min_depth <= query_max && world_max_depth >= query_min;
    }
    // SetUpdatedShape (0x29053C) + UpdateWorldAABB (0x290CA4).
    void set_updated_shape();
    void update_world_aabb();
};

// ─────────────────────────────────────────────────────────────────────────────
// CollisionPairSet — RegisterCollision / Purge
// ─────────────────────────────────────────────────────────────────────────────
//
// The engine keeps a map<CollisionPair,bool> and treats a repeated insertion as
// "these two have already been reported"; Purge clears it after a frame in which
// nothing moved much (Scene::Update: `dt > 0.001 && scene[4] < 1`).
class CollisionPairSet {
public:
    // Returns true when this exact pair was already registered this frame.
    bool register_collision(int a, int b);
    void purge() { pairs_.clear(); }
    size_t size() const { return pairs_.size(); }

private:
    std::vector<uint64_t> pairs_;
};

// ─────────────────────────────────────────────────────────────────────────────
// PhysicsObjectState — what the ground message actually does
// ─────────────────────────────────────────────────────────────────────────────
//
// A 2D body with the engine's fields: position, velocity, rotation, the ground
// state flags and the friction/deceleration values PhysicsObjectComponent
// loads.
struct PhysicsBody {
    Vec2  position;
    Vec2  velocity;
    float rotation = 0.0f;
    float rotation_speed = 0.0f;
    float radius = 0.0f;              // half the world AABB width, as the engine uses
    float gravity_magnitude = 0.0f;   // PhysicsObject.GravityMagnitude
    Vec2  gravity_direction{0.0f, -1.0f};
    float ground_friction = 1.0f;     // CollisionShape.Friction
    float elasticity = 0.0f;

    bool  on_ground = false;
    bool  on_solid_ground = false;
    bool  on_steep_ground = false;    // normal.y < 0.7  (PhysicsObjectState::isOnSolidGround)
    bool  stood_on_ground_this_frame = false;
    bool  use_gravity = true;
    int   ground_shape = -1;          // component index the body last stood on
    Vec2  ground_velocity;            // that surface's velocity

    // PhysicsObjectState::HandleGroundCollision(state, info) — 0x2BB278.
    // Pushes the body out of the surface along the contact normal and folds the
    // velocity onto the tangent. Returns true when contact was consumed.
    bool handle_ground_collision(const CollisionInfo& info, float world_aabb_width);
    // PhysicsObjectState::Update: gravity integration for one step.
    void integrate(float dt);
};

// PhysicsObjectState's own constructor defaults (0x204534): max speed 680,
// gravity vector (+96/+100) = (0, -808), drag 0, enabled 1. These are the values
// a PhysicsObjectState starts with before any PhysicsObjectComponent writes it.
constexpr float kPhysicsDefaultMaxSpeed      = 680.0f;
constexpr float kPhysicsDefaultGravityX      = 0.0f;
constexpr float kPhysicsDefaultGravityY      = -808.0f;

// Caver::PhysicsObjectState::AdjustGroundCollisionVector — 0x2BB1AC.
// For a wall/slope contact (normal.y <= 0.001) this rotates `normal` and scales
// `depth` so the push-out slides along the surface instead of into it.
void adjust_ground_collision_vector(Vec2& normal, Vec2& velocity, float& depth);

// ─────────────────────────────────────────────────────────────────────────────
// The scene-level collision loop
// ─────────────────────────────────────────────────────────────────────────────
//
// Caver::Scene::Update's collision half, in order:
//   1. every object's enabled CollisionShapeComponent with collides/ground set
//      is gathered and its world AABB refreshed;
//   2. candidates come from the spatial bounds of the mover's world AABB;
//   3. a candidate is skipped when it is the mover's own object, is hidden, is
//      disabled, or its world AABB does not overlap;
//   4. when exactly one of the pair is a polygon, the polygon is passed second;
//   5. `CollidesWithShape` decides, `RegisterCollision` de-duplicates, and both
//      objects get message 7 with the CollisionInfo (message 9 when the contact
//      is ground: normal.y >= 0.7).
class CollisionWorld {
public:
    // One entry per CollisionShapeComponent that is worth testing.
    // The owner transform CollisionShapeComponent::UpdateCollisionShape copies
    // onto its shape (SceneObject +72..+104 for pos/rot/scale/flip/velocity).
    struct ObjectTransform {
        Vec2  position;
        float rotation = 0.0f;
        float scale    = 1.0f;
        bool  flip     = false;
        Vec2  velocity;
        bool  hidden   = false;
    };

    struct Entry {
        CollisionShapeComponent component;
        int object_index = -1;
    };

    void clear() { entries_.clear(); }
    void add(CollisionShapeComponent component);
    size_t shape_count() const { return entries_.size(); }
    const std::vector<Entry>& entries() const { return entries_; }
    std::vector<Entry>& mutable_entries() { return entries_; }

    // A contact this frame, in the order the engine would have produced it.
    struct Contact {
        int   a = -1;                 // entry index of the mover
        int   b = -1;                 // entry index of the surface
        CollisionInfo info;
        bool  ground = false;         // normal.y >= 0.7  -> message 9
    };

    // Refresh every entry's world shape/AABB from the owner transforms
    // (`updates` is parallel to entries(); a null entry keeps the last
    // transform) and then resolve every overlapping pair.
    void update(const std::vector<ObjectTransform>& transforms,
                std::vector<Contact>& out_contacts);

    // Diagnostics: how many entries can participate, and how many pairs the last
    // resolution actually produced (after the pair-set de-duplication).
    size_t active_shape_count() const;
    size_t last_contact_count() const { return last_contacts_; }
    size_t update_calls() const { return update_calls_; }

    // Narrowphase + broadphase helper: all contacts involving one entry.
    void contacts_for(int entry_index, std::vector<Contact>& out_contacts) const;

    // Caver::Scene::LineSegmentIntersectsGround (0x3769D4) — the engine's ground
    // query, and the only place the character's footing is decided:
    //
    //   * the probe's bounds come from `LineSegment::Bounds` (0x1C0E6C);
    //   * candidates are the CollisionShapeComponents whose `enabled` and
    //     `collides` bytes are set and whose world depth band
    //     `[WorldMinDepth, WorldMaxDepth]` overlaps [depth_min, depth_max];
    //   * `CollisionShape::IntersectsWithLineSegment` (0x28F450) decides, and the
    //     hit NEAREST to p0 wins.
    //
    // p0 is the probe's origin (the character's AABB centre) and p1 its end, so
    // a downward probe that starts at the centre and ends 30 units below the
    // feet returns the highest surface under the character — exactly what
    // CharControllerComponent::Update's jump block tests. Returns false when
    // nothing is hit.
    bool line_segment_intersects_ground(const Vec2& p0, const Vec2& p1,
                                        float depth_min, float depth_max,
                                        Vec2* out_point, int* out_entry) const;

private:
    std::vector<Entry> entries_;
    CollisionPairSet   reported_;
    size_t             last_contacts_ = 0;
    size_t             update_calls_ = 0;
    // Uniform grid, the stand-in for Caver::SceneGrid::GetObjectsInBounds: cell
    // size 512 world units (the engine's own grid uses per-area cells; the
    // measured town_part1 AABBs are tens to hundreds of units).
    struct GridCell { std::vector<int> entries; };
};

} // namespace caver
