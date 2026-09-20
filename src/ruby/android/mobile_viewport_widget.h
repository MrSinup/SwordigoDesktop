#pragma once
// ============================================================================
// mobile_viewport_widget.h — Native Mobile 3D OpenGL ES 3.0 Viewport
//   Mobile-first, touch-optimized 3D scene & model editor for Ruby GG.
//   Includes embedded D-Pad HUD, multi-touch gestures (orbit, pinch-zoom, pan),
//   enlarged Gizmo touch hit-testing, and sliding inspection drawers.
// ============================================================================

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QMatrix4x4>
#include <QUndoStack>
#include <QElapsedTimer>
#include <memory>
#include <vector>
#include <string>

#include "tools/scene_loader.h"
#include "tools/pod_loader.h"
#include "ruby/viewport/ruby_gizmo.h"
#include "ruby/viewport/ruby_picking.h"

namespace ruby::android {

class MobileDPadWidget;
class MobileHierarchyDrawer;
class MobileInspectorDrawer;

class MobileViewportWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    enum class DisplayMode { Textured, Wireframe };
    enum class GizmoMode { Off, Move, Rotate, Scale };

    explicit MobileViewportWidget(QWidget* parent = nullptr);
    ~MobileViewportWidget() override;

    bool load_scene(const std::string& scene_path);
    bool load_model(const std::string& pod_path);
    bool save_current_scene();

    const std::string& current_file_path() const { return m_file_path; }
    bool is_dirty() const { return m_is_dirty; }
    void mark_dirty(bool dirty = true);

    void reset_camera();
    void focus_object(int index);

    QUndoStack* undo_stack() { return &m_undo_stack; }

signals:
    void backToHubRequested();
    void switchToCodeRequested(const QString& file_path);
    void fileDirtyStateChanged(bool dirty);
    void statusMessage(const QString& message);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void resizeEvent(QResizeEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private slots:
    void on_dpad_pan(float dx, float dy);
    void on_dpad_dolly(float forward, float strafe);
    void on_scene_object_selected(int index);
    void on_scene_object_transformed(int object_index,
                                    float px, float py, float pz,
                                    float rx, float rz, float ry,
                                    float sx, float sy, float sz);

private:
    void setup_hud_overlay();
    void layout_hud_elements();
    void update_matrices();
    void draw_scene_objects();
    void draw_grid();
    void draw_gizmo();

    // Scene & Data
    std::string m_file_path;
    bool m_is_scene = false;
    bool m_is_model = false;
    bool m_is_dirty = false;
    av::SceneData m_scene;
    av::PODModel m_model;
    int m_selected_object = -1;

    // Viewport camera parameters
    float m_cam_pitch = 20.0f;
    float m_cam_yaw   = -45.0f;
    float m_cam_dist  = 120.0f;
    float m_cam_target[3] = {0.0f, 15.0f, 0.0f};

    QMatrix4x4 m_view_mat;
    QMatrix4x4 m_proj_mat;

    // GLES 3.0 Shaders
    std::unique_ptr<QOpenGLShaderProgram> m_shader;
    std::unique_ptr<QOpenGLShaderProgram> m_unlit_shader;

    // Display & Gizmo modes
    DisplayMode m_display_mode = DisplayMode::Textured;
    GizmoMode m_gizmo_mode = GizmoMode::Move;
    ruby::gizmo::RubyGizmo m_gizmo;
    QUndoStack m_undo_stack;

    // Touch navigation tracking
    bool m_is_orbiting = false;
    bool m_is_gizmo_dragging = false;
    QPointF m_last_touch_pos;
    QPointF m_touch_press_pos;

    // HUD controls
    MobileDPadWidget* m_dpad = nullptr;
    QWidget* m_top_hud = nullptr;
    QWidget* m_right_action_hud = nullptr;
    MobileHierarchyDrawer* m_hierarchy_drawer = nullptr;
    MobileInspectorDrawer* m_inspector_drawer = nullptr;
};

} // namespace ruby::android
