#pragma once

#include "ruby/math/ruby_math.h"
#include <vector>
#include <cstdint>

namespace ruby::picking {

// Direct Raylib collision kernels extracted from rmodels.c
RayCollision get_ray_collision_box(Ray ray, BoundingBox box);
RayCollision get_ray_collision_triangle(Ray ray, Vector3 p1, Vector3 p2, Vector3 p3);

// Triangle mesh raycast test
RayCollision get_ray_collision_triangles(Ray ray, const float* vertices, size_t vert_stride_bytes,
                                        size_t vert_count, const uint32_t* indices, size_t index_count,
                                        const Matrix* transform = nullptr);

// Geometrically unproject mouse coordinates into an exact world-space Ray from camera eye
Ray get_camera_ray(float mouse_x, float mouse_y, float viewport_w, float viewport_h,
                   const Vector3& eye, const Vector3& target, float fov_degrees = 45.0f);

// Unproject mouse coordinates into a world-space Ray
Ray get_mouse_ray(float mouse_x, float mouse_y, float viewport_w, float viewport_h,
                  const QMatrix4x4& view, const QMatrix4x4& proj);

} // namespace ruby::picking
