#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QToolButton>
#include "tools/scene_loader.h"

class QTreeView;
class QStandardItemModel;
class QStandardItem;

namespace ruby::panels {

class SceneHierarchyPanel final : public QWidget {
    Q_OBJECT
public:
    explicit SceneHierarchyPanel(QWidget* parent = nullptr);

    void set_scene(const av::SceneData& scene);
    void select_object(int index);

signals:
    void objectSelected(int index);
    void objectVisibilityChanged(int index, bool visible);
    void objectCreated(const QString& kind);
    void objectDuplicated(int index);
    void objectDeleted(int index);
    void objectFocusRequested(int index);
    void cameraBoundsFitRequested();

    // Keyboard shortcuts (Del / Ctrl+C / Ctrl+V / Ctrl+D / Alt+Up-Down) —
    // ImGui asset_viewer parity. Emitted from QShortcuts so they work while
    // this panel (or its tree) has focus; the viewport handles the same keys
    // in its own keyPressEvent when IT has focus, so nothing double-fires.
    void copyRequested();
    void pasteRequested();
    void duplicateRequested();
    void deleteRequested();
    void moveRequested(int direction);   // -1 = up (earlier), +1 = down

private slots:
    void on_item_changed(QStandardItem* item);
    void show_context_menu(const QPoint& pos);
    void filter_tree(const QString& text);

private:
    QLineEdit* m_search = nullptr;
    QTreeView* m_tree = nullptr;
    QStandardItemModel* m_model = nullptr;
    bool m_updating_selection = false;
    int m_selected_index = -1;
};

} // namespace ruby::panels
