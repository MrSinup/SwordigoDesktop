// ============================================================================
// ruby_gizmo.cpp — RubyGizmo Implementation
//
// A ground-up, high-performance transform gizmo for Ruby GG.
// Replaces the vendored im3d library entirely.
//
// Design goals:
//   1. Pixel-perfect HiDPI: sizes expressed in physical pixels via DPR.
//   2. Constant screen-space size: handles scale with distance from camera.
//   3. Modern visuals: thick axes, glowing hover, smooth anti-aliased arcs.
//   4. Zero per-frame heap allocation: fixed-size vertex ring + command list.
//   5. Correct interaction: ImGuizmo-quality axis drag, plane drag, ring drag.
// ============================================================================

#include "ruby_gizmo.h"
#include "ruby/math/ruby_math.h"
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions>
#include <cstring>
#include <cstdio>
#include <cfloat>

// QOpenGLFunctions (inherited by RubyGizmo) covers GL ES 2.0 subset (VBOs, attribs).
// The compat-pipeline functions (glColor3f, glLineWidth, etc.) come from Qt's OpenGL headers.
#ifndef GL_LIGHTING
#  define GL_LIGHTING 0x0B90
#endif

static const float kPi  = 3.14159265358979323846f;
static const float kDeg = kPi / 180.0f;

// ── Colour palette (RGB) ──────────────────────────────────────────────────────
static constexpr float kColorX[3]    = { 0.92f, 0.20f, 0.20f }; // red
static constexpr float kColorY[3]    = { 0.24f, 0.86f, 0.30f }; // green
static constexpr float kColorZ[3]    = { 0.22f, 0.46f, 0.96f }; // blue
static constexpr float kColorWhite[3]= { 1.00f, 1.00f, 1.00f };
static constexpr float kColorGrey[3] = { 0.55f, 0.55f, 0.55f };
static constexpr float kAlphaNorm    = 0.92f;
static constexpr float kAlphaDim     = 0.38f;
static constexpr float kAlphaHot     = 1.00f;
static constexpr float kAlphaPlane   = 0.22f;

// Gizmo physical-pixel sizes (sleek, high-precision ImGuizmo proportions)
static constexpr float kAxisLen      = 96.0f;  // physical px from origin to tip
static constexpr float kLineW        = 4.0f;   // crisp, modern 4px stroke
static constexpr float kConeLen      = 22.0f;  // physical px
static constexpr float kConeRadius   = 6.0f;   // physical px (neat, sharp arrow tip)
static constexpr float kCubeSide     = 11.0f;  // for scale cube tip
static constexpr float kRingRadius   = 85.0f;  // rotate ring radius in physical px
static constexpr float kRingTube     = 6.0f;   // "tube" width visual (in px)
static constexpr float kPlaneSize    = 26.0f;  // plane handle square side (px)
static constexpr float kCenterR      = 7.0f;   // center view handle radius (px)
static constexpr float kHitCylR      = 12.0f;  // hit-test cylinder radius (px)
static constexpr float kHitRingTube  = 12.0f;  // hit-test ring tube radius (px)
static constexpr float kHitCubeR     = 14.0f;  // scale cube hit radius

namespace ruby::gizmo {

// ─────────────────────────────────────────────────────────────────────────────
// Static math helpers
// ─────────────────────────────────────────────────────────────────────────────

float RubyGizmo::dot3(const float a[3], const float b[3]) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
void RubyGizmo::cross3(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}
float RubyGizmo::len3(const float v[3]) {
    return std::sqrt(dot3(v, v));
}
void RubyGizmo::normalize3(float v[3]) {
    float l = len3(v);
    if (l > 1e-9f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

// Column-major 4x4 multiply: out = a * b
void RubyGizmo::mat4_mul(float out[16], const float a[16], const float b[16]) {
    float tmp[16] = {};
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                tmp[col*4+row] += a[k*4+row] * b[col*4+k];
    std::memcpy(out, tmp, 64);
}

// Project world pos (w=1) through VP to logical screen coords.
bool RubyGizmo::mat4_project(const float vp[16], const float world[4],
                               float vp_w, float vp_h, float& sx, float& sy) {
    float cx = vp[0]*world[0] + vp[4]*world[1] + vp[8]*world[2]  + vp[12]*world[3];
    float cy = vp[1]*world[0] + vp[5]*world[1] + vp[9]*world[2]  + vp[13]*world[3];
    //float cz= vp[2]*world[0] + vp[6]*world[1] + vp[10]*world[2] + vp[14]*world[3];
    float cw = vp[3]*world[0] + vp[7]*world[1] + vp[11]*world[2] + vp[15]*world[3];
    if (cw <= 0.0001f) return false;
    sx = ( cx / cw + 1.0f) * 0.5f * vp_w;
    sy = (-cy / cw + 1.0f) * 0.5f * vp_h;
    return true;
}

// Engine rotation matrix Rx(rot_x) * Ry(rot_z) * Rz(rot_y) — matches swk::object_world_matrix.
void RubyGizmo::rotation_matrix(const float rot_deg[3], float m[9]) {
    // rot_deg = { rot_x, rot_z, rot_y } (engine field names, angles in degrees)
    float rx = rot_deg[0] * kDeg;
    float ry = rot_deg[1] * kDeg;
    float rz = rot_deg[2] * kDeg;

    float cx = std::cos(rx), sx = std::sin(rx);
    float cy = std::cos(ry), sy = std::sin(ry);
    float cz = std::cos(rz), sz = std::sin(rz);

    // Rx
    float Rx[9] = { 1,0,0, 0,cx,-sx, 0,sx,cx };
    // Ry (this is actually rot_z in engine, but we call it "ry" for the rotation axis)
    float Ry[9] = { cy,0,sy, 0,1,0, -sy,0,cy };
    // Rz (this is rot_y in engine)
    float Rz[9] = { cz,-sz,0, sz,cz,0, 0,0,1 };

    // M = Rx * Ry * Rz
    float tmp[9];
    for (int i=0;i<3;i++) for (int j=0;j<3;j++) {
        tmp[i*3+j]=0;
        for (int k=0;k<3;k++) tmp[i*3+j]+=Rx[i*3+k]*Ry[k*3+j];
    }
    for (int i=0;i<3;i++) for (int j=0;j<3;j++) {
        m[i*3+j]=0;
        for (int k=0;k<3;k++) m[i*3+j]+=tmp[i*3+k]*Rz[k*3+j];
    }
}

void RubyGizmo::rotation_matrix_to_euler(const float m[9], float rot_deg[3]) {
    // Decompose R = Rx*Ry*Rz; matching engine's im3d_rotation_to_fields logic.
    float rot_z = std::asin(std::max(-1.0f, std::min(1.0f, m[0*3+2])));
    float rot_y = std::atan2(-m[0*3+1], m[0*3+0]);
    float rot_x = std::atan2(-m[1*3+2], m[2*3+2]);
    rot_deg[0] = rot_x / kDeg;
    rot_deg[1] = rot_z / kDeg;  // engine "rot_z" field
    rot_deg[2] = rot_y / kDeg;  // engine "rot_y" field
}

// ─────────────────────────────────────────────────────────────────────────────
// GL init / destroy
// ─────────────────────────────────────────────────────────────────────────────

RubyGizmo::~RubyGizmo() {
    // Caller must ensure GL context is current.
    destroy_gl();
}

static const char* kGizmoVert =
    "#version 120\n"
    "attribute vec3 aPos;\n"
    "attribute vec4 aColor;\n"
    "uniform mat4 uVP;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "    vColor = aColor;\n"
    "    gl_Position = uVP * vec4(aPos, 1.0);\n"
    "}\n";

static const char* kGizmoFrag =
    "#version 120\n"
    "varying vec4 vColor;\n"
    "uniform float uAlphaMul;\n"
    "uniform float uGreyMul;\n"
    "void main() {\n"
    "    float lum = dot(vColor.rgb, vec3(0.299, 0.587, 0.114));\n"
    "    vec3 col = mix(vColor.rgb, vec3(lum), clamp(uGreyMul, 0.0, 1.0));\n"
    "    gl_FragColor = vec4(col, vColor.a * uAlphaMul);\n"
    "}\n";

void RubyGizmo::init_gl() {
    if (m_gl_ok) return;

    // Bind all GL functions (including VBO/VAO extensions) from the current context.
    initializeOpenGLFunctions();

    // Shaders
    auto make_prog = [](const char* vs, const char* fs) -> QOpenGLShaderProgram* {
        auto* p = new QOpenGLShaderProgram();
        if (!p->addShaderFromSourceCode(QOpenGLShader::Vertex, vs) ||
            !p->addShaderFromSourceCode(QOpenGLShader::Fragment, fs)) {
            delete p; return nullptr;
        }
        p->bindAttributeLocation("aPos",   0);
        p->bindAttributeLocation("aColor", 1);
        if (!p->link()) { delete p; return nullptr; }
        return p;
    };

    m_sh_line = make_prog(kGizmoVert, kGizmoFrag);
    m_sh_fill = make_prog(kGizmoVert, kGizmoFrag);
    if (!m_sh_line || !m_sh_fill) return;

    glGenBuffers(1, &m_vbo);
    m_gl_ok = true;
}

void RubyGizmo::destroy_gl() {
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    delete m_sh_line; m_sh_line = nullptr;
    delete m_sh_fill; m_sh_fill = nullptr;
    m_gl_ok = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera / cursor feed
// ─────────────────────────────────────────────────────────────────────────────

void RubyGizmo::set_camera(const float view[16], const float proj[16],
                            const float eye[3],
                            float vp_w, float vp_h, float dpr) {
    std::memcpy(m_view, view, 64);
    std::memcpy(m_proj, proj, 64);
    std::memcpy(m_eye,  eye,  12);
    m_vp_w = vp_w;
    m_vp_h = vp_h;
    m_dpr  = dpr;
    mat4_mul(m_vp, m_proj, m_view);
}

void RubyGizmo::set_cursor(float x, float y, bool lmb_down) {
    m_cx = x;
    m_cy = y;
    m_lmb_prev = m_lmb;
    m_lmb = lmb_down;
}

void RubyGizmo::set_snap(float translate_units, float rotate_deg, float scale_step) {
    m_snap_t = translate_units;
    m_snap_r = rotate_deg;
    m_snap_s = scale_step;
}

// ─────────────────────────────────────────────────────────────────────────────
// screen_ray: unproject cursor into world-space ray
// ─────────────────────────────────────────────────────────────────────────────
void RubyGizmo::cursor_ray(float origin[3], float dir[3]) const {
    // NDC: x in [-1, 1], y in [-1, 1]
    float nx = (2.0f * m_cx / m_vp_w) - 1.0f;
    float ny = 1.0f - (2.0f * m_cy / m_vp_h);

    // Extract projection reciprocal scales
    float f_inv_x = (m_proj[0] != 0.0f) ? (1.0f / m_proj[0]) : 1.0f;
    float f_inv_y = (m_proj[5] != 0.0f) ? (1.0f / m_proj[5]) : 1.0f;

    // View matrix columns in paintGL are:
    // col 0 = sx, sy, sz, 0 (camera Right in world space)
    // col 1 = upx, upy, upz, 0 (camera Up in world space)
    // col 2 = -fx, -fy, -fz, 0 (-Forward in world space)
    float right[3]   = { m_view[0], m_view[4], m_view[8] };
    float up[3]      = { m_view[1], m_view[5], m_view[9] };
    float forward[3] = { -m_view[2], -m_view[6], -m_view[10] };

    // Ray direction in world space
    dir[0] = forward[0] + right[0] * (nx * f_inv_x) + up[0] * (ny * f_inv_y);
    dir[1] = forward[1] + right[1] * (nx * f_inv_x) + up[1] * (ny * f_inv_y);
    dir[2] = forward[2] + right[2] * (nx * f_inv_x) + up[2] * (ny * f_inv_y);
    normalize3(dir);

    origin[0] = m_eye[0];
    origin[1] = m_eye[1];
    origin[2] = m_eye[2];
}

bool RubyGizmo::world_to_screen(const float world[3], float& sx, float& sy) const {
    float w4[4] = { world[0], world[1], world[2], 1.0f };
    return mat4_project(m_vp, w4, m_vp_w, m_vp_h, sx, sy);
}

// ─────────────────────────────────────────────────────────────────────────────
// Gizmo scale: in actual 3D space so when zooming out, the gizmo shrinks
// naturally together with the scene object!
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// Gizmo scale: perspective world-units-per-screen-pixel computation
// Keeps gizmo at a crisp, constant, sleek screen-space size at any camera distance!
// ─────────────────────────────────────────────────────────────────────────────
float RubyGizmo::gizmo_scale(const float world_pos[3]) const {
    float clip_w = m_vp[3]*world_pos[0] + m_vp[7]*world_pos[1] + m_vp[11]*world_pos[2] + m_vp[15];
    float dist = std::abs(clip_w);
    if (dist < 1e-4f) {
        float d[3] = { world_pos[0]-m_eye[0], world_pos[1]-m_eye[1], world_pos[2]-m_eye[2] };
        dist = std::sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    }
    float proj_scale = std::max(0.2f, m_proj[5]);
    float vp_h = std::max(50.0f, m_vp_h);
    return (2.0f * dist) / (vp_h * proj_scale);
}

int RubyGizmo::hit_test(Mode mode, const float pos[3], const float rot_deg[3], const float scale[3]) {
    (void)scale;
    if (mode == Mode::None) {
        m_hover_axis = AXIS_NONE;
        return AXIS_NONE;
    }
    float scale_f = gizmo_scale(pos);
    int hit = AXIS_NONE;
    switch (mode) {
        case Mode::Translate: hit = hit_test_translate(pos, rot_deg, scale_f); break;
        case Mode::Rotate:    hit = hit_test_rotate(pos, rot_deg, scale_f); break;
        case Mode::Scale:     hit = hit_test_scale(pos, rot_deg, scale_f); break;
        default: break;
    }
    m_hover_axis = hit;
    return hit;
}

// ─────────────────────────────────────────────────────────────────────────────
// Ray-geometry intersection helpers
// ─────────────────────────────────────────────────────────────────────────────

float RubyGizmo::ray_sphere(const float ro[3], const float rd[3],
                            const float centre[3], float radius) {
    float oc[3] = { ro[0]-centre[0], ro[1]-centre[1], ro[2]-centre[2] };
    float b = dot3(oc, rd);
    float c = dot3(oc, oc) - radius*radius;
    float h = b*b - c;
    if (h < 0.0f) return -1.0f;
    float sq = std::sqrt(h);
    float t = -b - sq;
    if (t > 0.001f) return t;
    t = -b + sq;
    return (t > 0.001f) ? t : -1.0f;
}

// Ray-cylinder (infinite): ro=origin, rd=direction, ca/cb=cylinder axis endpoints, r=radius
// Returns positive t of nearest hit, or -1.
float RubyGizmo::ray_cylinder(const float ro[3], const float rd[3],
                                const float ca[3], const float cb[3], float radius) {
    float ba[3] = { cb[0]-ca[0], cb[1]-ca[1], cb[2]-ca[2] };
    float oc[3] = { ro[0]-ca[0], ro[1]-ca[1], ro[2]-ca[2] };

    float baba = dot3(ba, ba);
    float bard = dot3(ba, rd);
    float baoc = dot3(ba, oc);
    float rdrd = dot3(rd, rd);
    float rdoc = dot3(rd, oc);
    float ococ = dot3(oc, oc);

    float k2 = baba * rdrd - bard * bard;
    float k1 = baba * rdoc - bard * baoc;
    float k0 = baba * ococ - baoc * baoc - radius * radius * baba;

    float h = k1 * k1 - k2 * k0;
    if (h < 0.0f) return -1.0f;
    h = std::sqrt(h);

    // Try near hit
    float t = (-k1 - h) / k2;
    if (t < 0.001f) t = (-k1 + h) / k2;
    if (t < 0.001f) return -1.0f;

    // Check within caps
    float y = baoc + t * bard;
    if (y >= 0.0f && y <= baba) return t;
    return -1.0f;
}

// Ray-disc (one-sided).
float RubyGizmo::ray_disc(const float ro[3], const float rd[3],
                           const float centre[3], const float normal[3], float radius) {
    float t = ray_plane(ro, rd, normal, dot3(normal, centre));
    if (t < 0.001f) return -1.0f;
    float hit[3] = { ro[0]+rd[0]*t - centre[0],
                     ro[1]+rd[1]*t - centre[1],
                     ro[2]+rd[2]*t - centre[2] };
    if (dot3(hit,hit) > radius*radius) return -1.0f;
    return t;
}

// Ray-plane (infinite, one-sided).
float RubyGizmo::ray_plane(const float ro[3], const float rd[3],
                            const float n[3], float d) {
    float denom = dot3(rd, n);
    if (std::abs(denom) < 1e-6f) return -1.0f;
    float t = (d - dot3(ro, n)) / denom;
    return (t > 0.001f) ? t : -1.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Axis world direction vector
// ─────────────────────────────────────────────────────────────────────────────
void RubyGizmo::get_axes(const float rot_deg[3], float out_axes[3][3]) const {
    if (m_space == CoordinateSpace::Local && rot_deg) {
        float m[9];
        rotation_matrix(rot_deg, m);
        // Col 0 = X, Col 1 = Y, Col 2 = Z
        out_axes[0][0] = m[0]; out_axes[0][1] = m[3]; out_axes[0][2] = m[6];
        out_axes[1][0] = m[1]; out_axes[1][1] = m[4]; out_axes[1][2] = m[7];
        out_axes[2][0] = m[2]; out_axes[2][1] = m[5]; out_axes[2][2] = m[8];
    } else {
        out_axes[0][0] = 1.0f; out_axes[0][1] = 0.0f; out_axes[0][2] = 0.0f;
        out_axes[1][0] = 0.0f; out_axes[1][1] = 1.0f; out_axes[1][2] = 0.0f;
        out_axes[2][0] = 0.0f; out_axes[2][1] = 0.0f; out_axes[2][2] = 1.0f;
    }
}

void RubyGizmo::axis_vec(int axis_flag, const float rot_deg[3], float out[3]) {
    float m[9];
    rotation_matrix(rot_deg, m);
    // Matrix is col-major in row[0..2] format: m[row*3+col]
    // X column = m[*][0], Y column = m[*][1], Z column = m[*][2]
    switch (axis_flag) {
        case AXIS_X:     out[0]= m[0]; out[1]= m[3]; out[2]= m[6]; break;
        case AXIS_Y:     out[0]= m[1]; out[1]= m[4]; out[2]= m[7]; break;
        case AXIS_Z:     out[0]= m[2]; out[1]= m[5]; out[2]= m[8]; break;
        case AXIS_NEG_X: out[0]=-m[0]; out[1]=-m[3]; out[2]=-m[6]; break;
        case AXIS_NEG_Y: out[0]=-m[1]; out[1]=-m[4]; out[2]=-m[7]; break;
        case AXIS_NEG_Z: out[0]=-m[2]; out[1]=-m[5]; out[2]=-m[8]; break;
        default:         out[0]=1.0f;  out[1]=0.0f;  out[2]=0.0f;  break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Hit testing
// ─────────────────────────────────────────────────────────────────────────────

int RubyGizmo::hit_test_translate(const float pos[3], const float rot_deg[3], float scale_f) const {
    float ro[3], rd[3];
    cursor_ray(ro, rd);

    float best_t = FLT_MAX;
    int   best   = AXIS_NONE;

    // World-scale sizes
    float axis_len = kAxisLen   * scale_f * m_dpr;
    float hit_r    = kHitCylR   * scale_f * m_dpr;
    float plane_sz = kPlaneSize * scale_f * m_dpr;
    float center_r = kCenterR   * scale_f * m_dpr;

    // 1. Center view-plane move handle
    float t_center = ray_sphere(ro, rd, pos, center_r + hit_r * 0.5f);
    if (t_center > 0.0f && t_center < best_t) {
        best_t = t_center;
        best = AXIS_VIEW;
    }

    // Coordinate space axes
    float axes[3][3];
    get_axes(rot_deg, axes);

    // 2. Camera-aware plane handles (XY/XZ/YZ)
    int plane_ids[3] = { AXIS_YZ, AXIS_XZ, AXIS_XY };
    int p_ax0[3] = { 1, 0, 0 };
    int p_ax1[3] = { 2, 2, 1 };

    float to_cam[3] = { m_eye[0]-pos[0], m_eye[1]-pos[1], m_eye[2]-pos[2] };
    normalize3(to_cam);

    for (int i = 0; i < 3; ++i) {
        float* a0 = axes[p_ax0[i]];
        float* a1 = axes[p_ax1[i]];
        float n[3];
        cross3(a0, a1, n);
        normalize3(n);

        if (std::abs(dot3(to_cam, n)) < 0.08f) continue; // edge-on: suppress

        float cam_diff[3];
        sub3(pos, m_eye, cam_diff);
        float s0 = (dot3(cam_diff, a0) < 0.0f) ? 1.0f : -1.0f;
        float s1 = (dot3(cam_diff, a1) < 0.0f) ? 1.0f : -1.0f;

        float hit[3];
        float t = project_ray_plane(ro, rd, pos, n, hit);
        if (t > 0.0f && t < best_t) {
            float hit_diff[3];
            sub3(hit, pos, hit_diff);
            float u = dot3(hit_diff, a0) * s0;
            float v = dot3(hit_diff, a1) * s1;
            if (u >= 0.0f && u <= plane_sz && v >= 0.0f && v <= plane_sz) {
                best_t = t;
                best = plane_ids[i];
            }
        }
    }

    // 3. Axis lines + cone tips (camera-aware edge-on culling)
    int axis_ids[3] = { AXIS_X, AXIS_Y, AXIS_Z };
    for (int i = 0; i < 3; ++i) {
        // Singular/edge-on culling: if axis points almost straight into the camera eye ray (<15 deg),
        // dragging it causes extreme explosive screen jumps. Suppress hit test!
        if (std::abs(dot3(to_cam, axes[i])) > 0.965f) continue;

        float tip[3] = { pos[0] + axes[i][0]*axis_len,
                         pos[1] + axes[i][1]*axis_len,
                         pos[2] + axes[i][2]*axis_len };
        float sx0, sy0, sx1, sy1;
        if (world_to_screen(pos, sx0, sy0) && world_to_screen(tip, sx1, sy1)) {
            float slen = std::hypot(sx1 - sx0, sy1 - sy0);
            if (slen < 12.0f * m_dpr) continue; // pointing into camera: suppress
        }

        float ca[3] = { pos[0], pos[1], pos[2] };
        float t = ray_cylinder(ro, rd, ca, tip, hit_r);
        if (t > 0.0f && t < best_t) { best_t = t; best = axis_ids[i]; }
    }

    // 4. Negative ghost tails (-X, -Y, -Z)
    int neg_ids[3] = { AXIS_NEG_X, AXIS_NEG_Y, AXIS_NEG_Z };
    for (int i = 0; i < 3; ++i) {
        if (std::abs(dot3(to_cam, axes[i])) > 0.965f) continue;

        float neg_tip[3] = { pos[0] - axes[i][0]*axis_len*0.65f,
                             pos[1] - axes[i][1]*axis_len*0.65f,
                             pos[2] - axes[i][2]*axis_len*0.65f };
        float ca[3] = { pos[0], pos[1], pos[2] };
        float t = ray_cylinder(ro, rd, ca, neg_tip, hit_r * 0.85f);
        if (t > 0.0f && t < best_t) { best_t = t; best = neg_ids[i]; }
    }

    return best;
}

int RubyGizmo::hit_test_rotate(const float pos[3], const float rot_deg[3], float scale_f) const {
    float ro[3], rd[3];
    cursor_ray(ro, rd);

    float ring_r  = kRingRadius * scale_f * m_dpr;
    float tube_r  = kHitRingTube * scale_f * m_dpr;

    float best_t = FLT_MAX;
    int   best   = AXIS_NONE;

    // Each rotation ring lies in the plane perpendicular to its axis.
    // We sample 64 points on the ring and check distance to ray.
    int ring_axes[3] = { AXIS_X, AXIS_Y, AXIS_Z };
    float axis_vecs[3][3];
    for (int i = 0; i < 3; ++i)
        axis_vec(ring_axes[i], rot_deg, axis_vecs[i]);

    for (int i = 0; i < 3; ++i) {
        float* na = axis_vecs[i];

        // Build two tangent vectors in the ring's plane.
        float ta[3], tb[3];
        {
            float tmp[3] = { 0,1,0 };
            if (std::abs(dot3(na, tmp)) > 0.9f) { tmp[0]=1; tmp[1]=0; tmp[2]=0; }
            cross3(na, tmp, ta); normalize3(ta);
            cross3(na, ta, tb); normalize3(tb);
        }

        // Check many points on the ring via cylinder approximation:
        // treat ring as a torus — sample 32 chord segments and check each
        // as a short cylinder.
        int   best_seg = -1;
        float best_seg_t = FLT_MAX;
        const int N = 48;
        for (int s = 0; s < N; ++s) {
            float a0 = (s   * 2.0f * kPi) / N;
            float a1 = ((s+1)* 2.0f * kPi) / N;
            float p0[3] = {
                pos[0] + (ta[0]*std::cos(a0) + tb[0]*std::sin(a0)) * ring_r,
                pos[1] + (ta[1]*std::cos(a0) + tb[1]*std::sin(a0)) * ring_r,
                pos[2] + (ta[2]*std::cos(a0) + tb[2]*std::sin(a0)) * ring_r,
            };
            float p1[3] = {
                pos[0] + (ta[0]*std::cos(a1) + tb[0]*std::sin(a1)) * ring_r,
                pos[1] + (ta[1]*std::cos(a1) + tb[1]*std::sin(a1)) * ring_r,
                pos[2] + (ta[2]*std::cos(a1) + tb[2]*std::sin(a1)) * ring_r,
            };
            float t = ray_cylinder(ro, rd, p0, p1, tube_r);
            if (t > 0.0f && t < best_seg_t) { best_seg_t = t; best_seg = s; }
        }
        if (best_seg >= 0 && best_seg_t < best_t) {
            best_t = best_seg_t; best = ring_axes[i];
        }
    }

    // View-plane ring (always front-facing, screen-space circle)
    {
        float view_n[3] = { -m_view[2], -m_view[6], -m_view[10] };
        float ta[3], tb[3];
        float tmp[3] = { 0,1,0 };
        if (std::abs(dot3(view_n, tmp)) > 0.9f) { tmp[0]=1; tmp[1]=0; tmp[2]=0; }
        cross3(view_n, tmp, ta); normalize3(ta);
        cross3(view_n, ta, tb); normalize3(tb);

        const int N = 48;
        float outer_r = ring_r * 1.25f;
        float tube_v  = tube_r * 1.5f;
        for (int s = 0; s < N; ++s) {
            float a0 = (s   * 2.0f * kPi) / N;
            float a1 = ((s+1)* 2.0f * kPi) / N;
            float p0[3] = {
                pos[0] + (ta[0]*std::cos(a0) + tb[0]*std::sin(a0)) * outer_r,
                pos[1] + (ta[1]*std::cos(a0) + tb[1]*std::sin(a0)) * outer_r,
                pos[2] + (ta[2]*std::cos(a0) + tb[2]*std::sin(a0)) * outer_r,
            };
            float p1[3] = {
                pos[0] + (ta[0]*std::cos(a1) + tb[0]*std::sin(a1)) * outer_r,
                pos[1] + (ta[1]*std::cos(a1) + tb[1]*std::sin(a1)) * outer_r,
                pos[2] + (ta[2]*std::cos(a1) + tb[2]*std::sin(a1)) * outer_r,
            };
            float t = ray_cylinder(ro, rd, p0, p1, tube_v);
            if (t > 0.0f && t < best_t) { best_t = t; best = AXIS_VIEW; }
        }
    }

    return best;
}

int RubyGizmo::hit_test_scale(const float pos[3], const float rot_deg[3], float scale_f) const {
    float ro[3], rd[3];
    cursor_ray(ro, rd);

    float axis_len = kAxisLen  * scale_f * m_dpr;
    float hit_r    = kHitCylR * scale_f * m_dpr;
    float cube_r   = kHitCubeR* scale_f * m_dpr;

    float best_t = FLT_MAX;
    int   best   = AXIS_NONE;

    float axes[3][3];
    get_axes(rot_deg, axes);
    int axis_ids[3]  = { AXIS_X, AXIS_Y, AXIS_Z };

    // Cube tips on each axis
    for (int i = 0; i < 3; ++i) {
        float tip[3] = { pos[0] + axes[i][0]*axis_len,
                         pos[1] + axes[i][1]*axis_len,
                         pos[2] + axes[i][2]*axis_len };
        // Treat cube as disc
        float n[3] = { axes[i][0], axes[i][1], axes[i][2] };
        float t = ray_disc(ro, rd, tip, n, cube_r);
        if (t > 0.0f && t < best_t) { best_t = t; best = axis_ids[i]; }
    }

    // Plane handles (XY / XZ / YZ): small quads at the axis-plane corner, hit
    // as discs on the plane normal (the axis NOT in the plane).
    {
        const int plane_axes[3][2] = { {0,1}, {0,2}, {1,2} };   // XY, XZ, YZ
        const int plane_ids[3]     = { AXIS_XY, AXIS_XZ, AXIS_YZ };
        const int plane_normals[3] = { 2, 1, 0 };                // Z, Y, X
        for (int i = 0; i < 3; ++i) {
            const int a = plane_axes[i][0], b = plane_axes[i][1], n = plane_normals[i];
            float centre[3] = { pos[0] + (axes[a][0] + axes[b][0]) * axis_len * 0.5f,
                                pos[1] + (axes[a][1] + axes[b][1]) * axis_len * 0.5f,
                                pos[2] + (axes[a][2] + axes[b][2]) * axis_len * 0.5f };
            float nrm[3] = { axes[n][0], axes[n][1], axes[n][2] };
            float t = ray_disc(ro, rd, centre, nrm, axis_len * 0.30f);
            if (t > 0.0f && t < best_t) { best_t = t; best = plane_ids[i]; }
        }
    }

    // Centre uniform-scale ball — orientation-independent sphere (the old disc
    // with a fixed up-normal was unhittable from the side AND mapped to AXIS_Y,
    // so dragging the centre scaled only Y — the ImGuizmo-visible bug).
    {
        float t = ray_sphere(ro, rd, pos, cube_r * 1.2f);
        if (t > 0.0f && t < best_t) { best_t = t; best = AXIS_XYZ; }
    }

    // Shafts (for grabbing anywhere on shaft)
    for (int i = 0; i < 3; ++i) {
        float ca[3] = { pos[0], pos[1], pos[2] };
        float cb[3] = { pos[0]+axes[i][0]*axis_len, pos[1]+axes[i][1]*axis_len, pos[2]+axes[i][2]*axis_len };
        float t = ray_cylinder(ro, rd, ca, cb, hit_r);
        if (t > 0.0f && t < best_t) { best_t = t; best = axis_ids[i]; }
    }
    return best;
}

// ─────────────────────────────────────────────────────────────────────────────
// Geometry building helpers
// ─────────────────────────────────────────────────────────────────────────────

void RubyGizmo::reset_geometry() {
    m_n_verts = 0;
    m_n_cmds  = 0;
}

void RubyGizmo::push_line(const float a[3], const float b[3],
                           float r, float g, float b_, float a_) {
    if (m_n_verts + 2 >= kMaxVerts || m_n_cmds >= kMaxCmds) return;
    // Check if we can extend the previous line cmd
    bool extend = (m_n_cmds > 0 && m_cmds[m_n_cmds-1].lines &&
                   m_cmds[m_n_cmds-1].start + m_cmds[m_n_cmds-1].count == m_n_verts);
    if (!extend) {
        m_cmds[m_n_cmds++] = { m_n_verts, 0, true };
    }
    m_verts[m_n_verts++] = { a[0],a[1],a[2], r,g,b_,a_ };
    m_verts[m_n_verts++] = { b[0],b[1],b[2], r,g,b_,a_ };
    m_cmds[m_n_cmds-1].count += 2;
}

void RubyGizmo::push_cone(const float tip[3], const float base_centre[3], float radius,
                           float r, float g, float b_, float a_, int segments) {
    if (m_n_verts + segments*3 >= kMaxVerts || m_n_cmds >= kMaxCmds) return;

    // Build tangent basis
    float ax[3] = { base_centre[0]-tip[0], base_centre[1]-tip[1], base_centre[2]-tip[2] };
    normalize3(ax);
    float tmp[3] = { 0,1,0 };
    if (std::abs(dot3(ax,tmp)) > 0.9f) tmp[0]=1, tmp[1]=0, tmp[2]=0;
    float ta[3], tb[3];
    cross3(ax, tmp, ta); normalize3(ta);
    cross3(ax, ta, tb);

    m_cmds[m_n_cmds++] = { m_n_verts, 0, false };
    for (int i = 0; i < segments; ++i) {
        float a0 = (i   * 2.0f * kPi) / segments;
        float a1 = ((i+1)* 2.0f * kPi) / segments;
        float p0[3] = { base_centre[0]+(ta[0]*std::cos(a0)+tb[0]*std::sin(a0))*radius,
                        base_centre[1]+(ta[1]*std::cos(a0)+tb[1]*std::sin(a0))*radius,
                        base_centre[2]+(ta[2]*std::cos(a0)+tb[2]*std::sin(a0))*radius };
        float p1[3] = { base_centre[0]+(ta[0]*std::cos(a1)+tb[0]*std::sin(a1))*radius,
                        base_centre[1]+(ta[1]*std::cos(a1)+tb[1]*std::sin(a1))*radius,
                        base_centre[2]+(ta[2]*std::cos(a1)+tb[2]*std::sin(a1))*radius };
        m_verts[m_n_verts++] = { tip[0],tip[1],tip[2], r,g,b_,a_ };
        m_verts[m_n_verts++] = { p0[0],p0[1],p0[2], r,g,b_,a_ };
        m_verts[m_n_verts++] = { p1[0],p1[1],p1[2], r,g,b_,a_ };
    }
    m_cmds[m_n_cmds-1].count = segments*3;
}

void RubyGizmo::push_cube(const float centre[3], float hs,
                           float r, float g, float b_, float a_) {
    if (m_n_verts + 36 >= kMaxVerts || m_n_cmds >= kMaxCmds) return;
    // 6 faces × 2 tris × 3 verts
    static const float normals[6][3] = {
        {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
    };
    static const int face_verts[6][4] = {
        {1,3,7,5}, {0,4,6,2}, {2,6,7,3}, {0,1,5,4}, {4,5,7,6}, {0,2,3,1}
    };
    float corners[8][3];
    for (int i = 0; i < 8; ++i) {
        corners[i][0] = centre[0] + ((i&1)?hs:-hs);
        corners[i][1] = centre[1] + ((i&2)?hs:-hs);
        corners[i][2] = centre[2] + ((i&4)?hs:-hs);
    }
    m_cmds[m_n_cmds++] = { m_n_verts, 36, false };
    for (int f = 0; f < 6; ++f) {
        const float* A = corners[face_verts[f][0]];
        const float* B = corners[face_verts[f][1]];
        const float* C = corners[face_verts[f][2]];
        const float* D = corners[face_verts[f][3]];
        // tri 1
        m_verts[m_n_verts++] = {A[0],A[1],A[2], r,g,b_,a_};
        m_verts[m_n_verts++] = {B[0],B[1],B[2], r,g,b_,a_};
        m_verts[m_n_verts++] = {C[0],C[1],C[2], r,g,b_,a_};
        // tri 2
        m_verts[m_n_verts++] = {A[0],A[1],A[2], r,g,b_,a_};
        m_verts[m_n_verts++] = {C[0],C[1],C[2], r,g,b_,a_};
        m_verts[m_n_verts++] = {D[0],D[1],D[2], r,g,b_,a_};
    }
}

void RubyGizmo::push_circle_arc(const float centre[3],
                                 const float ta[3], const float tb[3],
                                 float radius,
                                 float angle_start, float angle_end,
                                 float r, float g, float b_, float a_,
                                 int segments, bool /*closed*/) {
    if (m_n_verts + segments*2 + 2 >= kMaxVerts || m_n_cmds >= kMaxCmds) return;
    m_cmds[m_n_cmds++] = { m_n_verts, 0, true };
    float step = (angle_end - angle_start) / segments;
    float prev[3] = {
        centre[0] + (ta[0]*std::cos(angle_start) + tb[0]*std::sin(angle_start))*radius,
        centre[1] + (ta[1]*std::cos(angle_start) + tb[1]*std::sin(angle_start))*radius,
        centre[2] + (ta[2]*std::cos(angle_start) + tb[2]*std::sin(angle_start))*radius,
    };
    for (int i = 1; i <= segments; ++i) {
        float ang = angle_start + step * i;
        float cur[3] = {
            centre[0] + (ta[0]*std::cos(ang) + tb[0]*std::sin(ang))*radius,
            centre[1] + (ta[1]*std::cos(ang) + tb[1]*std::sin(ang))*radius,
            centre[2] + (ta[2]*std::cos(ang) + tb[2]*std::sin(ang))*radius,
        };
        m_verts[m_n_verts++] = {prev[0],prev[1],prev[2], r,g,b_,a_};
        m_verts[m_n_verts++] = {cur[0],cur[1],cur[2], r,g,b_,a_};
        m_cmds[m_n_cmds-1].count += 2;
        std::memcpy(prev, cur, 12);
    }
}

void RubyGizmo::push_quad(const float p0[3], const float p1[3],
                           const float p2[3], const float p3[3],
                           float r, float g, float b_, float a_) {
    if (m_n_verts + 6 >= kMaxVerts || m_n_cmds >= kMaxCmds) return;
    m_cmds[m_n_cmds++] = { m_n_verts, 6, false };
    m_verts[m_n_verts++] = {p0[0],p0[1],p0[2], r,g,b_,a_};
    m_verts[m_n_verts++] = {p1[0],p1[1],p1[2], r,g,b_,a_};
    m_verts[m_n_verts++] = {p2[0],p2[1],p2[2], r,g,b_,a_};
    m_verts[m_n_verts++] = {p0[0],p0[1],p0[2], r,g,b_,a_};
    m_verts[m_n_verts++] = {p2[0],p2[1],p2[2], r,g,b_,a_};
    m_verts[m_n_verts++] = {p3[0],p3[1],p3[2], r,g,b_,a_};
}

void RubyGizmo::push_dashed_line(const float a[3], const float b[3], float dash_len,
                                 float r, float g, float b_, float a_) {
    float dir[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
    float total_len = len3(dir);
    if (total_len < 1e-4f || dash_len < 1e-4f) return;
    dir[0] /= total_len; dir[1] /= total_len; dir[2] /= total_len;

    float cur = 0.0f;
    while (cur < total_len) {
        float next = std::min(cur + dash_len, total_len);
        float p0[3] = { a[0] + dir[0] * cur, a[1] + dir[1] * cur, a[2] + dir[2] * cur };
        float p1[3] = { a[0] + dir[0] * next, a[1] + dir[1] * next, a[2] + dir[2] * next };
        push_line(p0, p1, r, g, b_, a_);
        cur += dash_len * 2.0f;
    }
}

void RubyGizmo::push_ruler_ticks(const float start[3], const float dir[3], float length,
                                 float step, float tick_sz,
                                 float r, float g, float b_, float a_) {
    if (step <= 0.0f || length <= 0.0f) return;
    float to_cam[3] = { m_eye[0] - start[0], m_eye[1] - start[1], m_eye[2] - start[2] };
    normalize3(to_cam);
    float tick_dir[3];
    cross3(dir, to_cam, tick_dir);
    if (len3(tick_dir) < 1e-3f) {
        float up[3] = { 0, 1, 0 };
        if (std::abs(dot3(dir, up)) > 0.9f) { up[0] = 1; up[1] = 0; up[2] = 0; }
        cross3(dir, up, tick_dir);
    }
    normalize3(tick_dir);

    for (float s = step; s <= length; s += step) {
        float p[3] = { start[0] + dir[0] * s, start[1] + dir[1] * s, start[2] + dir[2] * s };
        float t0[3] = { p[0] - tick_dir[0] * tick_sz * 0.5f,
                        p[1] - tick_dir[1] * tick_sz * 0.5f,
                        p[2] - tick_dir[2] * tick_sz * 0.5f };
        float t1[3] = { p[0] + tick_dir[0] * tick_sz * 0.5f,
                        p[1] + tick_dir[1] * tick_sz * 0.5f,
                        p[2] + tick_dir[2] * tick_sz * 0.5f };
        push_line(t0, t1, r, g, b_, a_);
    }
}

void RubyGizmo::push_protractor_wedge(const float centre[3], const float normal[3],
                                      const float ref_dir[3], float sweep_rad, float radius,
                                      float r, float g, float b_, float a_, int segments) {
    if (std::abs(sweep_rad) < 1e-4f) return;
    float n[3] = { normal[0], normal[1], normal[2] };
    normalize3(n);
    float u[3] = { ref_dir[0], ref_dir[1], ref_dir[2] };
    normalize3(u);
    float v[3];
    cross3(n, u, v);
    normalize3(v);

    int segs = std::clamp((int)(std::abs(sweep_rad) / (2.0f * kPi) * segments) + 4, 4, segments);
    float step = sweep_rad / (float)segs;

    float prev[3] = { centre[0] + u[0] * radius, centre[1] + u[1] * radius, centre[2] + u[2] * radius };
    push_line(centre, prev, r, g, b_, std::min(1.0f, a_ * 2.0f));

    for (int i = 1; i <= segs; ++i) {
        float ang = step * (float)i;
        float cos_a = std::cos(ang);
        float sin_a = std::sin(ang);
        float cur[3] = {
            centre[0] + (u[0] * cos_a + v[0] * sin_a) * radius,
            centre[1] + (u[1] * cos_a + v[1] * sin_a) * radius,
            centre[2] + (u[2] * cos_a + v[2] * sin_a) * radius
        };

        if (m_n_verts + 3 < kMaxVerts && m_n_cmds < kMaxCmds) {
            m_cmds[m_n_cmds++] = { m_n_verts, 3, false };
            m_verts[m_n_verts++] = { centre[0], centre[1], centre[2], r, g, b_, a_ };
            m_verts[m_n_verts++] = { prev[0], prev[1], prev[2], r, g, b_, a_ };
            m_verts[m_n_verts++] = { cur[0], cur[1], cur[2], r, g, b_, a_ };
        }
        push_line(prev, cur, r, g, b_, std::min(1.0f, a_ * 2.5f));
        std::memcpy(prev, cur, 12);
    }
    push_line(centre, prev, r, g, b_, std::min(1.0f, a_ * 2.0f));
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-mode draw helpers
// ─────────────────────────────────────────────────────────────────────────────

void RubyGizmo::draw_translate(const float pos[3], const float rot_deg[3], float scale_f, int hot) {
    float len    = kAxisLen   * scale_f * m_dpr;
    float cone_l = kConeLen   * scale_f * m_dpr;
    float cone_r = kConeRadius* scale_f * m_dpr;
    float plane  = kPlaneSize * scale_f * m_dpr;
    float center = kCenterR   * scale_f * m_dpr;

    float axes[3][3];
    get_axes(rot_deg, axes);
    const float* cols[3] = { kColorX, kColorY, kColorZ };
    int axis_ids[3] = { AXIS_X, AXIS_Y, AXIS_Z };
    int neg_ids[3]  = { AXIS_NEG_X, AXIS_NEG_Y, AXIS_NEG_Z };

    // ── 1. Center view-plane move handle ──────────────────────────────────────
    {
        bool is_hot = (hot == AXIS_VIEW);
        float alpha = is_hot ? 1.0f : 0.70f;
        float r = is_hot ? 1.00f : 0.90f;
        float g = is_hot ? 0.88f : 0.90f;
        float b = is_hot ? 0.20f : 0.90f;
        float to_cam[3] = { m_eye[0]-pos[0], m_eye[1]-pos[1], m_eye[2]-pos[2] };
        normalize3(to_cam);
        float ta[3], tb[3];
        float tmp[3] = { 0,1,0 };
        if (std::abs(dot3(to_cam, tmp)) > 0.9f) tmp[0]=1, tmp[1]=0, tmp[2]=0;
        cross3(to_cam, tmp, ta); normalize3(ta);
        cross3(to_cam, ta, tb);
        push_circle_arc(pos, ta, tb, center, 0.0f, 2.0f*kPi, r, g, b, alpha, 24);
    }

    // ── 2. Plane handles (XY/XZ/YZ) — camera-facing quadrant ──────────────────
    int plane_ids[3] = { AXIS_YZ, AXIS_XZ, AXIS_XY };
    int p_ax0[3] = { 1, 0, 0 };
    int p_ax1[3] = { 2, 2, 1 };
    const float* plane_cols[3] = { kColorX, kColorY, kColorZ };

    float to_cam[3] = { m_eye[0]-pos[0], m_eye[1]-pos[1], m_eye[2]-pos[2] };
    normalize3(to_cam);

    for (int i = 0; i < 3; ++i) {
        float* a0 = axes[p_ax0[i]];
        float* a1 = axes[p_ax1[i]];
        float n[3];
        cross3(a0, a1, n);
        normalize3(n);
        if (std::abs(dot3(to_cam, n)) < 0.08f) continue; // edge-on: suppress

        float cam_diff[3];
        sub3(pos, m_eye, cam_diff);
        float s0 = (dot3(cam_diff, a0) < 0.0f) ? 1.0f : -1.0f;
        float s1 = (dot3(cam_diff, a1) < 0.0f) ? 1.0f : -1.0f;

        bool is_hot = (hot == plane_ids[i]);
        float alpha = is_hot ? 0.70f : 0.25f;
        float r = is_hot ? 1.00f : plane_cols[i][0];
        float g = is_hot ? 0.88f : plane_cols[i][1];
        float b_ = is_hot ? 0.20f : plane_cols[i][2];

        float off = plane;
        float p0[3] = { pos[0] + a0[0]*s0*off, pos[1] + a0[1]*s0*off, pos[2] + a0[2]*s0*off };
        float p1[3] = { pos[0] + (a0[0]*s0 + a1[0]*s1)*off,
                        pos[1] + (a0[1]*s0 + a1[1]*s1)*off,
                        pos[2] + (a0[2]*s0 + a1[2]*s1)*off };
        float p2[3] = { pos[0] + a1[0]*s1*off, pos[1] + a1[1]*s1*off, pos[2] + a1[2]*s1*off };

        push_quad(pos, p0, p1, p2, r, g, b_, alpha);
        float line_a = is_hot ? 1.0f : 0.85f;
        push_line(p0, p1, r, g, b_, line_a);
        push_line(p1, p2, r, g, b_, line_a);
    }

    // ── 3. Negative ghost tails (-X, -Y, -Z) ──────────────────────────────────
    for (int i = 0; i < 3; ++i) {
        float cos_v = std::abs(dot3(to_cam, axes[i]));
        if (cos_v > 0.965f) continue;
        bool is_hot_neg = (hot == neg_ids[i]);
        float alpha = is_hot_neg ? 0.90f : 0.25f;
        float r = is_hot_neg ? 1.00f : cols[i][0];
        float g = is_hot_neg ? 0.88f : cols[i][1];
        float b_ = is_hot_neg ? 0.20f : cols[i][2];
        float neg_end[3] = { pos[0] - axes[i][0]*len*0.65f,
                             pos[1] - axes[i][1]*len*0.65f,
                             pos[2] - axes[i][2]*len*0.65f };
        push_dashed_line(pos, neg_end, 0.08f * scale_f * m_dpr, r, g, b_, alpha);
    }

    // ── 4. Ruler tick marks along axes (adaptive snap indicator) ─────────────
    for (int i = 0; i < 3; ++i) {
        bool is_axis_hot = (hot == axis_ids[i] || hot == neg_ids[i]);
        if (is_axis_hot || m_snap_t > 0.0f) {
            float tick_step = (m_snap_t > 0.0f) ? (m_snap_t * scale_f) : (len * 0.20f);
            if (tick_step > 0.02f * scale_f) {
                float tick_sz = 0.06f * scale_f * m_dpr;
                float tick_a = is_axis_hot ? 0.80f : 0.35f;
                push_ruler_ticks(pos, axes[i], len * 0.90f, tick_step, tick_sz,
                                 cols[i][0], cols[i][1], cols[i][2], tick_a);
            }
        }
    }

    // ── 5. Translate positive axis lines + cone tips ──────────────────────────
    for (int i = 0; i < 3; ++i) {
        float cos_v = std::abs(dot3(to_cam, axes[i]));
        float fade = 1.0f;
        if (cos_v > 0.85f) {
            fade = std::max(0.0f, (0.965f - cos_v) / (0.965f - 0.85f));
        }
        if (fade <= 0.01f) continue;

        float tip[3] = { pos[0]+axes[i][0]*len, pos[1]+axes[i][1]*len, pos[2]+axes[i][2]*len };
        float sx0, sy0, sx1, sy1;
        if (world_to_screen(pos, sx0, sy0) && world_to_screen(tip, sx1, sy1)) {
            float slen = std::hypot(sx1 - sx0, sy1 - sy0);
            if (slen < 12.0f * m_dpr) continue; // pointing into camera: suppress
        }

        bool is_hot = (hot == axis_ids[i]);
        float alpha = (is_hot ? 1.0f : kAlphaNorm) * fade;
        float r = is_hot ? 1.00f : cols[i][0];
        float g = is_hot ? 0.88f : cols[i][1];
        float b = is_hot ? 0.20f : cols[i][2];
        float cur_cone_r = is_hot ? (cone_r * 1.30f) : cone_r;

        float shaft_end[3] = { pos[0]+axes[i][0]*(len-cone_l),
                               pos[1]+axes[i][1]*(len-cone_l),
                               pos[2]+axes[i][2]*(len-cone_l) };

        push_line(pos, shaft_end, r, g, b, alpha);
        push_cone(tip, shaft_end, cur_cone_r, r, g, b, alpha);
    }
}

void RubyGizmo::draw_rotate(const float pos[3], const float rot_deg[3],
                              float scale_f, int hot) {
    float ring_r = kRingRadius * scale_f * m_dpr;

    int ring_axes[3]      = { AXIS_X, AXIS_Y, AXIS_Z };
    const float* cols[3]  = { kColorX, kColorY, kColorZ };

    // Draw protractor wedge when actively dragging rotation
    if (m_dragging && std::abs(m_rot_current_sweep) > 1e-4f) {
        float wedge_col[3] = { 1.0f, 0.85f, 0.2f };
        for (int i = 0; i < 3; ++i) {
            if (hot == ring_axes[i]) {
                wedge_col[0] = cols[i][0];
                wedge_col[1] = cols[i][1];
                wedge_col[2] = cols[i][2];
                break;
            }
        }
        push_protractor_wedge(pos, m_drag_plane_n, m_drag_ref, m_rot_current_sweep, ring_r,
                              wedge_col[0], wedge_col[1], wedge_col[2], 0.35f, 36);
    }

    for (int i = 0; i < 3; ++i) {
        bool is_hot = (hot == ring_axes[i]);
        float alpha = is_hot ? 1.0f : kAlphaNorm;
        float r = is_hot ? 1.00f : cols[i][0];
        float g = is_hot ? 0.88f : cols[i][1];
        float b_ = is_hot ? 0.15f : cols[i][2];

        float na[3];
        axis_vec(ring_axes[i], rot_deg, na);
        float ta[3], tb[3];
        float tmp[3] = { 0,1,0 };
        if (std::abs(dot3(na, tmp)) > 0.9f) { tmp[0]=1; tmp[1]=0; tmp[2]=0; }
        cross3(na, tmp, ta); normalize3(ta);
        cross3(na, ta, tb); normalize3(tb);

        push_circle_arc(pos, ta, tb, ring_r, 0.0f, 2.0f*kPi, r, g, b_, alpha, 64);
    }

    // View-plane ring (white, slightly bigger)
    {
        bool is_hot = (hot == AXIS_VIEW);
        float alpha = is_hot ? 1.0f : 0.6f;
        float r = is_hot ? 1.00f : kColorWhite[0];
        float g = is_hot ? 0.88f : kColorWhite[1];
        float b_ = is_hot ? 0.15f : kColorWhite[2];
        float view_n[3] = { -m_view[2], -m_view[6], -m_view[10] };
        float ta[3], tb[3];
        float tmp[3] = {0,1,0};
        if (std::abs(dot3(view_n, tmp)) > 0.9f) { tmp[0]=1; tmp[1]=0; tmp[2]=0; }
        cross3(view_n, tmp, ta); normalize3(ta);
        cross3(view_n, ta, tb); normalize3(tb);
        push_circle_arc(pos, ta, tb, ring_r*1.25f, 0.0f, 2.0f*kPi,
                        r, g, b_, alpha, 64);
    }
}

void RubyGizmo::draw_scale(const float pos[3], const float rot_deg[3], float scale_f, int hot) {
    float len    = kAxisLen * scale_f * m_dpr;
    float cube_h = kCubeSide * 0.5f * scale_f * m_dpr;

    float axes[3][3];
    get_axes(rot_deg, axes);
    const float* cols[3] = { kColorX, kColorY, kColorZ };
    int axis_ids[3] = { AXIS_X, AXIS_Y, AXIS_Z };

    for (int i = 0; i < 3; ++i) {
        bool is_hot = (hot == axis_ids[i]);
        float alpha = is_hot ? 1.0f : kAlphaNorm;
        float r = is_hot ? 1.00f : cols[i][0];
        float g = is_hot ? 0.88f : cols[i][1];
        float b_ = is_hot ? 0.15f : cols[i][2];
        float cur_cube_h = is_hot ? (cube_h * 1.35f) : cube_h;

        float tip[3] = { pos[0]+axes[i][0]*len, pos[1]+axes[i][1]*len, pos[2]+axes[i][2]*len };
        push_line(pos, tip, r, g, b_, alpha);
        push_cube(tip, cur_cube_h, r, g, b_, alpha);
    }

    // Plane quads (XY / XZ / YZ) — tinted with the axis NOT in the plane
    {
        const int plane_axes[3][2] = { {0,1}, {0,2}, {1,2} };
        const int plane_ids[3]     = { AXIS_XY, AXIS_XZ, AXIS_YZ };
        const int plane_col[3]     = { 2, 1, 0 };   // XY→Z, XZ→Y, YZ→X
        const float q = len * 0.5f, hs = len * 0.16f;
        for (int i = 0; i < 3; ++i) {
            const int a = plane_axes[i][0], b = plane_axes[i][1], col = plane_col[i];
            const bool is_hot = (hot == plane_ids[i]);
            const float alpha = is_hot ? 1.0f : kAlphaNorm;
            const float r = is_hot ? 1.00f : cols[col][0];
            const float g = is_hot ? 0.88f : cols[col][1];
            const float b_ = is_hot ? 0.15f : cols[col][2];
            const float c[3] = { pos[0] + (axes[a][0] + axes[b][0]) * q,
                                 pos[1] + (axes[a][1] + axes[b][1]) * q,
                                 pos[2] + (axes[a][2] + axes[b][2]) * q };
            const float p0[3] = { c[0] + (axes[a][0] + axes[b][0]) * hs,
                                  c[1] + (axes[a][1] + axes[b][1]) * hs,
                                  c[2] + (axes[a][2] + axes[b][2]) * hs };
            const float p1[3] = { c[0] + (axes[a][0] - axes[b][0]) * hs,
                                  c[1] + (axes[a][1] - axes[b][1]) * hs,
                                  c[2] + (axes[a][2] - axes[b][2]) * hs };
            const float p2[3] = { c[0] - (axes[a][0] + axes[b][0]) * hs,
                                  c[1] - (axes[a][1] + axes[b][1]) * hs,
                                  c[2] - (axes[a][2] + axes[b][2]) * hs };
            const float p3[3] = { c[0] + (axes[b][0] - axes[a][0]) * hs,
                                  c[1] + (axes[b][1] - axes[a][1]) * hs,
                                  c[2] + (axes[b][2] - axes[a][2]) * hs };
            push_quad(p0, p1, p2, p3, r, g, b_, alpha);
        }
    }

    // Centre uniform-scale ball (white, lights up gold on hover)
    push_cube(pos, cube_h*1.2f, kColorWhite[0], kColorWhite[1], kColorWhite[2], kAlphaNorm);
}

// ─────────────────────────────────────────────────────────────────────────────
// Flush geometry to VBO + draw
// ─────────────────────────────────────────────────────────────────────────────

void RubyGizmo::flush_geometry(float alpha_mul, float grey_mul) {
    if (!m_gl_ok || m_n_verts == 0 || m_n_cmds == 0) return;

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 m_n_verts * (GLsizeiptr)sizeof(Vert),
                 m_verts, GL_STREAM_DRAW);

    // Build VP matrix for uniform (col-major already)
    QMatrix4x4 qvp;
    static_assert(sizeof(m_vp) == 16*4, "");
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            qvp(row, col) = m_vp[col*4 + row];

    constexpr GLsizei stride = sizeof(Vert);

    // We draw lines with m_sh_line, triangles with m_sh_fill (both identical
    // shaders — they're split so we can set different GL state between them)
    QOpenGLShaderProgram* sh = m_sh_fill;
    sh->bind();
    sh->setUniformValue("uVP", qvp);
    sh->setUniformValue("uAlphaMul", alpha_mul);
    sh->setUniformValue("uGreyMul", grey_mul);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          (const void*)offsetof(Vert, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride,
                          (const void*)offsetof(Vert, r));

    bool prev_lines = false;
    for (int i = 0; i < m_n_cmds; ++i) {
        const DrawCmd& cmd = m_cmds[i];
        if (cmd.lines != prev_lines) {
            if (cmd.lines) {
                // Switch to line shader + state
                sh->release();
                sh = m_sh_line;
                sh->bind();
                sh->setUniformValue("uVP", qvp);
                sh->setUniformValue("uAlphaMul", alpha_mul);
                sh->setUniformValue("uGreyMul", grey_mul);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                                      (const void*)offsetof(Vert, x));
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride,
                                      (const void*)offsetof(Vert, r));
                // Use physical pixel line width for crisp lines
                glLineWidth(std::max(1.0f, kLineW * m_dpr));
            } else {
                sh->release();
                sh = m_sh_fill;
                sh->bind();
                sh->setUniformValue("uVP", qvp);
                sh->setUniformValue("uAlphaMul", alpha_mul);
                sh->setUniformValue("uGreyMul", grey_mul);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                                      (const void*)offsetof(Vert, x));
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride,
                                      (const void*)offsetof(Vert, r));
            }
            prev_lines = cmd.lines;
        }
        glDrawArrays(cmd.lines ? GL_LINES : GL_TRIANGLES, cmd.start, cmd.count);
    }

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    sh->release();
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glLineWidth(1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Drag logic helpers
// ─────────────────────────────────────────────────────────────────────────────

static float project_ray_onto_axis(const float ro[3], const float rd[3],
                                    const float origin[3], const float axis[3]) {
    // Closest point on ray to the axis line, projected onto the axis.
    // Method: solve least-squares for t such that |ro+rd*t - origin - axis*s|^2 is minimised.
    float w[3] = { ro[0]-origin[0], ro[1]-origin[1], ro[2]-origin[2] };
    float b = RubyGizmo::dot3(rd, axis);
    float c = RubyGizmo::dot3(rd, rd);
    float d = RubyGizmo::dot3(w, axis);
    float e = RubyGizmo::dot3(w, rd);
    float denom = c - b*b;
    if (std::abs(denom) < 1e-8f) return 0.0f;
    float t = (b*d - e) / denom;      // t along rd
    float s = (d + b*t) / 1.0f;       // s along axis (axis is unit)
    (void)t;
    return s;
}

static float project_ray_onto_plane(const float ro[3], const float rd[3],
                                     const float plane_origin[3], const float plane_n[3],
                                     float hit[3]) {
    float denom = RubyGizmo::dot3(rd, plane_n);
    if (std::abs(denom) < 1e-8f) return -1.0f;
    float d = RubyGizmo::dot3(plane_n, plane_origin);
    float t = (d - RubyGizmo::dot3(ro, plane_n)) / denom;
    if (t < 0.0f) return -1.0f;
    hit[0] = ro[0] + rd[0]*t;
    hit[1] = ro[1] + rd[1]*t;
    hit[2] = ro[2] + rd[2]*t;
    return t;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main draw + interact entry point
// ─────────────────────────────────────────────────────────────────────────────

GizmoResult RubyGizmo::draw(Mode mode,
                              float pos[3],
                              float rot_deg[3],
                              float scale[3]) {
    GizmoResult result;
    if (mode == Mode::None || !m_gl_ok) return result;

    float scale_f = gizmo_scale(pos);

    // ── Hit-test (cursor ray vs gizmo geometry) ───────────────────────────────
    int hover = AXIS_NONE;
    if (!m_dragging) {
        switch (mode) {
            case Mode::Translate: hover = hit_test_translate(pos, rot_deg, scale_f); break;
            case Mode::Rotate:    hover = hit_test_rotate(pos, rot_deg, scale_f); break;
            case Mode::Scale:     hover = hit_test_scale(pos, rot_deg, scale_f); break;
            default: break;
        }
        m_hover_axis = hover;
    }
    result.hovered = (m_hover_axis != AXIS_NONE);

    // ── Drag begin ────────────────────────────────────────────────────────────
    bool lmb_pressed  = m_lmb && !m_lmb_prev;
    bool lmb_released = !m_lmb && m_lmb_prev;

    if (lmb_pressed && m_hover_axis != AXIS_NONE) {
        m_dragging  = true;
        m_drag_axis = m_hover_axis;
        std::memcpy(m_drag_origin,       pos,     12);
        std::memcpy(m_drag_rot_origin,   rot_deg, 12);
        std::memcpy(m_drag_scale_origin, scale,   12);
        m_drag_accum = 0.0f;
        m_rot_current_sweep = 0.0f;

        // ImGuizmo frozen tripod: capture the camera-facing plane-quadrant
        // signs at grab time so the handles never flip while dragging.
        m_frozen_signs_valid = true;
        {
            float axes[3][3];
            get_axes(rot_deg, axes);
            float to_cam[3] = { m_eye[0]-pos[0], m_eye[1]-pos[1], m_eye[2]-pos[2] };
            normalize3(to_cam);
            float cam_diff[3];
            sub3(pos, m_eye, cam_diff);
            int p_ax0[3] = { 1, 0, 0 };
            int p_ax1[3] = { 2, 2, 1 };
            for (int i = 0; i < 3; ++i) {
                float* a0 = axes[p_ax0[i]];
                float* a1 = axes[p_ax1[i]];
                m_frozen_s0[i] = (dot3(cam_diff, a0) < 0.0f) ? 1.0f : -1.0f;
                m_frozen_s1[i] = (dot3(cam_diff, a1) < 0.0f) ? 1.0f : -1.0f;
            }
        }

        float ro[3], rd[3];
        cursor_ray(ro, rd);

        float to_cam[3] = { m_eye[0]-pos[0], m_eye[1]-pos[1], m_eye[2]-pos[2] };
        normalize3(to_cam);

        if (mode == Mode::Translate) {
            float axes[3][3];
            get_axes(rot_deg, axes);
            if (m_drag_axis == AXIS_VIEW) {
                // View-plane move: normal faces camera
                m_drag_plane_n[0] = to_cam[0];
                m_drag_plane_n[1] = to_cam[1];
                m_drag_plane_n[2] = to_cam[2];
            } else if (m_drag_axis == AXIS_XY) {
                cross3(axes[0], axes[1], m_drag_plane_n);
                normalize3(m_drag_plane_n);
            } else if (m_drag_axis == AXIS_XZ) {
                cross3(axes[0], axes[2], m_drag_plane_n);
                normalize3(m_drag_plane_n);
            } else if (m_drag_axis == AXIS_YZ) {
                cross3(axes[1], axes[2], m_drag_plane_n);
                normalize3(m_drag_plane_n);
            } else {
                // Single axis or negative axis: pick plane containing axis that faces camera directly
                float axis[3] = { 0, 0, 0 };
                if (m_drag_axis == AXIS_X) { std::memcpy(axis, axes[0], 12); }
                else if (m_drag_axis == AXIS_Y) { std::memcpy(axis, axes[1], 12); }
                else if (m_drag_axis == AXIS_Z) { std::memcpy(axis, axes[2], 12); }
                else if (m_drag_axis == AXIS_NEG_X) { axis[0] = -axes[0][0]; axis[1] = -axes[0][1]; axis[2] = -axes[0][2]; }
                else if (m_drag_axis == AXIS_NEG_Y) { axis[0] = -axes[1][0]; axis[1] = -axes[1][1]; axis[2] = -axes[1][2]; }
                else if (m_drag_axis == AXIS_NEG_Z) { axis[0] = -axes[2][0]; axis[1] = -axes[2][1]; axis[2] = -axes[2][2]; }

                float ortho[3];
                cross3(axis, to_cam, ortho);
                if (len3(ortho) < 1e-4f) {
                    float tmp[3] = { 0,1,0 };
                    if (std::abs(dot3(axis, tmp)) > 0.9f) tmp[0]=1, tmp[1]=0, tmp[2]=0;
                    cross3(axis, tmp, ortho);
                }
                normalize3(ortho);
                cross3(ortho, axis, m_drag_plane_n);
                normalize3(m_drag_plane_n);

                project_ray_plane(ro, rd, pos, m_drag_plane_n, m_drag_start_hit);
                float hit_diff[3];
                sub3(m_drag_start_hit, pos, hit_diff);
                m_drag_start_s = dot3(hit_diff, axis);
            }
            if (m_drag_axis == AXIS_VIEW || m_drag_axis == AXIS_XY || m_drag_axis == AXIS_XZ || m_drag_axis == AXIS_YZ) {
                project_ray_plane(ro, rd, pos, m_drag_plane_n, m_drag_start_hit);
            }
        } else if (mode == Mode::Scale) {
            float cx = 0.0f, cy = 0.0f;
            world_to_screen(pos, cx, cy);
            m_scale_screen_center[0] = cx;
            m_scale_screen_center[1] = cy;
            const float cdx = m_cx - cx, cdy = m_cy - cy;
            m_scale_start_dist = std::max(12.0f, std::sqrt(cdx*cdx + cdy*cdy));

            const float slen = kAxisLen * scale_f * m_dpr;
            float axes[3][3];
            get_axes(rot_deg, axes);
            for (int i = 0; i < 3; ++i) {
                float tip[3] = { pos[0] + axes[i][0]*slen,
                                 pos[1] + axes[i][1]*slen,
                                 pos[2] + axes[i][2]*slen };
                float tx = 0.0f, ty = 0.0f;
                if (world_to_screen(tip, tx, ty)) {
                    const float dx = tx - cx, dy = ty - cy;
                    const float sl = std::sqrt(dx*dx + dy*dy);
                    if (sl > 1e-3f) {
                        m_scale_screen_axis[i][0] = dx / sl;
                        m_scale_screen_axis[i][1] = dy / sl;
                        m_scale_screen_len[i] = sl;
                    } else {
                        m_scale_screen_axis[i][0] = 1.0f;
                        m_scale_screen_axis[i][1] = 0.0f;
                        m_scale_screen_len[i] = 1.0f;
                    }
                } else {
                    m_scale_screen_axis[i][0] = 1.0f;
                    m_scale_screen_axis[i][1] = 0.0f;
                    m_scale_screen_len[i] = 1.0f;
                }
            }

            // Grabbed handle: single axis, or the plane diagonal (two axes).
            if (m_drag_axis == AXIS_XY || m_drag_axis == AXIS_XZ || m_drag_axis == AXIS_YZ) {
                const int a = (m_drag_axis == AXIS_YZ) ? 1 : 0;
                const int b = (m_drag_axis == AXIS_XY) ? 1 : 2;
                float dx = m_scale_screen_axis[a][0] + m_scale_screen_axis[b][0];
                float dy = m_scale_screen_axis[a][1] + m_scale_screen_axis[b][1];
                const float dl = std::sqrt(dx*dx + dy*dy);
                if (dl > 1e-3f) {
                    m_scale_drag_dir[0] = dx / dl;
                    m_scale_drag_dir[1] = dy / dl;
                    m_scale_drag_len = dl;
                } else {
                    m_scale_drag_dir[0] = 1.0f; m_scale_drag_dir[1] = 0.0f;
                    m_scale_drag_len = 1.0f;
                }
            } else if (m_drag_axis == AXIS_X || m_drag_axis == AXIS_Y || m_drag_axis == AXIS_Z) {
                const int i = (m_drag_axis == AXIS_X) ? 0 : (m_drag_axis == AXIS_Y) ? 1 : 2;
                m_scale_drag_dir[0] = m_scale_screen_axis[i][0];
                m_scale_drag_dir[1] = m_scale_screen_axis[i][1];
                m_scale_drag_len = m_scale_screen_len[i];
            } else if (m_drag_axis == AXIS_XYZ) {
                m_scale_drag_dir[0] = 1.0f; m_scale_drag_dir[1] = 0.0f;
                m_scale_drag_len = 1.0f;
            }
            m_scale_start_s = (m_cx - cx) * m_scale_drag_dir[0] +
                              (m_cy - cy) * m_scale_drag_dir[1];
        } else if (mode == Mode::Rotate) {
            if (m_drag_axis == AXIS_VIEW) {
                m_drag_plane_n[0] = to_cam[0];
                m_drag_plane_n[1] = to_cam[1];
                m_drag_plane_n[2] = to_cam[2];
            } else {
                axis_vec(m_drag_axis, rot_deg, m_drag_plane_n);
            }
            project_ray_plane(ro, rd, pos, m_drag_plane_n, m_drag_start_hit);
            m_drag_ref[0] = m_drag_start_hit[0] - pos[0];
            m_drag_ref[1] = m_drag_start_hit[1] - pos[1];
            m_drag_ref[2] = m_drag_start_hit[2] - pos[2];
            normalize3(m_drag_ref);
        }
    }

    // ── Drag update ───────────────────────────────────────────────────────────
    if (m_dragging && m_lmb) {
        result.active = true;
        float ro[3], rd[3];
        cursor_ray(ro, rd);

        switch (mode) {
            case Mode::Translate: {
                float cur_hit[3];
                if (project_ray_plane(ro, rd, m_drag_origin, m_drag_plane_n, cur_hit) > 0.0f) {
                    if (m_drag_axis == AXIS_X || m_drag_axis == AXIS_Y || m_drag_axis == AXIS_Z ||
                        m_drag_axis == AXIS_NEG_X || m_drag_axis == AXIS_NEG_Y || m_drag_axis == AXIS_NEG_Z) {
                        float axes[3][3];
                        get_axes(m_drag_rot_origin, axes);
                        float axis[3] = { 0, 0, 0 };
                        if (m_drag_axis == AXIS_X) { std::memcpy(axis, axes[0], 12); }
                        else if (m_drag_axis == AXIS_Y) { std::memcpy(axis, axes[1], 12); }
                        else if (m_drag_axis == AXIS_Z) { std::memcpy(axis, axes[2], 12); }
                        else if (m_drag_axis == AXIS_NEG_X) { axis[0] = -axes[0][0]; axis[1] = -axes[0][1]; axis[2] = -axes[0][2]; }
                        else if (m_drag_axis == AXIS_NEG_Y) { axis[0] = -axes[1][0]; axis[1] = -axes[1][1]; axis[2] = -axes[1][2]; }
                        else if (m_drag_axis == AXIS_NEG_Z) { axis[0] = -axes[2][0]; axis[1] = -axes[2][1]; axis[2] = -axes[2][2]; }

                        float hit_diff[3];
                        sub3(cur_hit, m_drag_origin, hit_diff);
                        float cur_s = dot3(hit_diff, axis);
                        float delta_s = cur_s - m_drag_start_s;
                        if (m_snap_t > 0.0f) delta_s = std::round(delta_s / m_snap_t) * m_snap_t;
                        pos[0] = m_drag_origin[0] + axis[0] * delta_s;
                        pos[1] = m_drag_origin[1] + axis[1] * delta_s;
                        pos[2] = m_drag_origin[2] + axis[2] * delta_s;
                        result.changed = true;
                    } else {
                        // Plane (XY, XZ, YZ) or Screen (AXIS_VIEW)
                        float delta[3] = { cur_hit[0] - m_drag_start_hit[0],
                                           cur_hit[1] - m_drag_start_hit[1],
                                           cur_hit[2] - m_drag_start_hit[2] };
                        if (m_snap_t > 0.0f) {
                            delta[0] = std::round(delta[0] / m_snap_t) * m_snap_t;
                            delta[1] = std::round(delta[1] / m_snap_t) * m_snap_t;
                            delta[2] = std::round(delta[2] / m_snap_t) * m_snap_t;
                        }
                        pos[0] = m_drag_origin[0] + delta[0];
                        pos[1] = m_drag_origin[1] + delta[1];
                        pos[2] = m_drag_origin[2] + delta[2];
                        result.changed = true;
                    }
                }
                break;
            }
            case Mode::Rotate: {
                float cur_hit[3];
                if (project_ray_plane(ro, rd, pos, m_drag_plane_n, cur_hit) > 0.0f) {
                    float to[3] = { cur_hit[0]-pos[0], cur_hit[1]-pos[1], cur_hit[2]-pos[2] };
                    normalize3(to);
                    float cross_v[3];
                    cross3(m_drag_ref, to, cross_v);
                    float sin_a = dot3(cross_v, m_drag_plane_n);
                    float cos_a = dot3(m_drag_ref, to);
                    float angle = std::atan2(sin_a, cos_a);
                    if (m_snap_r > 0.0f) {
                        float snap_rad = m_snap_r * kDeg;
                        angle = std::round(angle / snap_rad) * snap_rad;
                    }
                    m_rot_current_sweep = angle;

                    Vector3 axis_v = { m_drag_plane_n[0], m_drag_plane_n[1], m_drag_plane_n[2] };
                    Quaternion delta_q = QuaternionFromAxisAngle(axis_v, angle);

                    float orig_m[9];
                    rotation_matrix(m_drag_rot_origin, orig_m);
                    Matrix mat = {
                        orig_m[0], orig_m[3], orig_m[6], 0.0f,
                        orig_m[1], orig_m[4], orig_m[7], 0.0f,
                        orig_m[2], orig_m[5], orig_m[8], 0.0f,
                        0.0f,      0.0f,      0.0f,      1.0f
                    };
                    Quaternion orig_q = QuaternionFromMatrix(mat);
                    Quaternion new_q = QuaternionMultiply(delta_q, orig_q);
                    Matrix new_m = QuaternionToMatrix(new_q);

                    float comb[9] = {
                        new_m.m0, new_m.m1, new_m.m2,
                        new_m.m4, new_m.m5, new_m.m6,
                        new_m.m8, new_m.m9, new_m.m10
                    };
                    rotation_matrix_to_euler(comb, rot_deg);
                    result.changed = true;
                }
                break;
            }
            case Mode::Scale: {
                float ratio = 1.0f;
                if (m_drag_axis == AXIS_XYZ) {
                    const float dx = m_cx - m_scale_screen_center[0];
                    const float dy = m_cy - m_scale_screen_center[1];
                    ratio = std::sqrt(dx*dx + dy*dy) / std::max(1e-3f, m_scale_start_dist);
                } else {
                    const float cur_s = (m_cx - m_scale_screen_center[0]) * m_scale_drag_dir[0] +
                                        (m_cy - m_scale_screen_center[1]) * m_scale_drag_dir[1];
                    ratio = 1.0f + (cur_s - m_scale_start_s) / std::max(1e-3f, m_scale_drag_len);
                }
                if (m_snap_s > 0.0f)
                    ratio = 1.0f + std::round((ratio - 1.0f) / m_snap_s) * m_snap_s;
                ratio = std::clamp(ratio, 0.01f, 100.0f);

                if (m_drag_axis == AXIS_XYZ) {
                    scale[0] = m_drag_scale_origin[0] * ratio;
                    scale[1] = m_drag_scale_origin[1] * ratio;
                    scale[2] = m_drag_scale_origin[2] * ratio;
                } else {
                    const bool sx = (m_drag_axis == AXIS_X || m_drag_axis == AXIS_XY || m_drag_axis == AXIS_XZ);
                    const bool sy = (m_drag_axis == AXIS_Y || m_drag_axis == AXIS_XY || m_drag_axis == AXIS_YZ);
                    const bool sz = (m_drag_axis == AXIS_Z || m_drag_axis == AXIS_XZ || m_drag_axis == AXIS_YZ);
                    scale[0] = m_drag_scale_origin[0] * (sx ? ratio : 1.0f);
                    scale[1] = m_drag_scale_origin[1] * (sy ? ratio : 1.0f);
                    scale[2] = m_drag_scale_origin[2] * (sz ? ratio : 1.0f);
                }
                result.changed = true;
                break;
            }
            default: break;
        }
    }

    // ── Drag end ──────────────────────────────────────────────────────────────
    if (lmb_released && m_dragging) {
        m_dragging   = false;
        m_drag_axis  = AXIS_NONE;
        m_hover_axis = AXIS_NONE;
        m_frozen_signs_valid = false;
        m_rot_current_sweep = 0.0f;
    }

    if (!m_lmb && m_dragging) {
        m_dragging   = false;
        m_drag_axis  = AXIS_NONE;
        m_hover_axis = AXIS_NONE;
        m_frozen_signs_valid = false;
        m_rot_current_sweep = 0.0f;
    }

    result.active = m_dragging;

    // ── Draw geometry ─────────────────────────────────────────────────────────
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    if (m_gl_ok) glDisable(GL_LIGHTING);  // keep compat pipeline quiet

    reset_geometry();
    int hot = m_dragging ? m_drag_axis : m_hover_axis;

    switch (mode) {
        case Mode::Translate: draw_translate(pos, rot_deg, scale_f, hot); break;
        case Mode::Rotate:    draw_rotate(pos, rot_deg, scale_f, hot); break;
        case Mode::Scale:     draw_scale(pos, rot_deg, scale_f, hot); break;
        default: break;
    }

    // Gizmo is always on top so handles are clearly visible and easy to grab
    glDisable(GL_DEPTH_TEST);
    flush_geometry(1.0f, 0.0f);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_LINE_SMOOTH);
    glEnable(GL_LIGHTING);
    glColor3f(1.0f, 1.0f, 1.0f);

    return result;
}

} // namespace ruby::gizmo
