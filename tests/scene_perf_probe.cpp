// Temporary perf probe: time the heavy scene operations on a real large scene.
#include "tools/scene_loader.h"
#include "tools/filerift.h"
#include "tools/av_renderer.h"
#include "ruby/viewport/ruby_picking.h"
#include <cmath>

// Defined for libswcore asset paths (normally lives in ruby/main.cpp).
std::string g_instance_assets_dir = "assets";
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static double ms_since(std::chrono::steady_clock::time_point& t0) {
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    t0 = t1;
    return ms;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1]
        : "/home/quantumcreeper/.local/share/swordigo-desktop/assets/resources/mountain.scene";
    auto t0 = std::chrono::steady_clock::now();
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::printf("cannot open %s\n", path); return 1; }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::printf("file: %s (%.1f MB)\n", path, bytes.size() / 1048576.0);
    std::printf("read: %.2f ms\n", ms_since(t0));

    std::vector<std::string> extra_roots;
    std::vector<uint8_t> buf(bytes.begin(), bytes.end());
    av::SceneData scene = av::scene_load_bytes(buf, path, extra_roots);
    std::printf("scene_load_bytes: %.2f ms, objects=%zu\n", ms_since(t0), scene.objects.size());
    if (scene.objects.empty()) { std::printf("parse failed\n"); return 1; }

    const std::string ser = av::scene_serialize(scene);
    std::printf("scene_serialize: %.2f ms (%zu bytes)\n", ms_since(t0), ser.size());

    const std::string markup = ::filerift::decode_protobuf(ser, "scene");
    std::printf("decode_protobuf: %.2f ms (%zu chars)\n", ms_since(t0), markup.size());

    // The old save round trip: re-parse from disk + apply.
    av::SceneData scene2 = av::scene_load_bytes(buf, path, extra_roots);
    std::printf("parse again: %.2f ms\n", ms_since(t0));

    // undo snapshot capture = scene_serialize (twice per push).
    const std::string ser2 = av::scene_serialize(scene);
    std::printf("scene_serialize #2: %.2f ms\n", ms_since(t0));

    // Inspector click path: per-object component field decode.
    double worst_fields = 0.0;
    size_t worst_idx = 0;
    for (size_t i = 0; i < scene.objects.size(); ++i) {
        auto t = std::chrono::steady_clock::now();
        size_t nfields = 0;
        for (const auto& c : scene.objects[i].components) {
            const auto f = av::scene_component_fields(c);
            nfields += f.size();
            (void)av::scene_component_class_name(c);
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
        if (ms > worst_fields) { worst_fields = ms; worst_idx = i; }
    }
    std::printf("inspector component decode (worst object #%zu): %.2f ms\n", worst_idx, worst_fields);

    // ── Pick raycast cost (viewport pick_object_at loop) ─────────────────────
    // Camera at the scene bounds centre, 45° FOV, like the viewport default.
    double tri_count = 0.0;
    for (const auto& o : scene.objects)
        for (const auto& gm : o.ground_meshes)
            tri_count += (gm.indices.empty() ? gm.positions.size() / 9 : gm.indices.size() / 3);
    std::printf("total ground triangles: %.0f\n", tri_count);

    auto pick_time = [&](float nx, float ny) {
        const float cx = (scene.bounds_min[0] + scene.bounds_max[0]) * 0.5f;
        const float cy = (scene.bounds_min[1] + scene.bounds_max[1]) * 0.5f;
        const float cz = (scene.bounds_min[2] + scene.bounds_max[2]) * 0.5f;
        const float ext = std::sqrt((scene.bounds_max[0]-scene.bounds_min[0])*(scene.bounds_max[0]-scene.bounds_min[0]) +
                                    (scene.bounds_max[1]-scene.bounds_min[1])*(scene.bounds_max[1]-scene.bounds_min[1]) +
                                    (scene.bounds_max[2]-scene.bounds_min[2])*(scene.bounds_max[2]-scene.bounds_min[2]));
        const float dist = std::max(120.0f, ext * 0.75f);
        const float pitch = 15.0f * 3.14159265f / 180.0f, yaw = -45.0f * 3.14159265f / 180.0f;
        const float ex = cx + dist * std::cos(pitch) * std::sin(yaw);
        const float ey = cy + dist * std::sin(pitch);
        const float ez = cz + dist * std::cos(pitch) * std::cos(yaw);

        Vector3 eye{ex, ey, ez};
        Vector3 target{cx, cy, cz};
        Ray ray = ruby::picking::get_camera_ray(nx, ny, 1440.0f, 900.0f, eye, target, 45.0f);

        auto t0p = std::chrono::steady_clock::now();
        float best = 1e30f; int best_idx = -1;
        for (size_t i = 0; i < scene.objects.size(); ++i) {
            const auto& obj = scene.objects[i];
            if (obj.hidden || !obj.background_name.empty() || obj.is_dimension_object) continue;
            for (const auto& gm : obj.ground_meshes) {
                if (gm.positions.empty()) continue;
                float world_mat[16];
                {
                    // swk::object_world_matrix inline (avoid imgui dep): T * Rx*Ry*Rz * S
                    float T[16], Rx[16], Ry[16], Rz[16], S[16], r_xy[16], R[16], temp[16];
                    av::mat4_translate(T, obj.pos_x, obj.pos_y, obj.pos_z);
                    av::mat4_rotate_x(Rx, obj.rot_x * 180.0f / 3.14159265f);
                    av::mat4_rotate_y(Ry, obj.rot_z * 180.0f / 3.14159265f);
                    av::mat4_rotate_z(Rz, obj.rot_y * 180.0f / 3.14159265f);
                    av::mat4_multiply(r_xy, Rx, Ry);
                    av::mat4_multiply(R, r_xy, Rz);
                    av::mat4_identity(S);
                    S[0] = obj.scale_x * obj.template_scaling;
                    S[5] = obj.scale_y * obj.template_scaling;
                    S[10] = obj.scale_z * obj.template_scaling;
                    av::mat4_multiply(temp, T, R);
                    av::mat4_multiply(world_mat, temp, S);
                }
                Matrix rmat = ruby::math::from_gl(world_mat);
                RayCollision c = ruby::picking::get_ray_collision_triangles(
                    ray, gm.positions.data(), 3 * sizeof(float),
                    gm.positions.size() / 3,
                    gm.indices.empty() ? nullptr : gm.indices.data(),
                    gm.indices.size(), &rmat);
                if (c.hit && c.distance < best) { best = c.distance; best_idx = (int)i; }
            }
        }
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0p).count();
    };

    // Warm-up then time three screen points.
    (void)pick_time(720.0f, 450.0f);
    const double p1 = pick_time(720.0f, 450.0f);
    const double p2 = pick_time(200.0f, 700.0f);
    const double p3 = pick_time(1200.0f, 200.0f);
    std::printf("pick raycast: %.2f / %.2f / %.2f ms\n", p1, p2, p3);

    std::printf("done\n");
    return 0;
}