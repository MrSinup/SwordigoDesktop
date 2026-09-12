#pragma once
// ============================================================================
// inspector_panel.h — Professional Object & Asset Properties Inspector for Ruby GG
// ============================================================================

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QScrollArea>
#include <QComboBox>
#include <QPushButton>
#include <QToolButton>
#include "tools/scene_loader.h"

namespace ruby::panels {

class InspectorPanel : public QWidget {
    Q_OBJECT

public:
    explicit InspectorPanel(QWidget* parent = nullptr);
    ~InspectorPanel() override = default;

    void inspect_file(const QString& path);
    void inspect_model_info(const QString& name, int meshCount, int vertCount);
    void inspect_scene(const av::SceneData& scene);
    void inspect_scene_object(const av::SceneData& scene, int object_index);
    void update_transform(float px, float py, float pz,
                          float rx, float rz, float ry,
                          float sx, float sy, float sz);
    void update_camera_bounds(const av::CameraBounds& cb);
    // Incremental: append a single new component group to the existing UI
    // without tearing down and rebuilding all component widgets. Used by the
    // bridge for Add Component / Paste Component so the panel feels live.
    void append_component(const av::SceneComponent& comp);
    void clear_inspection();
    int current_object_index() const { return m_inspected_object_idx; }

signals:
    void objectTransformChanged(int object_index,
                                float px, float py, float pz,
                                float rx, float rz, float ry,
                                float sx, float sy, float sz);
    void objectIdentityChanged(int object_index, const QString& name, const QString& tmpl);
    void objectHiddenChanged(int object_index, bool hidden);
    void componentAdded(int object_index, const QString& comp_type);
    void componentRemoved(int object_index, int comp_index);
    void componentFieldChanged(int object_index, int comp_index, const av::SceneComponentField& field);
    // Paste carries the clipboard component so the host can insert it with a
    // fresh type id (scene_paste_component does the id rewrite).
    void componentPasted(int object_index, const av::SceneComponent& component);
    void groundMeshRegenNormals(int object_index);
    void cameraBoundsChanged(const av::CameraBounds& cb);
    void cameraBoundsFitRequested();
    void cameraBoundsFrameRequested();
    void cameraBoundsRemoveRequested();

private slots:
    void on_transform_spinbox_changed();
    void on_bounds_spinbox_changed();
    void on_identity_changed();
    void on_hidden_toggled(bool checked);

private:
    void rebuild_components_ui(const av::SceneObject& obj);
    void rebuild_ground_meshes_ui(const av::SceneObject& obj);
    // Create a single component group box widget (used by rebuild + append).
    QGroupBox* make_component_widget(const av::SceneComponent& c, int component_index);

    int m_inspected_object_idx = -1;
    bool m_block_signals = false;

    // Scroll container
    QScrollArea* m_scroll_area = nullptr;
    QWidget* m_container = nullptr;
    QVBoxLayout* m_container_layout = nullptr;

    // Header
    QLabel* m_title_label = nullptr;
    QLabel* m_subtitle_label = nullptr;

    // Asset Stats
    QGroupBox* m_stats_group = nullptr;
    QLabel* m_mesh_count_label = nullptr;
    QLabel* m_vert_count_label = nullptr;

    // Object Identity
    QGroupBox* m_identity_group = nullptr;
    QLineEdit* m_name_edit = nullptr;
    QLineEdit* m_template_edit = nullptr;
    QCheckBox* m_hidden_check = nullptr;

    // Transform
    QGroupBox* m_transform_group = nullptr;
    QDoubleSpinBox* m_pos_x = nullptr;
    QDoubleSpinBox* m_pos_y = nullptr;
    QDoubleSpinBox* m_pos_z = nullptr;
    QDoubleSpinBox* m_rot_x = nullptr;
    QDoubleSpinBox* m_rot_z = nullptr;
    QDoubleSpinBox* m_rot_y = nullptr;
    QDoubleSpinBox* m_scale_x = nullptr;
    QDoubleSpinBox* m_scale_y = nullptr;
    QDoubleSpinBox* m_scale_z = nullptr;
    QCheckBox* m_uniform_scale = nullptr;

    // Asset References
    QGroupBox* m_refs_group = nullptr;
    QLabel* m_mesh_name_label = nullptr;
    QLabel* m_texture_name_label = nullptr;
    QLabel* m_bg_name_label = nullptr;

    // Components
    QGroupBox* m_components_group = nullptr;
    QVBoxLayout* m_components_layout = nullptr;
    QComboBox* m_add_comp_combo = nullptr;
    QPushButton* m_add_comp_btn = nullptr;
    QPushButton* m_paste_comp_btn = nullptr;

    // Ground Meshes
    QGroupBox* m_ground_mesh_group = nullptr;
    QVBoxLayout* m_ground_mesh_layout = nullptr;

    // Camera Bounds
    QGroupBox* m_bounds_group = nullptr;
    QDoubleSpinBox* m_bounds_x = nullptr;
    QDoubleSpinBox* m_bounds_y = nullptr;
    QDoubleSpinBox* m_bounds_w = nullptr;
    QDoubleSpinBox* m_bounds_h = nullptr;
    QLabel* m_bounds_max_x_label = nullptr;
    QLabel* m_bounds_max_y_label = nullptr;
    QLabel* m_bounds_center_label = nullptr;
    QPushButton* m_bounds_fit_btn = nullptr;
    QPushButton* m_bounds_frame_btn = nullptr;
    QPushButton* m_bounds_remove_btn = nullptr;
};

} // namespace ruby::panels
