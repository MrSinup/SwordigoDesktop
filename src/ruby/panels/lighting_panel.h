#pragma once
// ============================================================================
// lighting_panel.h — Lighting rig dock (Ruby GG)
//   Live control of the 3D viewport's sun/fill/bounce/ambient/fog rig — the
//   Qt equivalent of the ImGui editor's lighting panel for the Pod & Scene
//   viewers. Every adjustment streams straight to Viewport3DWidget.
// ============================================================================

#include <QWidget>
#include "ruby/viewport/viewport_lighting.h"

class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;

namespace ruby::panels {

class LightingPanel final : public QWidget {
    Q_OBJECT

public:
    explicit LightingPanel(QWidget* parent = nullptr);

    // Restore the tuned preset and push it to the viewport.
    void apply_preset(const ruby::viewport::ViewportLighting& light);
    ruby::viewport::ViewportLighting lighting() const;

signals:
    void lightingChanged(const ruby::viewport::ViewportLighting& lighting);

private slots:
    void emit_changed();

private:
    ruby::viewport::ViewportLighting current() const;

    QDoubleSpinBox* m_key = nullptr;
    QSpinBox* m_key_yaw = nullptr;
    QSpinBox* m_key_pitch = nullptr;
    QDoubleSpinBox* m_fill = nullptr;
    QDoubleSpinBox* m_bounce = nullptr;
    QDoubleSpinBox* m_ambient = nullptr;
    QCheckBox* m_fog = nullptr;
    QDoubleSpinBox* m_fog_density = nullptr;
};

} // namespace ruby::panels
