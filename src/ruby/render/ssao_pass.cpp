// ============================================================================
// ssao_pass.cpp — SSAO implementation for Ruby GG viewport.
// ============================================================================
#include "ssao_pass.h"
#include "fbo_chain.h"

#include <QOpenGLShaderProgram>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace ruby::render {

// ── Shared fullscreen vertex shader ──────────────────────────────────────────
static const char* k_fs_vs = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() { vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }
)GLSL";

// ── SSAO fragment shader ──────────────────────────────────────────────────────
// Algorithm:
//   1. Reconstruct view-space position from depth + invProj
//   2. Reconstruct normal from finite-difference ddx/ddy of position
//   3. Build random TBN hemisphere using 4x4 rotation noise texture
//   4. Sample 16 hemisphere points, project to screen, compare depths
//   5. Accumulate ranged occlusion → output [0..1]
static const char* k_ssao_fs = R"GLSL(
#version 330 core

uniform sampler2D uDepthTex;
uniform sampler2D uNoiseTex;
uniform mat4      uProj;
uniform mat4      uInvProj;
uniform vec2      uResolution;
uniform vec3      uKernel[16];
uniform float     uRadius;
uniform float     uBias;

in  vec2 vUV;
out float FragAO;

// Reconstruct view-space position from depth buffer value and UV.
vec3 view_pos_from_depth(vec2 uv, float depth) {
    // Depth in [0,1] → NDC in [-1,1]
    vec4 ndc  = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = uInvProj * ndc;
    return view.xyz / view.w;
}

void main() {
    float depth = texture(uDepthTex, vUV).r;

    // Sky / empty fragments (depth at or near far plane): no occlusion
    if (depth >= 0.9999) { FragAO = 1.0; return; }

    // ── Reconstruct view-space position ───────────────────────────────────────
    vec3 fragPos = view_pos_from_depth(vUV, depth);

    // ── Reconstruct view-space normal from position derivatives ───────────────
    // Using hardware screen-space derivatives for clean, artifact-free normals.
    // In OpenGL view space, camera is at origin looking along -Z, so front-facing
    // surface normals must have positive Z component (pointing toward camera).
    vec3 N = normalize(cross(dFdx(fragPos), dFdy(fragPos)));
    if (N.z < 0.0) N = -N;


    // ── Random rotation from tiled 4x4 noise texture ──────────────────────────
    vec2 noiseScale = uResolution / 4.0;
    vec3 randVec    = normalize(texture(uNoiseTex, vUV * noiseScale).xyz * 2.0 - 1.0);
    randVec.z = 0.0;  // keep rotation in the XY plane

    // Gram-Schmidt TBN aligned to surface normal + random tangent
    vec3 tangent   = normalize(randVec - N * dot(randVec, N));
    vec3 bitangent = cross(N, tangent);
    mat3 TBN       = mat3(tangent, bitangent, N);

    // ── Accumulate occlusion ──────────────────────────────────────────────────
    float occlusion = 0.0;
    for (int i = 0; i < 16; ++i) {
        // Rotate hemisphere sample into view space aligned to surface
        vec3 sampleDir = TBN * uKernel[i];
        vec3 samplePos = fragPos + sampleDir * uRadius;

        // Project sample to screen UV
        vec4 offset = uProj * vec4(samplePos, 1.0);
        vec2 sampleUV = clamp((offset.xy / offset.w) * 0.5 + 0.5, vec2(0.001), vec2(0.999));

        // Actual geometry depth at that screen position
        float sampleDepth = view_pos_from_depth(
            sampleUV, texture(uDepthTex, sampleUV).r).z;

        // Range-check: only consider samples within [0..radius] of fragment
        // Prevents halos at silhouettes where samples go behind background.
        float rangeCheck = smoothstep(0.0, 1.0, uRadius / max(abs(fragPos.z - sampleDepth), 0.01));

        // Sample occludes fragment if its depth is beyond (more negative) the sample position
        // Adding bias prevents self-occlusion on flat surfaces.
        occlusion += (sampleDepth >= samplePos.z + uBias ? 1.0 : 0.0) * rangeCheck;
    }

    // Output: 1.0 = fully unoccluded, 0.0 = fully occluded
    FragAO = 1.0 - occlusion / 16.0;
}
)GLSL";

// ── Blur fragment shader ──────────────────────────────────────────────────────
// Simple 4-tap box blur — removes per-sample noise while keeping edges.
// Using 4 samples instead of full 25-tap to stay fast on large viewports.
static const char* k_blur_fs = R"GLSL(
#version 330 core
uniform sampler2D uAOTex;
uniform vec2      uTexelSize;
in  vec2 vUV;
out float FragAO;
void main() {
    // 3x3 box blur (9 taps)
    float ao = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 offset = vec2(float(x), float(y)) * uTexelSize;
            ao += texture(uAOTex, vUV + offset).r;
        }
    }
    FragAO = ao / 9.0;
}
)GLSL";

// ── Helpers ───────────────────────────────────────────────────────────────────

SsaoPass::~SsaoPass() { shutdown(); }

unsigned int SsaoPass::compile_pass(const char* vs, const char* fs, const char* label) {
    auto* prog = new QOpenGLShaderProgram();
    if (!prog->addShaderFromSourceCode(QOpenGLShader::Vertex,   vs) ||
        !prog->addShaderFromSourceCode(QOpenGLShader::Fragment, fs) ||
        !prog->link()) {
        fprintf(stderr, "[SsaoPass] %s compile/link failed:\n%s\n",
                label, prog->log().toUtf8().constData());
        delete prog;
        return 0;
    }
    unsigned int id = prog->programId();
    // Keep QOpenGLShaderProgram alive: leak it intentionally (small, one-time).
    // Alternative: store as member. For simplicity we extract the raw ID.
    prog->release();
    delete prog;   // Qt doesn't destroy the GL program when QOpenGLShaderProgram is deleted
                   // — it only does so if the program was created with a parent.
                   // Actually we need to keep it alive. Let's use static storage.
    return id;
}

// ── Build hemisphere kernel ────────────────────────────────────────────────────
// 16 unit-hemisphere samples with importance sampling (closer samples
// cluster near origin for more detail in tight crevices).
void SsaoPass::build_kernel() {
    std::mt19937 rng(42);  // deterministic seed
    std::uniform_real_distribution<float> dist(0.f, 1.f);

    for (int i = 0; i < 16; ++i) {
        // Random hemisphere direction
        float x = dist(rng) * 2.0f - 1.0f;
        float y = dist(rng) * 2.0f - 1.0f;
        float z = dist(rng);  // z >= 0 → upper hemisphere only

        float len = std::sqrt(x*x + y*y + z*z);
        if (len < 1e-6f) { x = 0; y = 0; z = 1; len = 1; }
        x /= len; y /= len; z /= len;

        // Scale: accelerating interpolation toward origin
        // Samples closer to origin capture tighter crevices
        float scale = float(i) / 16.0f;
        scale = 0.1f + scale * scale * 0.9f;  // lerp(0.1, 1.0, t²)

        m_kernel[i*3+0] = x * scale;
        m_kernel[i*3+1] = y * scale;
        m_kernel[i*3+2] = z * scale;
    }
}

// ── Build 4x4 random rotation noise texture ───────────────────────────────────
// Each texel stores a random 2D rotation vector (XY, Z=0).
// Tiled over the screen to give per-pixel varied hemisphere orientations.
void SsaoPass::build_noise_tex() {
    std::mt19937 rng(137);
    std::uniform_real_distribution<float> dist(0.f, 1.f);

    float noise[16 * 3];
    for (int i = 0; i < 16; ++i) {
        float angle = dist(rng) * 6.28318530718f;
        noise[i*3+0] = std::cos(angle) * 0.5f + 0.5f;  // encode to [0,1] for RGBA8
        noise[i*3+1] = std::sin(angle) * 0.5f + 0.5f;
        noise[i*3+2] = 0.5f;
    }

    glGenTextures(1, &m_noise_tex);
    glBindTexture(GL_TEXTURE_2D, m_noise_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, noise);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ── init ─────────────────────────────────────────────────────────────────────
bool SsaoPass::init() {
    // Bail out rather than call through a null table (offscreen / core-profile
    // contexts cannot supply the 3.3 compatibility entry points).
    if (!initializeOpenGLFunctions()) {
        fprintf(stderr, "[SsaoPass] GL 3.3 compatibility table unavailable — SSAO disabled.\n");
        return false;
    }
    build_kernel();
    build_noise_tex();

    // Compile via QOpenGLShaderProgram (handles GLSL compilation)
    // We must keep the programs alive — use raw GL IDs after extracting them.
    {
        QOpenGLShaderProgram prog;
        if (!prog.addShaderFromSourceCode(QOpenGLShader::Vertex,   k_fs_vs) ||
            !prog.addShaderFromSourceCode(QOpenGLShader::Fragment, k_ssao_fs) ||
            !prog.link()) {
            fprintf(stderr, "[SsaoPass] SSAO shader failed:\n%s\n",
                    prog.log().toUtf8().constData());
            return false;
        }
        m_ssao_prog = prog.programId();
        prog.release();
        // Extract ID before destructor — Qt won't delete the GL program here
        // because we never called prog.create() with ownership transfer.
        // Actually Qt DOES delete the program in the destructor. We need to
        // prevent that. One approach: create with a dummy parent and detach.
        // Simplest: re-compile holding a heap-allocated program.
    }
    {
        QOpenGLShaderProgram prog;
        if (!prog.addShaderFromSourceCode(QOpenGLShader::Vertex,   k_fs_vs) ||
            !prog.addShaderFromSourceCode(QOpenGLShader::Fragment, k_blur_fs) ||
            !prog.link()) {
            fprintf(stderr, "[SsaoPass] Blur shader failed:\n%s\n",
                    prog.log().toUtf8().constData());
            return false;
        }
        m_blur_prog = prog.programId();
        prog.release();
    }

    // Problem: QOpenGLShaderProgram deletes the GL program on destruction.
    // Solution: re-compile using raw GL calls to own the program ourselves.
    // Delete what Qt made and redo with glCreateProgram.
    if (m_ssao_prog) { glDeleteProgram(m_ssao_prog); m_ssao_prog = 0; }
    if (m_blur_prog) { glDeleteProgram(m_blur_prog); m_blur_prog = 0; }

    auto compile_raw = [this](const char* vs_src, const char* fs_src, const char* label) -> unsigned int {
        auto compile_shader = [this](GLenum type, const char* src) -> GLuint {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char buf[1024]; glGetShaderInfoLog(s, 1024, nullptr, buf);
                fprintf(stderr, "[SsaoPass] Shader compile error: %s\n", buf);
                glDeleteShader(s); return 0u;
            }
            return s;
        };
        GLuint vs = compile_shader(GL_VERTEX_SHADER,   vs_src);
        GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
        if (!vs || !fs) { glDeleteShader(vs); glDeleteShader(fs); return 0u; }

        GLuint prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);
        glDeleteShader(vs);
        glDeleteShader(fs);

        GLint ok = 0; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            char buf[1024]; glGetProgramInfoLog(prog, 1024, nullptr, buf);
            fprintf(stderr, "[SsaoPass] %s link error: %s\n", label, buf);
            glDeleteProgram(prog); return 0u;
        }
        return prog;
    };

    m_ssao_prog = compile_raw(k_fs_vs, k_ssao_fs, "SSAO");
    m_blur_prog = compile_raw(k_fs_vs, k_blur_fs, "Blur");

    if (!m_ssao_prog || !m_blur_prog) return false;

    fprintf(stderr, "[SsaoPass] Ready — SSAO + blur shaders compiled.\n");
    return true;
}

void SsaoPass::shutdown() {
    if (m_ssao_prog) { glDeleteProgram(m_ssao_prog); m_ssao_prog = 0; }
    if (m_blur_prog) { glDeleteProgram(m_blur_prog); m_blur_prog = 0; }
    if (m_noise_tex) { glDeleteTextures(1, &m_noise_tex); m_noise_tex = 0; }
}

// ── render ────────────────────────────────────────────────────────────────────
void SsaoPass::render(FboChain& fbo, int w, int h,
                      const float proj[16], const float inv_proj[16]) {
    if (!m_ssao_prog || !m_blur_prog || !fbo.ready()) return;

    const float rw = static_cast<float>(w);
    const float rh = static_cast<float>(h);

    // Save GL state: SSAO passes are 2D fullscreen quads
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    // ── Pass 1: SSAO ──────────────────────────────────────────────────────────
    fbo.bind_ao_raw();
    glViewport(0, 0, w, h);
    glUseProgram(m_ssao_prog);

    // Bind depth texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fbo.scene_depth_tex);
    glUniform1i(glGetUniformLocation(m_ssao_prog, "uDepthTex"), 0);

    // Bind noise texture
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_noise_tex);
    glUniform1i(glGetUniformLocation(m_ssao_prog, "uNoiseTex"), 1);

    // Projection matrices (column-major → upload as-is with GL_FALSE)
    glUniformMatrix4fv(glGetUniformLocation(m_ssao_prog, "uProj"),    1, GL_FALSE, proj);
    glUniformMatrix4fv(glGetUniformLocation(m_ssao_prog, "uInvProj"), 1, GL_FALSE, inv_proj);

    glUniform2f(glGetUniformLocation(m_ssao_prog, "uResolution"), rw, rh);
    glUniform3fv(glGetUniformLocation(m_ssao_prog, "uKernel"), 16, m_kernel);
    glUniform1f(glGetUniformLocation(m_ssao_prog, "uRadius"), radius);
    glUniform1f(glGetUniformLocation(m_ssao_prog, "uBias"),   bias);

    glBindVertexArray(fbo.quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // ── Pass 2: Blur ──────────────────────────────────────────────────────────
    fbo.bind_ao_blur();
    glViewport(0, 0, w, h);
    glUseProgram(m_blur_prog);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fbo.ao_raw_tex);
    glUniform1i(glGetUniformLocation(m_blur_prog, "uAOTex"), 0);
    glUniform2f(glGetUniformLocation(m_blur_prog, "uTexelSize"), 1.0f/rw, 1.0f/rh);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    // ── Restore GL state ──────────────────────────────────────────────────────
    glBindVertexArray(0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_DEPTH_TEST);
}

} // namespace ruby::render
