#pragma once
// ============================================================================
// ssao_pass.h — Screen-Space Ambient Occlusion for Ruby GG viewport.
// ============================================================================

#include <QOpenGLFunctions_3_3_Compatibility>

namespace ruby::render {
class FboChain;
}

namespace ruby::render {

class SsaoPass : protected QOpenGLFunctions_3_3_Compatibility {
public:
    SsaoPass() = default;
    ~SsaoPass();

    // Init shaders + kernel + noise texture. Call from initializeGL().
    bool init();

    // Destroy GL resources.
    void shutdown();

    // Run SSAO → AO blur passes. Writes into fbo.ao_blur_tex.
    // proj[16]: column-major projection matrix (for reconstructing positions).
    // inv_proj[16]: its inverse (pre-computed by caller).
    void render(FboChain& fbo, int width, int height,
                const float proj[16], const float inv_proj[16]);

    // AO tuning — may be exposed to the lighting panel
    float radius   = 5.0f;   // hemisphere radius in view-space units
    float bias     = 0.05f;  // depth bias to avoid self-occlusion acne
    float strength = 0.85f;  // how strongly AO darkens (0=off, 1=full)

    bool ready() const { return m_ssao_prog != 0; }

private:
    unsigned int m_ssao_prog = 0;
    unsigned int m_blur_prog = 0;
    unsigned int m_noise_tex = 0;

    // Hemisphere kernel: 16 samples in unit hemisphere
    float m_kernel[16 * 3]; // 16 vec3s

    void build_kernel();
    void build_noise_tex();
    unsigned int compile_pass(const char* vs, const char* fs, const char* label);
};

} // namespace ruby::render
