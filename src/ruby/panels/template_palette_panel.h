#pragma once
// template_palette_panel.h — Template & Model palette dock (master TODO 2.3).
// Lists scene templates (from the open scene's ObjectLibrary imports plus any
// .scl files under the asset roots) and model assets (*.pod). Double-click or
// Add drops them into the open scene; models get a LocalAABB measured from the
// pod's own bounds at add time (web editor §3.4 parity).

#include <QWidget>
#include <QStringList>
#include <vector>

#include "tools/template_sources.h"

class QTreeWidget;
class QLineEdit;

namespace ruby::panels {

class TemplatePalettePanel final : public QWidget {
    Q_OBJECT
public:
    explicit TemplatePalettePanel(QWidget* parent = nullptr);

    // Re-scan roots + the open scene's libraries and rebuild the lists.
    void refresh(const av::SceneData& scene, const QStringList& roots);

    // Populate from a precomputed scan (the main window scans once and feeds
    // both the palette and the shared catalog — avoids a second 1s walk).
    void set_entries(const std::vector<av::TemplateSourceEntry>& entries);

signals:
    void addTemplateRequested(const QString& template_name, const QString& scl_path);
    void addModelRequested(const QString& pod_path, const QString& display_name);

private slots:
    void on_add_clicked();
    void filter_list(const QString& text);

private:
    void add_entry(const av::TemplateSourceEntry& e);
    bool emit_current();

    QLineEdit* m_search = nullptr;
    QTreeWidget* m_tree = nullptr;
    std::vector<av::TemplateSourceEntry> m_entries;
};

} // namespace ruby::panels