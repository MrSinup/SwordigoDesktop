#pragma once
// ============================================================================
// light_rig.h — Light set manager for the Ruby GG 3D viewport.
//
//   Decouples light DATA from shader upload so the lighting panel, future
//   scripting, and game preview can all drive the same rig independently.
//
//   Architecture: Raylib-style direct uniform arrays — ideal for OpenGL
//   Compatibility Profile (used by QOpenGLWidget on desktop Linux/Windows)
//   where UBO binding is possible but adds complexity for little gain at
//   <= 4 directional lights.
//
//   Usage:
//     m_light_rig.build_from_viewport_lighting(m_lighting);
//     m_viewport_shader.bind();
//     m_light_rig.upload_to_shader(m_viewport_shader, m_gizmo_view);
//     m_viewport_shader.release();
// ============================================================================

namespace ruby::viewport { struct ViewportLighting; }

namespace ruby::render {

class ViewportShader;

// Maximum directional lights supported — matches shader define MAX_DIR_LIGHTS.
static constexpr int k_max_dir_lights = 4;

// ── Per-light data ────────────────────────────────────────────────────────────
struct DirLightData {
    float dir[3]    = {0.f, 1.f, 0.f};  // world-space normalized direction TO light
    float color[3]  = {1.f, 1.f, 1.f};  // linear HDR color (can exceed 1)
    float intensity = 1.f;
    bool  enabled   = true;
};

// ── Sky / hemisphere ambient ──────────────────────────────────────────────────
struct SkyLight {
    float sky_color[3]    = {0.40f, 0.42f, 0.46f};  // zenith
    float ground_color[3] = {0.19f, 0.17f, 0.14f};  // nadir
    float intensity = 1.f;
};

// ── Fog ───────────────────────────────────────────────────────────────────────
struct FogParams {
    bool  enabled = false;
    float color[3] = {0.07f, 0.075f, 0.086f};
    float density  = 0.00035f;  // EXP2 density
};

// ── LightRig ──────────────────────────────────────────────────────────────────
class LightRig {
public:
    // Build the standard 3-light studio rig from ViewportLighting panel data.
    // Called each frame from apply_lighting().
    void build_from_viewport_lighting(const ruby::viewport::ViewportLighting& vl);

    // Upload all light uniforms to an already-BOUND ViewportShader.
    // view[16]: column-major view matrix — used to rotate world-space light
    //           directions into view space for the view-space lighting shader.
    void upload_to_shader(ViewportShader& shader, const float view[16]) const;

    // ── Public data — editable directly from the lighting panel ──────────────
    DirLightData  dir_lights[k_max_dir_lights];
    int           dir_light_count = 3;
    SkyLight      sky;
    FogParams     fog;
    float         exposure = 1.0f;   // scene-wide HDR exposure multiplier
};

} // namespace ruby::render
