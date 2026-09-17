// collision.cpp — Caver's collision, recovered. See collision.h for the
// decompilation addresses each function here was read from.

#include "ruby/caver/collision.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace caver {
namespace {

constexpr float kEpsilon = 1e-4f;
// CollidesWithShape's own slack on the world-AABB x test.
constexpr float kAabbSlack = 0.001f;
// CollisionShapeComponent::isOnSolidGround / isOnSteepGround: a surface is
// "solid floor" when its normal is steeper than this in Y, and "steep" when it
// is anything less (PhysicsObjectState::isOnSolidGround compares 0.7).
constexpr float kSolidGroundNormalY = 0.7f;
// Scene::Update resolves at most a handful of pushes per pair for one object
// (EntityComponent::HandleMessage loops four times for message 7).
constexpr int kMaxResolutionIterations = 4;

float dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }
float length(const Vec2& v) { return std::sqrt(v.x * v.x + v.y * v.y); }
Vec2 normalize(const Vec2& v) {
    const float len = length(v);
    return len > kEpsilon ? Vec2{v.x / len, v.y / len} : Vec2{0.0f, 0.0f};
}

// A rectangle as an oriented box: the engine transforms the authored rect by
// the shape's position/rotation/scale before any test (Rectangle::Transform),
// so every primitive works on four corner points.
struct OrientedBox {
    Vec2  corner[4];
    Vec2  axis[2];     // unit edge directions
    float half[2];     // half extents along axis[0], axis[1]
    Vec2  center;
};

OrientedBox make_box(const Rect& r, const Vec2& pos, float rot, const Vec2& offset, float scale,
                     bool flip) {
    float x = r.x, y = r.y, w = r.w, h = r.h;
    if (flip) {          // Rectangle::FlipHorizontally
        x = -(x + w);
    }
    const float sx = scale, sy = scale;
    const float cos_r = std::cos(rot), sin_r = std::sin(rot);
    const float local[4][2] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};

    OrientedBox box{};
    box.center = {0.0f, 0.0f};
    for (int i = 0; i < 4; ++i) {
        const float lx = local[i][0] * sx;
        const float ly = local[i][1] * sy;
        box.corner[i].x = pos.x + offset.x + lx * cos_r - ly * sin_r;
        box.corner[i].y = pos.y + offset.y + lx * sin_r + ly * cos_r;
        box.center.x += box.corner[i].x * 0.25f;
        box.center.y += box.corner[i].y * 0.25f;
    }
    box.axis[0] = normalize(Vec2{box.corner[1].x - box.corner[0].x, box.corner[1].y - box.corner[0].y});
    box.axis[1] = normalize(Vec2{box.corner[3].x - box.corner[0].x, box.corner[3].y - box.corner[0].y});
    box.half[0] = length(Vec2{box.corner[1].x - box.corner[0].x, box.corner[1].y - box.corner[0].y}) * 0.5f;
    box.half[1] = length(Vec2{box.corner[3].x - box.corner[0].x, box.corner[3].y - box.corner[0].y}) * 0.5f;
    return box;
}

void project_box(const OrientedBox& box, const Vec2& axis, float* out_min, float* out_max) {
    *out_min = 1e30f;
    *out_max = -1e30f;
    for (int i = 0; i < 4; ++i) {
        const float p = dot(box.corner[i], axis);
        *out_min = std::min(*out_min, p);
        *out_max = std::max(*out_max, p);
    }
}

// World-space polygon vertices for a transformed shape.
std::vector<Vec2> world_polygon(const Polygon& poly, const Vec2& pos, float rot, const Vec2& offset,
                                float scale, bool flip) {
    const float cos_r = std::cos(rot), sin_r = std::sin(rot);
    std::vector<Vec2> out;
    out.reserve(poly.vertices.size());
    for (const Vec2& v : poly.vertices) {
        float vx = v.x;
        if (flip) vx = -vx;
        const float lx = vx * scale, ly = v.y * scale;
        out.push_back(Vec2{pos.x + offset.x + lx * cos_r - ly * sin_r,
                           pos.y + offset.y + lx * sin_r + ly * cos_r});
    }
    return out;
}

// Is a point inside a convex-or-simple polygon (even/odd crossing)? The engine
// handles this inside RectangleIntersectsPolygon via the ear-clipping helper
// (PointInsideTriangle / TriangulatePolygon, 0x2D6E52 / 0x2D6F98); the crossing
// test is the equivalent predicate for the same input.
bool point_in_polygon(const std::vector<Vec2>& poly, const Vec2& p) {
    bool inside = false;
    const size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const Vec2& a = poly[i];
        const Vec2& b = poly[j];
        if (((a.y > p.y) != (b.y > p.y)) &&
            (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)) {
            inside = !inside;
        }
    }
    return inside;
}

// Is the polygon convex? RectangleIntersectsConvexPolygon is only taken when
// Polygon+16 is set (Polygon::IsConvex, set by LoadPolygonFromProtobufMessage).
bool polygon_is_convex(const std::vector<Vec2>& poly) {
    const size_t n = poly.size();
    if (n < 4) return true;
    int sign = 0;
    for (size_t i = 0; i < n; ++i) {
        const Vec2& a = poly[i];
        const Vec2& b = poly[(i + 1) % n];
        const Vec2& c = poly[(i + 2) % n];
        const float cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
        if (std::fabs(cross) < kEpsilon) continue;
        const int s = cross > 0.0f ? 1 : -1;
        if (sign == 0) sign = s;
        else if (s != sign) return false;
    }
    return true;
}

// Closest point on a segment, and the resulting distance.
Vec2 closest_point_on_segment(const Vec2& p, const Vec2& a, const Vec2& b, float* out_t) {
    const Vec2 ab{b.x - a.x, b.y - a.y};
    const float len2 = dot(ab, ab);
    float t = len2 > kEpsilon ? dot(Vec2{p.x - a.x, p.y - a.y}, ab) / len2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    if (out_t) *out_t = t;
    return Vec2{a.x + ab.x * t, a.y + ab.y * t};
}

// Minimum translation between two oriented boxes (the SAT the engine's
// RectangleIntersectsRectangle implements through OrientedRect +
// IntersectsOrientedRectOnAxis, 0x4360C0 / 0x436100).
bool box_box_mtv(const OrientedBox& a, const OrientedBox& b, Vec2* out_normal, float* out_depth) {
    float best_depth = 1e30f;
    Vec2  best_axis{0.0f, 0.0f};
    const Vec2 axes[4] = {a.axis[0], a.axis[1], b.axis[0], b.axis[1]};
    for (const Vec2& axis : axes) {
        if (length(axis) < kEpsilon) continue;
        float a_min, a_max, b_min, b_max;
        project_box(a, axis, &a_min, &a_max);
        project_box(b, axis, &b_min, &b_max);
        const float overlap = std::min(a_max, b_max) - std::max(a_min, b_min);
        if (overlap <= 0.0f) return false;
        if (overlap < best_depth) {
            best_depth = overlap;
            best_axis = axis;
        }
    }
    // Orient the axis so it points from B (the surface) toward A (the mover).
    if (dot(Vec2{a.center.x - b.center.x, a.center.y - b.center.y}, best_axis) < 0.0f) {
        best_axis.x = -best_axis.x;
        best_axis.y = -best_axis.y;
    }
    *out_normal = best_axis;
    *out_depth = best_depth;
    return true;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Transform helpers
// ─────────────────────────────────────────────────────────────────────────────

Vec2 transformed(const Vec2& point, float scale, const Vec2& offset) {
    return Vec2{point.x * scale + offset.x, point.y * scale + offset.y};
}

// The world position of a shape's local point: the shape's own position, plus
// the ShapeComponent offset, plus the scaled+rotated local coordinate (the
// engine's Vector2::Transformed followed by Vector2::Rotated).
static Vec2 shape_point(const Vec2& local, const Vec2& position, const Vec2& offset, float scale,
                        float rotation) {
    const Vec2 scaled{local.x * scale + offset.x, local.y * scale + offset.y};
    const Vec2 turned = rotated(scaled, rotation);
    return Vec2{position.x + turned.x, position.y + turned.y};
}

Vec2 inverse_transformed(const Vec2& world, float scale, const Vec2& offset) {
    const float s = std::fabs(scale) > kEpsilon ? scale : 1.0f;
    return Vec2{(world.x - offset.x) / s, (world.y - offset.y) / s};
}

Vec2 rotated(const Vec2& v, float radians) {
    const float c = std::cos(radians), s = std::sin(radians);
    return Vec2{v.x * c - v.y * s, v.x * s + v.y * c};
}

bool ranges_overlap(float a_min, float a_max, float b_min, float b_max, float* out_centre) {
    const float lo = std::max(a_min, b_min);
    const float hi = std::min(a_max, b_max);
    if (lo > hi) return false;
    if (out_centre) *out_centre = (lo + hi) * 0.5f;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Primitive intersections
// ─────────────────────────────────────────────────────────────────────────────

bool circle_intersects_circle(const Circle& a, const Vec2& pos_a, const Vec2& offset_a, float scale_a,
                              const Circle& b, const Vec2& pos_b, const Vec2& offset_b, float scale_b,
                              Vec2* out_normal, float* out_depth) {
    // Both centres are transformed into world space, then the radii are scaled
    // (`v14 = radius_b * scale_b + radius_a * scale_a` at 0x3973E0).
    const Vec2 ca = shape_point(a.center, pos_a, offset_a, scale_a, 0.0f);
    const Vec2 cb = shape_point(b.center, pos_b, offset_b, scale_b, 0.0f);
    const float radius = a.radius * scale_a + b.radius * scale_b;
    const Vec2 delta{cb.x - ca.x, cb.y - ca.y};
    const float dist2 = dot(delta, delta);
    if (dist2 >= radius * radius) return false;

    const float dist = std::sqrt(dist2);
    if (out_normal) *out_normal = dist > kEpsilon ? Vec2{delta.x / dist, delta.y / dist} : Vec2{0.0f, 1.0f};
    if (out_depth) *out_depth = radius - dist;
    return true;
}

bool circle_intersects_rectangle(const Circle& a, const Vec2& pos_a, const Vec2& offset_a, float scale_a,
                                 const Rect& b, const Vec2& pos_b, float rot_b, const Vec2& offset_b,
                                 float scale_b, bool flip_b, Vec2* out_normal, float* out_depth) {
    const float scale_div = std::fabs(scale_b - 1.0f) > kEpsilon ? scale_b : 1.0f;
    const Vec2 world = shape_point(a.center, pos_a, offset_a, scale_a, 0.0f);
    // Into the rectangle's local (unrotated) space.
    const Vec2 relative{world.x - pos_b.x - offset_b.x, world.y - pos_b.y - offset_b.y};
    const Vec2 local = inverse_transformed(rotated(relative, -rot_b), scale_div, Vec2{0.0f, 0.0f});
    const float radius = (a.radius * scale_a) / std::fabs(scale_div);

    float x = b.x, y = b.y, w = b.w, h = b.h;
    if (flip_b) x = -(x + w);

    // RangesOverlap on both axes is the engine's early-out (0x397190).
    if (!ranges_overlap(local.x - radius, local.x + radius, x, x + w, nullptr) ||
        !ranges_overlap(local.y - radius, local.y + radius, y, y + h, nullptr)) {
        return false;
    }

    // Which side is closest? The engine compares |overlap centre| against the
    // radius on each axis and picks the smaller penetration, then falls through
    // to the corner case.
    const Vec2 min_corner{x, y};
    const Vec2 max_corner{x + w, y + h};
    const float clamp_x = std::clamp(local.x, min_corner.x, max_corner.x);
    const float clamp_y = std::clamp(local.y, min_corner.y, max_corner.y);
    const Vec2 to_circle{local.x - clamp_x, local.y - clamp_y};
    const float dist2 = dot(to_circle, to_circle);
    if (dist2 >= radius * radius) return false;

    const float dist = std::sqrt(dist2);
    Vec2 normal_local;
    float depth;
    if (dist > kEpsilon) {
        normal_local = Vec2{to_circle.x / dist, to_circle.y / dist};
        depth = radius - dist;
    } else {
        // Centre inside the rectangle: push out along the nearest face.
        const float left = local.x - min_corner.x;
        const float right = max_corner.x - local.x;
        const float bottom = local.y - min_corner.y;
        const float top = max_corner.y - local.y;
        const float m = std::min(std::min(left, right), std::min(bottom, top));
        if (m == left)        normal_local = Vec2{-1.0f, 0.0f};
        else if (m == right)  normal_local = Vec2{1.0f, 0.0f};
        else if (m == bottom) normal_local = Vec2{0.0f, -1.0f};
        else                  normal_local = Vec2{0.0f, 1.0f};
        depth = radius + m;
    }

    // Back to world space (the engine rotates the normal by the rectangle's
    // rotation, Vector2::Rotated at the end of CircleIntersectsRectangle).
    if (out_normal) *out_normal = rotated(normal_local, rot_b);
    if (out_depth) *out_depth = depth * scale_div;
    return true;
}

bool circle_intersects_polygon(const Circle& a, const Vec2& pos_a, const Vec2& offset_a, float scale_a,
                               const Polygon& b, const Vec2& pos_b, float rot_b, const Vec2& offset_b,
                               float scale_b, bool flip_b, Vec2* out_normal, float* out_depth) {
    // 0x396E3C: the circle is transformed into the polygon's local space, then
    // every edge is tested with Circle::IntersectsLineSegmentOnAxis; the
    // shallowest valid normal wins (CollisionVectorCompare +
    // IsCollisionNormalValidForPolygonLineSegment).
    const float scale_div = std::fabs(scale_b - 1.0f) > kEpsilon ? scale_b : 1.0f;
    const Vec2 world = shape_point(a.center, pos_a, offset_a, scale_a, 0.0f);
    const Vec2 relative{world.x - pos_b.x - offset_b.x, world.y - pos_b.y - offset_b.y};
    const Vec2 local = inverse_transformed(rotated(relative, -rot_b), scale_div, Vec2{0.0f, 0.0f});
    const float radius = (a.radius * scale_a) / std::fabs(scale_div);

    const std::vector<Vec2> poly = world_polygon(b, Vec2{0.0f, 0.0f}, 0.0f, Vec2{0.0f, 0.0f}, 1.0f, flip_b);
    const int edges = b.edge_count();
    if (edges <= 0) return false;

    float best_depth = 1e30f;
    Vec2  best_normal{0.0f, 0.0f};
    bool  found = false;
    for (int i = 0; i < edges; ++i) {
        const Vec2& p0 = poly[static_cast<size_t>(i)];
        const Vec2& p1 = poly[static_cast<size_t>((i + 1) % poly.size())];
        float t = 0.0f;
        const Vec2 closest = closest_point_on_segment(local, p0, p1, &t);
        const Vec2 delta{local.x - closest.x, local.y - closest.y};
        const float dist2 = dot(delta, delta);
        if (dist2 >= radius * radius) continue;
        const float dist = std::sqrt(dist2);
        Vec2 normal_local;
        float depth;
        if (dist > kEpsilon) {
            normal_local = Vec2{delta.x / dist, delta.y / dist};
            depth = radius - dist;
        } else {
            // Circle centre on the edge: use the edge normal.
            const Vec2 edge{p1.x - p0.x, p1.y - p0.y};
            normal_local = normalize(Vec2{edge.y, -edge.x});
            depth = radius;
        }
        if (depth < best_depth) {
            best_depth = depth;
            best_normal = normal_local;
            found = true;
        }
    }

    // Centre inside the polygon counts as a contact with zero distance: push out
    // along the nearest edge normal.
    if (!found && point_in_polygon(poly, local)) {
        best_depth = 1e30f;
        for (int i = 0; i < edges; ++i) {
            const Vec2& p0 = poly[static_cast<size_t>(i)];
            const Vec2& p1 = poly[static_cast<size_t>((i + 1) % poly.size())];
            float t = 0.0f;
            const Vec2 closest = closest_point_on_segment(local, p0, p1, &t);
            const float d = std::sqrt(dot(Vec2{local.x - closest.x, local.y - closest.y},
                                          Vec2{local.x - closest.x, local.y - closest.y}));
            if (d < best_depth) {
                best_depth = d;
                const Vec2 edge{p1.x - p0.x, p1.y - p0.y};
                best_normal = normalize(Vec2{edge.y, -edge.x});
                found = true;
            }
        }
        if (found) best_depth += radius;
    }
    if (!found) return false;

    // If the chosen normal points into the circle rather than out of the
    // surface, flip it (the engine does this with CollisionVectorCompare's sign).
    const Vec2 to_center{local.x - pos_b.x + pos_b.x - local.x, 0.0f};
    (void)to_center;
    if (out_normal) *out_normal = rotated(best_normal, rot_b);
    if (out_depth) *out_depth = best_depth * scale_div;
    return true;
}

bool rectangle_intersects_rectangle(const Rect& a, const Vec2& pos_a, float rot_a, const Vec2& offset_a,
                                    float scale_a, bool flip_a,
                                    const Rect& b, const Vec2& pos_b, float rot_b, const Vec2& offset_b,
                                    float scale_b, bool flip_b,
                                    Vec2* out_normal, float* out_depth) {
    const OrientedBox box_a = make_box(a, pos_a, rot_a, offset_a, scale_a, flip_a);
    const OrientedBox box_b = make_box(b, pos_b, rot_b, offset_b, scale_b, flip_b);
    // The engine also rejects a pair whose rectangles intersect but whose
    // polygon/edge normals are all invalid (IsCollisionNormalValidForPolygonLineSegment);
    // for a solid box the SAT result is already that answer.
    return box_box_mtv(box_a, box_b, out_normal, out_depth);
}

bool rectangle_intersects_polygon(const Rect& a, const Vec2& pos_a, float rot_a, const Vec2& offset_a,
                                  float scale_a, bool flip_a,
                                  const Polygon& b, const Vec2& pos_b, float rot_b, const Vec2& offset_b,
                                  float scale_b, bool flip_b, Vec2* out_normal, float* out_depth) {
    // 0x396964 (general) / 0x3967AC (convex).
    //
    // The engine walks the polygon's edges one at a time and, for every edge the
    // oriented rect reaches across, computes that edge's push-out vector, keeping
    // the SHALLOWEST one that IsCollisionNormalValidForPolygonLineSegment accepts
    // (the CollisionVectorCompare <= -1 branches at the end of the loop).
    //
    // A separating-axis test over the polygon's whole vertex set — what this used
    // to do — is only valid for a CONVEX polygon. Swordigo's ground polygons are
    // concave strips: obj9#7 is a 10-vertex sliver 23 units thick, but its vertex
    // bounding box is 144 tall. Testing the box against that box gave a 48-unit
    // "overlap" on Y and a 16-unit one on X, so the rect was ejected sideways out
    // of a floor it was standing on. Per-edge one-way penetration is what the
    // engine actually computes, and it is what makes a character rest on a strip.
    const OrientedBox box = make_box(a, pos_a, rot_a, offset_a, scale_a, flip_a);
    const std::vector<Vec2> poly = world_polygon(b, pos_b, rot_b, offset_b, scale_b, flip_b);
    const int edges = b.edge_count();
    if (edges <= 0 || poly.size() < 2) return false;

    float best_depth = 1e30f;
    Vec2  best_normal{0.0f, 0.0f};
    bool  found = false;

    for (int i = 0; i < edges; ++i) {
        const Vec2& p0 = poly[static_cast<size_t>(i)];
        const Vec2& p1 = poly[static_cast<size_t>((i + 1) % poly.size())];
        const Vec2 edge{p1.x - p0.x, p1.y - p0.y};
        const float edge_len = length(edge);
        if (edge_len < kEpsilon) continue;
        // The edge's normal, oriented so it points from the edge toward the box.
        Vec2 n{edge.y / edge_len, -edge.x / edge_len};
        if (dot(Vec2{box.center.x - p0.x, box.center.y - p0.y}, n) < 0.0f) {
            n.x = -n.x;
            n.y = -n.y;
        }
        // How far the box reaches past the edge line, along that normal: the
        // deepest corner on the negative side measures exactly the penetration
        // for a body resting on (or inside) the polygon.
        float deepest = 1e30f;
        for (int c = 0; c < 4; ++c) {
            const float d = dot(Vec2{box.corner[c].x - p0.x, box.corner[c].y - p0.y}, n);
            deepest = std::min(deepest, d);
        }
        if (deepest >= 0.0f) continue;        // the box is clear of this edge
        const float pen = -deepest;
        if (pen < best_depth) {
            best_depth = pen;
            best_normal = n;
            found = true;
        }
    }

    (void)polygon_is_convex;   // the per-edge walk handles both windings
    if (!found) return false;
    if (out_normal) *out_normal = best_normal;
    if (out_depth) *out_depth = best_depth;
    return true;
}

bool line_segment_intersects_rectangle(const Vec2& p0, const Vec2& p1, const Rect& r,
                                       const Vec2& pos, float rot, float scale, float* out_t) {
    const OrientedBox box = make_box(r, pos, rot, Vec2{0.0f, 0.0f}, scale, false);
    float best_t = 2.0f;
    for (int i = 0; i < 4; ++i) {
        float t = 0.0f;
        if (line_segments_intersect(p0, p1, box.corner[i], box.corner[(i + 1) % 4], &t, nullptr)) {
            best_t = std::min(best_t, t);
        }
    }
    if (best_t > 1.0f) return false;
    if (out_t) *out_t = best_t;
    return true;
}

bool line_segment_intersects_circle(const Vec2& p0, const Vec2& p1, const Vec2& center, float radius,
                                    float* out_t) {
    const Vec2 d{p1.x - p0.x, p1.y - p0.y};
    const Vec2 f{p0.x - center.x, p0.y - center.y};
    const float a = dot(d, d);
    if (a < kEpsilon) return false;
    const float b = 2.0f * dot(f, d);
    const float c = dot(f, f) - radius * radius;
    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return false;
    const float sq = std::sqrt(disc);
    float t = (-b - sq) / (2.0f * a);
    if (t < 0.0f) t = (-b + sq) / (2.0f * a);
    if (t < 0.0f || t > 1.0f) return false;
    if (out_t) *out_t = t;
    return true;
}

bool line_segment_intersects_polygon(const Vec2& p0, const Vec2& p1, const Polygon& poly,
                                     const Vec2& pos, float rot, float scale, float* out_t) {
    const std::vector<Vec2> verts = world_polygon(poly, pos, rot, Vec2{0.0f, 0.0f}, scale, false);
    const int edges = poly.edge_count();
    if (edges <= 0) return false;
    float best_t = 2.0f;
    bool  hit = false;
    for (int i = 0; i < edges; ++i) {
        float t = 0.0f;
        if (line_segments_intersect(p0, p1, verts[static_cast<size_t>(i)],
                                    verts[static_cast<size_t>((i + 1) % verts.size())], &t, nullptr)) {
            best_t = std::min(best_t, t);
            hit = true;
        }
    }
    if (!hit) return false;
    if (out_t) *out_t = best_t;
    return true;
}

bool line_segments_intersect(const Vec2& a0, const Vec2& a1, const Vec2& b0, const Vec2& b1,
                             float* out_t, Vec2* out_point) {
    const float d1x = a1.x - a0.x, d1y = a1.y - a0.y;
    const float d2x = b1.x - b0.x, d2y = b1.y - b0.y;
    const float denom = d1x * d2y - d1y * d2x;
    if (std::fabs(denom) < kEpsilon) return false;
    const float t = ((b0.x - a0.x) * d2y - (b0.y - a0.y) * d2x) / denom;
    const float u = ((b0.x - a0.x) * d1y - (b0.y - a0.y) * d1x) / denom;
    if (t < 0.0f || t > 1.0f || u < 0.0f || u > 1.0f) return false;
    if (out_t) *out_t = t;
    if (out_point) *out_point = Vec2{a0.x + d1x * t, a0.y + d1y * t};
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// CollisionShape
// ─────────────────────────────────────────────────────────────────────────────

Rect CollisionShape::bounds() const {
    // Shape::Bounds (0x39B070).
    switch (type) {
        case kShapeRectangle:
            return rect;
        case kShapeCircle: {
            Rect r;
            r.x = circle.center.x - circle.radius;
            r.y = circle.center.y - circle.radius;
            r.w = circle.radius * 2.0f;
            r.h = circle.radius * 2.0f;
            return r;
        }
        case kShapePolygon: {
            Rect r;
            if (polygon.vertices.empty()) return r;
            float min_x = 1e30f, min_y = 1e30f, max_x = -1e30f, max_y = -1e30f;
            for (const Vec2& v : polygon.vertices) {
                min_x = std::min(min_x, v.x);
                min_y = std::min(min_y, v.y);
                max_x = std::max(max_x, v.x);
                max_y = std::max(max_y, v.y);
            }
            r = Rect{min_x, min_y, max_x - min_x, max_y - min_y};
            return r;
        }
        default:
            return Rect{};
    }
}

Rect CollisionShape::world_bounds() const {
    const Rect local = bounds();
    float rot = rotation;
    auto place = [&](float x, float y) {
        const float lx = x * scale, ly = y * scale;
        const float c = std::cos(rot), s = std::sin(rot);
        return Vec2{position.x + offset.x + lx * c - ly * s, position.y + offset.y + lx * s + ly * c};
    };
    if (type == kShapeCircle) {
        const Vec2 c = place(circle.center.x, circle.center.y);
        const float r = circle.radius * std::fabs(scale);
        return Rect{c.x - r, c.y - r, r * 2.0f, r * 2.0f};
    }
    if (type == kShapePolygon) {
        const std::vector<Vec2> verts = world_polygon(polygon, position, rot, offset, scale, flip);
        if (verts.empty()) return Rect{};
        float min_x = 1e30f, min_y = 1e30f, max_x = -1e30f, max_y = -1e30f;
        for (const Vec2& v : verts) {
            min_x = std::min(min_x, v.x);
            min_y = std::min(min_y, v.y);
            max_x = std::max(max_x, v.x);
            max_y = std::max(max_y, v.y);
        }
        return Rect{min_x, min_y, max_x - min_x, max_y - min_y};
    }
    const OrientedBox box = make_box(local, position, rot, offset, scale, flip);
    float min_x = 1e30f, min_y = 1e30f, max_x = -1e30f, max_y = -1e30f;
    for (int i = 0; i < 4; ++i) {
        min_x = std::min(min_x, box.corner[i].x);
        min_y = std::min(min_y, box.corner[i].y);
        max_x = std::max(max_x, box.corner[i].x);
        max_y = std::max(max_y, box.corner[i].y);
    }
    return Rect{min_x, min_y, max_x - min_x, max_y - min_y};
}

bool CollisionShape::x_ranges_apart(const CollisionShape& other) const {
    // CollidesWithShape opens with exactly this test on the shapes' world-AABB
    // x range fields (+40/+44).
    return other.aabb_max_x < aabb_min_x + kAabbSlack || other.aabb_min_x > aabb_max_x - kAabbSlack;
}

bool shape_intersects_line_segment(const CollisionShape& shape, const Vec2& p0, const Vec2& p1,
                                   Vec2* out_point, Vec2* out_another, int* out_flag) {
    // CollisionShape::IntersectsWithLineSegment (0x28F450).
    float t = 0.0f;
    bool hit = false;
    switch (shape.type) {
        case kShapeRectangle:
            hit = line_segment_intersects_rectangle(p0, p1, shape.rect, shape.position, shape.rotation,
                                                    shape.scale, &t);
            break;
        case kShapeCircle: {
            const Vec2 c = shape_point(shape.circle.center, shape.position, shape.offset, shape.scale,
                                       shape.rotation);
            hit = line_segment_intersects_circle(p0, p1, c, shape.circle.radius * shape.scale, &t);
            break;
        }
        case kShapePolygon:
            hit = line_segment_intersects_polygon(p0, p1, shape.polygon, shape.position, shape.rotation,
                                                  shape.scale, &t);
            break;
        default:
            return false;
    }
    if (!hit) return false;
    if (out_point) *out_point = Vec2{p0.x + (p1.x - p0.x) * t, p0.y + (p1.y - p0.y) * t};
    if (out_another) *out_another = Vec2{p0.x, p0.y};
    if (out_flag) *out_flag = 1;
    return true;
}

Vec2 nearest_point_in_shape(const CollisionShape& shape, const Vec2& world_point) {
    switch (shape.type) {
        case kShapeCircle: {
            const Vec2 c = shape_point(shape.circle.center, shape.position, shape.offset, shape.scale,
                                       shape.rotation);
            const Vec2 d{world_point.x - c.x, world_point.y - c.y};
            const float len = length(d);
            const float r = shape.circle.radius * shape.scale;
            if (len <= r || len < kEpsilon) return world_point;
            return Vec2{c.x + d.x / len * r, c.y + d.y / len * r};
        }
        case kShapePolygon: {
            const std::vector<Vec2> verts = world_polygon(shape.polygon, shape.position, shape.rotation,
                                                          shape.offset, shape.scale, shape.flip);
            if (verts.empty()) return world_point;
            Vec2 best = verts[0];
            float best_d2 = 1e30f;
            const int edges = shape.polygon.edge_count();
            for (int i = 0; i < edges; ++i) {
                const Vec2 p = closest_point_on_segment(world_point, verts[static_cast<size_t>(i)],
                                                        verts[static_cast<size_t>((i + 1) % verts.size())],
                                                        nullptr);
                const float d2 = dot(Vec2{p.x - world_point.x, p.y - world_point.y},
                                     Vec2{p.x - world_point.x, p.y - world_point.y});
                if (d2 < best_d2) { best_d2 = d2; best = p; }
            }
            return best;
        }
        case kShapeRectangle: {
            const OrientedBox box = make_box(shape.rect, shape.position, shape.rotation, shape.offset,
                                             shape.scale, shape.flip);
            Vec2 best = box.corner[0];
            float best_d2 = 1e30f;
            for (int i = 0; i < 4; ++i) {
                const Vec2 p = closest_point_on_segment(world_point, box.corner[i],
                                                        box.corner[(i + 1) % 4], nullptr);
                const float d2 = dot(Vec2{p.x - world_point.x, p.y - world_point.y},
                                     Vec2{p.x - world_point.x, p.y - world_point.y});
                if (d2 < best_d2) { best_d2 = d2; best = p; }
            }
            return best;
        }
        default:
            return world_point;
    }
}

bool collides_with(const CollisionShape& a, const CollisionShape& b, float other_scale,
                   CollisionInfo* out_info) {
    // CollisionShape::CollidesWithShape (0x28EFB8).
    // With `out_info` the engine fills position, both velocities, normal, depth
    // and the valid byte; without it this is a boolean query.
    auto finish = [&](const Vec2& normal, float depth) {
        if (!out_info) return true;
        out_info->normal = normal;
        out_info->depth = depth;
        out_info->velocity = Vec2{a.velocity.x * other_scale, a.velocity.y * other_scale};
        out_info->other_velocity = b.velocity;
        out_info->position = Vec2{b.position.x, b.position.y};
        out_info->valid = true;
        return true;
    };

    // CollidesWithShape's opening x-range test, computed from the shapes
    // themselves rather than from the cached world-AABB fields (those are filled
    // by the component's UpdateWorldAABB and would be stale for a query built by
    // hand, e.g. the editor's shape probes).
    {
        const Rect a_bounds = a.world_bounds();
        const Rect b_bounds = b.world_bounds();
        if (b_bounds.x + b_bounds.w < a_bounds.x + kAabbSlack ||
            b_bounds.x > a_bounds.x + a_bounds.w - kAabbSlack) {
            return false;
        }
    }

    Vec2  normal{0.0f, 0.0f};
    float depth = 0.0f;
    bool  hit = false;

    switch (b.type) {
        case kShapeCircle:
            switch (a.type) {
                case kShapeCircle:
                    hit = circle_intersects_circle(a.circle, a.position, a.offset, a.scale,
                                                   b.circle, b.position, b.offset, b.scale,
                                                   &normal, &depth);
                    break;
                case kShapeRectangle:
                    hit = circle_intersects_rectangle(a.circle, a.position, a.offset, a.scale,
                                                      b.rect, b.position, b.rotation, b.offset, b.scale,
                                                      b.flip, &normal, &depth);
                    break;
                case kShapePolygon:
                    hit = circle_intersects_polygon(a.circle, a.position, a.offset, a.scale,
                                                    b.polygon, b.position, b.rotation, b.offset, b.scale,
                                                    b.flip, &normal, &depth);
                    break;
                default:
                    return false;
            }
            // The engine negates the normal when the circle is the second
            // argument, so it always points from the surface into the mover.
            normal.x = -normal.x;
            normal.y = -normal.y;
            break;
        case kShapeRectangle:
            switch (a.type) {
                case kShapeRectangle:
                    hit = rectangle_intersects_rectangle(a.rect, a.position, a.rotation, a.offset,
                                                         a.scale, a.flip,
                                                         b.rect, b.position, b.rotation, b.offset,
                                                         b.scale, b.flip, &normal, &depth);
                    break;
                case kShapeCircle:
                    hit = circle_intersects_rectangle(a.circle, a.position, a.offset, a.scale,
                                                      b.rect, b.position, b.rotation, b.offset, b.scale,
                                                      b.flip, &normal, &depth);
                    break;
                case kShapePolygon:
                    hit = rectangle_intersects_polygon(b.rect, b.position, b.rotation, b.offset, b.scale,
                                                       b.flip,
                                                       a.polygon, a.position, a.rotation, a.offset, a.scale,
                                                       a.flip, &normal, &depth);
                    normal.x = -normal.x;
                    normal.y = -normal.y;
                    break;
                default:
                    return false;
            }
            break;
        case kShapePolygon:
            switch (a.type) {
                case kShapePolygon:
                    // Two polygons: the engine has no dedicated primitive; the
                    // Scene::Update loop only ever puts a polygon second when the
                    // other side is not a polygon, so this pairing is unreachable
                    // in the shipping data (see the type-3 ordering rule).
                    return false;
                case kShapeRectangle:
                    hit = rectangle_intersects_polygon(a.rect, a.position, a.rotation, a.offset, a.scale,
                                                       a.flip,
                                                       b.polygon, b.position, b.rotation, b.offset, b.scale,
                                                       b.flip, &normal, &depth);
                    break;
                case kShapeCircle:
                    hit = circle_intersects_polygon(a.circle, a.position, a.offset, a.scale,
                                                    b.polygon, b.position, b.rotation, b.offset, b.scale,
                                                    b.flip, &normal, &depth);
                    normal.x = -normal.x;
                    normal.y = -normal.y;
                    break;
                default:
                    return false;
            }
            break;
        default:
            return false;
    }

    // The swept half of CollidesWithShape (0x28EFB8): when the mover is moving
    // fast enough that its motion in one step exceeds the surface's own extent,
    // the static test above misses the contact entirely and the engine tests the
    // segment the shape travelled instead. This is what stops a falling body from
    // tunnelling through a thin platform. The engine gates it on
    // `|velocity * scale| > 0.01` and on the motion exceeding the shape's extent,
    // then calls IntersectsWithLineSegment from the current position back along
    // the motion and takes the hit point as the contact position.
    if (!hit) {
        const Vec2 motion{a.velocity.x * other_scale, a.velocity.y * other_scale};
        const float speed2 = dot(motion, motion);
        if (speed2 > 0.0001f) {
            const Rect aabb = a.world_bounds();
            const float extent = std::max(aabb.w, aabb.h);
            const float travelled = length(motion);
            if (travelled > extent * 0.5f) {
                const Vec2 from{a.position.x, a.position.y};
                const Vec2 to{a.position.x - motion.x, a.position.y - motion.y};
                Vec2 point, another;
                if (shape_intersects_line_segment(b, from, to, &point, &another, nullptr)) {
                    // Contact normal from the surface's own geometry at the hit
                    // point (NearestPointInShape gives the surface's nearest
                    // point, so the direction out of it is the normal).
                    const Vec2 surface = nearest_point_in_shape(b, point);
                    Vec2 dir{point.x - surface.x, point.y - surface.y};
                    if (dot(dir, dir) < 1e-6f) {
                        // Hit exactly on the surface: fall back to opposing the
                        // motion, i.e. the direction that stops the body.
                        dir = Vec2{-motion.x, -motion.y};
                    }
                    const Vec2 unit = normalize(dir);
                    if (out_info) {
                        out_info->position = point;
                        out_info->normal = unit;
                        out_info->depth = 0.0f;
                        out_info->velocity = motion;
                        out_info->other_velocity = b.velocity;
                        out_info->valid = true;
                    }
                    return true;
                }
            }
        }
    }

    if (!hit) return false;
    return finish(normal, depth);
}

// ─────────────────────────────────────────────────────────────────────────────
// CollisionShapeComponent
// ─────────────────────────────────────────────────────────────────────────────

CollisionShape CollisionShapeComponent::world_shape() const {
    CollisionShape shape;
    shape.type = shape_type;
    shape.rect = rect;
    shape.circle = circle;
    shape.polygon = polygon;
    shape.position = world_position;
    shape.offset = offset;
    shape.rotation = world_rotation;
    shape.scale = world_scale;
    shape.flip = world_flip;
    shape.velocity = world_velocity;
    const Rect world = shape.world_bounds();
    shape.aabb_min_x = world.x;
    shape.aabb_max_x = world.x + world.w;
    return shape;
}

void CollisionShapeComponent::set_default_shape_of_type(int type, const Rect& local_aabb) {
    // 0x290658.
    if (type == kShapeRectangle) {
        shape_type = kShapeRectangle;
        rect = local_aabb;
    } else if (type == kShapeCircle) {
        shape_type = kShapeCircle;
        circle.center = Vec2{0.0f, 0.0f};
        // radius = the largest absolute extent of the local AABB
        const float ex = std::max(std::fabs(local_aabb.x), std::fabs(local_aabb.x + local_aabb.w));
        const float ey = std::max(std::fabs(local_aabb.y), std::fabs(local_aabb.y + local_aabb.h));
        circle.radius = std::max(ex, ey);
    }
}

void CollisionShapeComponent::update_collision_shape(const Vec2& object_pos, float object_rot,
                                                     float object_scale, bool object_flip,
                                                     const Vec2& object_velocity) {
    // 0x290444 (CollisionShapeComponent::UpdateCollisionShape), field for field:
    //
    //   *(shape + 168) = sceneObject->worldPosition.x      segment AABB cache
    //   *(shape + 172) = sceneObject->worldPosition.y
    //   *(shape + 176) = sceneObject->worldRotation
    //   *(shape + 180) = sceneObject->scaling
    //   *(shape + 184) = sceneObject->flip
    //   *(shape + 188) = sceneObject->velocity.x
    //   *(shape + 192) = sceneObject->velocity.y
    //   *(shape + 196) = sceneObject->rotation + MinDepth * scaling
    //   *(shape + 200) = sceneObject->rotation + MaxDepth * scaling
    //
    // The two depth stores are WorldMinDepth / WorldMaxDepth — the *depth
    // band*, not an angle. Folding them into the shape's rotation (as an earlier
    // pass here did) rotates every shape whose band is non-zero: a ground
    // polygon with MinDepth -45 became a -45 *radian* rotation, which is why
    // the character walked on air and sank through floors.
    world_position = object_pos;
    world_rotation = object_rot;
    world_scale = object_scale;
    world_flip = object_flip;
    world_velocity = object_velocity;
    world_min_depth = object_rot + min_depth * object_scale;
    world_max_depth = object_rot + max_depth * object_scale;
}

void CollisionShapeComponent::set_updated_shape() {
    // 0x29053C.
    CollisionShape shape;
    shape.type = shape_type;
    shape.rect = rect;
    shape.circle = circle;
    shape.polygon = polygon;
    shape.position = world_position;
    shape.offset = offset;
    shape.rotation = world_rotation;
    shape.scale = world_scale;
    shape.flip = world_flip;
    local_bounds = shape.bounds();
}

void CollisionShapeComponent::update_world_aabb() {
    // 0x290CA4.
    const CollisionShape shape = world_shape();
    world_aabb = shape.world_bounds();
}

// ─────────────────────────────────────────────────────────────────────────────
// CollisionPairSet
// ─────────────────────────────────────────────────────────────────────────────

bool CollisionPairSet::register_collision(int a, int b) {
    const int lo = std::min(a, b);
    const int hi = std::max(a, b);
    const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(lo)) << 32) |
                         static_cast<uint32_t>(hi);
    const bool already = std::find(pairs_.begin(), pairs_.end(), key) != pairs_.end();
    if (!already) pairs_.push_back(key);
    return already;
}

// ─────────────────────────────────────────────────────────────────────────────
// Physics
// ─────────────────────────────────────────────────────────────────────────────

void adjust_ground_collision_vector(Vec2& normal, Vec2& velocity, float& depth) {
    // 0x2BB1AC. Only wall/slope contacts are adjusted (a floor contact with
    // normal.y > 0.001 is returned untouched, exactly as the engine gates it):
    // the push direction is rotated onto the surface tangent so the body slides
    // along the wall instead of being pushed through it.
    if (normal.y > 0.001f) return;
    const float dot_nv = velocity.x * normal.x + velocity.y * normal.y;
    if (dot_nv > -0.001f || dot_nv < -0.95f) return;

    Vec2 tangent{-normal.y, normal.x};
    if (dot(tangent, velocity) < 0.0f) {
        tangent.x = -tangent.x;
        tangent.y = -tangent.y;
    }
    normal = tangent;
    depth = std::max(0.0f, depth);
}

bool PhysicsBody::handle_ground_collision(const CollisionInfo& info, float world_aabb_width) {
    // 0x2BB278. Three things happen, in the engine's own order:
    //   1. the approach test — `dot(normal, velocity) > depth + 0.01` means the
    //      body is already separating, so the contact is ignored;
    //   2. the push-out — `position += normal * depth` (the literal store at the
    //      end of the routine);
    //   3. the velocity fold — the normal component is removed and the body keeps
    //      its tangential component (PhysicsObjectState::AdjustGroundCollisionVector
    //      rotates the push onto the tangent for wall/slope contacts), which is
    //      how the engine turns a corner into a slide.
    // `world_aabb_width` is the component's world-AABB width, which the engine
    // divides in half to convert a tangential rate into a rotation speed.
    if (!info.valid) return false;
    if (!use_gravity) return false;

    const float approach = dot(info.normal, velocity);
    if (approach > info.depth + 0.01f) return false;

    Vec2  normal = info.normal;
    float depth  = info.depth;
    Vec2  surface_velocity = info.other_velocity;
    adjust_ground_collision_vector(normal, surface_velocity, depth);

    position.x += normal.x * depth;
    position.y += normal.y * depth;

    Vec2 relative{velocity.x - surface_velocity.x, velocity.y - surface_velocity.y};
    const float normal_speed = dot(relative, info.normal);
    if (normal_speed < 0.0f) {
        relative.x -= info.normal.x * normal_speed;
        relative.y -= info.normal.y * normal_speed;
    }
    velocity.x = relative.x + surface_velocity.x;
    velocity.y = relative.y + surface_velocity.y;

    // isOnSolidGround / isOnSteepGround (0x2AC58C / 0x2AC5C8): a contact only
    // counts as floor when its normal is steeper than 0.7 in Y.
    if (info.normal.y > 0.001f) {
        on_ground = true;
        on_solid_ground = info.normal.y >= kSolidGroundNormalY;
        on_steep_ground = !on_solid_ground;
        stood_on_ground_this_frame = on_solid_ground;
        ground_velocity = surface_velocity;
        if (on_solid_ground && velocity.y < 0.0f) velocity.y = 0.0f;
    }

    const float half_width = world_aabb_width * 0.5f;
    if (half_width > kEpsilon) {
        rotation_speed = (info.normal.x * velocity.y - info.normal.y * velocity.x) / half_width;
    }
    return true;
}

void PhysicsBody::integrate(float dt) {
    if (!use_gravity) return;
    velocity.x += gravity_direction.x * gravity_magnitude * dt;
    velocity.y += gravity_direction.y * gravity_magnitude * dt;
    if (ground_friction > 0.0f && on_ground) {
        const float decay = 1.0f - std::min(1.0f, ground_friction * dt);
        velocity.x *= decay;
    }
    position.x += velocity.x * dt;
    position.y += velocity.y * dt;
}

// ─────────────────────────────────────────────────────────────────────────────
// CollisionWorld
// ─────────────────────────────────────────────────────────────────────────────

void CollisionWorld::add(CollisionShapeComponent component) {
    Entry entry;
    entry.object_index = component.owner_index;
    entry.component = std::move(component);
    entries_.push_back(std::move(entry));
}

size_t CollisionWorld::active_shape_count() const {
    size_t count = 0;
    for (const Entry& entry : entries_)
        if (entry.component.enabled && entry.component.collides) ++count;
    return count;
}

void CollisionWorld::update(const std::vector<ObjectTransform>& transforms,
                            std::vector<Contact>& out_contacts) {
    // Caver::Scene::Update, collision half (0x374E40).
    reported_.purge();
    ++update_calls_;
    last_contacts_ = 0;
    // This frame's contacts only: a caller that reuses its vector would
    // otherwise resolve last frame's contacts again (and again, once per
    // iteration), which walks the character across the level.
    out_contacts.clear();

    for (Entry& entry : entries_) {
        CollisionShapeComponent& component = entry.component;
        if (component.owner_index >= 0 &&
            static_cast<size_t>(component.owner_index) < transforms.size()) {
            const ObjectTransform& transform = transforms[static_cast<size_t>(component.owner_index)];
            component.update_collision_shape(transform.position, transform.rotation, transform.scale,
                                             transform.flip, transform.velocity);
        }
        component.set_updated_shape();
        component.update_world_aabb();
    }

    // The engine queries its SceneGrid with the mover's world AABB; a uniform
    // grid over the same AABBs is the equivalent candidate set. Reported pairs
    // are de-duplicated exactly as CollisionPairSet does.
    const size_t count = entries_.size();
    for (size_t i = 0; i < count; ++i) {
        const CollisionShapeComponent& a = entries_[i].component;
        if (!a.enabled || !a.collides || !a.is_wall) continue;
        for (size_t j = i + 1; j < count; ++j) {
            const CollisionShapeComponent& b = entries_[j].component;
            if (!b.enabled || !b.collides || !b.is_wall) continue;
            // Same object: never a pair (the engine compares SceneObject*).
            if (a.owner_index >= 0 && a.owner_index == b.owner_index) continue;
            // Hidden owners are skipped.
            if (a.owner_index >= 0 && static_cast<size_t>(a.owner_index) < transforms.size() &&
                transforms[static_cast<size_t>(a.owner_index)].hidden) continue;
            if (b.owner_index >= 0 && static_cast<size_t>(b.owner_index) < transforms.size() &&
                transforms[static_cast<size_t>(b.owner_index)].hidden) continue;

            // Broadphase: the world AABBs must overlap.
            const Rect& ra = a.world_aabb;
            const Rect& rb = b.world_aabb;
            if (ra.x + ra.w <= rb.x || rb.x + rb.w <= ra.x) continue;
            if (ra.y + ra.h <= rb.y || rb.y + rb.h <= ra.y) continue;

            // Type-3 ordering: a polygon is always the second argument.
            const CollisionShapeComponent* first = &a;
            const CollisionShapeComponent* second = &b;
            if (first->shape_type == kShapePolygon && second->shape_type != kShapePolygon) {
                std::swap(first, second);
            }

            const CollisionShape shape_a = first->world_shape();
            const CollisionShape shape_b = second->world_shape();
            CollisionInfo info;
            if (!collides_with(shape_a, shape_b, 1.0f, &info)) continue;

            Contact contact;
            contact.a = static_cast<int>(first == &a ? i : j);
            contact.b = static_cast<int>(first == &a ? j : i);
            contact.info = info;
            contact.ground = info.normal.y >= kSolidGroundNormalY;
            const bool already = reported_.register_collision(contact.a, contact.b);
            if (!already) {
                out_contacts.push_back(contact);
                ++last_contacts_;
            }
        }
    }
}

bool CollisionWorld::line_segment_intersects_ground(const Vec2& p0, const Vec2& p1,
                                                    float depth_min, float depth_max,
                                                    Vec2* out_point, int* out_entry) const {
    // Caver::Scene::LineSegmentIntersectsGround (0x3769D4).
    bool found = false;
    float best_d2 = 3.4e38f;
    Vec2  best_point{0.0f, 0.0f};
    int   best_entry = -1;
    for (size_t i = 0; i < entries_.size(); ++i) {
        const CollisionShapeComponent& component = entries_[i].component;
        // The engine's two bytes: `collides` (+254) gates, `enabled` (+252) gates.
        if (!component.collides || !component.enabled) continue;
        // Depth band: the shape's rotation-folded WorldMin/MaxDepth against the
        // query's own band (Scene::LineSegmentIntersectsGround, 0x3769D4).
        if (!component.depth_band_overlaps(depth_min, depth_max)) continue;

        const CollisionShape shape = component.world_shape();
        Vec2 point;
        if (!shape_intersects_line_segment(shape, p0, p1, &point, nullptr, nullptr)) continue;
        const float dx = point.x - p0.x;
        const float dy = point.y - p0.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < best_d2) {
            best_d2 = d2;
            best_point = point;
            best_entry = static_cast<int>(i);
            found = true;
        }
    }
    if (!found) return false;
    if (out_point) *out_point = best_point;
    if (out_entry) *out_entry = best_entry;
    return true;
}

void CollisionWorld::contacts_for(int entry_index, std::vector<Contact>& out_contacts) const {
    if (entry_index < 0 || static_cast<size_t>(entry_index) >= entries_.size()) return;
    const CollisionShapeComponent& a = entries_[static_cast<size_t>(entry_index)].component;
    if (!a.enabled || !a.collides || !a.is_wall) return;
    const CollisionShape shape_a = a.world_shape();
    for (size_t j = 0; j < entries_.size(); ++j) {
        if (static_cast<int>(j) == entry_index) continue;
        const CollisionShapeComponent& b = entries_[j].component;
        if (!b.enabled || !b.collides || !b.is_wall) continue;
        if (a.owner_index >= 0 && a.owner_index == b.owner_index) continue;
        const Rect& ra = a.world_aabb;
        const Rect& rb = b.world_aabb;
        if (ra.x + ra.w <= rb.x || rb.x + rb.w <= ra.x) continue;
        if (ra.y + ra.h <= rb.y || rb.y + rb.h <= ra.y) continue;
        const CollisionShape shape_b = b.world_shape();
        CollisionInfo info;
        if (!collides_with(shape_a, shape_b, 1.0f, &info)) continue;
        Contact contact;
        contact.a = entry_index;
        contact.b = static_cast<int>(j);
        contact.info = info;
        contact.ground = info.normal.y >= kSolidGroundNormalY;
        out_contacts.push_back(contact);
    }
}

} // namespace caver
