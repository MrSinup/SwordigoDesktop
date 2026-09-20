#pragma once
// ============================================================================
// mobile_hierarchy_drawer.h — Mobile Touch Scene Hierarchy Drawer
//   Translucent off-canvas sheet providing finger-friendly scene node selection,
//   visibility toggling, object creation, deletion, and search filtering.
// ============================================================================

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "tools/scene_loader.h"

namespace ruby::android {

class MobileHierarchyDrawer : public QWidget {
    Q_OBJECT

public:
    explicit MobileHierarchyDrawer(QWidget* parent = nullptr);
    ~MobileHierarchyDrawer() override = default;

    void set_scene(const av::SceneData* scene);
    void select_object(int index);
    int selected_object() const { return m_selected_index; }

signals:
    void objectSelected(int index);
    void objectVisibilityToggled(int index, bool visible);
    void objectFocusRequested(int index);
    void objectDeleteRequested(int index);
    void objectDuplicateRequested(int index);
    void addObjectRequested();
    void closeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void on_item_clicked(QListWidgetItem* item);
    void on_search_changed(const QString& text);
    void on_delete_clicked();
    void on_duplicate_clicked();
    void on_focus_clicked();

private:
    void populate_list();

    const av::SceneData* m_scene = nullptr;
    int m_selected_index = -1;

    QLineEdit* m_search_edit = nullptr;
    QListWidget* m_list = nullptr;
    QPushButton* m_btn_close = nullptr;
    QPushButton* m_btn_focus = nullptr;
    QPushButton* m_btn_dup = nullptr;
    QPushButton* m_btn_del = nullptr;
    QPushButton* m_btn_add = nullptr;
    QLabel* m_title_label = nullptr;
};

} // namespace ruby::android
