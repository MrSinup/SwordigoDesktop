// ============================================================================
// doc_viewer_dialog.cpp — Implementation of Embedded Documentation Viewer
// ============================================================================

#include "doc_viewer_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QFile>
#include <QIcon>
#include <QStyle>
#include <QShortcut>
#include <QKeySequence>
#include <QScrollBar>
#include <QFontDatabase>

namespace ruby::editor {

DocViewerDialog::DocViewerDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Swordigo Caver Engine & FileRift Documentation");
    resize(1100, 750);

    setup_ui();
    populate_doc_tree();

    // Select first document by default
    if (m_doc_tree->topLevelItemCount() > 0) {
        auto* first_group = m_doc_tree->topLevelItem(0);
        if (first_group->childCount() > 0) {
            m_doc_tree->setCurrentItem(first_group->child(0));
        }
    }
}

void DocViewerDialog::setup_ui() {
    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(8, 8, 8, 8);
    main_layout->setSpacing(6);

    // Top Navigation & Toolbar Bar
    auto* top_bar = new QHBoxLayout();
    m_current_title_label = new QLabel("Documentation", this);
    m_current_title_label->setStyleSheet("font-size: 15px; font-weight: bold; color: #61afef;");
    top_bar->addWidget(m_current_title_label);
    top_bar->addStretch();

    auto* zoom_out_btn = new QPushButton("A-", this);
    zoom_out_btn->setToolTip("Decrease Font Size (Ctrl+-)");
    zoom_out_btn->setFixedWidth(32);
    connect(zoom_out_btn, &QPushButton::clicked, this, &DocViewerDialog::zoom_out);

    auto* zoom_reset_btn = new QPushButton("100%", this);
    zoom_reset_btn->setToolTip("Reset Font Size (Ctrl+0)");
    zoom_reset_btn->setFixedWidth(48);
    connect(zoom_reset_btn, &QPushButton::clicked, this, &DocViewerDialog::reset_zoom);

    auto* zoom_in_btn = new QPushButton("A+", this);
    zoom_in_btn->setToolTip("Increase Font Size (Ctrl++)");
    zoom_in_btn->setFixedWidth(32);
    connect(zoom_in_btn, &QPushButton::clicked, this, &DocViewerDialog::zoom_in);

    top_bar->addWidget(zoom_out_btn);
    top_bar->addWidget(zoom_reset_btn);
    top_bar->addWidget(zoom_in_btn);

    main_layout->addLayout(top_bar);

    // Splitter between Navigation Tree and Markdown Browser
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // Left Panel: Search Box + Tree Widget
    auto* left_panel = new QWidget(splitter);
    auto* left_layout = new QVBoxLayout(left_panel);
    left_layout->setContentsMargins(0, 0, 0, 0);
    left_layout->setSpacing(4);

    m_search_box = new QLineEdit(left_panel);
    m_search_box->setPlaceholderText("Filter documents...");
    m_search_box->setClearButtonEnabled(true);
    connect(m_search_box, &QLineEdit::textChanged, this, &DocViewerDialog::on_search_text_changed);
    left_layout->addWidget(m_search_box);

    m_doc_tree = new QTreeWidget(left_panel);
    m_doc_tree->setHeaderHidden(true);
    m_doc_tree->setAnimated(true);
    m_doc_tree->setIndentation(16);
    m_doc_tree->setStyleSheet(
        "QTreeWidget { background-color: #21252b; border: 1px solid #181a1f; color: #abb2bf; font-size: 13px; }"
        "QTreeWidget::item { padding: 4px 6px; margin: 1px 0; border-radius: 4px; }"
        "QTreeWidget::item:hover { background-color: #2c313a; color: #ffffff; }"
        "QTreeWidget::item:selected { background-color: #3b4252; color: #61afef; font-weight: bold; }"
    );
    connect(m_doc_tree, &QTreeWidget::currentItemChanged, this, &DocViewerDialog::on_item_selected);
    left_layout->addWidget(m_doc_tree);

    splitter->addWidget(left_panel);

    // Right Panel: Markdown TextBrowser
    m_markdown_view = new QTextBrowser(splitter);
    m_markdown_view->setOpenExternalLinks(true);
    m_markdown_view->setStyleSheet(
        "QTextBrowser {"
        "  background-color: #1e1e1e;"
        "  color: #dcdfe4;"
        "  border: 1px solid #181a1f;"
        "  padding: 16px;"
        "  selection-background-color: #3e4451;"
        "  selection-color: #ffffff;"
        "}"
    );

    splitter->addWidget(m_markdown_view);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({ 280, 800 });

    main_layout->addWidget(splitter);

    // Keyboard shortcuts
    auto* sc_in = new QShortcut(QKeySequence::ZoomIn, this);
    connect(sc_in, &QShortcut::activated, this, &DocViewerDialog::zoom_in);

    auto* sc_out = new QShortcut(QKeySequence::ZoomOut, this);
    connect(sc_out, &QShortcut::activated, this, &DocViewerDialog::zoom_out);

    auto* sc_reset = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this);
    connect(sc_reset, &QShortcut::activated, this, &DocViewerDialog::reset_zoom);
}

void DocViewerDialog::populate_doc_tree() {
    m_doc_items = {
        { "Engine Architecture & ECS", "Architecture", ":/docs/engine_architecture.md", "SP_FileDialogDetailedView" },
        { "Components Catalog (76 Components)", "Components", ":/docs/components_catalog.md", "SP_FileDialogListView" },
        { "Lua Scripting API Reference", "Scripting", ":/docs/lua_scripting_api.md", "SP_FileDialogContentsView" },
        { "Engine Enums & Constants", "Constants", ":/docs/enums_and_constants.md", "SP_FileIcon" },
        { "FileRift Asset Format Specification", "File Formats", ":/docs/filerift_format_specification.md", "SP_FileIcon" }
    };

    m_doc_tree->clear();

    std::unordered_map<QString, QTreeWidgetItem*> category_nodes;

    for (const auto& item : m_doc_items) {
        QTreeWidgetItem* parent_node = nullptr;
        auto cat_it = category_nodes.find(item.category);
        if (cat_it == category_nodes.end()) {
            parent_node = new QTreeWidgetItem(m_doc_tree);
            parent_node->setText(0, item.category);
            parent_node->setExpanded(true);
            QFont f = parent_node->font(0);
            f.setBold(true);
            parent_node->setFont(0, f);
            parent_node->setForeground(0, QColor("#e5c07b"));
            category_nodes[item.category] = parent_node;
        } else {
            parent_node = cat_it->second;
        }

        auto* child = new QTreeWidgetItem(parent_node);
        child->setText(0, item.title);
        child->setData(0, Qt::UserRole, item.qrc_path);
        child->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
    }

    m_doc_tree->expandAll();
}

void DocViewerDialog::on_item_selected(QTreeWidgetItem* current, QTreeWidgetItem* /* previous */) {
    if (!current) return;
    QString qrc_path = current->data(0, Qt::UserRole).toString();
    if (!qrc_path.isEmpty()) {
        m_current_title_label->setText(current->text(0));
        load_markdown(qrc_path);
    }
}

void DocViewerDialog::open_document(const QString& qrc_path) {
    for (int i = 0; i < m_doc_tree->topLevelItemCount(); ++i) {
        auto* top = m_doc_tree->topLevelItem(i);
        for (int j = 0; j < top->childCount(); ++j) {
            auto* child = top->child(j);
            if (child->data(0, Qt::UserRole).toString() == qrc_path) {
                m_doc_tree->setCurrentItem(child);
                return;
            }
        }
    }
}

void DocViewerDialog::load_markdown(const QString& qrc_path) {
    QFile file(qrc_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_markdown_view->setHtml("<div style='color: #e06c75; font-size: 14px; padding: 20px;'>"
                                 "<h3>Error loading document</h3>"
                                 "<p>Could not open resource: <code>" + qrc_path + "</code></p></div>");
        return;
    }

    QString raw_md = QString::fromUtf8(file.readAll());
    file.close();

    // Use Qt6 native Markdown rendering
    m_markdown_view->setMarkdown(raw_md);

    // Apply document-level styling font
    QFont doc_font = m_markdown_view->font();
    doc_font.setPointSize(m_font_point_size);
    m_markdown_view->setFont(doc_font);

    // Scroll to top
    m_markdown_view->verticalScrollBar()->setValue(0);
}

void DocViewerDialog::on_search_text_changed(const QString& text) {
    QString filter = text.trimmed().toLower();
    for (int i = 0; i < m_doc_tree->topLevelItemCount(); ++i) {
        auto* top = m_doc_tree->topLevelItem(i);
        bool any_child_visible = false;
        for (int j = 0; j < top->childCount(); ++j) {
            auto* child = top->child(j);
            bool match = filter.isEmpty() || child->text(0).toLower().contains(filter);
            child->setHidden(!match);
            if (match) any_child_visible = true;
        }
        top->setHidden(!any_child_visible);
    }
}

void DocViewerDialog::zoom_in() {
    if (m_font_point_size < 24) {
        m_font_point_size += 1;
        QFont f = m_markdown_view->font();
        f.setPointSize(m_font_point_size);
        m_markdown_view->setFont(f);
    }
}

void DocViewerDialog::zoom_out() {
    if (m_font_point_size > 8) {
        m_font_point_size -= 1;
        QFont f = m_markdown_view->font();
        f.setPointSize(m_font_point_size);
        m_markdown_view->setFont(f);
    }
}

void DocViewerDialog::reset_zoom() {
    m_font_point_size = 12;
    QFont f = m_markdown_view->font();
    f.setPointSize(m_font_point_size);
    m_markdown_view->setFont(f);
}

} // namespace ruby::editor
