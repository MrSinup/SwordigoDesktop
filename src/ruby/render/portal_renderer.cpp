// ============================================================================
// portal_renderer.cpp — Animated procedural portal vortex renderer
// ============================================================================

#include "portal_renderer.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace ruby::render {

static const char* k_portal_vs = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;

uniform mat4 uMVP;

out vec2 vUV;

void main() {
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)GLSL";

static const char* k_portal_fs = R"GLSL(
#version 330 core
in vec2 vUV;

uniform vec3  uColor;
uniform float uTime;
uniform float uSpeed;

out vec4 FragColor;

void main() {
    vec2 c = vUV - 0.5;
    // Swordigo portal arch aperture is a vertical oval (aspect ratio ~ 1.35 : 1)
    c.y *= 0.74;
    float r = length(c) * 2.0; // 0 at center, 1 at edge
    if (r >= 1.0) discard;

    // Extremely soft, delicate feathering — no harsh edges, no solid blob center
    float radial = 1.0 - smoothstep(0.0, 1.0, r);
    float glow = pow(radial, 1.6);

    // Very subtle, gentle ethereal shimmer (low contrast, slow wave)
    float theta = atan(c.y, c.x);
    float wave = sin(r * 8.0 - uTime * uSpeed * 2.0 + theta * 2.0) * 0.06;
    glow = clamp(glow + wave * (1.0 - r), 0.0, 1.0);

    // Ethereal cyan-tinted mystical tint (soft, translucent)
    vec3 tint = mix(uColor, vec3(0.70, 0.85, 1.0), 0.45);
    vec3 col = tint * glow;

    // Soft, delicate translucent alpha (max 0.22 at the very center, feathering smoothly to 0)
    float alpha = glow * 0.22;
    FragColor = vec4(col, alpha);
}
)GLSL";

static void mult_mat4(float out[16], const float a[16], const float b[16]) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
                sum += a[k * 4 + row] * b[col * 4 + k];
            out[col * 4 + row] = sum;
        }
    }
}

PortalRenderer::~PortalRenderer() {
    shutdown();
}

bool PortalRenderer::init() {
    // Bail out rather than call through a null table (offscreen / core-profile
    // contexts cannot supply the 3.3 compatibility entry points).
    if (!initializeOpenGLFunctions()) {
        fprintf(stderr, "[PortalRenderer] GL 3.3 compatibility table unavailable — portal effects disabled.\n");
        return false;
    }

    auto compile_shader = [this](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char buf[512];
            glGetShaderInfoLog(s, sizeof(buf), nullptr, buf);
            fprintf(stderr, "[PortalRenderer] Shader error: %s\n", buf);
            glDeleteShader(s);
            return 0u;
        }
        return s;
    };

    GLuint vs = compile_shader(GL_VERTEX_SHADER, k_portal_vs);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, k_portal_fs);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    m_prog = glCreateProgram();
    glAttachShader(m_prog, vs);
    glAttachShader(m_prog, fs);
    glLinkProgram(m_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(m_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetProgramInfoLog(m_prog, sizeof(buf), nullptr, buf);
        fprintf(stderr, "[PortalRenderer] Program link error: %s\n", buf);
        glDeleteProgram(m_prog);
        m_prog = 0;
        return false;
    }

    m_loc_mvp   = glGetUniformLocation(m_prog, "uMVP");
    m_loc_color = glGetUniformLocation(m_prog, "uColor");
    m_loc_time  = glGetUniformLocation(m_prog, "uTime");
    m_loc_speed = glGetUniformLocation(m_prog, "uSpeed");

    // Unit billboard quad (-1..1), UVs (0..1)
    static const float k_verts[] = {
        -1.0f, -1.0f, 0.0f,  0.0f, 1.0f,
         1.0f, -1.0f, 0.0f,  1.0f, 1.0f,
         1.0f,  1.0f, 0.0f,  1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f,  0.0f, 0.0f,
    };
    static const uint16_t k_idx[] = { 0, 1, 2, 0, 2, 3 };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(k_verts), k_verts, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(k_idx), k_idx, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    return true;
}

void PortalRenderer::shutdown() {
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_ebo) { glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
    if (m_prog) { glDeleteProgram(m_prog); m_prog = 0; }
}

void PortalRenderer::render(const std::vector<av::SceneObject>& objects,
                            const float view[16],
                            const float view_proj[16],
                            float time_sec) {
    if (!m_prog || !m_vao || objects.empty()) return;

    // Derive camera right and up basis vectors from view matrix
    const float right[3] = { view[0], view[4], view[8]  };
    const float up[3]    = { view[1], view[5], view[9]  };

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); // Subtle luminous aperture glow
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    glUseProgram(m_prog);
    glUniform1f(m_loc_time, time_sec);
    glBindVertexArray(m_vao);

    for (const auto& pobj : objects) {
        if (pobj.hidden) continue;

        // Non-portal frame portals are scene-edge based transition triggers with NO 3D mesh.
        // ONLY portals with the actual portal model/mesh have visual effects!
        if (pobj.mesh_name.empty()) continue;

        std::string low_mesh = pobj.mesh_name;
        for (char& c : low_mesh) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string low_tpl = pobj.template_name;
        for (char& c : low_tpl) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (low_mesh.find("portal") == std::string::npos && low_tpl.find("portal") == std::string::npos) {
            continue;
        }

        const auto& comps = pobj.resolved_components.empty()
                                ? pobj.components
                                : pobj.resolved_components;

        float color[3] = {0.60f, 0.40f, 0.95f}; // Soft Swordigo portal violet
        float speed = 1.0f;

        for (const auto& c : comps) {
            const bool is_effect_by_name = c.type_name.find("PortalEffect") != std::string::npos;
            const bool is_effect_by_tag = (c.payload_field == 4058) || (c.type_id == 4058);
            if (!is_effect_by_name && !is_effect_by_tag) continue;

            for (const auto& f : av::scene_component_fields(c)) {
                if (f.name == "Color" && f.bytes_value.size() >= 12) {
                    float rgb[3];
                    std::memcpy(rgb, f.bytes_value.data(), 12);
                    for (int k = 0; k < 3; ++k)
                        if (std::isfinite(rgb[k])) color[k] = std::clamp(rgb[k], 0.0f, 1.0f);
                } else if (f.name == "Speed" && f.bytes_value.size() >= 4) {
                    float sx = 0.0f;
                    std::memcpy(&sx, f.bytes_value.data(), 4);
                    if (std::isfinite(sx) && sx != 0.0f) speed = std::fabs(sx);
                }
            }
        }

        // Aperture glow sized to fit neatly inside the stone archway opening
        const float base_sz = 40.0f;
        const float scale = std::max(0.4f, std::fabs(pobj.scale_x));
        const float h = base_sz * scale * 0.5f;

        const float pos[3] = { pobj.pos_x, pobj.pos_y + 27.0f * scale, pobj.pos_z };

        // Construct camera-facing billboard transform
        float M[16];
        M[0] = right[0] * h; M[4] = up[0] * h; M[8]  = 0.0f; M[12] = pos[0];
        M[1] = right[1] * h; M[5] = up[1] * h; M[9]  = 0.0f; M[13] = pos[1];
        M[2] = right[2] * h; M[6] = up[2] * h; M[10] = 0.0f; M[14] = pos[2];
        M[3] = 0.0f;         M[7] = 0.0f;         M[11] = 0.0f; M[15] = 1.0f;

        float mvp[16];
        mult_mat4(mvp, view_proj, M);

        glUniformMatrix4fv(m_loc_mvp, 1, GL_FALSE, mvp);
        glUniform3fv(m_loc_color, 1, color);
        glUniform1f(m_loc_speed, speed > 0.0f ? speed : 1.0f);

        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
    }

    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(0);
}

} // namespace ruby::render
