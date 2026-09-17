// gl_pipeline.cpp — GL 3.3 core shader pipeline for the opensw preview.
//
// Shader sources below are the Ruby GG viewport shader (ShaderLibrary +
// ViewportShader), ported from Qt/GLSL-330-compat to plain core-profile GLSL
// 330: no matrix stacks (matrices arrive as uniforms), no QOpenGLShaderProgram,
// same lighting math and same view-space convention.

#include "ruby/caver/render/gl_pipeline.h"

#include <cmath>
#include <cstdio>
#include <cstring>

// Modern GL 3.3 entry points (glCreateShader, VAOs, FBOs …) on Linux.
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include "platform/gl_inc.h"
#include <GL/glext.h>

namespace caver {
namespace render {
namespace {

// ─────────────────────────────────────────────────────────────────────────────
// GLSL 330 core — scene (the ported Ruby GG viewport shader)
// ─────────────────────────────────────────────────────────────────────────────
const char* kSceneVS = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNorm;
layout(location = 2) in vec2 aUV;

uniform mat4 uProj;        // projection
uniform mat4 uModelView;   // view * model
uniform mat3 uNormalMat;   // view-space normal matrix

out vec3 vNormal;
out vec2 vUV;
out vec3 vViewPos;

void main() {
    vec4 p = uModelView * vec4(aPos, 1.0);
    vViewPos = p.xyz;
    vNormal  = normalize(uNormalMat * aNorm);
    vUV      = aUV;
    gl_Position = uProj * p;
}
)GLSL";

const char* kSceneFS = R"GLSL(#version 330 core
#define MAX_DIR_LIGHTS 4
#define MAX_POINT_LIGHTS 16

in vec3 vNormal;
in vec2 vUV;
in vec3 vViewPos;

out vec4 fragColor;

uniform vec4  uBaseColor;
uniform vec3  uEmissive;
uniform int   uHasTexture;
uniform int   uTexIsSRGB;
uniform int   uFlipV;
uniform sampler2D uAlbedoTex;
uniform vec2  uUVOffset;

uniform int   uDirCount;
uniform vec3  uDirDir[MAX_DIR_LIGHTS];    // view space, toward the light
uniform vec3  uDirCol[MAX_DIR_LIGHTS];

uniform int   uPointCount;
uniform vec3  uPointPos[MAX_POINT_LIGHTS];   // view space
uniform vec3  uPointCol[MAX_POINT_LIGHTS];   // rgb * intensity
uniform float uPointRadius[MAX_POINT_LIGHTS];

uniform vec3  uAmbSky;
uniform vec3  uAmbGround;
uniform vec3  uWorldUpView;

uniform int   uFogEnabled;
uniform vec3  uFogColor;
uniform float uFogDensity;
uniform float uExposure;

// Half-Lambert^2 directional diffuse (Valve, TF2/HL2 shader pipeline):
// stylized games never go fully dark, which is what the Swordigo palette needs.
float halflambert(float ndl) {
    float wrap = ndl * 0.5 + 0.5;
    return wrap * wrap;
}

vec3 hemisphere_ambient(vec3 n, vec3 sky, vec3 ground, vec3 up) {
    float weight = dot(n, normalize(up)) * 0.5 + 0.5;
    return max(mix(ground, sky, clamp(weight, 0.0, 1.0)), vec3(0.04));
}

vec3 apply_fog_exp2(vec3 color, vec3 fog_color, float density, float dist) {
    float f = exp(-(density * density * dist * dist));
    return mix(fog_color, color, clamp(f, 0.0, 1.0));
}

// Extended Reinhard, white point 2.5 (the V7.1 desktop-port calibration).
vec3 tonemap_reinhard(vec3 color) {
    const float white_sq = 2.5 * 2.5;
    return (color * (1.0 + color / white_sq)) / (1.0 + color);
}

// Swordigo grade: +15% saturation, warm luminosity push in the highlights.
vec3 swordigo_grade(vec3 color) {
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    vec3  sat  = mix(vec3(luma), color, 1.15);
    float warmth = luma * luma;
    sat.r += warmth * 0.028;
    sat.g += warmth * 0.012;
    sat.b -= warmth * 0.018;
    return clamp(sat, 0.0, 1.0);
}

vec3 gamma_out(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(1.0 / 1.6));
}

void main() {
    vec3  albedo = uBaseColor.rgb;
    float alpha  = uBaseColor.a;
    vec2  uv     = vUV + uUVOffset;
    if (uFlipV == 1) uv.y = 1.0 - uv.y;
    if (uHasTexture == 1) {
        vec4 texel = texture(uAlbedoTex, uv);
        if (uTexIsSRGB == 1) texel.rgb = pow(max(texel.rgb, vec3(0.0)), vec3(2.2));
        albedo *= texel.rgb;
        alpha  *= texel.a;
    }
    if (alpha <= 0.003) discard;

    vec3 n = normalize(vNormal);
    vec3 lit = vec3(0.0);
    for (int i = 0; i < uDirCount && i < MAX_DIR_LIGHTS; ++i) {
        float ndl = dot(n, normalize(uDirDir[i]));
        lit += uDirCol[i] * halflambert(ndl) * albedo;
    }
    for (int i = 0; i < uPointCount && i < MAX_POINT_LIGHTS; ++i) {
        vec3  d = uPointPos[i] - vViewPos;
        float dist = length(d);
        float atten = clamp(1.0 - dist / max(uPointRadius[i], 1e-3), 0.0, 1.0);
        atten *= atten;
        float ndl = dot(n, normalize(d + vec3(1e-5)));
        lit += uPointCol[i] * halflambert(ndl) * albedo * atten;
    }

    vec3 ambient = hemisphere_ambient(n, uAmbSky, uAmbGround, uWorldUpView) * albedo;
    vec3 color = ambient + lit + uEmissive;
    color *= uExposure;
    color = tonemap_reinhard(color);
    color = swordigo_grade(color);
    if (uFogEnabled == 1) color = apply_fog_exp2(color, uFogColor, uFogDensity, length(vViewPos));
    fragColor = vec4(gamma_out(color), alpha);
}
)GLSL";

// ─────────────────────────────────────────────────────────────────────────────
// Billboards (light glows, fire, shadow blobs). Vertices are already in world
// space, built CPU-side against the camera's right/up so the quad faces the
// camera; the shader only projects.
// ─────────────────────────────────────────────────────────────────────────────
const char* kSpriteVS = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

uniform mat4 uProj;
uniform mat4 uView;

out vec2 vUV;
out vec4 vColor;

void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = uProj * (uView * vec4(aPos, 1.0));
}
)GLSL";

const char* kSpriteFS = R"GLSL(#version 330 core
in vec2 vUV;
in vec4 vColor;
out vec4 fragColor;

uniform int uAdditive;

void main() {
    float d = length(vUV * 2.0 - 1.0);
    float a = clamp(1.0 - d, 0.0, 1.0);
    a = a * a;
    if (uAdditive == 1) fragColor = vec4(vColor.rgb * a, a * vColor.a);
    else                fragColor = vec4(vColor.rgb, a * vColor.a);
}
)GLSL";

// ─────────────────────────────────────────────────────────────────────────────
// Composite: one fullscreen triangle, generated from gl_VertexID (no VBO).
// ─────────────────────────────────────────────────────────────────────────────
const char* kBlitVS = R"GLSL(#version 330 core
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

const char* kBlitFS = R"GLSL(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
void main() {
    fragColor = texture(uTex, vUV);
}
)GLSL";

// ─────────────────────────────────────────────────────────────────────────────
// Caver::LightOverlay — the recovered darkness veil.
//
// Caver::LightOverlay::CreateVertices (0x36F6F0) fills a grid mesh with a
// constant colour of black at half alpha (`Caver::Color::operator*(&color,
// 0.5f)` on 0xFF000000); SetPointLights (0x36F898) uploads the frame's point
// lights; Draw (0x36FCDC) draws that veil over the camera rect, which Scene::Draw
// (0x3755EC) does right after the ground and model passes. Lights punch
// brightness back through it, which is what makes Swordigo caves read as dark
// with glowing torches.
//
// This is the same shape in one fragment pass: a fullscreen triangle whose alpha
// is the veil alpha everywhere EXCEPT inside a point light's radius.
const char* kVeilVS = R"GLSL(#version 330 core
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 1.0, 1.0);
}
)GLSL";

const char* kVeilFS = R"GLSL(#version 330 core
#define MAX_VEIL_LIGHTS 16
in vec2 vUV;
out vec4 fragColor;

uniform float uVeilAlpha;
uniform int   uVeilCount;
uniform vec2  uVeilPos[MAX_VEIL_LIGHTS];      // NDC (-1..1)
uniform float uVeilRadius[MAX_VEIL_LIGHTS];   // NDC units, vertical
uniform float uVeilAspect;

void main() {
    vec2 uv = vUV * 2.0 - 1.0;
    float mask = 0.0;
    for (int i = 0; i < uVeilCount && i < MAX_VEIL_LIGHTS; ++i) {
        vec2 d = uv - uVeilPos[i];
        d.x *= uVeilAspect;
        float r = max(uVeilRadius[i], 1e-4);
        float f = clamp(1.0 - length(d) / r, 0.0, 1.0);
        mask = max(mask, f * f);
    }
    float alpha = uVeilAlpha * (1.0 - mask);
    if (alpha <= 0.002) discard;
    fragColor = vec4(0.0, 0.0, 0.0, alpha);
}
)GLSL";

// ─────────────────────────────────────────────────────────────────────────────
// Background (BackgroundComponent) — an unlit, textured NDC quad drawn last.
// ─────────────────────────────────────────────────────────────────────────────
const char* kBackgroundVS = R"GLSL(#version 330 core
out vec2 vUV;
uniform vec2 uOffset;
uniform vec2 uHalfExtent;
uniform float uDepth;
void main() {
    // Triangle strip: (-1,-1) (1,-1) (1,1) (-1,1), UV 0..1 with v = 0 at the
    // bottom — the native tex container's own row order.
    vec2 corner = vec2((gl_VertexID & 1) == 0 ? -1.0 : 1.0, (gl_VertexID & 2) == 0 ? -1.0 : 1.0);
    vUV = corner * 0.5 + 0.5;
    gl_Position = vec4(corner * uHalfExtent + uOffset, uDepth, 1.0);
}
)GLSL";

const char* kBackgroundFS = R"GLSL(#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
uniform vec4 uColor;
void main() {
    fragColor = texture(uTex, vUV) * uColor;
}
)GLSL";

unsigned int compile_stage(GLenum type, const char* source, std::string* error) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
        if (error) *error = log;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

unsigned int link_program(const char* vs_src, const char* fs_src, std::string* error) {
    const GLuint vs = compile_stage(GL_VERTEX_SHADER, vs_src, error);
    if (!vs) return 0;
    const GLuint fs = compile_stage(GL_FRAGMENT_SHADER, fs_src, error);
    if (!fs) {
        glDeleteShader(vs);
        return 0;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[2048];
        glGetProgramInfoLog(program, sizeof(log) - 1, nullptr, log);
        if (error) *error = log;
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

} // namespace

GlPipeline::~GlPipeline() { shutdown(); }

// ============================================================================
// Lifecycle
// ============================================================================

bool GlPipeline::build_programs(std::string* error) {
    scene_program_ = link_program(kSceneVS, kSceneFS, error);
    if (!scene_program_) return false;
    sprite_program_ = link_program(kSpriteVS, kSpriteFS, error);
    if (!sprite_program_) return false;
    blit_program_ = link_program(kBlitVS, kBlitFS, error);
    if (!blit_program_) return false;
    veil_program_ = link_program(kVeilVS, kVeilFS, error);
    if (!veil_program_) return false;
    background_program_ = link_program(kBackgroundVS, kBackgroundFS, error);
    if (!background_program_) return false;

    u_proj_        = glGetUniformLocation(scene_program_, "uProj");
    u_model_view_  = glGetUniformLocation(scene_program_, "uModelView");
    u_normal_mat_  = glGetUniformLocation(scene_program_, "uNormalMat");
    u_dir_count_   = glGetUniformLocation(scene_program_, "uDirCount");
    u_dir_dir_     = glGetUniformLocation(scene_program_, "uDirDir");
    u_dir_col_     = glGetUniformLocation(scene_program_, "uDirCol");
    u_point_count_ = glGetUniformLocation(scene_program_, "uPointCount");
    u_point_pos_   = glGetUniformLocation(scene_program_, "uPointPos");
    u_point_col_   = glGetUniformLocation(scene_program_, "uPointCol");
    u_point_radius_= glGetUniformLocation(scene_program_, "uPointRadius");
    u_amb_sky_     = glGetUniformLocation(scene_program_, "uAmbSky");
    u_amb_ground_  = glGetUniformLocation(scene_program_, "uAmbGround");
    u_world_up_view_ = glGetUniformLocation(scene_program_, "uWorldUpView");
    u_fog_enabled_ = glGetUniformLocation(scene_program_, "uFogEnabled");
    u_fog_color_   = glGetUniformLocation(scene_program_, "uFogColor");
    u_fog_density_ = glGetUniformLocation(scene_program_, "uFogDensity");
    u_exposure_    = glGetUniformLocation(scene_program_, "uExposure");
    u_uv_offset_   = glGetUniformLocation(scene_program_, "uUVOffset");
    u_base_color_  = glGetUniformLocation(scene_program_, "uBaseColor");
    u_emissive_    = glGetUniformLocation(scene_program_, "uEmissive");
    u_has_texture_ = glGetUniformLocation(scene_program_, "uHasTexture");
    u_albedo_tex_  = glGetUniformLocation(scene_program_, "uAlbedoTex");
    u_tex_is_srgb_ = glGetUniformLocation(scene_program_, "uTexIsSRGB");
    u_flip_v_      = glGetUniformLocation(scene_program_, "uFlipV");

    s_proj_ = glGetUniformLocation(sprite_program_, "uProj");
    s_view_ = glGetUniformLocation(sprite_program_, "uView");

    b_tex_  = glGetUniformLocation(blit_program_, "uTex");

    v_alpha_  = glGetUniformLocation(veil_program_, "uVeilAlpha");
    v_count_  = glGetUniformLocation(veil_program_, "uVeilCount");
    v_pos_    = glGetUniformLocation(veil_program_, "uVeilPos");
    v_radius_ = glGetUniformLocation(veil_program_, "uVeilRadius");
    v_aspect_ = glGetUniformLocation(veil_program_, "uVeilAspect");

    g_offset_ = glGetUniformLocation(background_program_, "uOffset");
    g_extent_ = glGetUniformLocation(background_program_, "uHalfExtent");
    g_color_  = glGetUniformLocation(background_program_, "uColor");
    g_tex_    = glGetUniformLocation(background_program_, "uTex");
    g_depth_  = glGetUniformLocation(background_program_, "uDepth");

    // One empty VAO for the generated fullscreen triangle.
    glGenVertexArrays(1, &quad_vao_);
    glGenVertexArrays(1, &veil_vao_);
    glGenBuffers(2, sprite_vbo_);
    return true;
}

bool GlPipeline::init(std::string* error) {
    if (!build_programs(error)) {
        if (error && !error->empty()) *error = "gl_pipeline: " + *error;
        return false;
    }
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    // Swordigo's POD meshes are single-sided strips whose winding does not
    // consistently match GL's CCW front face, and both the studio viewport and
    // the engine's own GLES1.1 pipeline render them with culling off. Culling
    // here removes most of a level (walls, terrain, characters).
    glDisable(GL_CULL_FACE);
    return true;
}

void GlPipeline::shutdown() {
    if (scene_program_) glDeleteProgram(scene_program_);
    if (sprite_program_) glDeleteProgram(sprite_program_);
    if (blit_program_) glDeleteProgram(blit_program_);
    if (veil_program_) glDeleteProgram(veil_program_);
    if (background_program_) glDeleteProgram(background_program_);
    scene_program_ = sprite_program_ = blit_program_ = veil_program_ = background_program_ = 0;
    for (unsigned int& vbo : sprite_vbo_) {
        if (vbo) glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (quad_vao_) glDeleteVertexArrays(1, &quad_vao_);
    quad_vao_ = 0;
    if (veil_vao_) glDeleteVertexArrays(1, &veil_vao_);
    veil_vao_ = 0;
    if (depth_rb_) glDeleteRenderbuffers(1, &depth_rb_);
    depth_rb_ = 0;
    if (color_tex_) glDeleteTextures(1, &color_tex_);
    color_tex_ = 0;
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    fbo_ = 0;
    width_ = height_ = 0;
}

// ============================================================================
// Target
// ============================================================================

bool GlPipeline::resize(int w, int h) {
    width_ = w;
    height_ = h;
    if (w <= 0 || h <= 0) return true;      // draw straight to the window

    if (!fbo_) glGenFramebuffers(1, &fbo_);
    if (!color_tex_) glGenTextures(1, &color_tex_);
    if (!depth_rb_) glGenRenderbuffers(1, &depth_rb_);

    glBindTexture(GL_TEXTURE_2D, color_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindRenderbuffer(GL_RENDERBUFFER, depth_rb_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb_);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "caver::render: scene FBO incomplete (0x%x) — drawing directly\n",
                     status);
        if (fbo_) glDeleteFramebuffers(1, &fbo_);
        if (color_tex_) glDeleteTextures(1, &color_tex_);
        if (depth_rb_) glDeleteRenderbuffers(1, &depth_rb_);
        fbo_ = color_tex_ = depth_rb_ = 0;
        width_ = height_ = 0;
        return false;
    }
    return true;
}

void GlPipeline::begin_scene(const float proj[16], const float view[16]) {
    std::memcpy(proj_, proj, sizeof(proj_));
    std::memcpy(view_, view, sizeof(view_));
    // The view matrix's rows are the camera basis in world space (column-major
    // storage: m[0..2] = right, m[4..6] = up).
    cam_right_[0] = view[0]; cam_right_[1] = view[4]; cam_right_[2] = view[8];
    cam_up_[0]    = view[1]; cam_up_[1]    = view[5]; cam_up_[2]    = view[9];

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_ > 0 ? width_ : 1, height_ > 0 ? height_ : 1);
    glClearColor(0.06f, 0.07f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    draw_calls_ = 0;
    triangles_ = 0;
    sprites_[0].clear();
    sprites_[1].clear();

    glUseProgram(scene_program_);
    glUniformMatrix4fv(u_proj_, 1, GL_FALSE, proj_);
    scene_uniforms();
    in_scene_ = true;
}

void GlPipeline::scene_uniforms() {
    glUniform1i(u_dir_count_, static_cast<int>(dir_lights_.size()));
    if (!dir_lights_.empty()) {
        float dirs[4 * 3] = {0};
        float cols[4 * 3] = {0};
        const size_t count = dir_lights_.size() < 4 ? dir_lights_.size() : 4;
        for (size_t i = 0; i < count; ++i) {
            // Rotate the world-space direction into view space (the shader lights
            // in view space, exactly like the Ruby GG viewport).
            const float dx = dir_lights_[i].dir[0], dy = dir_lights_[i].dir[1],
                        dz = dir_lights_[i].dir[2];
            dirs[i * 3 + 0] = view_[0] * dx + view_[4] * dy + view_[8]  * dz;
            dirs[i * 3 + 1] = view_[1] * dx + view_[5] * dy + view_[9]  * dz;
            dirs[i * 3 + 2] = view_[2] * dx + view_[6] * dy + view_[10] * dz;
            cols[i * 3 + 0] = dir_lights_[i].color[0];
            cols[i * 3 + 1] = dir_lights_[i].color[1];
            cols[i * 3 + 2] = dir_lights_[i].color[2];
        }
        glUniform3fv(u_dir_dir_, static_cast<int>(count), dirs);
        glUniform3fv(u_dir_col_, static_cast<int>(count), cols);
    }
    glUniform3fv(u_amb_sky_, 1, ambient_sky_);
    glUniform3fv(u_amb_ground_, 1, ambient_ground_);
    // World up (0,1,0) in view space.
    const float up_view[3] = {view_[1], view_[5], view_[9]};
    glUniform3fv(u_world_up_view_, 1, up_view);
    glUniform1i(u_fog_enabled_, fog_enabled_ ? 1 : 0);
    glUniform3fv(u_fog_color_, 1, fog_color_);
    glUniform1f(u_fog_density_, fog_density_);
    glUniform1f(u_exposure_, exposure_);
}

void GlPipeline::set_point_lights(const std::vector<PointLight>& lights) {
    point_lights_ = lights;
    if (point_lights_.size() > 16) point_lights_.resize(16);
    if (!in_scene_) return;
    glUseProgram(scene_program_);
    glUniform1i(u_point_count_, static_cast<int>(point_lights_.size()));
    if (point_lights_.empty()) return;
    float pos[16 * 3] = {0};
    float col[16 * 3] = {0};
    float radius[16] = {0};
    for (size_t i = 0; i < point_lights_.size(); ++i) {
        const PointLight& l = point_lights_[i];
        // View-space position, so the fragment shader lights without a camera
        // uniform (the viewport's own convention).
        pos[i * 3 + 0] = view_[0] * l.pos[0] + view_[4] * l.pos[1] + view_[8]  * l.pos[2] + view_[12];
        pos[i * 3 + 1] = view_[1] * l.pos[0] + view_[5] * l.pos[1] + view_[9]  * l.pos[2] + view_[13];
        pos[i * 3 + 2] = view_[2] * l.pos[0] + view_[6] * l.pos[1] + view_[10] * l.pos[2] + view_[14];
        col[i * 3 + 0] = l.color[0];
        col[i * 3 + 1] = l.color[1];
        col[i * 3 + 2] = l.color[2];
        radius[i] = l.radius;
    }
    glUniform3fv(u_point_pos_, static_cast<int>(point_lights_.size()), pos);
    glUniform3fv(u_point_col_, static_cast<int>(point_lights_.size()), col);
    glUniform1fv(u_point_radius_, static_cast<int>(point_lights_.size()), radius);
}

void GlPipeline::set_dir_lights(const std::vector<DirLight>& lights) {
    dir_lights_ = lights;
    if (dir_lights_.size() > 4) dir_lights_.resize(4);
    if (in_scene_) {
        glUseProgram(scene_program_);
        scene_uniforms();
    }
}

void GlPipeline::set_ambient(const float sky[3], const float ground[3]) {
    std::memcpy(ambient_sky_, sky, sizeof(ambient_sky_));
    std::memcpy(ambient_ground_, ground, sizeof(ambient_ground_));
    if (in_scene_) {
        glUseProgram(scene_program_);
        glUniform3fv(u_amb_sky_, 1, ambient_sky_);
        glUniform3fv(u_amb_ground_, 1, ambient_ground_);
    }
}

void GlPipeline::set_fog(bool enabled, const float color[3], float density) {
    fog_enabled_ = enabled;
    std::memcpy(fog_color_, color, sizeof(fog_color_));
    fog_density_ = density;
    if (in_scene_) {
        glUseProgram(scene_program_);
        glUniform1i(u_fog_enabled_, fog_enabled_ ? 1 : 0);
        glUniform3fv(u_fog_color_, 1, fog_color_);
        glUniform1f(u_fog_density_, fog_density_);
    }
}

// ============================================================================
// Geometry
// ============================================================================

void GlPipeline::upload_mesh(const av::PODMesh& mesh, GlMesh& out, bool flip_uv_v) {
    free_mesh(out);
    if (mesh.num_vertices <= 0) return;

    // A mesh with no index list is a *non-indexed* triangle list: three vertices
    // per face, in order. Swordigo uses this for exactly the meshes that were
    // missing from the frame — every GroundMesh FrontMesh (the level's vertical
    // front face, `GroundMeshComponent` field 9) and the hand-authored aprons on
    // top of the generated ground. The engine's renderer draws a null index array
    // as a straight `glDrawArrays`; skipping them, as this did, dropped the whole
    // side of the world.
    std::vector<uint32_t> indices = mesh.indices;
    if (indices.empty()) {
        if (mesh.num_vertices < 3) return;
        indices.resize(static_cast<size_t>(mesh.num_vertices));
        for (size_t i = 0; i < indices.size(); ++i) indices[i] = static_cast<uint32_t>(i);
    }

    const int vertex_count = mesh.num_vertices;
    std::vector<float> interleaved(static_cast<size_t>(vertex_count) * 8, 0.0f);
    for (int i = 0; i < vertex_count; ++i) {
        const size_t base = static_cast<size_t>(i) * 8;
        if (mesh.positions.size() >= static_cast<size_t>(i) * 3 + 3) {
            interleaved[base + 0] = mesh.positions[static_cast<size_t>(i) * 3 + 0];
            interleaved[base + 1] = mesh.positions[static_cast<size_t>(i) * 3 + 1];
            interleaved[base + 2] = mesh.positions[static_cast<size_t>(i) * 3 + 2];
        }
        if (mesh.normals.size() >= static_cast<size_t>(i) * 3 + 3) {
            interleaved[base + 3] = mesh.normals[static_cast<size_t>(i) * 3 + 0];
            interleaved[base + 4] = mesh.normals[static_cast<size_t>(i) * 3 + 1];
            interleaved[base + 5] = mesh.normals[static_cast<size_t>(i) * 3 + 2];
        } else {
            interleaved[base + 4] = 1.0f;
        }
        if (mesh.uvs.size() >= static_cast<size_t>(i) * 2 + 2) {
            interleaved[base + 6] = mesh.uvs[static_cast<size_t>(i) * 2 + 0];
            interleaved[base + 7] = mesh.uvs[static_cast<size_t>(i) * 2 + 1];
        }
    }

    glGenVertexArrays(1, &out.vao);
    glGenBuffers(1, &out.vbo);
    glGenBuffers(1, &out.ebo);

    glBindVertexArray(out.vao);
    glBindBuffer(GL_ARRAY_BUFFER, out.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(interleaved.size() * sizeof(float)),
                 interleaved.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                 indices.data(), GL_STATIC_DRAW);

    const GLsizei stride = 8 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(6 * sizeof(float)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    out.index_count  = static_cast<int>(indices.size());
    out.vertex_count = vertex_count;
    out.flip_uv_v    = flip_uv_v;
}

void GlPipeline::update_vertices(GlMesh& mesh, const float* positions, const float* normals,
                                 const float* uvs, int count) {
    if (!mesh.vao || !mesh.vbo || count <= 0 || count != mesh.vertex_count) return;
    std::vector<float> interleaved(static_cast<size_t>(count) * 8, 0.0f);
    for (int i = 0; i < count; ++i) {
        const size_t base = static_cast<size_t>(i) * 8;
        if (positions) {
            interleaved[base + 0] = positions[static_cast<size_t>(i) * 3 + 0];
            interleaved[base + 1] = positions[static_cast<size_t>(i) * 3 + 1];
            interleaved[base + 2] = positions[static_cast<size_t>(i) * 3 + 2];
        }
        if (normals) {
            interleaved[base + 3] = normals[static_cast<size_t>(i) * 3 + 0];
            interleaved[base + 4] = normals[static_cast<size_t>(i) * 3 + 1];
            interleaved[base + 5] = normals[static_cast<size_t>(i) * 3 + 2];
        }
        if (uvs) {
            interleaved[base + 6] = uvs[static_cast<size_t>(i) * 2 + 0];
            interleaved[base + 7] = uvs[static_cast<size_t>(i) * 2 + 1];
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(interleaved.size() * sizeof(float)),
                    interleaved.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GlPipeline::free_mesh(GlMesh& mesh) {
    if (mesh.ebo) glDeleteBuffers(1, &mesh.ebo);
    if (mesh.vbo) glDeleteBuffers(1, &mesh.vbo);
    if (mesh.vao) glDeleteVertexArrays(1, &mesh.vao);
    mesh = GlMesh{};
}

void GlPipeline::draw(const GlMesh& mesh, const float model[16], const Material& material) {
    if (!mesh.ready() || !scene_program_ || !in_scene_) return;

    float model_view[16];
    av::mat4_multiply(model_view, view_, model);
    // The shader lights in VIEW space, so the normal matrix belongs to the
    // model-view, not the model (the viewport's own convention).
    float normal_mat[9];
    av::mat4_normal_matrix(normal_mat, model_view);

    glUseProgram(scene_program_);
    glUniformMatrix4fv(u_model_view_, 1, GL_FALSE, model_view);
    glUniformMatrix3fv(u_normal_mat_, 1, GL_FALSE, normal_mat);
    glUniform4fv(u_base_color_, 1, material.base_color);
    glUniform3fv(u_emissive_, 1, material.emissive);
    glUniform2fv(u_uv_offset_, 1, material.uv_offset);
    glUniform1i(u_flip_v_, material.texture != 0 && mesh.flip_uv_v ? 1 : 0);
    glUniform1i(u_has_texture_, material.texture != 0 ? 1 : 0);
    if (material.texture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, material.texture);
        glUniform1i(u_albedo_tex_, 0);
        glUniform1i(u_tex_is_srgb_, material.texture_is_srgb ? 1 : 0);
    }

    if (material.base_color[3] < 0.999f) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    }
    glBindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    if (material.base_color[3] < 0.999f) {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
    ++draw_calls_;
    triangles_ += mesh.index_count / 3;
}

// ============================================================================
// Light overlay (Caver::LightOverlay)
// ============================================================================

void GlPipeline::draw_light_veil(const std::vector<PointLight>& lights, float alpha) {
    if (!veil_program_ || !in_scene_ || alpha <= 0.002f) return;

    // Project each point light into NDC. A light behind the camera (or one whose
    // radius cannot be expressed in NDC units) is skipped: the engine's overlay
    // only punches holes for lights inside the frame anyway.
    float ndc[16 * 2] = {0};
    float radius[16] = {0};
    int count = 0;
    const float proj5 = std::fabs(proj_[5]) > 1e-6f ? proj_[5] : 1.732f;   // 1/tan(fov/2)
    for (const PointLight& light : lights) {
        if (count >= 16) break;
        const float vx = view_[0] * light.pos[0] + view_[4] * light.pos[1] + view_[8]  * light.pos[2] + view_[12];
        const float vy = view_[1] * light.pos[0] + view_[5] * light.pos[1] + view_[9]  * light.pos[2] + view_[13];
        const float vz = view_[2] * light.pos[0] + view_[6] * light.pos[1] + view_[10] * light.pos[2] + view_[14];
        const float depth = -vz;
        if (depth <= 1.0f) continue;
        const float w = light.radius > 1.0f ? light.radius : 1.0f;
        (void)w;
        const float clip_x = proj_[0] * vx;
        const float clip_y = proj_[5] * vy;
        ndc[count * 2 + 0] = clip_x / depth;
        ndc[count * 2 + 1] = clip_y / depth;
        radius[count] = light.radius * proj5 / depth;
        ++count;
    }
    if (count == 0 && alpha <= 0.002f) return;

    glUseProgram(veil_program_);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform1f(v_alpha_, alpha);
    glUniform1i(v_count_, count);
    glUniform2fv(v_pos_, count, ndc);
    glUniform1fv(v_radius_, count, radius);
    glUniform1f(v_aspect_, static_cast<float>(width_) / static_cast<float>(height_ > 0 ? height_ : 1));
    glBindVertexArray(veil_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    ++draw_calls_;
}

void GlPipeline::draw_background(unsigned int texture, const float offset[2],
                                 const float half_extent[2], const float color[4]) {
    if (!background_program_ || !texture) return;
    glUseProgram(background_program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(g_tex_, 0);
    glUniform2fv(g_offset_, 1, offset);
    glUniform2fv(g_extent_, 1, half_extent);
    glUniform4fv(g_color_, 1, color);
    // Just inside the far plane: the depth test then keeps the quad where the
    // world left the buffer cleared (the engine's own last-pass behaviour).
    glUniform1f(g_depth_, 0.999999f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glBindVertexArray(veil_vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    ++draw_calls_;
    triangles_ += 2;
}

// ============================================================================
// Billboards
// ============================================================================

void GlPipeline::queue_sprite(const float pos[3], const float color[3], float radius_x,
                              float radius_z, float intensity, bool additive) {
    const int slot = additive ? 0 : 1;
    const float rx = radius_x * 0.5f;
    const float ry = radius_z * 0.5f;
    const float c[4] = {color[0], color[1], color[2], intensity};
    auto push = [&](float sx, float sy, float u, float v) {
        SpriteVertex vertex;
        vertex.x = pos[0] + cam_right_[0] * sx * rx + cam_up_[0] * sy * ry;
        vertex.y = pos[1] + cam_right_[1] * sx * rx + cam_up_[1] * sy * ry;
        vertex.z = pos[2] + cam_right_[2] * sx * rx + cam_up_[2] * sy * ry;
        vertex.u = u;
        vertex.v = v;
        vertex.r = c[0];
        vertex.g = c[1];
        vertex.b = c[2];
        vertex.a = c[3];
        sprites_[slot].push_back(vertex);
    };
    push(-1.0f, -1.0f, 0.0f, 0.0f);
    push( 1.0f, -1.0f, 1.0f, 0.0f);
    push( 1.0f,  1.0f, 1.0f, 1.0f);
    push(-1.0f, -1.0f, 0.0f, 0.0f);
    push( 1.0f,  1.0f, 1.0f, 1.0f);
    push(-1.0f,  1.0f, 0.0f, 1.0f);
}

void GlPipeline::draw_glow(const float pos[3], const float color[3], float size, float intensity) {
    queue_sprite(pos, color, size, size, intensity, true);
}

void GlPipeline::draw_shadow(const float pos[3], float radius_x, float radius_z,
                             const float color[3]) {
    queue_sprite(pos, color, radius_x, radius_z, 0.55f, false);
}

void GlPipeline::upload_sprite_buffer(bool additive) {
    const int slot = additive ? 0 : 1;
    if (!sprite_vbo_[slot]) glGenBuffers(1, &sprite_vbo_[slot]);
    glBindBuffer(GL_ARRAY_BUFFER, sprite_vbo_[slot]);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sprites_[slot].size() * sizeof(SpriteVertex)),
                 sprites_[slot].data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (!quad_vao_) glGenVertexArrays(1, &quad_vao_);
    glBindVertexArray(quad_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, sprite_vbo_[slot]);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex),
                          reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex),
                          reinterpret_cast<void*>(5 * sizeof(float)));
    glBindVertexArray(0);
}

void GlPipeline::flush_sprites() {
    if (!sprite_program_ || !in_scene_) return;
    for (int pass = 0; pass < 2; ++pass) {
        const bool additive = (pass == 0);
        if (sprites_[pass].empty()) continue;
        // The quad's colour rides in the vertex stream, so one draw covers the
        // whole batch (torch glows, fires, shadow blobs).
        upload_sprite_buffer(additive);
        glUseProgram(sprite_program_);
        glUniformMatrix4fv(s_proj_, 1, GL_FALSE, proj_);
        glUniformMatrix4fv(s_view_, 1, GL_FALSE, view_);
        glUniform1i(glGetUniformLocation(sprite_program_, "uAdditive"), additive ? 1 : 0);
        if (additive) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glDepthMask(GL_FALSE);
        } else {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        }
        glBindVertexArray(quad_vao_);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(sprites_[pass].size()));
        glBindVertexArray(0);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        ++draw_calls_;
    }
    sprites_[0].clear();
    sprites_[1].clear();
}

void GlPipeline::end_scene() {
    flush_sprites();
    glBindVertexArray(0);
    glUseProgram(0);
    in_scene_ = false;
}

void GlPipeline::composite(unsigned int default_fbo) {
    if (!fbo_ || !color_tex_ || !blit_program_) return;
    glBindFramebuffer(GL_FRAMEBUFFER, default_fbo);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(blit_program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, color_tex_);
    glUniform1i(b_tex_, 0);
    glBindVertexArray(quad_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
    glEnable(GL_DEPTH_TEST);
}

} // namespace render
} // namespace caver
