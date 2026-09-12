#pragma once
// ============================================================================
// ruby_gizmo.h — RubyGizmo: Bespoke High-Performance Transform Gizmo
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <QOpenGLFunctions>

class QOpenGLShaderProgram;
typedef unsigned int GLuint;

namespace ruby::gizmo {

// ── Mode ─────────────────────────────────────────────────────────────────────
enum class Mode { None, Translate, Rotate, Scale };
enum class CoordinateSpace { World, Local };

// ── Per-frame result ─────────────────────────────────────────────────────────
struct GizmoResult {
    bool hovered = false;   // cursor over any handle (for cursor shape)
    bool active  = false;   // a drag is in progress this frame
    bool changed = false;   // transform was mutated this frame
};

// ── Internal axis flags ───────────────────────────────────────────────────────
enum HoverAxis {
    AXIS_NONE  = 0,
    AXIS_X     = 1,
    AXIS_Y     = 2,
    AXIS_Z     = 4,
    AXIS_XY    = 8,    // XY plane (translate / scale)
    AXIS_XZ    = 16,
    AXIS_YZ    = 32,
    AXIS_VIEW  = 64,   // screen-space rotate ring / center move
    AXIS_XYZ   = 128,  // uniform scale (center ball)
    AXIS_NEG_X = 256,  // negative X ghost tail
    AXIS_NEG_Y = 512,  // negative Y ghost tail
    AXIS_NEG_Z = 1024, // negative Z ghost tail
};

// ── Main gizmo class ─────────────────────────────────────────────────────────
class RubyGizmo : protected QOpenGLFunctions {
public:
    RubyGizmo() = default;
    ~RubyGizmo();

    // ── GL lifecycle ─────────────────────────────────────────────────────────
    void init_gl();
    void destroy_gl();

    // ── Per-frame camera feed ─────────────────────────────────────────────────
    void set_camera(const float view[16], const float proj[16],
                    const float eye[3],
                    float viewport_w, float viewport_h, float dpr);

    // ── Per-frame cursor feed ─────────────────────────────────────────────────
    void set_cursor(float x, float y, bool lmb_down);

    // ── Snap settings ────────────────────────────────────────────────────────
    void set_snap(float translate_units, float rotate_deg, float scale_step);

    // ── Draw + interact ───────────────────────────────────────────────────────
    GizmoResult draw(Mode mode, float pos[3], float rot_deg[3], float scale[3]);

    // Test if cursor is currently over any gizmo handle
    int hit_test(Mode mode, const float pos[3], const float rot_deg[3], const float scale[3]);

    // ── Coordinate space ─────────────────────────────────────────────────────
    void set_coord_space(CoordinateSpace space) { m_space = space; }
    CoordinateSpace coord_space() const { return m_space; }
    void toggle_coord_space() {
        m_space = (m_space == CoordinateSpace::World) ? CoordinateSpace::Local : CoordinateSpace::World;
    }

    // ── Accessors ────────────────────────────────────────────────────────────
    bool is_hovering() const { return m_hover_axis != AXIS_NONE; }
    bool is_active()   const { return m_dragging; }
    int  hover_axis()  const { return m_hover_axis; }
    int  active_axis() const { return m_drag_axis; }

    bool world_to_screen(const float world[3], float& sx, float& sy) const;

    // ── Public math helpers (used by free-function helpers in .cpp) ───────────
    static float dot3(const float a[3], const float b[3]);
    static void  cross3(const float a[3], const float b[3], float out[3]);
    static float len3(const float v[3]);
    static void  normalize3(float v[3]);
    static void  sub3(const float a[3], const float b[3], float out[3]) {
        out[0] = a[0] - b[0];
        out[1] = a[1] - b[1];
        out[2] = a[2] - b[2];
    }
    static float project_ray_plane(const float ro[3], const float rd[3],
                                   const float plane_pos[3], const float plane_n[3],
                                   float hit[3]) {
        float denom = dot3(rd, plane_n);
        if (std::abs(denom) < 1e-6f) return -1.0f;
        float diff[3];
        sub3(plane_pos, ro, diff);
        float t = dot3(diff, plane_n) / denom;
        if (t < 0.001f) return -1.0f;
        hit[0] = ro[0] + rd[0] * t;
        hit[1] = ro[1] + rd[1] * t;
        hit[2] = ro[2] + rd[2] * t;
        return t;
    }

    // World scale factor
    float gizmo_scale(const float world_pos[3]) const;

    // Engine rotation helpers (rot_x/rot_z/rot_y Euler angles)
    static void rotation_matrix(const float rot_deg[3], float out[9]);
    static void rotation_matrix_to_euler(const float m[9], float rot_deg[3]);

private:
    // ── GL resources ─────────────────────────────────────────────────────────
    GLuint m_vbo        = 0;
    GLuint m_vao        = 0; // for core-profile (compat path uses raw attribs)
    QOpenGLShaderProgram* m_sh_line = nullptr;
    QOpenGLShaderProgram* m_sh_fill = nullptr;
    bool m_gl_ok = false;

    // ── Camera state (set per-frame) ─────────────────────────────────────────
    float m_view[16]  = {};
    float m_proj[16]  = {};
    float m_vp[16]    = {};   // proj * view
    float m_eye[3]    = {};
    float m_vp_w      = 1.0f;
    float m_vp_h      = 1.0f;
    float m_dpr       = 1.0f;

    // ── Cursor state ─────────────────────────────────────────────────────────
    float m_cx       = 0.0f;  // logical pixels
    float m_cy       = 0.0f;
    bool  m_lmb      = false;
    bool  m_lmb_prev = false;

    // ── Snap ────────────────────────────────────────────────────────────────
    float m_snap_t  = 0.0f;
    float m_snap_r  = 0.0f;
    float m_snap_s  = 0.0f;

    // ── Drag state ───────────────────────────────────────────────────────────
    bool        m_dragging   = false;
    int         m_hover_axis = AXIS_NONE;
    int         m_drag_axis  = AXIS_NONE;
    // ImGuizmo-style frozen tripod: plane-quadrant signs captured at grab time
    // so the drawn handles never flip or jump while the object moves mid-drag.
    bool        m_frozen_signs_valid = false;
    float       m_frozen_s0[3] = {1, 1, 1};
    float       m_frozen_s1[3] = {1, 1, 1};
    float       m_drag_origin[3] = {};  // world pos at drag start
    float       m_drag_rot_origin[3] = {};
    float       m_drag_scale_origin[3] = {};
    float       m_drag_accum = 0.0f;   // accumulated angle (rotate) or delta
    float       m_drag_ref[3] = {};    // reference world point on drag plane
    float       m_drag_plane_n[3] = {};// camera-facing plane normal for relative drag
    float       m_drag_start_hit[3] = {}; // initial hit on drag plane
    float       m_drag_start_s = 0.0f; // initial scalar projection along axis

    // ── Screen-space scale drag basis (ImGuizmo-style) ───────────────────────
    // Scale ratios derive from PIXEL deltas instead of ray/plane hits: robust
    // at the object origin and when the drag crosses it (the old world-plane
    // ratio inverted the moment the cursor passed the centre, collapsing the
    // object to 0.01x). Captured at grab time.
    float m_scale_screen_center[2] = {0.0f, 0.0f};  // gizmo centre in px
    float m_scale_screen_axis[3][2] = {{1.0f,0.0f},{1.0f,0.0f},{1.0f,0.0f}}; // axis screen dirs
    float m_scale_screen_len[3] = {1.0f, 1.0f, 1.0f}; // axis handle screen lengths (px)
    float m_scale_drag_dir[2] = {1.0f, 0.0f};  // grabbed handle's screen dir (axis or plane diag)
    float m_scale_drag_len  = 1.0f;            // grabbed handle's screen length (px)
    float m_scale_start_s   = 0.0f;            // initial cursor projection along drag dir
    float m_scale_start_dist = 1.0f;           // initial |cursor − centre| (uniform)

    // ── Internal geometry builder ─────────────────────────────────────────────
    struct Vert {
        float x, y, z;   // world pos
        float r, g, b, a;
    };
    static constexpr int kMaxVerts = 8192;
    Vert m_verts[kMaxVerts];
    int  m_n_verts = 0;

    // GL draw commands (start index + count + is_lines)
    struct DrawCmd { int start; int count; bool lines; };
    static constexpr int kMaxCmds = 64;
    DrawCmd m_cmds[kMaxCmds];
    int     m_n_cmds = 0;

    // ── Geometry helpers ─────────────────────────────────────────────────────
    void reset_geometry();
    void push_line(const float a[3], const float b[3], float r, float g, float b_, float a_);
    void push_dashed_line(const float a[3], const float b[3], float dash_len,
                          float r, float g, float b_, float a_);
    void push_cone(const float tip[3], const float base[3], float radius,
                   float r, float g, float b_, float a_, int segments=16);
    void push_circle_arc(const float centre[3],
                         const float axis_a[3], const float axis_b[3],
                         float radius, float angle_start, float angle_end,
                         float r, float g, float b_, float a_, int segments=64,
                         bool closed=false);
    void push_protractor_wedge(const float centre[3], const float normal[3],
                               const float ref_dir[3], float sweep_rad, float radius,
                               float r, float g, float b_, float a_, int segments=32);
    void push_ruler_ticks(const float start[3], const float dir[3], float length,
                          float step, float tick_sz,
                          float r, float g, float b_, float a_);
    void push_quad(const float p0[3], const float p1[3], const float p2[3], const float p3[3],
                   float r, float g, float b_, float a_);
    void push_cube(const float centre[3], float half_size,
                   float r, float g, float b_, float a_);
    void flush_geometry(float alpha_mul = 1.0f, float grey_mul = 0.0f); // upload to VBO + draw

    // ── Coordinate space & smart tracking ─────────────────────────────────────
    CoordinateSpace m_space = CoordinateSpace::World;
    float m_rot_current_sweep = 0.0f; // sweep angle in radians during drag
    float m_rot_reference_vec[3] = {1.0f, 0.0f, 0.0f};
    float m_rot_active_normal[3] = {0.0f, 1.0f, 0.0f};

    // ── Interaction helpers ───────────────────────────────────────────────────
    void  cursor_ray(float origin[3], float dir[3]) const;

    // Axis world vectors (taking into account coordinate space / object rotation)
    void get_axes(const float rot_deg[3], float out_axes[3][3]) const;
    static void axis_vec(int axis, const float rot_deg[3], float out[3]);

    // Ray-cylinder intersection: returns t or -1.
    static float ray_cylinder(const float ro[3], const float rd[3],
                               const float ca[3], const float cb[3], float radius);
    // Ray-disc intersection (plane with disc cull).
    static float ray_disc(const float ro[3], const float rd[3],
                           const float centre[3], const float normal[3], float radius);
    // Ray-sphere intersection (for center handle).
    static float ray_sphere(const float ro[3], const float rd[3],
                            const float centre[3], float radius);
    // Ray-plane (infinite).
    static float ray_plane(const float ro[3], const float rd[3],
                            const float plane_n[3], float plane_d);

    // ── Hit testing (returns the nearest AXIS_* hit) ─────────────────────────
    int hit_test_translate(const float pos[3], const float rot_deg[3], float scale_f) const;
    int hit_test_rotate(const float pos[3], const float rot_deg[3], float scale_f) const;
    int hit_test_scale(const float pos[3], const float rot_deg[3], float scale_f) const;

    // ── Per-mode draw helpers ─────────────────────────────────────────────────
    void draw_translate(const float pos[3], const float rot_deg[3], float scale_f, int hot);
    void draw_rotate(const float pos[3], const float rot_deg[3], float scale_f, int hot);
    void draw_scale(const float pos[3], const float rot_deg[3], float scale_f, int hot);

    // Matrix helpers
    static void mat4_mul(float out[16], const float a[16], const float b[16]);
    static bool mat4_project(const float vp[16], const float world[4],
                              float vp_w, float vp_h, float& sx, float& sy);
};

} // namespace ruby::gizmo
