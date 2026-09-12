#pragma once
// ============================================================================
// fbo_chain.h — Framebuffer Object chain for Ruby GG post-processing.
//
//   Manages three framebuffers:
//     Scene FBO:  RGBA8 color + DEPTH32F — receives all geometry rendering
//     AO FBO:     R8 float — receives raw SSAO output
//     Blur FBO:   R8 float — receives box-blurred AO
//
//   Usage each frame in paintGL():

#include <QOpenGLFunctions_3_3_Compatibility>

//     1. m_fbo_chain.bind_scene()           → clear + render geometry
//     2. m_fbo_chain.unbind(default_fbo)    → restore Qt framebuffer
//     3. m_ssao_pass.render(m_fbo_chain)    → compute AO into fbo_chain.ao_tex
//     4. m_post_pass.render(m_fbo_chain)    → composite to screen
//     5. Draw overlays/gizmos directly      → no AO applied (correct)
//
//   Resizes automatically when the viewport changes size via resize().
// ============================================================================
#include <cstdint>

namespace ruby::render {

class FboChain : protected QOpenGLFunctions_3_3_Compatibility {
public:
    // ── GL texture handles (read by SsaoPass + PostPass) ──────────────────────
    uint32_t scene_color_tex = 0;  // RGBA8  — lit scene color
    uint32_t scene_depth_tex = 0;  // DEPTH32F — scene depth (linear z)
    uint32_t ao_raw_tex      = 0;  // R16F  — raw SSAO factor [0..1]
    uint32_t ao_blur_tex     = 0;  // R16F  — blurred AO factor [0..1]

    // ── FBO handles ───────────────────────────────────────────────────────────
    uint32_t scene_fbo = 0;
    uint32_t ao_raw_fbo= 0;
    uint32_t ao_blur_fbo = 0;

    // ── Fullscreen quad (shared across all post passes) ───────────────────────
    uint32_t quad_vao = 0;
    uint32_t quad_vbo = 0;

    // ── Current dimensions ────────────────────────────────────────────────────
    int m_width  = 0;
    int m_height = 0;

    // Initialise all FBOs + fullscreen quad. Call from initializeGL().
    // Returns false on FBO completeness failure (falls back gracefully).
    bool init(int w, int h);

    // Resize all FBOs to new viewport dimensions. Call from resizeGL().
    void resize(int w, int h);

    // Destroy all GL resources.
    void shutdown();

    // Bind scene FBO — subsequent GL rendering goes here.
    // Clears color + depth.
    void bind_scene();

    // Bind the raw AO FBO — SSAO writes here.
    void bind_ao_raw();

    // Bind the blur FBO — blur pass writes here.
    void bind_ao_blur();

    // Restore the given framebuffer (Qt's defaultFramebufferObject()).
    void unbind(uint32_t default_fbo);

    // True after successful init().
    bool ready() const { return scene_fbo != 0 && quad_vao != 0; }

private:
    void create_fbos(int w, int h);
    void destroy_fbos();
    void create_quad();

    // True once initializeOpenGLFunctions() resolved the 3.3 table. Every
    // public entry point is a no-op until then: a context that cannot supply
    // the compatibility entry points (offscreen / core profile) leaves the
    // table's pointers null, and calling through them segfaults.
    bool m_gl_ready = false;
};

} // namespace ruby::render
