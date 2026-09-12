#include "ruby/panels/template_palette_panel.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QFileInfo>
#include <QStyle>
#include <cmath>

#include "ruby/core/project_context.h"
#include "tools/scene_loader.h"

#include <QMimeData>
#include <QUrl>
#include <QMenu>

namespace {
class PaletteTreeWidget : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
protected:
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override {
        if (items.isEmpty()) return nullptr;
        QTreeWidgetItem* it = items.first();
        QMimeData* mime = new QMimeData();
        QString kind = it->data(0, Qt::UserRole).toString();
        QString name = it->text(0);
        QString sourcePath = it->data(0, Qt::UserRole + 1).toString();
        if (kind == QStringLiteral("template")) {
            mime->setData(QStringLiteral("application/x-ruby-template"), name.toUtf8());
            mime->setText(name);
        } else {
            mime->setData(QStringLiteral("application/x-ruby-model"), sourcePath.toUtf8());
            mime->setText(sourcePath);
            mime->setUrls({ QUrl::fromLocalFile(sourcePath) });
        }
        return mime;
    }
};
}

namespace ruby::panels {

TemplatePalettePanel::TemplatePalettePanel(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Filter templates / models…"));
    connect(m_search, &QLineEdit::textChanged, this, &TemplatePalettePanel::filter_list);
    layout->addWidget(m_search);

    m_tree = new PaletteTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Name"), tr("Source")});
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setDragEnabled(true);
    m_tree->setDragDropMode(QAbstractItemView::DragOnly);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pt) {
        QTreeWidgetItem* it = m_tree->itemAt(pt);
        if (!it) return;
        QMenu menu(this);
        menu.addAction(tr("Add to Scene"), this, &TemplatePalettePanel::on_add_clicked);
        menu.exec(m_tree->viewport()->mapToGlobal(pt));
    });
    connect(m_tree, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem*, int) { on_add_clicked(); });
    layout->addWidget(m_tree, 1);

    auto* add_btn = new QPushButton(tr("Add to Scene"), this);
    connect(add_btn, &QPushButton::clicked, this, &TemplatePalettePanel::on_add_clicked);
    layout->addWidget(add_btn);
}

void TemplatePalettePanel::add_entry(const av::TemplateSourceEntry& e) {
    auto* item = new QTreeWidgetItem(m_tree);
    item->setText(0, QString::fromStdString(e.name));
    item->setData(0, Qt::UserRole + 1, QString::fromStdString(e.source_path));
    QString source;
    if (e.kind == av::TemplateSourceEntry::Template) {
        if (e.source_path.empty())
            source = tr("(scene)");
        else
            source = QFileInfo(QString::fromStdString(e.source_path)).fileName();
        if (std::fabs(e.scaling - 1.0f) > 1e-4f)
            source += QStringLiteral("  ×%1").arg(e.scaling, 0, 'g', 3);
        item->setIcon(0, style()->standardIcon(QStyle::SP_FileDialogListView));
        item->setData(0, Qt::UserRole, "template");
    } else {
        source = QFileInfo(QString::fromStdString(e.source_path)).fileName();
        item->setIcon(0, style()->standardIcon(QStyle::SP_FileDialogDetailedView));
        item->setData(0, Qt::UserRole, "model");
    }
    item->setText(1, source);
}

void TemplatePalettePanel::set_entries(const std::vector<av::TemplateSourceEntry>& entries) {
    m_entries = entries;
    m_tree->clear();
    m_tree->setSortingEnabled(false);
    for (const auto& e : m_entries) add_entry(e);
    m_tree->sortItems(0, Qt::AscendingOrder);
}

void TemplatePalettePanel::refresh(const av::SceneData& scene, const QStringList& roots) {
    std::vector<std::string> root_list;
    root_list.reserve(static_cast<size_t>(roots.size()));
    for (const QString& r : roots)
        root_list.push_back(r.toStdString());

    set_entries(av::scan_template_sources(root_list, scene));
}

void TemplatePalettePanel::filter_list(const QString& text) {
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_tree->topLevelItem(i);
        item->setHidden(!text.isEmpty() &&
                        !item->text(0).contains(text, Qt::CaseInsensitive));
    }
}

bool TemplatePalettePanel::emit_current() {
    QTreeWidgetItem* item = m_tree->currentItem();
    if (!item) return false;
    const QString name = item->text(0);
    for (const auto& e : m_entries) {
        if (QString::fromStdString(e.name) != name) continue;
        if (e.kind == av::TemplateSourceEntry::Template) {
            emit addTemplateRequested(name, QString::fromStdString(e.source_path));
        } else {
            emit addModelRequested(QString::fromStdString(e.source_path), name);
        }
        return true;
    }
    return false;
}

void TemplatePalettePanel::on_add_clicked() {
    if (!emit_current()) {
        ruby::core::ProjectContext::instance().set_status(
            "Select a template or model in the palette first.");
    }
}

} // namespace ruby::panels