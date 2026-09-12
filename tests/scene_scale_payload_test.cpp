// scene_scale_payload_test.cpp — scale-gizmo payload propagation (web editor
// `scaleObjectData` parity, master TODO 1.3).
//
// Scaling an object in the gizmo must also scale its authored geometry data:
//   - LocalAABB (object field 8, Rectangle bytes)  → X/Y/W/H × (sx, sy, sx, sy)
//   - ShapeComponent payload: Rectangle × (sx, sy, sx, sy), Circle center ×
//     (sx, sy) + radius × max(sx, sy), Polygon points × (sx, sy)
//   - CollisionShapeComponent payload: MinDepth/MaxDepth (fields 6/7) × depth
//   - Model objects scale uniformly by the dominant-axis ratio (transform AND
//     payload); non-model objects keep per-axis ratios.
//
// Wire field numbers are tag>>3 (floats are fixed32 / WIRE_I32) — the same
// numbers scene_collision.cpp's parser reads.
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

#include "tools/scene_loader.h"
#include "platform/protobuf_reader.h"

using av::SceneData;
using av::SceneObject;
using av::SceneComponent;

// Required by libswcore (asset resolution global; other tests define it too).
std::string g_instance_assets_dir = "assets";

// ── tiny wire helpers ────────────────────────────────────────────────────────
static std::string vec2(float x, float y) {
    proto::Writer w;
    w.write_float_field(1, x);
    w.write_float_field(2, y);
    return w.to_string();
}

static std::string rect(float x, float y, float w, float h) {
    proto::Writer r;
    r.write_float_field(1, x);
    r.write_float_field(2, y);
    r.write_float_field(3, w);
    r.write_float_field(4, h);
    return r.to_string();
}

// ShapeComponent wrapper: field 120 (LEN) = { 1: Rectangle, 2: Circle,
// 3: Polygon{1: Point repeated} }.
static SceneComponent make_shape_component() {
    SceneComponent comp;
    comp.type_name = "ShapeComponent";
    proto::Writer payload;
    payload.write_bytes_field(1, rect(1.0f, 2.0f, 3.0f, 4.0f));          // Rectangle
    proto::Writer circle;                                                 // Circle
    circle.write_bytes_field(1, vec2(0.0f, 0.0f));                        // center
    circle.write_float_field(2, 5.0f);                                    // radius
    payload.write_bytes_field(2, circle.to_string());
    proto::Writer polygon;                                                // Polygon
    polygon.write_bytes_field(1, vec2(1.0f, 2.0f));
    polygon.write_bytes_field(1, vec2(3.0f, 4.0f));
    payload.write_bytes_field(3, polygon.to_string());
    proto::Writer wrap;
    wrap.write_bytes_field(120, payload.to_string());
    comp.raw_data = wrap.to_string();
    return comp;
}

// CollisionShapeComponent wrapper: field 121 (LEN) = { 6: MinDepth, 7: MaxDepth }.
static SceneComponent make_collision_component() {
    SceneComponent comp;
    comp.type_name = "CollisionShapeComponent";
    proto::Writer payload;
    payload.write_float_field(6, 10.0f);
    payload.write_float_field(7, 20.0f);
    proto::Writer wrap;
    wrap.write_bytes_field(121, payload.to_string());
    comp.raw_data = wrap.to_string();
    return comp;
}

// ── decode helpers ───────────────────────────────────────────────────────────
static float f32(const std::string& bytes, uint32_t field_number) {
    proto::Reader r(bytes);
    proto::Field f;
    while (r.read_field(f))
        if (f.field_number == field_number && f.wire_type == proto::WIRE_I32)
            return f.float_val;
    return 0.0f;
}

static std::string len_field(const std::string& bytes, uint32_t field_number) {
    proto::Reader r(bytes);
    proto::Field f;
    while (r.read_field(f))
        if (f.field_number == field_number && f.wire_type == proto::WIRE_LEN)
            return f.bytes_val;
    return {};
}

// Pull the component's payload bytes (the LEN field ≥ 50 inside raw_data).
static std::string component_payload(const SceneComponent& comp) {
    proto::Reader r(comp.raw_data);
    proto::Field f;
    while (r.read_field(f))
        if (f.field_number >= 50 && f.wire_type == proto::WIRE_LEN)
            return f.bytes_val;
    return {};
}

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}
static void check_f32(float got, float want, const char* what) {
    if (std::fabs(got - want) > 1e-3f) {
        std::printf("FAIL: %s (got %.2f want %.2f)\n", what, (double)got, (double)want);
        ++failures;
    }
}

static SceneObject make_non_model_object() {
    SceneObject obj;
    obj.name = "test_shape";
    obj.pos_x = obj.pos_y = obj.pos_z = 0.0f;
    obj.scale_x = obj.scale_y = obj.scale_z = 1.0f;
    obj.local_aabb = rect(1.0f, 2.0f, 3.0f, 4.0f);
    obj.components.push_back(make_shape_component());
    obj.components.push_back(make_collision_component());
    return obj;
}

int main() {
    // ── Non-model: per-axis ratios (2, 3, depth 4) ─────────────────────────
    {
        SceneData scene;
        scene.objects.push_back(make_non_model_object());
        if (!av::scene_scale_object_payload(scene, 0, 2.0f, 3.0f, 4.0f)) {
            std::printf("FAIL: scene_scale_object_payload returned false\n");
            return 1;
        }
        const SceneObject& o = scene.objects[0];

        // Transform untouched for non-model objects.
        check_f32(o.scale_x, 1.0f, "non-model scale_x unchanged");
        check_f32(o.scale_y, 1.0f, "non-model scale_y unchanged");

        // LocalAABB × (2, 3, 2, 3).
        check_f32(f32(o.local_aabb, 1), 2.0f, "aabb X ×2");
        check_f32(f32(o.local_aabb, 2), 6.0f, "aabb Y ×3");
        check_f32(f32(o.local_aabb, 3), 6.0f, "aabb W ×2");
        check_f32(f32(o.local_aabb, 4), 12.0f, "aabb H ×3");

        const SceneComponent& shape = o.components[0];
        const std::string payload = component_payload(shape);
        check(!payload.empty(), "shape payload present");

        const std::string r = len_field(payload, 1);
        check_f32(f32(r, 1), 2.0f, "rect X ×2");
        check_f32(f32(r, 2), 6.0f, "rect Y ×3");
        check_f32(f32(r, 3), 6.0f, "rect W ×2");
        check_f32(f32(r, 4), 12.0f, "rect H ×3");

        const std::string circle = len_field(payload, 2);
        const std::string center = len_field(circle, 1);
        check_f32(f32(center, 1), 0.0f, "circle center X");
        check_f32(f32(center, 2), 0.0f, "circle center Y");
        check_f32(f32(circle, 2), 15.0f, "circle radius × max(2,3)=3 (5→15)");

        const std::string polygon = len_field(payload, 3);
        proto::Reader pr(polygon);
        proto::Field pf;
        std::vector<std::string> points;
        while (pr.read_field(pf))
            if (pf.field_number == 1 && pf.wire_type == proto::WIRE_LEN)
                points.push_back(pf.bytes_val);
        check(points.size() == 2, "polygon has 2 points");
        if (points.size() == 2) {
            check_f32(f32(points[0], 1), 2.0f, "polygon p0.x ×2");
            check_f32(f32(points[0], 2), 6.0f, "polygon p0.y ×3");
            check_f32(f32(points[1], 1), 6.0f, "polygon p1.x ×2");
            check_f32(f32(points[1], 2), 12.0f, "polygon p1.y ×3");
        }

        const SceneComponent& coll = o.components[1];
        const std::string cpayload = component_payload(coll);
        check_f32(f32(cpayload, 6), 40.0f, "minDepth ×4 (10→40)");
        check_f32(f32(cpayload, 7), 80.0f, "maxDepth ×4 (20→80)");
    }

    // ── Model: dominant-axis uniform ratio (drag X ×2 → everything ×2) ─────
    {
        SceneData scene;
        SceneObject obj = make_non_model_object();
        obj.mesh_name = "hero.pod";   // makes it a model object
        // Simulate the real gizmo flow: the drag already wrote the per-axis
        // scale (old 1,1,1 × ratios 2,1,1) BEFORE the payload fold runs.
        obj.scale_x = 2.0f;
        obj.scale_y = 1.0f;
        obj.scale_z = 1.0f;
        scene.objects.push_back(obj);
        if (!av::scene_scale_object_payload(scene, 0, 2.0f, 1.0f, 1.0f)) {
            std::printf("FAIL: model scale returned false\n");
            return 1;
        }
        const SceneObject& o = scene.objects[0];
        // Dominant ratio a=2 → uniform ×2 on every axis.
        check_f32(o.scale_x, 2.0f, "model scale_x uniform ×2");
        check_f32(o.scale_y, 2.0f, "model scale_y uniform ×2");
        check_f32(o.scale_z, 2.0f, "model scale_z uniform ×2");
        check_f32(f32(o.local_aabb, 1), 2.0f, "model aabb X ×2");
        check_f32(f32(o.local_aabb, 3), 6.0f, "model aabb W ×2");
    }

    // ── Out-of-range index ───────────────────────────────────────────────────
    {
        SceneData scene;
        if (av::scene_scale_object_payload(scene, 0, 2.0f, 2.0f, 2.0f)) {
            std::printf("FAIL: out-of-range index accepted\n");
            ++failures;
        }
    }

    std::printf(failures == 0 ? "PASS: scale gizmo payload propagation works\n"
                              : "FAIL: %d assertion(s)\n", failures);
    return failures == 0 ? 0 : 1;
}