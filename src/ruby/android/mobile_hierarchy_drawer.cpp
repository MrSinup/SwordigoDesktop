// ============================================================================
// mobile_hierarchy_drawer.cpp — Implementation of Mobile Scene Hierarchy Drawer
// ============================================================================

#include "mobile_hierarchy_drawer.h"
#include <QPainter>
#include <QListWidgetItem>
#include <QCheckBox>
#include <QHBoxLayout>

namespace ruby::android {

MobileHierarchyDrawer::MobileHierarchyDrawer(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NoSystemBackground, true);
    setFixedWidth(300);

    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(14, 14, 14, 14);
    root_layout->setSpacing(10);

    // Header bar
    auto* header = new QHBoxLayout();
    m_title_label = new QLabel(QStringLiteral("Scene Hierarchy"), this);
    m_title_label->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold; color: #E0E4F0;"));
    m_btn_close = new QPushButton(QStringLiteral("X"), this);
    m_btn_close->setFixedSize(36, 36);
    m_btn_close->setStyleSheet(
        QStringLiteral("background: #252835; color: #A0A5BC; border: 1px solid #3A3F55; "
                       "border-radius: 18px; font-size: 14px; font-weight: bold;"));
    header->addWidget(m_title_label);
    header->addStretch();
    header->addWidget(m_btn_close);
    root_layout->addLayout(header);

    // Search bar
    m_search_edit = new QLineEdit(this);
    m_search_edit->setPlaceholderText(QStringLiteral("Filter objects..."));
    m_search_edit->setFixedHeight(38);
    m_search_edit->setStyleSheet(
        QStringLiteral("background: #181A22; border: 1px solid #33384C; border-radius: 8px; "
                       "color: #FFFFFF; padding: 4px 10px; font-size: 13px;"));
    root_layout->addWidget(m_search_edit);

    // Object list
    m_list = new QListWidget(this);
    m_list->setStyleSheet(
        QStringLiteral("QListWidget { background: #151720; border: 1px solid #2B3042; "
                       "border-radius: 8px; color: #D0D5E5; padding: 4px; } "
                       "QListWidget::item { height: 44px; padding-left: 8px; border-bottom: 1px solid #1E2230; } "
                       "QListWidget::item:selected { background: #2B5585; color: #FFFFFF; border-radius: 6px; }"));
    root_layout->addWidget(m_list, 1);

    // Bottom Action Row
    auto* actions_layout = new QHBoxLayout();
    actions_layout->setSpacing(6);

    auto make_btn = [this](const QString& text, const QString& bg) {
        auto* b = new QPushButton(text, this);
        b->setFixedHeight(36);
        b->setStyleSheet(QString(
            "background: %1; color: #FFFFFF; border: none; border-radius: 6px; font-size: 11px; font-weight: bold;"
        ).arg(bg));
        return b;
    };

    m_btn_focus = make_btn(QStringLiteral("Focus"), QStringLiteral("#264F78"));
    m_btn_dup   = make_btn(QStringLiteral("Duplicate"), QStringLiteral("#334D5C"));
    m_btn_del   = make_btn(QStringLiteral("Delete"), QStringLiteral("#7A2525"));
    m_btn_add   = make_btn(QStringLiteral("+ Add"), QStringLiteral("#2D7A4D"));

    actions_layout->addWidget(m_btn_focus);
    actions_layout->addWidget(m_btn_dup);
    actions_layout->addWidget(m_btn_del);
    actions_layout->addWidget(m_btn_add);
    root_layout->addLayout(actions_layout);

    // Connections
    connect(m_btn_close, &QPushButton::clicked, this, &MobileHierarchyDrawer::closeRequested);
    connect(m_list, &QListWidget::itemClicked, this, &MobileHierarchyDrawer::on_item_clicked);
    connect(m_search_edit, &QLineEdit::textChanged, this, &MobileHierarchyDrawer::on_search_changed);
    connect(m_btn_focus, &QPushButton::clicked, this, &MobileHierarchyDrawer::on_focus_clicked);
    connect(m_btn_dup, &QPushButton::clicked, this, &MobileHierarchyDrawer::on_duplicate_clicked);
    connect(m_btn_del, &QPushButton::clicked, this, &MobileHierarchyDrawer::on_delete_clicked);
    connect(m_btn_add, &QPushButton::clicked, this, &MobileHierarchyDrawer::addObjectRequested);
}

void MobileHierarchyDrawer::set_scene(const av::SceneData* scene) {
    m_scene = scene;
    m_selected_index = -1;
    populate_list();
}

void MobileHierarchyDrawer::select_object(int index) {
    m_selected_index = index;
    if (!m_scene || index < 0 || index >= static_cast<int>(m_scene->objects.size())) {
        m_list->clearSelection();
        return;
    }

    for (int r = 0; r < m_list->count(); ++r) {
        auto* it = m_list->item(r);
        if (it->data(Qt::UserRole).toInt() == index) {
            m_list->setCurrentItem(it);
            m_list->scrollToItem(it);
            break;
        }
    }
}

void MobileHierarchyDrawer::populate_list() {
    m_list->clear();
    if (!m_scene) return;

    const QString query = m_search_edit->text().trimmed().toLower();

    for (size_t i = 0; i < m_scene->objects.size(); ++i) {
        const auto& obj = m_scene->objects[i];
        QString name = QString::fromStdString(obj.name.empty() ? ("obj" + std::to_string(i)) : obj.name);

        QString subtitle = QString::fromStdString(obj.template_name);
        if (subtitle.isEmpty()) subtitle = QString::fromStdString(obj.mesh_name);

        if (!query.isEmpty()) {
            if (!name.toLower().contains(query) && !subtitle.toLower().contains(query)) {
                continue;
            }
        }

        QString display_text = QStringLiteral("%1. %2").arg(i).arg(name);
        if (!subtitle.isEmpty()) {
            display_text += QStringLiteral("  [%1]").arg(subtitle);
        }

        auto* item = new QListWidgetItem(display_text, m_list);
        item->setData(Qt::UserRole, static_cast<int>(i));

        if (obj.hidden) {
            item->setForeground(QColor(120, 130, 150));
        } else {
            item->setForeground(QColor(230, 235, 245));
        }
    }
}

void MobileHierarchyDrawer::on_item_clicked(QListWidgetItem* item) {
    if (!item) return;
    int idx = item->data(Qt::UserRole).toInt();
    m_selected_index = idx;
    emit objectSelected(idx);
}

void MobileHierarchyDrawer::on_search_changed(const QString& /*text*/) {
    populate_list();
    if (m_selected_index >= 0) select_object(m_selected_index);
}

void MobileHierarchyDrawer::on_focus_clicked() {
    if (m_selected_index >= 0) emit objectFocusRequested(m_selected_index);
}

void MobileHierarchyDrawer::on_duplicate_clicked() {
    if (m_selected_index >= 0) emit objectDuplicateRequested(m_selected_index);
}

void MobileHierarchyDrawer::on_delete_clicked() {
    if (m_selected_index >= 0) emit objectDeleteRequested(m_selected_index);
}

void MobileHierarchyDrawer::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    // Dark glassmorphic background
    p.fillRect(rect(), QColor(14, 16, 22, 235));
    // Left accent border separating drawer from 3D viewport
    p.setPen(QPen(QColor(50, 58, 80, 200), 2.0));
    p.drawLine(0, 0, 0, height());
}

} // namespace ruby::android
