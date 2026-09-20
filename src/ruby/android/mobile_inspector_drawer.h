#pragma once
// ============================================================================
// mobile_inspector_drawer.h — Mobile Touch Object & Properties Inspector Drawer
//   Translucent off-canvas sheet providing finger-friendly transform manipulation,
//   rotation presets, component inspection, and identity editing.
// ============================================================================

#include <QWidget>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "tools/scene_loader.h"

namespace ruby::android {

class MobileInspectorDrawer : public QWidget {
    Q_OBJECT

public:
    explicit MobileInspectorDrawer(QWidget* parent = nullptr);
    ~MobileInspectorDrawer() override = default;

    void set_scene(av::SceneData* scene);
    void inspect_object(int index);
    int current_object_index() const { return m_object_index; }
    void update_transform_values(float px, float py, float pz,
                                float rx, float rz, float ry,
                                float sx, float sy, float sz);

signals:
    void transformChanged(int object_index,
                          float px, float py, float pz,
                          float rx, float rz, float ry,
                          float sx, float sy, float sz);
    void identityChanged(int object_index, const QString& name, const QString& template_name);
    void hiddenToggled(int object_index, bool hidden);
    void closeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void on_transform_input_changed();
    void on_identity_input_changed();
    void on_hidden_changed(int state);
    void apply_rotation_preset(float rot_deg);

private:
    void rebuild_ui_for_object();

    av::SceneData* m_scene = nullptr;
    int m_object_index = -1;
    bool m_updating_ui = false;

    QLabel* m_title_label = nullptr;
    QPushButton* m_btn_close = nullptr;

    QLineEdit* m_name_edit = nullptr;
    QLineEdit* m_template_edit = nullptr;
    QCheckBox* m_hidden_check = nullptr;

    // Transform fields
    QDoubleSpinBox* m_pos_x = nullptr;
    QDoubleSpinBox* m_pos_y = nullptr;
    QDoubleSpinBox* m_pos_z = nullptr;

    QDoubleSpinBox* m_rot_x = nullptr;
    QDoubleSpinBox* m_rot_y = nullptr;
    QDoubleSpinBox* m_rot_z = nullptr;

    QDoubleSpinBox* m_scale_x = nullptr;
    QDoubleSpinBox* m_scale_y = nullptr;
    QDoubleSpinBox* m_scale_z = nullptr;

    QVBoxLayout* m_components_layout = nullptr;
    QWidget* m_components_container = nullptr;
};

} // namespace ruby::android
