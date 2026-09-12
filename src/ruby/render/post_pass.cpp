// ============================================================================
// post_pass.cpp — Final composite pass
// ============================================================================
#include "post_pass.h"
#include "fbo_chain.h"

#include <cstdio>

namespace ruby::render {

static const char* k_composite_vs = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() { vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }
)GLSL";

// AO composite: darken scene color by SSAO factor.
// ao_factor = mix(1.0, ao, strength) → 1.0 in fully lit areas, <1.0 in crevices.
// We apply a slight contrast lift to the AO so it doesn't look too linear:
//   pow(ao, 2.0) makes it darker in corners but keeps open areas bright.
static const char* k_composite_fs = R"GLSL(
#version 330 core
uniform sampler2D uSceneTex;
uniform sampler2D uAOTex;
uniform float     uAOStrength;
in  vec2 vUV;
out vec4 FragColor;
void main() {
    vec3  color = texture(uSceneTex, vUV).rgb;
    float ao    = texture(uAOTex,    vUV).r;

    // Gentle AO application: brings out crevices without crushing the scene to black.
    // Even maximum occlusion in tight corners stays above 0.55 brightness.
    float factor = mix(1.0, max(ao, 0.55), clamp(uAOStrength, 0.0, 1.0));

    FragColor = vec4(color * factor, 1.0);
}
)GLSL";


PostPass::~PostPass() { shutdown(); }

bool PostPass::init() {
    // Bail out rather than call through a null table (offscreen / core-profile
    // contexts cannot supply the 3.3 compatibility entry points).
    if (!initializeOpenGLFunctions()) {
        fprintf(stderr, "[PostPass] GL 3.3 compatibility table unavailable — composite disabled.\n");
        return false;
    }

    // Compile with raw GL (same approach as SsaoPass — avoids Qt ownership issues)
    auto compile_shader = [this](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char buf[512]; glGetShaderInfoLog(s, 512, nullptr, buf);
            fprintf(stderr, "[PostPass] Shader error: %s\n", buf);
            glDeleteShader(s); return 0u;
        }
        return s;
    };

    GLuint vs = compile_shader(GL_VERTEX_SHADER,   k_composite_vs);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, k_composite_fs);
    if (!vs || !fs) { glDeleteShader(vs); glDeleteShader(fs); return false; }

    m_prog = glCreateProgram();
    glAttachShader(m_prog, vs);
    glAttachShader(m_prog, fs);
    glLinkProgram(m_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0; glGetProgramiv(m_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetProgramInfoLog(m_prog, 512, nullptr, buf);
        fprintf(stderr, "[PostPass] Link error: %s\n", buf);
        glDeleteProgram(m_prog); m_prog = 0; return false;
    }

    fprintf(stderr, "[PostPass] Ready — AO composite shader.\n");
    return true;
}

void PostPass::shutdown() {
    if (m_prog) { glDeleteProgram(m_prog); m_prog = 0; }
}

void PostPass::render(FboChain& fbo, float ao_strength) {
    if (!m_prog || !fbo.ready()) return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    glUseProgram(m_prog);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fbo.scene_color_tex);
    glUniform1i(glGetUniformLocation(m_prog, "uSceneTex"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, fbo.ao_blur_tex);
    glUniform1i(glGetUniformLocation(m_prog, "uAOTex"), 1);

    glUniform1f(glGetUniformLocation(m_prog, "uAOStrength"), ao_strength);

    glBindVertexArray(fbo.quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glEnable(GL_DEPTH_TEST);
}

} // namespace ruby::render
