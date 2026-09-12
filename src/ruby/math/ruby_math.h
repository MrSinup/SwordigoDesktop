#pragma once

#ifndef RAYMATH_STATIC_INLINE
#define RAYMATH_STATIC_INLINE
#endif

// Include raw Raylib math directly without rewriting
#include "raymath.h"

// Raylib Ray, RayCollision, BoundingBox types
#ifndef RL_RAY_TYPE
typedef struct Ray {
    Vector3 position;       // Ray position (origin)
    Vector3 direction;      // Ray direction (normalized)
} Ray;
#define RL_RAY_TYPE
#endif

#ifndef RL_RAYCOLLISION_TYPE
typedef struct RayCollision {
    bool hit;               // Did the ray hit something?
    float distance;         // Distance to the nearest hit
    Vector3 point;          // Point of the nearest hit
    Vector3 normal;         // Surface normal of hit
} RayCollision;
#define RL_RAYCOLLISION_TYPE
#endif

#ifndef RL_BOUNDINGBOX_TYPE
typedef struct BoundingBox {
    Vector3 min;            // Minimum vertex box-corner
    Vector3 max;            // Maximum vertex box-corner
} BoundingBox;
#define RL_BOUNDINGBOX_TYPE
#endif

#include <QVector3D>
#include <QVector4D>
#include <QMatrix4x4>
#include <QQuaternion>

namespace ruby::math {
    using Vec2 = Vector2;
    using Vec3 = Vector3;
    using Vec4 = Vector4;
    using Quat = Quaternion;
    using Mat4 = Matrix;
    using Ray  = ::Ray;
    using Collision = ::RayCollision;
    using AABB = ::BoundingBox;

    // Zero-copy conversions to/from Qt
    inline QVector3D to_qt(const Vector3& v) { return QVector3D(v.x, v.y, v.z); }
    inline Vector3 from_qt(const QVector3D& v) { return Vector3{v.x(), v.y(), v.z()}; }

    inline QQuaternion to_qt(const Quaternion& q) { return QQuaternion(q.w, q.x, q.y, q.z); }
    inline Quaternion from_qt(const QQuaternion& q) { return Quaternion{q.x(), q.y(), q.z(), q.scalar()}; }

    inline QMatrix4x4 to_qt(const Matrix& m) {
        return QMatrix4x4(
            m.m0, m.m4, m.m8,  m.m12,
            m.m1, m.m5, m.m9,  m.m13,
            m.m2, m.m6, m.m10, m.m14,
            m.m3, m.m7, m.m11, m.m15
        );
    }
    inline Matrix from_qt(const QMatrix4x4& m) {
        const float* d = m.constData();
        Matrix res;
        res.m0 = d[0];  res.m4 = d[4];  res.m8 = d[8];   res.m12 = d[12];
        res.m1 = d[1];  res.m5 = d[5];  res.m9 = d[9];   res.m13 = d[13];
        res.m2 = d[2];  res.m6 = d[6];  res.m10 = d[10]; res.m14 = d[14];
        res.m3 = d[3];  res.m7 = d[7];  res.m11 = d[11]; res.m15 = d[15];
        return res;
    }

    inline Matrix from_gl(const float gl[16]) {
        Matrix res;
        res.m0 = gl[0];  res.m4 = gl[4];  res.m8 = gl[8];   res.m12 = gl[12];
        res.m1 = gl[1];  res.m5 = gl[5];  res.m9 = gl[9];   res.m13 = gl[13];
        res.m2 = gl[2];  res.m6 = gl[6];  res.m10 = gl[10]; res.m14 = gl[14];
        res.m3 = gl[3];  res.m7 = gl[7];  res.m11 = gl[11]; res.m15 = gl[15];
        return res;
    }
}
