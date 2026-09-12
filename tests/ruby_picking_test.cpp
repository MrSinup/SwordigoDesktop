#include "ruby/viewport/ruby_picking.h"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    std::cout << "[ruby_picking_test] Testing get_camera_ray...\n";
    Vector3 eye{0.0f, 0.0f, 100.0f};
    Vector3 target{0.0f, 0.0f, 0.0f};
    float w = 800.0f, h = 600.0f;

    // Center of screen should point directly forward: (0, 0, -1)
    Ray center_ray = ruby::picking::get_camera_ray(400.0f, 300.0f, w, h, eye, target, 45.0f);
    assert(std::abs(center_ray.position.x - 0.0f) < 1e-4f);
    assert(std::abs(center_ray.position.y - 0.0f) < 1e-4f);
    assert(std::abs(center_ray.position.z - 100.0f) < 1e-4f);
    assert(std::abs(center_ray.direction.x) < 1e-4f);
    assert(std::abs(center_ray.direction.y) < 1e-4f);
    assert(std::abs(center_ray.direction.z - (-1.0f)) < 1e-4f);

    // Right of screen should have positive X direction
    Ray right_ray = ruby::picking::get_camera_ray(700.0f, 300.0f, w, h, eye, target, 45.0f);
    assert(right_ray.direction.x > 0.05f);

    std::cout << "[ruby_picking_test] Testing get_ray_collision_box...\n";
    BoundingBox box{Vector3{-10.0f, -10.0f, -10.0f}, Vector3{10.0f, 10.0f, 10.0f}};
    RayCollision hit_box = ruby::picking::get_ray_collision_box(center_ray, box);
    assert(hit_box.hit);
    assert(std::abs(hit_box.distance - 90.0f) < 1e-3f); // from z=100 to z=10 is 90

    // Ray pointing away should miss box
    Ray miss_ray{Vector3{0.0f, 50.0f, 100.0f}, Vector3{0.0f, 0.0f, -1.0f}};
    RayCollision miss_box = ruby::picking::get_ray_collision_box(miss_ray, box);
    assert(!miss_box.hit);

    std::cout << "[ruby_picking_test] Testing get_ray_collision_triangles with Matrix transform...\n";
    float verts[] = {
        -10.0f, -10.0f, 0.0f,
         10.0f, -10.0f, 0.0f,
          0.0f,  10.0f, 0.0f
    };
    uint32_t indices[] = {0, 1, 2};

    // Transform translating by (50, 0, 0)
    Matrix trans = MatrixTranslate(50.0f, 0.0f, 0.0f);

    Ray hit_tri_ray{Vector3{50.0f, 0.0f, 100.0f}, Vector3{0.0f, 0.0f, -1.0f}};
    RayCollision tri_hit = ruby::picking::get_ray_collision_triangles(
        hit_tri_ray, verts, 3 * sizeof(float), 3, indices, 3, &trans);

    assert(tri_hit.hit);
    assert(std::abs(tri_hit.point.x - 50.0f) < 1e-3f);
    assert(std::abs(tri_hit.point.y - 0.0f) < 1e-3f);
    assert(std::abs(tri_hit.point.z - 0.0f) < 1e-3f);
    assert(std::abs(tri_hit.distance - 100.0f) < 1e-3f);

    // Ray aimed at origin should miss since triangle was translated to x=50
    Ray origin_ray{Vector3{0.0f, 0.0f, 100.0f}, Vector3{0.0f, 0.0f, -1.0f}};
    RayCollision tri_miss = ruby::picking::get_ray_collision_triangles(
        origin_ray, verts, 3 * sizeof(float), 3, indices, 3, &trans);
    assert(!tri_miss.hit);

    std::cout << "[ruby_picking_test] All picking and matrix calculation tests passed successfully!\n";
    return 0;
}
