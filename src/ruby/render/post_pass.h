#pragma once
// ============================================================================
// post_pass.h — Final composite pass for Ruby GG viewport.
// ============================================================================

#include <QOpenGLFunctions_3_3_Compatibility>

namespace ruby::render {
class FboChain;
}

namespace ruby::render {

class PostPass : protected QOpenGLFunctions_3_3_Compatibility {
public:
    PostPass() = default;
    ~PostPass();

    // Init composite shader. Call from initializeGL().
    bool init();

    // Destroy GL resources.
    void shutdown();

    // Blit scene × AO to the currently bound framebuffer (Qt's default FBO).
    // ao_strength: 0 = no AO visible, 1 = full SSAO effect.
    void render(FboChain& fbo, float ao_strength = 0.80f);

    bool ready() const { return m_prog != 0; }

private:
    unsigned int m_prog = 0;
};

} // namespace ruby::render
