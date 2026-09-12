#include "scene_hierarchy_panel.h"
#include <QTreeView>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QLineEdit>
#include <QToolButton>
#include <QMenu>
#include <QAction>
#include <QShortcut>

namespace ruby::panels {

SceneHierarchyPanel::SceneHierarchyPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(4);

    // ── Header & Toolbar ──
    auto* header_layout = new QHBoxLayout();
    header_layout->setSpacing(4);

    auto* label = new QLabel("SCENE OUTLINER", this);
    label->setStyleSheet("font-weight: 700; color: #9aa2b1; font-size: 10px;");
    header_layout->addWidget(label);
    header_layout->addStretch();

    // Add Object button with dropdown
    auto* add_btn = new QToolButton(this);
    add_btn->setText("+ Add");
    add_btn->setPopupMode(QToolButton::InstantPopup);
    add_btn->setStyleSheet("QToolButton { background: #262a33; border: 1px solid #3d4452; border-radius: 3px; padding: 2px 6px; color: #e2e8f0; font-size: 11px; }"
                           "QToolButton:hover { background: #323844; }");
    auto* add_menu = new QMenu(add_btn);
    add_menu->addAction("Empty Object", this, [this]() { emit objectCreated("Empty"); });
    add_menu->addAction("Model Object", this, [this]() { emit objectCreated("Model"); });
    add_menu->addAction("Spawn Point", this, [this]() { emit objectCreated("Spawn"); });
    add_menu->addAction("Portal", this, [this]() { emit objectCreated("Portal"); });
    add_btn->setMenu(add_menu);
    header_layout->addWidget(add_btn);

    // Delete button
    auto* del_btn = new QToolButton(this);
    del_btn->setText("- Del");
    del_btn->setToolTip("Delete selected object");
    del_btn->setStyleSheet("QToolButton { background: #262a33; border: 1px solid #3d4452; border-radius: 3px; padding: 2px 6px; color: #e2e8f0; font-size: 11px; }"
                           "QToolButton:hover { background: #7f1d1d; border-color: #ef4444; }");
    connect(del_btn, &QToolButton::clicked, this, [this]() {
        if (m_selected_index >= 0) emit objectDeleted(m_selected_index);
    });
    header_layout->addWidget(del_btn);
    layout->addLayout(header_layout);

    // ── Search filter ──
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText("Filter objects...");
    m_search->setClearButtonEnabled(true);
    m_search->setStyleSheet("QLineEdit { background: #16181d; border: 1px solid #2d333f; border-radius: 3px; padding: 3px 6px; color: #e2e8f0; font-size: 11px; }"
                            "QLineEdit:focus { border-color: #3b82f6; }");
    connect(m_search, &QLineEdit::textChanged, this, &SceneHierarchyPanel::filter_tree);
    layout->addWidget(m_search);

    // ── Tree View & Model ──
    m_model = new QStandardItemModel(this);
    m_model->setHorizontalHeaderLabels({"Object", "Kind"});
    m_tree = new QTreeView(this);
    m_tree->setModel(m_model);
    m_tree->setHeaderHidden(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeView::activated, this, [this](const QModelIndex& i) {
        int idx = i.data(Qt::UserRole).toInt();
        if (idx >= 0 && !m_updating_selection) {
            m_selected_index = idx;
            emit objectSelected(idx);
        }
    });
    connect(m_tree, &QTreeView::clicked, this, [this](const QModelIndex& i) {
        int idx = i.data(Qt::UserRole).toInt();
        if (idx >= 0 && !m_updating_selection) {
            m_selected_index = idx;
            emit objectSelected(idx);
        }
    });
    connect(m_tree, &QTreeView::customContextMenuRequested, this, &SceneHierarchyPanel::show_context_menu);
    connect(m_model, &QStandardItemModel::itemChanged, this, &SceneHierarchyPanel::on_item_changed);

    // ── Keyboard shortcuts (ImGui asset_viewer parity) ──
    // QShortcut (not a keyPressEvent override) so the actions fire whenever
    // this panel or its tree has focus. The viewport's keyPressEvent handles
    // the same keys when IT has focus and accepts them, so the shortcut map is
    // never reached — no double handling. Ctrl+C/V inside the search box are
    // consumed by QLineEdit natively, which is the desired behavior.
    auto* copy_sc = new QShortcut(QKeySequence::Copy, this);
    connect(copy_sc, &QShortcut::activated, this, &SceneHierarchyPanel::copyRequested);
    auto* paste_sc = new QShortcut(QKeySequence::Paste, this);
    connect(paste_sc, &QShortcut::activated, this, &SceneHierarchyPanel::pasteRequested);
    auto* dup_sc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), this);
    connect(dup_sc, &QShortcut::activated, this, &SceneHierarchyPanel::duplicateRequested);
    auto* del_sc = new QShortcut(QKeySequence::Delete, this);
    connect(del_sc, &QShortcut::activated, this, &SceneHierarchyPanel::deleteRequested);
    auto* bspace_sc = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
    connect(bspace_sc, &QShortcut::activated, this, &SceneHierarchyPanel::deleteRequested);
    auto* up_sc = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Up), this);
    connect(up_sc, &QShortcut::activated, this, [this]() { emit moveRequested(-1); });
    auto* down_sc = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Down), this);
    connect(down_sc, &QShortcut::activated, this, [this]() { emit moveRequested(1); });
}

void SceneHierarchyPanel::set_scene(const av::SceneData& scene) {
    m_model->removeRows(0, m_model->rowCount());
    m_selected_index = -1;

    // ── 1. External & Modular Scene Libraries ──
    if (!scene.imported_library_names.empty() || !scene.missing_libraries.empty()) {
        auto* lib_category = new QStandardItem(QString("Libraries (%1)").arg(scene.imported_library_names.size()));
        lib_category->setData(-1, Qt::UserRole);
        lib_category->setEditable(false);
        auto* lib_cat_kind = new QStandardItem("");
        lib_cat_kind->setData(-1, Qt::UserRole);
        lib_cat_kind->setEditable(false);

        for (size_t li = 0; li < scene.imported_library_names.size(); ++li) {
            const auto& lname = scene.imported_library_names[li];
            QString lpath = li < scene.imported_library_paths.size() ? QString::fromStdString(scene.imported_library_paths[li]) : "";
            auto* litem = new QStandardItem(QString::fromStdString(lname + ".scl"));
            litem->setData(-1, Qt::UserRole);
            litem->setToolTip(lpath);
            litem->setEditable(false);
            auto* lkind = new QStandardItem("Loaded");
            lkind->setData(-1, Qt::UserRole);
            lkind->setForeground(QBrush(QColor(74, 222, 128)));
            lkind->setEditable(false);
            lib_category->appendRow({litem, lkind});
        }
        for (const auto& mname : scene.missing_libraries) {
            auto* mitem = new QStandardItem(QString("[Missing] %1.scl").arg(QString::fromStdString(mname)));
            mitem->setData(-1, Qt::UserRole);
            mitem->setToolTip(QString("Could not find %1.scl in any candidate search root").arg(QString::fromStdString(mname)));
            mitem->setForeground(QBrush(QColor(248, 113, 113)));
            mitem->setEditable(false);
            auto* mkind = new QStandardItem("Missing");
            mkind->setData(-1, Qt::UserRole);
            mkind->setForeground(QBrush(QColor(248, 113, 113)));
            mkind->setEditable(false);
            lib_category->appendRow({mitem, mkind});
        }
        m_model->appendRow({lib_category, lib_cat_kind});
        m_tree->expand(lib_category->index());
    }

    // ── 2. Scene Objects ──
    for (int index = 0; index < static_cast<int>(scene.objects.size()); ++index) {
        const auto& object = scene.objects[index];
        QString disp_name = QString::fromStdString(object.name.empty() ? object.template_name : object.name);
        if (disp_name.isEmpty()) disp_name = QString("Object #%1").arg(index);

        auto* name = new QStandardItem(disp_name);
        name->setData(index, Qt::UserRole);
        name->setCheckable(true);
        name->setCheckState(object.hidden ? Qt::Unchecked : Qt::Checked);

        QString kind = !object.mesh_name.empty() ? "Model"
                     : !object.ground_meshes.empty() ? "Ground mesh"
                     : object.is_portal ? "Portal"
                     : object.is_dimension_object ? "Rift"
                     : object.is_spawn_point ? "Spawn"
                     : "Object";
        auto* type = new QStandardItem(kind);
        type->setData(index, Qt::UserRole);
        type->setEditable(false);
        m_model->appendRow({name, type});
    }

    if (m_search && !m_search->text().isEmpty()) {
        filter_tree(m_search->text());
    }
}

void SceneHierarchyPanel::select_object(int index) {
    if (m_updating_selection) return;
    m_updating_selection = true;
    m_selected_index = index;

    if (index < 0) {
        m_tree->clearSelection();
        m_updating_selection = false;
        return;
    }

    for (int r = 0; r < m_model->rowCount(); ++r) {
        auto* item = m_model->item(r, 0);
        if (item && item->data(Qt::UserRole).toInt() == index) {
            QModelIndex mi = m_model->indexFromItem(item);
            m_tree->setCurrentIndex(mi);
            m_tree->scrollTo(mi, QAbstractItemView::EnsureVisible);
            break;
        }
    }
    m_updating_selection = false;
}

void SceneHierarchyPanel::on_item_changed(QStandardItem* item) {
    if (!item || item->column() != 0) return;
    int idx = item->data(Qt::UserRole).toInt();
    if (idx < 0) return;
    bool visible = (item->checkState() == Qt::Checked);
    emit objectVisibilityChanged(idx, visible);
}

void SceneHierarchyPanel::show_context_menu(const QPoint& pos) {
    QModelIndex index = m_tree->indexAt(pos);
    if (!index.isValid()) return;

    int obj_idx = index.data(Qt::UserRole).toInt();
    if (obj_idx < 0) return;

    QMenu menu(this);
    QAction* focus_act = menu.addAction("Focus in Viewport");
    QAction* dup_act   = menu.addAction("Duplicate (Ctrl+D)");
    menu.addSeparator();
    QAction* del_act   = menu.addAction("Delete (Del)");

    QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (chosen == focus_act) {
        emit objectFocusRequested(obj_idx);
    } else if (chosen == dup_act) {
        // Route through the clipboard path (select first, then duplicate) so
        // ground-mesh objects get GPU buffers uploaded and names stay unique
        // — same behavior as the Ctrl+D shortcut.
        emit objectSelected(obj_idx);
        emit duplicateRequested();
    } else if (chosen == del_act) {
        emit objectSelected(obj_idx);
        emit deleteRequested();
    }
}

void SceneHierarchyPanel::filter_tree(const QString& text) {
    QString filter = text.trimmed().toLower();
    for (int r = 0; r < m_model->rowCount(); ++r) {
        auto* item = m_model->item(r, 0);
        if (!item) continue;
        if (filter.isEmpty()) {
            m_tree->setRowHidden(r, QModelIndex(), false);
        } else {
            bool match = item->text().toLower().contains(filter);
            m_tree->setRowHidden(r, QModelIndex(), !match);
        }
    }
}

} // namespace ruby::panels
