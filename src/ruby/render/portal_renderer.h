#pragma once
// ============================================================================
// portal_renderer.h — Animated procedural portal vortex renderer
//   Renders glowing swirling portals with dual animated spiral arms and
//   camera-facing billboard alignment.
// ============================================================================

#include <QOpenGLFunctions_3_3_Compatibility>
#include <vector>
#include "tools/scene_loader.h"

namespace ruby::render {

class PortalRenderer : protected QOpenGLFunctions_3_3_Compatibility {
public:
    PortalRenderer() = default;
    ~PortalRenderer();

    PortalRenderer(const PortalRenderer&) = delete;
    PortalRenderer& operator=(const PortalRenderer&) = delete;

    /// Compile portal vortex shaders and init quad VAO. Call in initializeGL().
    bool init();

    /// Destroy GL resources.
    void shutdown();

    /// Render portal vortices for all portal objects in the scene.
    void render(const std::vector<av::SceneObject>& objects,
                const float view[16],
                const float view_proj[16],
                float time_sec);

    bool ready() const { return m_prog != 0; }

private:
    GLuint m_prog = 0;
    GLuint m_vao  = 0;
    GLuint m_vbo  = 0;
    GLuint m_ebo  = 0;

    GLint m_loc_mvp   = -1;
    GLint m_loc_color = -1;
    GLint m_loc_time  = -1;
    GLint m_loc_speed = -1;
};

} // namespace ruby::render
