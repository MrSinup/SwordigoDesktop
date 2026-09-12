#pragma once

#include <QWidget>
#include <QSettings>
#include <memory>

namespace ruby::tools {

// Full-area authoring workspace (hosted as a central tab, not a narrow dock).
// Each authoring discipline gets a roomy page: Ground Mesh Studio, RubyMesh,
// Scene Generators, SCL / Objects, Batch Converter, Blender, Settings.
class RubyToolsWorkspace final : public QWidget {
    Q_OBJECT
public:
    explicit RubyToolsWorkspace(QWidget* parent = nullptr);
    ~RubyToolsWorkspace() override;
    void set_scene_camera_focus(double x, double y);

signals:
    void groundMeshApplied(const QString& source);
    // A generated ground-mesh object is ready to paste into the open scene.
    void groundMeshAddToScene(const QString& identifier,
                              const QByteArray& scene_bytes,
                              double pos_x, double pos_y, double depth);
    void collisionApplied(const QString& source);
    void objectImported(const QString& path);
    void rendererQualityChanged(int quality, bool wireframe, bool grid);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ruby::tools
