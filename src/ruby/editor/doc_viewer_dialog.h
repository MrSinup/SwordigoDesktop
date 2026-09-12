#pragma once
// ============================================================================
// doc_viewer_dialog.h — Embedded Markdown Documentation Viewer for ruby_gg
// ============================================================================

#include <QDialog>
#include <QTreeWidget>
#include <QTextBrowser>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <vector>

namespace ruby::editor {

struct DocItem {
    QString title;
    QString category;
    QString qrc_path;
    QString icon_name;
};

class DocViewerDialog : public QDialog {
    Q_OBJECT

public:
    explicit DocViewerDialog(QWidget* parent = nullptr);
    ~DocViewerDialog() override = default;

    void open_document(const QString& qrc_path);

private slots:
    void on_search_text_changed(const QString& text);
    void on_item_selected(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void zoom_in();
    void zoom_out();
    void reset_zoom();

private:
    void setup_ui();
    void populate_doc_tree();
    void load_markdown(const QString& qrc_path);
    QString build_styled_html(const QString& raw_markdown);

    QLineEdit* m_search_box = nullptr;
    QTreeWidget* m_doc_tree = nullptr;
    QTextBrowser* m_markdown_view = nullptr;
    QLabel* m_current_title_label = nullptr;

    std::vector<DocItem> m_doc_items;
    int m_font_point_size = 12;
};

} // namespace ruby::editor
