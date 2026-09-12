#pragma once
// ============================================================================
// water_renderer.h — Animated fluid sheet renderer for Ruby GG
//   Renders dynamic water surfaces matching the Swordigo visual pipeline and
//   web editor specifications: dual sine wave displacement, front and depth
//   surface gradients, scrolling texture coordinates, and depth testing.
// ============================================================================

#include <QOpenGLFunctions_3_3_Compatibility>
#include <vector>
#include <string>
#include <unordered_map>
#include "tools/scene_loader.h"

namespace ruby::render {

class WaterRenderer : protected QOpenGLFunctions_3_3_Compatibility {
public:
    WaterRenderer() = default;
    ~WaterRenderer();

    // Non-copyable (owns GL resources)
    WaterRenderer(const WaterRenderer&) = delete;
    WaterRenderer& operator=(const WaterRenderer&) = delete;

    /// Compile GLSL 330 shaders and initialize dynamic buffers. Call in initializeGL().
    bool init();

    /// Destroy GL resources.
    void shutdown();

    /// Render all active water sheets in the scene with animated waves.
    void render(const std::vector<av::SceneData::SceneWater>& waters,
                const std::vector<av::SceneObject>& objects,
                const std::unordered_map<std::string, GLuint>& tex_cache,
                const float view_proj[16],
                float time_sec);

    bool ready() const { return m_prog != 0; }

private:
    GLuint m_prog = 0;
    GLuint m_vao  = 0;
    GLuint m_vbo  = 0;
    GLuint m_ebo  = 0;

    GLint m_loc_mvp          = -1;
    GLint m_loc_texture      = -1;
    GLint m_loc_has_tex      = -1;
    GLint m_loc_front_color  = -1;
    GLint m_loc_surface_color= -1;
    GLint m_loc_scroll       = -1;
    GLint m_loc_time         = -1;
    GLint m_loc_is_back_face = -1;
};

} // namespace ruby::render
