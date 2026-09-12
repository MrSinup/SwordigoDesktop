#include "ruby_picking.h"
#include <algorithm>
#include <cmath>

namespace ruby::picking {

RayCollision get_ray_collision_box(Ray ray, BoundingBox box)
{
    RayCollision collision = { 0 };

    bool insideBox = (ray.position.x > box.min.x) && (ray.position.x < box.max.x) &&
                     (ray.position.y > box.min.y) && (ray.position.y < box.max.y) &&
                     (ray.position.z > box.min.z) && (ray.position.z < box.max.z);

    if (insideBox) ray.direction = Vector3Negate(ray.direction);

    float t[11] = { 0 };

    t[8] = (std::abs(ray.direction.x) > 1e-9f) ? (1.0f / ray.direction.x) : 1e9f;
    t[9] = (std::abs(ray.direction.y) > 1e-9f) ? (1.0f / ray.direction.y) : 1e9f;
    t[10] = (std::abs(ray.direction.z) > 1e-9f) ? (1.0f / ray.direction.z) : 1e9f;

    t[0] = (box.min.x - ray.position.x)*t[8];
    t[1] = (box.max.x - ray.position.x)*t[8];
    t[2] = (box.min.y - ray.position.y)*t[9];
    t[3] = (box.max.y - ray.position.y)*t[9];
    t[4] = (box.min.z - ray.position.z)*t[10];
    t[5] = (box.max.z - ray.position.z)*t[10];
    t[6] = (float)fmax(fmax(fmin(t[0], t[1]), fmin(t[2], t[3])), fmin(t[4], t[5]));
    t[7] = (float)fmin(fmin(fmax(t[0], t[1]), fmax(t[2], t[3])), fmax(t[4], t[5]));

    collision.hit = !((t[7] < 0) || (t[6] > t[7]));
    collision.distance = t[6];
    collision.point = Vector3Add(ray.position, Vector3Scale(ray.direction, collision.distance));

    // Get box center point
    collision.normal = Vector3Lerp(box.min, box.max, 0.5f);
    collision.normal = Vector3Subtract(collision.point, collision.normal);
    collision.normal = Vector3Scale(collision.normal, 2.01f);
    collision.normal = Vector3Divide(collision.normal, Vector3Subtract(box.max, box.min));
    collision.normal.x = (float)((int)collision.normal.x);
    collision.normal.y = (float)((int)collision.normal.y);
    collision.normal.z = (float)((int)collision.normal.z);
    collision.normal = Vector3Normalize(collision.normal);

    if (insideBox)
    {
        ray.direction = Vector3Negate(ray.direction);
        collision.distance *= -1.0f;
        collision.normal = Vector3Negate(collision.normal);
    }

    return collision;
}

RayCollision get_ray_collision_triangle(Ray ray, Vector3 p1, Vector3 p2, Vector3 p3)
{
    constexpr float kEpsilon = 0.000001f;

    RayCollision collision = { 0 };
    Vector3 edge1 = Vector3Subtract(p2, p1);
    Vector3 edge2 = Vector3Subtract(p3, p1);

    Vector3 p = Vector3CrossProduct(ray.direction, edge2);
    float det = Vector3DotProduct(edge1, p);

    if ((det > -kEpsilon) && (det < kEpsilon)) return collision;

    float invDet = 1.0f / det;
    Vector3 tv = Vector3Subtract(ray.position, p1);

    float u = Vector3DotProduct(tv, p) * invDet;
    if ((u < 0.0f) || (u > 1.0f)) return collision;

    Vector3 q = Vector3CrossProduct(tv, edge1);
    float v = Vector3DotProduct(ray.direction, q) * invDet;
    if ((v < 0.0f) || ((u + v) > 1.0f)) return collision;

    float t = Vector3DotProduct(edge2, q) * invDet;
    if (t > kEpsilon)
    {
        collision.hit = true;
        collision.distance = t;
        collision.normal = Vector3Normalize(Vector3CrossProduct(edge1, edge2));
        collision.point = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
    }

    return collision;
}

RayCollision get_ray_collision_triangles(Ray ray, const float* vertices, size_t vert_stride_bytes,
                                        size_t vert_count, const uint32_t* indices, size_t index_count,
                                        const Matrix* transform)
{
    RayCollision best = { 0 };
    if (!vertices || vert_count == 0) return best;

    size_t stride_floats = (vert_stride_bytes > 0) ? (vert_stride_bytes / sizeof(float)) : 3;

    Ray test_ray = ray;
    Matrix inv_transform;
    bool use_local_ray = false;

    if (transform) {
        float det = MatrixDeterminant(*transform);
        if (std::abs(det) > 1e-8f) {
            inv_transform = MatrixInvert(*transform);
            Vector3 local_pos = Vector3Transform(ray.position, inv_transform);
            Vector3 ray_target = Vector3Add(ray.position, ray.direction);
            Vector3 local_target = Vector3Transform(ray_target, inv_transform);
            Vector3 local_dir = Vector3Normalize(Vector3Subtract(local_target, local_pos));

            test_ray.position = local_pos;
            test_ray.direction = local_dir;
            use_local_ray = true;
        }
    }

    auto get_vert = [&](size_t idx) -> Vector3 {
        const float* p = vertices + idx * stride_floats;
        Vector3 v{p[0], p[1], p[2]};
        if (!use_local_ray && transform) {
            v = Vector3Transform(v, *transform);
        }
        return v;
    };

    size_t num_tris = indices ? (index_count / 3) : (vert_count / 3);
    for (size_t i = 0; i < num_tris; ++i) {
        size_t i0 = indices ? indices[i * 3 + 0] : (i * 3 + 0);
        size_t i1 = indices ? indices[i * 3 + 1] : (i * 3 + 1);
        size_t i2 = indices ? indices[i * 3 + 2] : (i * 3 + 2);

        Vector3 a = get_vert(i0);
        Vector3 b = get_vert(i1);
        Vector3 c = get_vert(i2);

        RayCollision hit = get_ray_collision_triangle(test_ray, a, b, c);
        if (hit.hit) {
            if (!best.hit || hit.distance < best.distance) {
                best = hit;
            }
        }
    }

    if (best.hit && use_local_ray && transform) {
        Vector3 world_pt = Vector3Transform(best.point, *transform);
        best.point = world_pt;
        best.distance = Vector3Length(Vector3Subtract(world_pt, ray.position));
        Matrix norm_mat = MatrixTranspose(inv_transform);
        best.normal = Vector3Normalize(Vector3Transform(best.normal, norm_mat));
    }

    return best;
}

Ray get_camera_ray(float mouse_x, float mouse_y, float viewport_w, float viewport_h,
                   const Vector3& eye, const Vector3& target, float fov_degrees)
{
    float w = (viewport_w > 1.0f) ? viewport_w : 1.0f;
    float h = (viewport_h > 1.0f) ? viewport_h : 1.0f;

    Vector3 forward = Vector3Normalize(Vector3Subtract(target, eye));
    Vector3 up_approx{0.0f, 1.0f, 0.0f};

    if (std::abs(forward.y) > 0.999f) {
        up_approx = Vector3{0.0f, 0.0f, (forward.y > 0.0f) ? -1.0f : 1.0f};
    }

    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, up_approx));
    Vector3 up = Vector3CrossProduct(right, forward);

    const float aspect = w / h;
    const float fov_rad = fov_degrees * (3.14159265358979323846f / 180.0f);
    const float tan_half_fov = std::tan(fov_rad * 0.5f);

    const float ndc_x = (2.0f * mouse_x / w) - 1.0f;
    const float ndc_y = 1.0f - (2.0f * mouse_y / h);

    Vector3 dir = Vector3Normalize(Vector3Add(
        forward,
        Vector3Add(
            Vector3Scale(right, ndc_x * tan_half_fov * aspect),
            Vector3Scale(up, ndc_y * tan_half_fov)
        )
    ));

    return Ray{ eye, dir };
}

Ray get_mouse_ray(float mouse_x, float mouse_y, float viewport_w, float viewport_h,
                  const QMatrix4x4& view, const QMatrix4x4& proj)
{
    float w = (viewport_w > 1.0f) ? viewport_w : 1.0f;
    float h = (viewport_h > 1.0f) ? viewport_h : 1.0f;

    float x = (2.0f * mouse_x / w) - 1.0f;
    float y = 1.0f - (2.0f * mouse_y / h);

    QMatrix4x4 invVP = (proj * view).inverted();
    QVector4D nearPt = invVP.map(QVector4D(x, y, -1.0f, 1.0f));
    QVector4D farPt  = invVP.map(QVector4D(x, y,  1.0f, 1.0f));

    if (std::abs(nearPt.w()) > 1e-6f) nearPt /= nearPt.w();
    if (std::abs(farPt.w()) > 1e-6f)  farPt  /= farPt.w();

    QVector3D dir = (farPt.toVector3D() - nearPt.toVector3D()).normalized();

    Ray ray;
    ray.position = Vector3{nearPt.x(), nearPt.y(), nearPt.z()};
    ray.direction = Vector3{dir.x(), dir.y(), dir.z()};
    return ray;
}

} // namespace ruby::picking
