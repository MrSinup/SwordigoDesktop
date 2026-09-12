// ============================================================================
// viewport_shader.cpp — Swordigo-specific GLSL 330 viewport shader.
//
//   Designed for Ruby GG which edits Swordigo exclusively.
//   Asset characteristics (from research):
//     - Low-poly mobile meshes: position (float3) + normal (float3) + UV (float2)
//     - No specular maps, no normal maps, no gloss maps in POD materials
//     - PODMaterial: diffuse[3] + opacity only (+ diffuse_texture_index)
//     - Textures: PVRTC/ETC1 decoded to RGBA8 (no GL_SRGB tagging)
//     - Original renderer: OpenGL ES 1.1 fixed-function, no gamma-correct pipeline
//     - V7.1 desktop port: Extended Reinhard (L_white=2.5), +15% sat, warm grade
//
//   Fragment shader choices:
//     - Half-Lambert² diffuse (Valve 2004) — stylized wrap, no harsh terminator
//     - Hemisphere ambient — sky/ground color cast from world-up direction
//     - No GGX specular — no gloss maps, stylized assets look wrong with it
//     - Reinhard (L_white=2.5) tone mapping — matches V7.1 PostFX pipeline
//     - +15% saturation boost — V7.1 pipeline characteristic
//     - Warm highlight push — "fantastical adventure" color grade
//     - Mild gamma 1/1.6 output — accounts for linear framebuffer on sRGB display
//       without decoding textures (matching original renderer's approach)
// ============================================================================

#include "viewport_shader.h"
#include "material_params.h"
#include "shader_library.h"

#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QMatrix3x3>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace ruby::render {

// ============================================================================
// Vertex Shader — unchanged from previous iteration
// ============================================================================
static const char* VS_SOURCE = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNorm;
layout(location = 2) in vec2 aUV;

// View-space pipeline — matches OpenGL fixed-function exactly:
//   uModelView = GL_MODELVIEW_MATRIX  (view * model)
//   uProj      = GL_PROJECTION_MATRIX
uniform mat4 uProj;
uniform mat4 uModelView;

out vec3 vViewPos;  // fragment position in VIEW space
out vec3 vNormal;   // surface normal in VIEW space (normalised)
out vec2 vUV;

void main() {
    vec4 vp     = uModelView * vec4(aPos, 1.0);
    vViewPos    = vp.xyz;
    mat3 normMat = transpose(inverse(mat3(uModelView)));
    vNormal     = normalize(normMat * aNorm);
    vUV         = aUV;
    gl_Position = uProj * vp;
}
)GLSL";


// ============================================================================
// Fragment Shader — built from ShaderLibrary snippets at init() time
// ============================================================================
static std::string build_fs_source() {
    std::string src;
    src.reserve(6000);
    src += "#version 330 core\n";

    // ── Snippet includes ──────────────────────────────────────────────────────
    src += ShaderLibrary::get("math");
    src += ShaderLibrary::get("halflambert");   // Half-Lambert² — no GGX
    src += ShaderLibrary::get("hemisphere");
    src += ShaderLibrary::get("tonemap");       // Reinhard + swordigo_grade + gamma_out
    src += ShaderLibrary::get("fog_exp2");

    // ── Uniforms ──────────────────────────────────────────────────────────────
    src += R"GLSL(
in vec3 vViewPos;
in vec3 vNormal;
in vec2 vUV;

// Directional lights (view-space directions, pre-transformed by LightRig)
#define MAX_DIR_LIGHTS 4
uniform int   uDirCount;
uniform vec3  uDirDir[MAX_DIR_LIGHTS];
uniform vec3  uDirCol[MAX_DIR_LIGHTS];

// Point lights (view-space positions, up to 8 lights)
#define MAX_POINT_LIGHTS 8
uniform int   uPointCount;
uniform vec3  uPointPosView[MAX_POINT_LIGHTS];
uniform vec3  uPointCol[MAX_POINT_LIGHTS];
uniform float uPointRadius[MAX_POINT_LIGHTS];

// Hemisphere ambient
uniform vec3 uAmbSky;
uniform vec3 uAmbGround;
uniform vec3 uWorldUpView;   // world (0,1,0) in view space — from LightRig

// Fog
uniform int   uFogEnabled;
uniform vec3  uFogColor;
uniform float uFogDensity;

// Exposure
uniform float uExposure;

// Material — PODMaterial provides diffuse[3] + opacity only.
// Roughness/metalness are kept for API completeness but unused in this shader.
uniform vec4      uBaseColor;       // diffuse tint (from PODMaterial.diffuse + opacity)
uniform int       uHasAlbedoTex;    // 1 if diffuse texture is bound
uniform sampler2D uAlbedoTex;       // unit 0

out vec4 FragColor;
)GLSL";

    // ── Main ──────────────────────────────────────────────────────────────────
    src += R"GLSL(
void main() {
    vec3 N = normalize(vNormal);

    // ── Sample albedo ─────────────────────────────────────────────────────────
    // Swordigo textures are PVRTC/ETC1 decoded to RGBA8 (GL_RGBA internal format,
    // no GL_SRGB tagging). The original GLES1.1 renderer sampled them as-is.
    // We do the same — NO pow(2.2) decode — to preserve the original palette.
    vec3 albedo = uBaseColor.rgb;
    float alpha = uBaseColor.a;
    if (uHasAlbedoTex != 0) {
        vec4 tex = texture(uAlbedoTex, vUV);
        albedo  *= tex.rgb;
        alpha   *= tex.a;
    }

    // ── Hemisphere ambient ────────────────────────────────────────────────────
    // Drives a warm ground / cool sky color cast across the whole scene.
    // Critical for Swordigo's forest/cave/dungeon environments — even
    // completely shadowed surfaces have visible ambient color.
    vec3 amb = hemisphere_ambient(N, uAmbSky, uAmbGround, uWorldUpView);

    // ── Half-Lambert² directional lights ─────────────────────────────────────
    vec3 direct = vec3(0.0);
    for (int i = 0; i < uDirCount && i < MAX_DIR_LIGHTS; ++i) {
        vec3 L = normalize(uDirDir[i]);
        direct += uDirCol[i] * halflambert(dot(N, L));
    }

    // ── Half-Lambert² point lights (view space) ──────────────────────────────
    for (int i = 0; i < uPointCount && i < MAX_POINT_LIGHTS; ++i) {
        vec3 toLight = uPointPosView[i] - vViewPos;
        float dist = length(toLight);
        float radius = uPointRadius[i];
        if (dist < radius && radius > 0.001) {
            vec3 L = toLight / dist;
            float atten = clamp(1.0 - dist / radius, 0.0, 1.0);
            atten = atten * atten; // Quadratic falloff matching mobile lighting
            direct += uPointCol[i] * halflambert(dot(N, L)) * atten;
        }
    }

    // ── Combine ───────────────────────────────────────────────────────────────
    // Albedo multiplies both ambient and direct — this matches GLES1.1 behavior
    // where the fragment color = diffuse_tex * (ambient_color + diffuse_light).
    vec3 lit = albedo * (amb + direct);

    // ── Exposure ──────────────────────────────────────────────────────────────
    lit *= uExposure;

    // ── Fog ───────────────────────────────────────────────────────────────────
    if (uFogEnabled != 0) {
        float dist = length(vViewPos);
        lit = apply_fog_exp2(lit, uFogColor, uFogDensity, dist);
    }

    // ── Tone mapping: Extended Reinhard (L_white = 2.5) ──────────────────────
    // Matches V7.1 Swordigo Desktop PostFX pipeline.
    // Compresses HDR output while preserving Swordigo's warm saturated palette.
    // Never clips colors — soft roll-off into white.
    lit = tonemap_reinhard(lit);

    // ── Swordigo color grade ──────────────────────────────────────────────────
    // +15% saturation + warm highlight push — V7.1 PostFX characteristics.
    // Brings back the vibrant green/amber/coral palette of the original game.
    lit = swordigo_grade(lit);

    // ── Gamma output ──────────────────────────────────────────────────────────
    // Linear framebuffer on sRGB display: apply mild gamma to prevent
    // the viewport appearing washed out. Using 1/1.6 (not 1/2.2) because
    // we didn't decode the sRGB textures — doing full 1/2.2 would overbrighten.
    lit = gamma_out(lit);

    FragColor = vec4(lit, alpha);
}
)GLSL";
    return src;
}

// ============================================================================
// Normal matrix: cofactor of upper-left 3×3 of the modelview matrix.
// Column-major input: mv[col*4 + row].
// Output is row-major floats — QMatrix3x3(nm).transposed() gives correct mat.
// ============================================================================
static void compute_normal_matrix(float out[9], const float mv[16]) {
    const float a00 = mv[0], a10 = mv[1], a20 = mv[2];
    const float a01 = mv[4], a11 = mv[5], a21 = mv[6];
    const float a02 = mv[8], a12 = mv[9], a22 = mv[10];

    out[0] = a11*a22 - a12*a21;
    out[1] = a12*a20 - a10*a22;
    out[2] = a10*a21 - a11*a20;
    out[3] = a02*a21 - a01*a22;
    out[4] = a00*a22 - a02*a20;
    out[5] = a01*a20 - a00*a21;
    out[6] = a01*a12 - a02*a11;
    out[7] = a02*a10 - a00*a12;
    out[8] = a00*a11 - a01*a10;
}

// ============================================================================
// Lifecycle
// ============================================================================
ViewportShader::~ViewportShader() { shutdown(); }

bool ViewportShader::init() {
    if (m_ready) return true;

    const std::string fs_src = build_fs_source();
    m_program = new QOpenGLShaderProgram();

    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, VS_SOURCE)) {
        fprintf(stderr, "[ViewportShader] VS compile failed:\n%s\n",
                m_program->log().toUtf8().constData());
        delete m_program; m_program = nullptr;
        return false;
    }
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, fs_src.c_str())) {
        fprintf(stderr, "[ViewportShader] FS compile failed:\n%s\n",
                m_program->log().toUtf8().constData());
        delete m_program; m_program = nullptr;
        return false;
    }
    if (!m_program->link()) {
        fprintf(stderr, "[ViewportShader] Link failed:\n%s\n",
                m_program->log().toUtf8().constData());
        delete m_program; m_program = nullptr;
        return false;
    }

    // ── Cache all uniform locations (no string lookups per frame) ─────────────
    m_loc_proj         = m_program->uniformLocation("uProj");
    m_loc_modelView    = m_program->uniformLocation("uModelView");
    m_loc_normalMat    = m_program->uniformLocation("uNormalMat");
    m_loc_dirCount     = m_program->uniformLocation("uDirCount");
    m_loc_dirDir       = m_program->uniformLocation("uDirDir[0]");
    m_loc_dirCol       = m_program->uniformLocation("uDirCol[0]");
    m_loc_pointCount   = m_program->uniformLocation("uPointCount");
    m_loc_pointPos     = m_program->uniformLocation("uPointPosView[0]");
    m_loc_pointCol     = m_program->uniformLocation("uPointCol[0]");
    m_loc_pointRadius  = m_program->uniformLocation("uPointRadius[0]");
    m_loc_ambSky       = m_program->uniformLocation("uAmbSky");
    m_loc_ambGround    = m_program->uniformLocation("uAmbGround");
    m_loc_worldUpView  = m_program->uniformLocation("uWorldUpView");
    m_loc_fogEnabled   = m_program->uniformLocation("uFogEnabled");
    m_loc_fogColor     = m_program->uniformLocation("uFogColor");
    m_loc_fogDensity   = m_program->uniformLocation("uFogDensity");
    m_loc_exposure     = m_program->uniformLocation("uExposure");
    m_loc_baseColor    = m_program->uniformLocation("uBaseColor");
    m_loc_roughness    = m_program->uniformLocation("uRoughness");   // kept for API compat
    m_loc_metalness    = m_program->uniformLocation("uMetalness");   // kept for API compat
    m_loc_emissive     = m_program->uniformLocation("uEmissive");    // kept for API compat
    m_loc_hasAlbedoTex = m_program->uniformLocation("uHasAlbedoTex");
    m_loc_albedoTex    = m_program->uniformLocation("uAlbedoTex");
    m_loc_texIsSRGB    = m_program->uniformLocation("uTexIsSRGB");   // kept for API compat

    // ── Safe defaults ─────────────────────────────────────────────────────────
    m_program->bind();
    // Exposure tuned for Swordigo: slightly above 1.0 for the warm, bright look
    if (m_loc_exposure     >= 0) m_program->setUniformValue(m_loc_exposure,     1.15f);
    if (m_loc_baseColor    >= 0) m_program->setUniformValue(m_loc_baseColor,    1.f, 1.f, 1.f, 1.f);
    if (m_loc_hasAlbedoTex >= 0) m_program->setUniformValue(m_loc_hasAlbedoTex, 0);
    if (m_loc_albedoTex    >= 0) m_program->setUniformValue(m_loc_albedoTex,    0);
    if (m_loc_fogEnabled   >= 0) m_program->setUniformValue(m_loc_fogEnabled,   0);
    if (m_loc_dirCount     >= 0) m_program->setUniformValue(m_loc_dirCount,     0);
    if (m_loc_pointCount   >= 0) m_program->setUniformValue(m_loc_pointCount,   0);
    if (m_loc_worldUpView  >= 0) m_program->setUniformValue(m_loc_worldUpView,  0.f, 1.f, 0.f);
    // Ambient defaults: warm earth ground, cool sky — Swordigo forest look
    if (m_loc_ambSky       >= 0) m_program->setUniformValue(m_loc_ambSky,       0.32f, 0.37f, 0.45f);
    if (m_loc_ambGround    >= 0) m_program->setUniformValue(m_loc_ambGround,    0.28f, 0.22f, 0.16f);
    m_program->release();

    fprintf(stderr, "[ViewportShader] Ready — Swordigo Half-Lambert² + Reinhard pipeline.\n");
    m_ready = true;
    return true;
}

void ViewportShader::shutdown() {
    delete m_program;
    m_program = nullptr;
    m_ready   = false;
}

void ViewportShader::bind()    { if (m_program) m_program->bind();    }
void ViewportShader::release() { if (m_program) m_program->release(); }

// ============================================================================
// Uniform setters
// NOTE: Qt's QMatrix4x4(const float*) reads the input as ROW-major.
// OpenGL glGetFloatv returns COLUMN-major arrays.
// Passing a column-major array into QMatrix4x4() → Qt reads it as row-major
// (effectively transposes it). .transposed() brings it back to correct layout.
// Qt's setUniformValue(QMatrix4x4) uploads with GL_FALSE transpose → correct.
// ============================================================================

void ViewportShader::setProj(const float proj[16]) {
    if (m_loc_proj >= 0)
        m_program->setUniformValue(m_loc_proj, QMatrix4x4(proj).transposed());
}

void ViewportShader::setModelView(const float mv[16]) {
    if (m_loc_modelView >= 0)
        m_program->setUniformValue(m_loc_modelView, QMatrix4x4(mv).transposed());
    if (m_loc_normalMat >= 0) {
        float nm[9];
        compute_normal_matrix(nm, mv);
        m_program->setUniformValue(m_loc_normalMat, QMatrix3x3(nm).transposed());
    }
}

void ViewportShader::setDirLights(const DirLightData* lights, int count) {
    count = (count < 0) ? 0 : (count > 4 ? 4 : count);
    if (m_loc_dirCount >= 0)
        m_program->setUniformValue(m_loc_dirCount, count);
    if (count > 0 && m_loc_dirDir >= 0) {
        float dirs[4][3], cols[4][3];
        for (int i = 0; i < count; ++i) {
            std::memcpy(dirs[i], lights[i].dir,   3 * sizeof(float));
            std::memcpy(cols[i], lights[i].color, 3 * sizeof(float));
        }
        m_program->setUniformValueArray(m_loc_dirDir, &dirs[0][0], count, 3);
        m_program->setUniformValueArray(m_loc_dirCol, &cols[0][0], count, 3);
    }
}

void ViewportShader::setPointLights(const PointLightData* lights, int count) {
    count = (count < 0) ? 0 : (count > 8 ? 8 : count);
    if (m_loc_pointCount >= 0)
        m_program->setUniformValue(m_loc_pointCount, count);
    if (count > 0) {
        if (m_loc_pointPos >= 0) {
            float positions[8][3];
            for (int i = 0; i < count; ++i)
                std::memcpy(positions[i], lights[i].pos_view, 3 * sizeof(float));
            m_program->setUniformValueArray(m_loc_pointPos, &positions[0][0], count, 3);
        }
        if (m_loc_pointCol >= 0) {
            float colors[8][3];
            for (int i = 0; i < count; ++i)
                std::memcpy(colors[i], lights[i].color, 3 * sizeof(float));
            m_program->setUniformValueArray(m_loc_pointCol, &colors[0][0], count, 3);
        }
        if (m_loc_pointRadius >= 0) {
            float radii[8];
            for (int i = 0; i < count; ++i) radii[i] = lights[i].radius;
            m_program->setUniformValueArray(m_loc_pointRadius, radii, count, 1);
        }
    }
}

void ViewportShader::setAmbient(const float sky[3], const float ground[3]) {
    if (m_loc_ambSky    >= 0) m_program->setUniformValue(m_loc_ambSky,    sky[0],    sky[1],    sky[2]);
    if (m_loc_ambGround >= 0) m_program->setUniformValue(m_loc_ambGround, ground[0], ground[1], ground[2]);
}

void ViewportShader::setWorldUpView(float x, float y, float z) {
    if (m_loc_worldUpView >= 0) m_program->setUniformValue(m_loc_worldUpView, x, y, z);
}

void ViewportShader::setFog(bool enabled, const float color[3], float density) {
    if (m_loc_fogEnabled >= 0) m_program->setUniformValue(m_loc_fogEnabled, enabled ? 1 : 0);
    if (m_loc_fogColor   >= 0) m_program->setUniformValue(m_loc_fogColor,   color[0], color[1], color[2]);
    if (m_loc_fogDensity >= 0) m_program->setUniformValue(m_loc_fogDensity, density);
}

void ViewportShader::setExposure(float exposure) {
    if (m_loc_exposure >= 0) m_program->setUniformValue(m_loc_exposure, exposure);
}

void ViewportShader::setMaterial(const ruby::render::MaterialParams& mat) {
    if (m_loc_baseColor    >= 0)
        m_program->setUniformValue(m_loc_baseColor,
            mat.base_color[0], mat.base_color[1], mat.base_color[2], mat.base_color[3]);
    if (m_loc_hasAlbedoTex >= 0) m_program->setUniformValue(m_loc_hasAlbedoTex, mat.has_albedo_tex);
    if (m_loc_albedoTex    >= 0) m_program->setUniformValue(m_loc_albedoTex,    mat.albedo_unit);
    // roughness, metalness, emissive kept for API completeness — not used in Swordigo shader
    if (m_loc_roughness    >= 0) m_program->setUniformValue(m_loc_roughness,    mat.roughness);
    if (m_loc_metalness    >= 0) m_program->setUniformValue(m_loc_metalness,    mat.metalness);
    if (m_loc_emissive     >= 0)
        m_program->setUniformValue(m_loc_emissive,
            mat.emissive[0], mat.emissive[1], mat.emissive[2]);
}

// ── Legacy convenience setters — used by existing draw_scene / draw_model paths ──
void ViewportShader::setMaterialColor(float r, float g, float b, float a) {
    if (m_loc_baseColor >= 0)
        m_program->setUniformValue(m_loc_baseColor, r, g, b, a);
}

void ViewportShader::setHasTexture(bool has) {
    if (m_loc_hasAlbedoTex >= 0) m_program->setUniformValue(m_loc_hasAlbedoTex, has ? 1 : 0);
}

void ViewportShader::setTexture(int unit) {
    if (m_loc_albedoTex >= 0) m_program->setUniformValue(m_loc_albedoTex, unit);
}

} // namespace ruby::render
