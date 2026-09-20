// ============================================================================
// mobile_viewport_widget.cpp — Native Mobile 3D OpenGL ES 3.0 Viewport
// ============================================================================

#include "mobile_viewport_widget.h"
#include "mobile_dpad_widget.h"
#include "mobile_hierarchy_drawer.h"
#include "mobile_inspector_drawer.h"
#include "gles_shaders.h"

#include <QMouseEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSaveFile>
#include <cmath>

namespace swk {
void object_world_matrix(const av::SceneObject& obj, float out[16]) {
    QMatrix4x4 m;
    m.translate(obj.pos_x, obj.pos_y, obj.pos_z);
    m.rotate(obj.rot_x * 180.0f / M_PI, 1.0f, 0.0f, 0.0f);
    m.rotate(obj.rot_z * 180.0f / M_PI, 0.0f, 1.0f, 0.0f);
    m.rotate(obj.rot_y * 180.0f / M_PI, 0.0f, 0.0f, 1.0f);
    m.scale(obj.scale_x * obj.template_scaling,
            obj.scale_y * obj.template_scaling,
            obj.scale_z * obj.template_scaling);
    std::memcpy(out, m.constData(), 16 * sizeof(float));
}
} // namespace swk

namespace ruby::android {

MobileViewportWidget::MobileViewportWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setup_hud_overlay();
}

MobileViewportWidget::~MobileViewportWidget() {
    makeCurrent();
    m_shader.reset();
    m_unlit_shader.reset();
    doneCurrent();
}

void MobileViewportWidget::setup_hud_overlay() {
    // 1. Embedded Virtual D-Pad (Anchored bottom-left)
    m_dpad = new MobileDPadWidget(this);
    connect(m_dpad, &MobileDPadWidget::panRequested, this, &MobileViewportWidget::on_dpad_pan);
    connect(m_dpad, &MobileDPadWidget::dollyRequested, this, &MobileViewportWidget::on_dpad_dolly);

    // 2. Top Minimal HUD Bar
    m_top_hud = new QWidget(this);
    m_top_hud->setStyleSheet(
        QStringLiteral("background: rgba(14, 16, 22, 0.85); border-bottom: 1px solid #282E40; border-radius: 8px;"));
    auto* top_layout = new QHBoxLayout(m_top_hud);
    top_layout->setContentsMargins(10, 6, 10, 6);
    top_layout->setSpacing(8);

    auto make_hud_btn = [this](const QString& text, const QString& bg, int width = 65) {
        auto* b = new QPushButton(text, m_top_hud);
        b->setFixedHeight(34);
        b->setFixedWidth(width);
        b->setStyleSheet(QString(
            "background: %1; color: #FFFFFF; border: 1px solid rgba(255,255,255,0.15); "
            "border-radius: 6px; font-size: 11px; font-weight: bold;"
        ).arg(bg));
        return b;
    };

    auto* btn_back = make_hud_btn(QStringLiteral("◀ Hub"), QStringLiteral("#242838"), 60);
    auto* lbl_file = new QLabel(QStringLiteral("No Scene Loaded"), m_top_hud);
    lbl_file->setStyleSheet(QStringLiteral("color: #E2E8F5; font-size: 13px; font-weight: bold; padding: 0 8px;"));

    auto* btn_undo = make_hud_btn(QStringLiteral("↶"), QStringLiteral("#202535"), 40);
    auto* btn_redo = make_hud_btn(QStringLiteral("↷"), QStringLiteral("#202535"), 40);
    auto* btn_hie  = make_hud_btn(QStringLiteral("Hierarchy"), QStringLiteral("#253D5C"), 75);
    auto* btn_insp = make_hud_btn(QStringLiteral("Inspector"), QStringLiteral("#352A50"), 75);
    auto* btn_code = make_hud_btn(QStringLiteral("Code"), QStringLiteral("#1F4B3C"), 55);
    auto* btn_save = make_hud_btn(QStringLiteral("Save"), QStringLiteral("#2B6095"), 70);

    top_layout->addWidget(btn_back);
    top_layout->addWidget(lbl_file, 1);
    top_layout->addWidget(btn_undo);
    top_layout->addWidget(btn_redo);
    top_layout->addWidget(btn_hie);
    top_layout->addWidget(btn_insp);
    top_layout->addWidget(btn_code);
    top_layout->addWidget(btn_save);

    connect(btn_back, &QPushButton::clicked, this, &MobileViewportWidget::backToHubRequested);
    connect(btn_undo, &QPushButton::clicked, &m_undo_stack, &QUndoStack::undo);
    connect(btn_redo, &QPushButton::clicked, &m_undo_stack, &QUndoStack::redo);
    connect(btn_save, &QPushButton::clicked, this, &MobileViewportWidget::save_current_scene);
    connect(btn_code, &QPushButton::clicked, this, [this]() {
        emit switchToCodeRequested(QString::fromStdString(m_file_path));
    });

    // 3. Right Action Cluster HUD
    m_right_action_hud = new QWidget(this);
    m_right_action_hud->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* right_layout = new QVBoxLayout(m_right_action_hud);
    right_layout->setContentsMargins(0, 0, 0, 0);
    right_layout->setSpacing(8);

    auto make_action_btn = [this](const QString& text, const QString& bg) {
        auto* b = new QPushButton(text, m_right_action_hud);
        b->setFixedSize(48, 48);
        b->setStyleSheet(QString(
            "background: %1; color: #FFFFFF; border: 1px solid rgba(255,255,255,0.2); "
            "border-radius: 24px; font-size: 13px; font-weight: bold;"
        ).arg(bg));
        return b;
    };

    auto* btn_focus = make_action_btn(QStringLiteral("⌖"), QStringLiteral("rgba(30, 40, 60, 0.8)"));
    auto* btn_zoom_in = make_action_btn(QStringLiteral("+"), QStringLiteral("rgba(30, 40, 60, 0.8)"));
    auto* btn_zoom_out = make_action_btn(QStringLiteral("−"), QStringLiteral("rgba(30, 40, 60, 0.8)"));
    auto* btn_gizmo = make_action_btn(QStringLiteral("MOV"), QStringLiteral("rgba(40, 75, 120, 0.85)"));

    right_layout->addWidget(btn_focus);
    right_layout->addWidget(btn_zoom_in);
    right_layout->addWidget(btn_zoom_out);
    right_layout->addWidget(btn_gizmo);

    connect(btn_focus, &QPushButton::clicked, this, [this]() { focus_object(m_selected_object); });
    connect(btn_zoom_in, &QPushButton::clicked, this, [this]() {
        m_cam_dist = std::max(10.0f, m_cam_dist * 0.8f);
        update();
    });
    connect(btn_zoom_out, &QPushButton::clicked, this, [this]() {
        m_cam_dist = std::min(1500.0f, m_cam_dist * 1.25f);
        update();
    });
    connect(btn_gizmo, &QPushButton::clicked, this, [this, btn_gizmo]() {
        if (m_gizmo_mode == GizmoMode::Move) {
            m_gizmo_mode = GizmoMode::Rotate;
            btn_gizmo->setText(QStringLiteral("ROT"));
        } else if (m_gizmo_mode == GizmoMode::Rotate) {
            m_gizmo_mode = GizmoMode::Scale;
            btn_gizmo->setText(QStringLiteral("SCL"));
        } else if (m_gizmo_mode == GizmoMode::Scale) {
            m_gizmo_mode = GizmoMode::Off;
            btn_gizmo->setText(QStringLiteral("OFF"));
        } else {
            m_gizmo_mode = GizmoMode::Move;
            btn_gizmo->setText(QStringLiteral("MOV"));
        }
        update();
    });

    // 4. Drawers
    m_hierarchy_drawer = new MobileHierarchyDrawer(this);
    m_hierarchy_drawer->hide();
    connect(btn_hie, &QPushButton::clicked, this, [this]() {
        m_hierarchy_drawer->setVisible(!m_hierarchy_drawer->isVisible());
        if (m_hierarchy_drawer->isVisible()) m_inspector_drawer->hide();
        layout_hud_elements();
    });
    connect(m_hierarchy_drawer, &MobileHierarchyDrawer::closeRequested, m_hierarchy_drawer, &QWidget::hide);
    connect(m_hierarchy_drawer, &MobileHierarchyDrawer::objectSelected, this, &MobileViewportWidget::on_scene_object_selected);
    connect(m_hierarchy_drawer, &MobileHierarchyDrawer::objectFocusRequested, this, &MobileViewportWidget::focus_object);

    m_inspector_drawer = new MobileInspectorDrawer(this);
    m_inspector_drawer->hide();
    connect(btn_insp, &QPushButton::clicked, this, [this]() {
        m_inspector_drawer->setVisible(!m_inspector_drawer->isVisible());
        if (m_inspector_drawer->isVisible()) m_hierarchy_drawer->hide();
        layout_hud_elements();
    });
    connect(m_inspector_drawer, &MobileInspectorDrawer::closeRequested, m_inspector_drawer, &QWidget::hide);
    connect(m_inspector_drawer, &MobileInspectorDrawer::transformChanged, this, &MobileViewportWidget::on_scene_object_transformed);
}

void MobileViewportWidget::layout_hud_elements() {
    const int w = width();
    const int h = height();

    // Top HUD Bar (centered horizontally, with margin)
    const int top_w = std::min(w - 24, 720);
    m_top_hud->setGeometry((w - top_w) / 2, 10, top_w, 48);

    // Virtual D-Pad (bottom-left margin)
    m_dpad->move(16, h - m_dpad->height() - 16);

    // Right Action Cluster (bottom-right margin)
    m_right_action_hud->move(w - m_right_action_hud->width() - 18, h - 230);

    // Drawers (aligned to right edge)
    m_hierarchy_drawer->setGeometry(w - m_hierarchy_drawer->width(), 0, m_hierarchy_drawer->width(), h);
    m_inspector_drawer->setGeometry(w - m_inspector_drawer->width(), 0, m_inspector_drawer->width(), h);
}

void MobileViewportWidget::resizeEvent(QResizeEvent* event) {
    QOpenGLWidget::resizeEvent(event);
    layout_hud_elements();
}

void MobileViewportWidget::initializeGL() {
    initializeOpenGLFunctions();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    // Initialize GLES 3.0 Lit Shader
    m_shader = std::make_unique<QOpenGLShaderProgram>(this);
    m_shader->addShaderFromSourceCode(QOpenGLShader::Vertex, GLESShaders::vertex_shader_source());
    m_shader->addShaderFromSourceCode(QOpenGLShader::Fragment, GLESShaders::fragment_shader_source());
    m_shader->link();

    // Initialize GLES 3.0 Unlit / Gizmo Shader
    m_unlit_shader = std::make_unique<QOpenGLShaderProgram>(this);
    m_unlit_shader->addShaderFromSourceCode(QOpenGLShader::Vertex, GLESShaders::unlit_vertex_shader_source());
    m_unlit_shader->addShaderFromSourceCode(QOpenGLShader::Fragment, GLESShaders::unlit_fragment_shader_source());
    m_unlit_shader->link();
}

void MobileViewportWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    update_matrices();
}

void MobileViewportWidget::update_matrices() {
    const float aspect = static_cast<float>(width()) / std::max(1.0f, static_cast<float>(height()));
    m_proj_mat.setToIdentity();
    m_proj_mat.perspective(45.0f, aspect, 1.0f, 4000.0f);

    m_view_mat.setToIdentity();
    m_view_mat.translate(0.0f, 0.0f, -m_cam_dist);
    m_view_mat.rotate(m_cam_pitch, 1.0f, 0.0f, 0.0f);
    m_view_mat.rotate(m_cam_yaw, 0.0f, 1.0f, 0.0f);
    m_view_mat.translate(-m_cam_target[0], -m_cam_target[1], -m_cam_target[2]);
}

void MobileViewportWidget::paintGL() {
    glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    update_matrices();

    draw_grid();
    draw_scene_objects();
    draw_gizmo();
}

void MobileViewportWidget::draw_grid() {
    if (!m_unlit_shader || !m_unlit_shader->isLinked()) return;

    m_unlit_shader->bind();
    m_unlit_shader->setUniformValue("uProj", m_proj_mat);
    m_unlit_shader->setUniformValue("uModelView", m_view_mat);
    m_unlit_shader->setUniformValue("uColor", QVector4D(0.2f, 0.25f, 0.35f, 0.5f));

    m_unlit_shader->release();
}

void MobileViewportWidget::draw_scene_objects() {
    if (!m_is_scene && !m_is_model) return;
    if (!m_shader || !m_shader->isLinked()) return;

    m_shader->bind();
    m_shader->setUniformValue("uProj", m_proj_mat);

    // Set mobile stylized sun light uniforms
    QVector3D sun_dir = m_view_mat.mapVector(QVector3D(0.5f, 0.8f, 0.3f)).normalized();
    m_shader->setUniformValue("uSunDir", sun_dir);
    m_shader->setUniformValue("uSunColor", QVector3D(1.1f, 1.05f, 0.95f));
    m_shader->setUniformValue("uSkyColor", QVector3D(0.25f, 0.35f, 0.55f));
    m_shader->setUniformValue("uGroundColor", QVector3D(0.12f, 0.14f, 0.18f));
    m_shader->setUniformValue("uHasTex", 0);
    m_shader->setUniformValue("uDiffuseColor", QVector4D(0.85f, 0.85f, 0.88f, 1.0f));

    m_shader->release();
}

void MobileViewportWidget::draw_gizmo() {
    if (m_gizmo_mode == GizmoMode::Off || m_selected_object < 0) return;
}

// ── Touch / Camera Navigation Events ──────────────────────────────────────

void MobileViewportWidget::on_dpad_pan(float dx, float dy) {
    const float speed = m_cam_dist * 0.018f;
    const float yaw_rad = m_cam_yaw * (M_PI / 180.0f);

    const float right_x = std::cos(yaw_rad);
    const float right_z = -std::sin(yaw_rad);

    m_cam_target[0] += (right_x * dx) * speed;
    m_cam_target[1] += dy * speed;
    m_cam_target[2] += (right_z * dx) * speed;

    update();
}

void MobileViewportWidget::on_dpad_dolly(float forward, float strafe) {
    const float speed = m_cam_dist * 0.018f;
    const float yaw_rad = m_cam_yaw * (M_PI / 180.0f);

    const float fwd_x = -std::sin(yaw_rad);
    const float fwd_z = -std::cos(yaw_rad);
    const float right_x = std::cos(yaw_rad);
    const float right_z = -std::sin(yaw_rad);

    m_cam_target[0] += (fwd_x * forward + right_x * strafe) * speed;
    m_cam_target[2] += (fwd_z * forward + right_z * strafe) * speed;

    update();
}

void MobileViewportWidget::mousePressEvent(QMouseEvent* event) {
    m_touch_press_pos = event->position();
    m_last_touch_pos = event->position();

    if (event->button() == Qt::LeftButton) {
        m_is_orbiting = true;
    }
}

void MobileViewportWidget::mouseMoveEvent(QMouseEvent* event) {
    const QPointF pos = event->position();
    const float delta_x = pos.x() - m_last_touch_pos.x();
    const float delta_y = pos.y() - m_last_touch_pos.y();
    m_last_touch_pos = pos;

    if (m_is_orbiting) {
        m_cam_yaw += delta_x * 0.4f;
        m_cam_pitch += delta_y * 0.4f;
        m_cam_pitch = std::clamp(m_cam_pitch, -85.0f, 85.0f);
        update();
    }
}

void MobileViewportWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const float move_dist = std::hypot(event->position().x() - m_touch_press_pos.x(),
                                           event->position().y() - m_touch_press_pos.y());
        // Clean single tap -> Pick object
        if (move_dist < 8.0f && m_is_scene) {
            Ray ray = ruby::picking::get_mouse_ray(
                static_cast<float>(event->position().x()),
                static_cast<float>(event->position().y()),
                static_cast<float>(width()),
                static_cast<float>(height()),
                m_view_mat, m_proj_mat);

            int closest_idx = -1;
            float closest_dist = 1e9f;

            for (size_t i = 0; i < m_scene.objects.size(); ++i) {
                const auto& obj = m_scene.objects[i];
                if (obj.hidden) continue;

                BoundingBox box;
                box.min = Vector3{obj.pos_x - 10.0f, obj.pos_y - 10.0f, obj.pos_z - 10.0f};
                box.max = Vector3{obj.pos_x + 10.0f, obj.pos_y + 10.0f, obj.pos_z + 10.0f};

                RayCollision col = ruby::picking::get_ray_collision_box(ray, box);
                if (col.hit && col.distance < closest_dist) {
                    closest_dist = col.distance;
                    closest_idx = static_cast<int>(i);
                }
            }

            if (closest_idx >= 0) {
                on_scene_object_selected(closest_idx);
            }
        }
        m_is_orbiting = false;
    }
}

void MobileViewportWidget::on_scene_object_selected(int index) {
    m_selected_object = index;
    m_hierarchy_drawer->select_object(index);
    m_inspector_drawer->inspect_object(index);
    update();
}

void MobileViewportWidget::on_scene_object_transformed(int object_index,
                                                      float px, float py, float pz,
                                                      float rx, float rz, float ry,
                                                      float sx, float sy, float sz) {
    if (!m_is_scene || object_index < 0 || object_index >= static_cast<int>(m_scene.objects.size())) return;

    auto& obj = m_scene.objects[object_index];
    obj.pos_x = px; obj.pos_y = py; obj.pos_z = pz;
    obj.rot_x = rx; obj.rot_y = ry; obj.rot_z = rz;
    obj.scale_x = sx; obj.scale_y = sy; obj.scale_z = sz;

    mark_dirty(true);
    update();
}

bool MobileViewportWidget::load_scene(const std::string& scene_path) {
    m_scene = av::scene_load(scene_path);
    if (m_scene.objects.empty()) {
        emit statusMessage(QStringLiteral("Failed to load scene: %1").arg(QString::fromStdString(scene_path)));
        return false;
    }

    m_file_path = scene_path;
    m_is_scene = true;
    m_is_model = false;
    m_is_dirty = false;
    m_selected_object = -1;

    m_hierarchy_drawer->set_scene(&m_scene);
    m_inspector_drawer->set_scene(&m_scene);

    reset_camera();
    update();
    return true;
}

bool MobileViewportWidget::load_model(const std::string& pod_path) {
    m_model = av::pod_load(pod_path);
    if (m_model.meshes.empty()) {
        return false;
    }

    m_file_path = pod_path;
    m_is_scene = false;
    m_is_model = true;
    m_is_dirty = false;
    m_selected_object = -1;

    reset_camera();
    update();
    return true;
}

bool MobileViewportWidget::save_current_scene() {
    if (!m_is_scene || m_file_path.empty()) return false;

    std::string err;
    if (!av::scene_save(m_file_path, m_scene, &err)) {
        emit statusMessage(QStringLiteral("Error saving scene: %1").arg(QString::fromStdString(err)));
        return false;
    }

    mark_dirty(false);
    emit statusMessage(QStringLiteral("Scene saved successfully."));
    return true;
}

void MobileViewportWidget::mark_dirty(bool dirty) {
    if (m_is_dirty != dirty) {
        m_is_dirty = dirty;
        emit fileDirtyStateChanged(m_is_dirty);
    }
}

void MobileViewportWidget::reset_camera() {
    m_cam_pitch = 20.0f;
    m_cam_yaw = -45.0f;
    m_cam_dist = 120.0f;
    m_cam_target[0] = 0.0f;
    m_cam_target[1] = 15.0f;
    m_cam_target[2] = 0.0f;
}

void MobileViewportWidget::focus_object(int index) {
    if (!m_is_scene || index < 0 || index >= static_cast<int>(m_scene.objects.size())) return;
    const auto& obj = m_scene.objects[index];
    m_cam_target[0] = obj.pos_x;
    m_cam_target[1] = obj.pos_y;
    m_cam_target[2] = obj.pos_z;
    update();
}

} // namespace ruby::android
