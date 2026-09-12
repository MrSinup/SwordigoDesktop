// ============================================================================
// fbo_chain.cpp — FBO chain implementation
// ============================================================================
#include "fbo_chain.h"
#include <cstdio>

namespace ruby::render {

// ── Quad vertices: NDC xy + UV ────────────────────────────────────────────────
static const float k_quad_verts[] = {
    // NDC xy     UV
    -1.f, -1.f,  0.f, 0.f,
     1.f, -1.f,  1.f, 0.f,
     1.f,  1.f,  1.f, 1.f,
    -1.f, -1.f,  0.f, 0.f,
     1.f,  1.f,  1.f, 1.f,
    -1.f,  1.f,  0.f, 1.f,
};


// ── FboChain::create_fbos ─────────────────────────────────────────────────────
void FboChain::create_fbos(int w, int h) {
    m_width  = w;
    m_height = h;

    // ── Scene FBO: RGBA8 color + DEPTH32F ─────────────────────────────────────
    glGenTextures(1, &scene_color_tex);
    glBindTexture(GL_TEXTURE_2D, scene_color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &scene_depth_tex);
    glBindTexture(GL_TEXTURE_2D, scene_depth_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &scene_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scene_color_tex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,  GL_TEXTURE_2D, scene_depth_tex, 0);
    { GLenum s = glCheckFramebufferStatus(GL_FRAMEBUFFER); if (s != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "[FboChain] Scene FBO incomplete: 0x%X\n", s); }

    // ── AO raw FBO: R16F ──────────────────────────────────────────────────────
    glGenTextures(1, &ao_raw_tex);
    glBindTexture(GL_TEXTURE_2D, ao_raw_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, w, h, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &ao_raw_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, ao_raw_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ao_raw_tex, 0);
    { GLenum s = glCheckFramebufferStatus(GL_FRAMEBUFFER); if (s != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "[FboChain] AO-raw FBO incomplete: 0x%X\n", s); }

    // ── AO blur FBO: R16F ─────────────────────────────────────────────────────
    glGenTextures(1, &ao_blur_tex);
    glBindTexture(GL_TEXTURE_2D, ao_blur_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, w, h, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &ao_blur_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, ao_blur_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ao_blur_tex, 0);
    { GLenum s = glCheckFramebufferStatus(GL_FRAMEBUFFER); if (s != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "[FboChain] AO-blur FBO incomplete: 0x%X\n", s); }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ── FboChain::destroy_fbos ────────────────────────────────────────────────────
void FboChain::destroy_fbos() {
    if (scene_fbo)    { glDeleteFramebuffers(1, &scene_fbo);    scene_fbo    = 0; }
    if (ao_raw_fbo)   { glDeleteFramebuffers(1, &ao_raw_fbo);   ao_raw_fbo   = 0; }
    if (ao_blur_fbo)  { glDeleteFramebuffers(1, &ao_blur_fbo);  ao_blur_fbo  = 0; }
    if (scene_color_tex) { glDeleteTextures(1, &scene_color_tex); scene_color_tex = 0; }
    if (scene_depth_tex) { glDeleteTextures(1, &scene_depth_tex); scene_depth_tex = 0; }
    if (ao_raw_tex)      { glDeleteTextures(1, &ao_raw_tex);      ao_raw_tex      = 0; }
    if (ao_blur_tex)     { glDeleteTextures(1, &ao_blur_tex);     ao_blur_tex     = 0; }
}

// ── FboChain::create_quad ─────────────────────────────────────────────────────
void FboChain::create_quad() {
    glGenVertexArrays(1, &quad_vao);
    glGenBuffers(1, &quad_vbo);
    glBindVertexArray(quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(k_quad_verts), k_quad_verts, GL_STATIC_DRAW);
    // aPos: location 0, 2 floats
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    // aUV:  location 1, 2 floats
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// ── Public API ────────────────────────────────────────────────────────────────

bool FboChain::init(int w, int h) {
    // Check the table resolved BEFORE calling through it: offscreen and
    // core-profile contexts cannot supply the 3.3 compatibility entry points,
    // which leaves the pointers null and segfaults on first use.
    if (!initializeOpenGLFunctions()) {
        fprintf(stderr, "[FboChain] GL 3.3 compatibility table unavailable — post-processing disabled.\n");
        return false;
    }
    m_gl_ready = true;
    create_quad();
    create_fbos(w, h);
    if (!ready()) {
        fprintf(stderr, "[FboChain] Init failed — post-processing disabled.\n");
        return false;
    }
    fprintf(stderr, "[FboChain] Ready at %dx%d.\n", w, h);
    return true;
}

void FboChain::resize(int w, int h) {
    if (!m_gl_ready) return;
    if (w == m_width && h == m_height) return;
    destroy_fbos();
    create_fbos(w, h);
}

void FboChain::shutdown() {
    if (!m_gl_ready) return;
    destroy_fbos();
    if (quad_vao) { glDeleteVertexArrays(1, &quad_vao); quad_vao = 0; }
    if (quad_vbo) { glDeleteBuffers(1, &quad_vbo);      quad_vbo = 0; }
    m_gl_ready = false;
}

// When the chain is not ready these deliberately do nothing: rendering falls
// back to the default framebuffer, which is the pre-post-processing behaviour.
void FboChain::bind_scene() {
    if (!m_gl_ready) return;
    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void FboChain::bind_ao_raw() {
    if (!m_gl_ready) return;
    glBindFramebuffer(GL_FRAMEBUFFER, ao_raw_fbo);
    glClear(GL_COLOR_BUFFER_BIT);
}

void FboChain::bind_ao_blur() {
    if (!m_gl_ready) return;
    glBindFramebuffer(GL_FRAMEBUFFER, ao_blur_fbo);
    glClear(GL_COLOR_BUFFER_BIT);
}

void FboChain::unbind(uint32_t default_fbo) {
    if (!m_gl_ready) return;
    glBindFramebuffer(GL_FRAMEBUFFER, default_fbo);
}

} // namespace ruby::render
