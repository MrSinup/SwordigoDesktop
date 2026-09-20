#include "tools/av_renderer.h"
#include <cmath>
#include <cstring>

namespace av {

static constexpr float DEG2RAD = 3.14159265358979323846f / 180.0f;

void mat4_identity(float m[16]) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void mat4_multiply(float out[16], const float a[16], const float b[16]) {
    float tmp[16];
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            tmp[c * 4 + r] =
                a[0 * 4 + r] * b[c * 4 + 0] +
                a[1 * 4 + r] * b[c * 4 + 1] +
                a[2 * 4 + r] * b[c * 4 + 2] +
                a[3 * 4 + r] * b[c * 4 + 3];
        }
    }
    std::memcpy(out, tmp, 16 * sizeof(float));
}

void mat4_translate(float out[16], float tx, float ty, float tz) {
    mat4_identity(out);
    out[12] = tx;
    out[13] = ty;
    out[14] = tz;
}

void mat4_rotate_x(float out[16], float angle_deg) {
    mat4_identity(out);
    float rad = angle_deg * DEG2RAD;
    float c = std::cos(rad), s = std::sin(rad);
    out[5]  =  c;  out[6]  = s;
    out[9]  = -s;  out[10] = c;
}

void mat4_rotate_y(float out[16], float angle_deg) {
    mat4_identity(out);
    float rad = angle_deg * DEG2RAD;
    float c = std::cos(rad), s = std::sin(rad);
    out[0]  =  c;  out[2]  = -s;
    out[8]  =  s;  out[10] =  c;
}

void mat4_rotate_z(float out[16], float angle_deg) {
    mat4_identity(out);
    float rad = angle_deg * DEG2RAD;
    float c = std::cos(rad), s = std::sin(rad);
    out[0]  =  c;  out[1]  =  s;
    out[4]  = -s;  out[5]  =  c;
}

void mat4_perspective(float out[16], float fov_deg, float aspect, float near_p, float far_p) {
    std::memset(out, 0, 16 * sizeof(float));
    float f = 1.0f / std::tan(fov_deg * DEG2RAD * 0.5f);
    out[0]  = f / aspect;
    out[5]  = f;
    out[10] = (far_p + near_p) / (near_p - far_p);
    out[11] = -1.0f;
    out[14] = (2.0f * far_p * near_p) / (near_p - far_p);
}

void mat4_ortho(float out[16], float left, float right, float bottom, float top, float near_p, float far_p) {
    std::memset(out, 0, 16 * sizeof(float));
    const float rl = (right - left);
    const float tb = (top - bottom);
    const float fn = (far_p - near_p);
    out[0]  = 2.0f / rl;
    out[5]  = 2.0f / tb;
    out[10] = -2.0f / fn;
    out[12] = -(right + left) / rl;
    out[13] = -(top + bottom) / tb;
    out[14] = -(far_p + near_p) / fn;
    out[15] = 1.0f;
}

void mat4_look_at(float out[16], float ex, float ey, float ez, float cx, float cy, float cz, float ux, float uy, float uz) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float flen = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (flen > 1e-6f) { fx /= flen; fy /= flen; fz /= flen; }
    float sx = fy * uz - fz * uy, sy = fz * ux - fx * uz, sz = fx * uy - fy * ux;
    float slen = std::sqrt(sx * sx + sy * sy + sz * sz);
    if (slen > 1e-6f) { sx /= slen; sy /= slen; sz /= slen; }
    float uux = sy * fz - sz * fy, uuy = sz * fx - sx * fz, uuz = sx * fy - sy * fx;
    out[0] = sx;  out[1] = uux; out[2] = -fx; out[3] = 0.0f;
    out[4] = sy;  out[5] = uuy; out[6] = -fy; out[7] = 0.0f;
    out[8] = sz;  out[9] = uuz; out[10] = -fz; out[11] = 0.0f;
    out[12] = -(sx * ex + sy * ey + sz * ez);
    out[13] = -(uux * ex + uuy * ey + uuz * ez);
    out[14] = -(-fx * ex - fy * ey - fz * ez);
    out[15] = 1.0f;
}

void camera_get_view_matrix(const Camera& cam, float out[16]) {
    float yaw_rad   = cam.yaw   * DEG2RAD;
    float pitch_rad = cam.pitch * DEG2RAD;
    float cos_p = std::cos(pitch_rad);
    float eye_x = cam.target[0] + cam.distance * cos_p * std::sin(yaw_rad);
    float eye_y = cam.target[1] + cam.distance * std::sin(pitch_rad);
    float eye_z = cam.target[2] + cam.distance * cos_p * std::cos(yaw_rad);
    mat4_look_at(out, eye_x, eye_y, eye_z, cam.target[0], cam.target[1], cam.target[2], 0.0f, 1.0f, 0.0f);
}

void camera_get_projection(const Camera& cam, float aspect, float out[16]) {
    if (cam.orthographic) {
        const float zoom = (cam.ortho_zoom > 1e-4f) ? cam.ortho_zoom : 1.0f;
        float half_h = cam.distance * std::tan(cam.fov * DEG2RAD * 0.5f) / zoom;
        if (half_h < 1e-4f) half_h = 1e-4f;
        const float half_w = half_h * aspect;
        mat4_ortho(out, -half_w, half_w, -half_h, half_h, cam.near_plane, cam.far_plane);
        return;
    }
    mat4_perspective(out, cam.fov, aspect, cam.near_plane, cam.far_plane);
}

} // namespace av
