#pragma once
// template_inspector_panel.h — template hierarchy view for the selected scene
// object (master TODO 2.3/2.4). Shows which template an object inherits from,
// which components come from the template (inherited) vs the object itself
// (local overrides), and offers the template operations:
//   * retarget to another template (or unlink),
//   * override one inherited component locally (keeps the link),
//   * unlink + materialize the whole resolved set,
//   * reset to the clean template (drop local overrides).
// Every operation is emitted to the host, which mutates the viewport's scene
// through the normal undoable path, so the change syncs to the 3D view, the
// FileRift text buffer and the on-disk save automatically.

#include <QWidget>
#include <vector>

#include "tools/scene_loader.h"

class QComboBox;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

namespace ruby::panels {

class TemplateInspectorPanel final : public QWidget {
    Q_OBJECT
public:
    explicit TemplateInspectorPanel(QWidget* parent = nullptr);

    // Show the template state of one object. `catalog` = every known template
    // (scene-embedded + scanned .scl) so the combo can list them; the panel
    // resolves the object's own template from the scene libraries.
    void set_scene(const av::SceneData& scene, int object_index,
                   const std::vector<av::SclTemplateEntry>& catalog);

signals:
    void templateChanged(int object_index, const QString& template_name);
    void materializeRequested(int object_index);
    void resetToTemplateRequested(int object_index);
    void overrideComponentRequested(int object_index, const QString& class_name);

private slots:
    void on_template_edited(const QString& text);
    void on_override_clicked();
    void on_materialize_clicked();
    void on_reset_clicked();

private:
    void rebuild();

    int m_object_index = -1;
    av::SceneData m_scene;                 // copy of the live scene (display only)
    std::vector<av::SclTemplateEntry> m_catalog;
    bool m_updating = false;

    QComboBox* m_template_combo = nullptr;
    QLabel* m_status = nullptr;
    QTreeWidget* m_tree = nullptr;
};

} // namespace ruby::panels