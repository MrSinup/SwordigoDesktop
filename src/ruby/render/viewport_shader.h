#pragma once
// ============================================================================
// viewport_shader.h — Modern GLSL 330 shader for the Ruby GG 3D viewport.
//
//   Matrix pipeline (view-space — matches OpenGL fixed-function exactly):
//     uProj      = GL_PROJECTION_MATRIX  (set once per frame)
//     uModelView = GL_MODELVIEW_MATRIX   (set per-draw = view * model)
//   gl_Position = uProj * (uModelView * aPos)  — no double-transform.
//
//   Lighting: Cook-Torrance GGX specular + Burley diffuse in VIEW space.
//     - Light directions pre-transformed to view space by LightRig.
//     - Camera at vec3(0,0,0) in view space — no uCamPos needed.
//   Tone mapping: Godot ACES fitted matrices + linear_to_srgb().
//
//   Attribute layout:
//     location 0 = vec3 aPos
//     location 1 = vec3 aNorm
//     location 2 = vec2 aUV
// ============================================================================
#include <cstring>

namespace ruby::render {
class  LightRig;
struct MaterialParams;
}
class QOpenGLShaderProgram;

namespace ruby::render {

class ViewportShader {
public:
    ViewportShader() = default;
    ~ViewportShader();

    // Non-copyable (owns GL resources)
    ViewportShader(const ViewportShader&) = delete;
    ViewportShader& operator=(const ViewportShader&) = delete;

    /// Compile shaders. Call after QOpenGLFunctions::initializeOpenGLFunctions().
    bool init();

    /// Destroy GL resources.
    void shutdown();

    /// Bind program. Must be called before any setXxx() uniform uploads.
    void bind();

    /// Unbind (glUseProgram 0).
    void release();

    /// True after successful init().
    bool ready() const { return m_ready; }

    // ── Matrix uniforms (set while bound) ────────────────────────────────────

    /// Upload GL_PROJECTION_MATRIX (column-major). Call once per frame.
    void setProj(const float proj[16]);

    /// Upload GL_MODELVIEW_MATRIX = view*model (column-major). Call per draw.
    /// Automatically derives and uploads the normal matrix.
    void setModelView(const float mv[16]);

    // ── Light rig (set while bound, once per frame) ──────────────────────────

    /// Per-light data passed by LightRig::upload_to_shader.
    /// Directions are already in VIEW space.
    struct DirLightData {
        float dir[3]   = {0.f, 1.f, 0.f};
        float color[3] = {1.f, 1.f, 1.f};
    };

    /// Upload directional lights (view-space directions, up to k_max_dir_lights).
    void setDirLights(const DirLightData* lights, int count);

    /// Per-point-light data. Positions are in VIEW space.
    struct PointLightData {
        float pos_view[3] = {0.f, 0.f, 0.f};
        float color[3]    = {1.f, 1.f, 1.f}; // rgb * intensity
        float radius      = 100.f;
    };

    /// Upload point lights (view-space positions, up to 8 lights).
    void setPointLights(const PointLightData* lights, int count);

    /// Upload hemisphere ambient colors (sky / ground in linear HDR).
    void setAmbient(const float sky[3], const float ground[3]);

    /// Upload world-up direction in view space (col1 of view matrix).
    void setWorldUpView(float x, float y, float z);

    /// Upload fog parameters.
    void setFog(bool enabled, const float color[3], float density);

    /// Upload scene exposure multiplier (default 1.0).
    void setExposure(float exposure);

    // ── Material (set while bound, per draw-call) ────────────────────────────

    /// Upload all material PBR parameters at once.
    void setMaterial(const ruby::render::MaterialParams& mat);

    // Legacy convenience setters (kept for compatibility during transition)
    void setMaterialColor(float r, float g, float b, float a);
    void setHasTexture(bool has);
    void setTexture(int unit);

private:
    QOpenGLShaderProgram* m_program = nullptr;
    bool m_ready = false;

    // Cached uniform locations — resolved once at init(), never per-frame
    int m_loc_proj         = -1;
    int m_loc_modelView    = -1;
    int m_loc_normalMat    = -1;
    // Light rig
    int m_loc_dirCount     = -1;
    int m_loc_dirDir       = -1;
    int m_loc_dirCol       = -1;
    int m_loc_pointCount   = -1;
    int m_loc_pointPos     = -1;
    int m_loc_pointCol     = -1;
    int m_loc_pointRadius  = -1;
    int m_loc_ambSky       = -1;
    int m_loc_ambGround    = -1;
    int m_loc_worldUpView  = -1;
    int m_loc_fogEnabled   = -1;
    int m_loc_fogColor     = -1;
    int m_loc_fogDensity   = -1;
    int m_loc_exposure     = -1;
    // Material
    int m_loc_baseColor    = -1;
    int m_loc_roughness    = -1;
    int m_loc_metalness    = -1;
    int m_loc_emissive     = -1;
    int m_loc_hasAlbedoTex = -1;
    int m_loc_albedoTex    = -1;
    int m_loc_texIsSRGB    = -1;
};

} // namespace ruby::render
