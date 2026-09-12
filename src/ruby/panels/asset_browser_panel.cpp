// ============================================================================
// asset_browser_panel.cpp — Asset & Project Tree Browser
// ============================================================================

#include "asset_browser_panel.h"
#include <QHeaderView>
#include <QFileInfo>
#include <QMenu>
#include <QAction>
#include <QDesktopServices>
#include <QUrl>
#include <QClipboard>
#include <QGuiApplication>
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QIcon>
#include <QPixmap>
#include <QPolygonF>
#include <QFont>
#include <QStyle>
#include <QTimer>
#include <QDir>
#include <functional>
#include <algorithm>

namespace {

QIcon render_icon(int sz, const std::function<void(QPainter&, int)>& draw_fn) {
    QPixmap px(sz, sz);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    draw_fn(p, sz);
    p.end();
    return QIcon(px);
}

QIcon create_multi_res_icon(const std::function<void(QPainter&, int)>& draw_fn) {
    QIcon icon;
    icon.addPixmap(render_icon(16, draw_fn).pixmap(16, 16));
    icon.addPixmap(render_icon(32, draw_fn).pixmap(32, 32));
    return icon;
}

// 1. Directory Folder icon
QIcon get_folder_icon() {
    static QIcon icon = [] {
        if (qApp && qApp->style()) {
            QIcon std_icon = qApp->style()->standardIcon(QStyle::SP_DirIcon);
            if (!std_icon.isNull()) return std_icon;
        }
        return create_multi_res_icon([](QPainter& p, int s) {
            const float sc = s / 32.0f;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#D97706"));
            p.drawRoundedRect(QRectF(3 * sc, 7 * sc, 12 * sc, 7 * sc), 2 * sc, 2 * sc);
            p.drawRoundedRect(QRectF(3 * sc, 10 * sc, 26 * sc, 17 * sc), 3 * sc, 3 * sc);
            p.setBrush(QColor("#FBBF24"));
            p.drawRoundedRect(QRectF(3 * sc, 13 * sc, 26 * sc, 14 * sc), 3 * sc, 3 * sc);
        });
    }();
    return icon;
}

// 2. POD 3D Model icon: Isometric 3D wireframe cube in Cyan (#38BDF8)
QIcon get_pod_icon() {
    static QIcon icon = create_multi_res_icon([](QPainter& p, int s) {
        const float sc = s / 32.0f;
        QPointF top(16 * sc, 5 * sc);
        QPointF tr(26 * sc, 11 * sc);
        QPointF br(26 * sc, 22 * sc);
        QPointF bot(16 * sc, 28 * sc);
        QPointF bl(6 * sc, 22 * sc);
        QPointF tl(6 * sc, 11 * sc);
        QPointF c(16 * sc, 16 * sc);

        p.setPen(QPen(QColor("#38BDF8"), 1.8f * sc, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(56, 189, 248, 80));
        p.drawPolygon(QPolygonF({top, tr, c, tl}));

        p.setBrush(QColor(56, 189, 248, 40));
        p.drawPolygon(QPolygonF({tl, c, bot, bl}));

        p.setBrush(QColor(56, 189, 248, 120));
        p.drawPolygon(QPolygonF({c, tr, br, bot}));
    });
    return icon;
}

// 3. Scene icon: 3D world level / terrain landscape in Gold/Amber (#F59E0B)
QIcon get_scene_icon() {
    static QIcon icon = create_multi_res_icon([](QPainter& p, int s) {
        const float sc = s / 32.0f;
        QRectF bg(4 * sc, 4 * sc, 24 * sc, 24 * sc);
        p.setPen(QPen(QColor("#F59E0B"), 1.8f * sc));
        p.setBrush(QColor(245, 158, 11, 40));
        p.drawRoundedRect(bg, 4 * sc, 4 * sc);

        QPainterPath mountain;
        mountain.moveTo(5 * sc, 23 * sc);
        mountain.lineTo(13 * sc, 12 * sc);
        mountain.lineTo(18 * sc, 18 * sc);
        mountain.lineTo(22 * sc, 14 * sc);
        mountain.lineTo(27 * sc, 23 * sc);
        mountain.closeSubpath();
        p.setPen(QPen(QColor("#FBBF24"), 1.5f * sc, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(QColor(251, 191, 36, 140));
        p.drawPath(mountain);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#FEF08A"));
        p.drawEllipse(QPointF(21 * sc, 9 * sc), 2.5f * sc, 2.5f * sc);
    });
    return icon;
}

// 4. SCL icon (Object Library): 3 stacked library shelves / catalogs in Emerald Green (#10B981)
QIcon get_scl_icon() {
    static QIcon icon = create_multi_res_icon([](QPainter& p, int s) {
        const float sc = s / 32.0f;
        p.setPen(QPen(QColor("#34D399"), 1.5f * sc));
        p.setBrush(QColor(16, 185, 129, 90));
        p.drawRoundedRect(QRectF(5 * sc, 6 * sc, 22 * sc, 5.5f * sc), 2 * sc, 2 * sc);
        p.drawRoundedRect(QRectF(5 * sc, 13 * sc, 22 * sc, 5.5f * sc), 2 * sc, 2 * sc);
        p.drawRoundedRect(QRectF(5 * sc, 20 * sc, 22 * sc, 5.5f * sc), 2 * sc, 2 * sc);

        p.setPen(QPen(QColor("#6EE7B7"), 1.5f * sc));
        p.drawLine(QPointF(10 * sc, 6 * sc), QPointF(10 * sc, 25.5f * sc));
    });
    return icon;
}

// 5. PVR / TEX icon: PowerVR Texture canvas with checkerboard pattern in Violet (#A855F7)
QIcon get_pvr_icon() {
    static QIcon icon = create_multi_res_icon([](QPainter& p, int s) {
        const float sc = s / 32.0f;
        QRectF frame(5 * sc, 5 * sc, 22 * sc, 22 * sc);
        p.setPen(QPen(QColor("#C084FC"), 1.8f * sc));
        p.setBrush(QColor(30, 20, 45));
        p.drawRoundedRect(frame, 3 * sc, 3 * sc);

        const float hx = 16 * sc, hy = 16 * sc;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(192, 132, 252, 200));
        p.drawRect(QRectF(6 * sc, 6 * sc, 10 * sc, 10 * sc));
        p.drawRect(QRectF(hx, hy, 10 * sc, 10 * sc));

        p.setBrush(QColor(147, 51, 234, 160));
        p.drawRect(QRectF(hx, 6 * sc, 10 * sc, 10 * sc));
        p.drawRect(QRectF(6 * sc, hy, 10 * sc, 10 * sc));
    });
    return icon;
}

// 6. FNT icon: BMFont typography descriptor with prominent "F" in Warm Orange (#FB923C)
QIcon get_fnt_icon() {
    static QIcon icon = create_multi_res_icon([](QPainter& p, int s) {
        const float sc = s / 32.0f;
        QRectF card(5 * sc, 5 * sc, 22 * sc, 22 * sc);
        p.setPen(QPen(QColor("#FB923C"), 1.8f * sc));
        p.setBrush(QColor(45, 25, 15));
        p.drawRoundedRect(card, 3 * sc, 3 * sc);

        QFont font("Sans-serif", std::max(7, static_cast<int>(13 * sc)), QFont::Bold);
        font.setStyleHint(QFont::SansSerif);
        p.setFont(font);
        p.setPen(QColor("#FED7AA"));
        p.drawText(card, Qt::AlignCenter, QStringLiteral("F"));
    });
    return icon;
}

// 7. LUA script icon: Blue document with code brackets
QIcon get_lua_icon() {
    static QIcon icon = create_multi_res_icon([](QPainter& p, int s) {
        const float sc = s / 32.0f;
        QRectF doc(6 * sc, 4 * sc, 20 * sc, 24 * sc);
        p.setPen(QPen(QColor("#60A5FA"), 1.6f * sc));
        p.setBrush(QColor(15, 30, 60));
        p.drawRoundedRect(doc, 3 * sc, 3 * sc);

        QFont font("Monospace", std::max(6, static_cast<int>(10 * sc)), QFont::Bold);
        p.setFont(font);
        p.setPen(QColor("#93C5FD"));
        p.drawText(doc, Qt::AlignCenter, QStringLiteral("{}"));
    });
    return icon;
}

// 8. Raster Image icon (png, jpg): Teal framed image
QIcon get_image_icon() {
    static QIcon icon = create_multi_res_icon([](QPainter& p, int s) {
        const float sc = s / 32.0f;
        QRectF frame(5 * sc, 5 * sc, 22 * sc, 22 * sc);
        p.setPen(QPen(QColor("#2DD4BF"), 1.6f * sc));
        p.setBrush(QColor(15, 45, 40));
        p.drawRoundedRect(frame, 3 * sc, 3 * sc);

        p.setPen(QPen(QColor("#5EEAD4"), 1.4f * sc));
        p.setBrush(QColor(45, 212, 191, 100));
        QPolygonF m({QPointF(6 * sc, 22 * sc), QPointF(13 * sc, 14 * sc),
                     QPointF(18 * sc, 19 * sc), QPointF(22 * sc, 15 * sc),
                     QPointF(26 * sc, 22 * sc)});
        p.drawPolygon(m);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#99F6E4"));
        p.drawEllipse(QPointF(20 * sc, 10 * sc), 2 * sc, 2 * sc);
    });
    return icon;
}

// 9. RubyMesh collision / mesh data icon (rbm, swdm): Rose/Red wireframe diamond
QIcon get_mesh_icon() {
    static QIcon icon = create_multi_res_icon([](QPainter& p, int s) {
        const float sc = s / 32.0f;
        p.setPen(QPen(QColor("#F43F5E"), 1.8f * sc));
        p.setBrush(QColor(244, 63, 94, 60));
        QPolygonF poly({QPointF(16 * sc, 5 * sc), QPointF(27 * sc, 16 * sc),
                        QPointF(16 * sc, 27 * sc), QPointF(5 * sc, 16 * sc)});
        p.drawPolygon(poly);
        p.drawLine(QPointF(16 * sc, 5 * sc), QPointF(16 * sc, 27 * sc));
        p.drawLine(QPointF(5 * sc, 16 * sc), QPointF(27 * sc, 16 * sc));
    });
    return icon;
}

// 10. Source code icon (c, cpp, h, js, ts, glsl): Indigo document with lines
QIcon get_code_icon() {
    static QIcon icon = create_multi_res_icon([](QPainter& p, int s) {
        const float sc = s / 32.0f;
        QRectF doc(6 * sc, 4 * sc, 20 * sc, 24 * sc);
        p.setPen(QPen(QColor("#818CF8"), 1.6f * sc));
        p.setBrush(QColor(25, 25, 55));
        p.drawRoundedRect(doc, 3 * sc, 3 * sc);

        p.setPen(QPen(QColor("#A5B4FC"), 1.5f * sc, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(10 * sc, 10 * sc), QPointF(20 * sc, 10 * sc));
        p.drawLine(QPointF(10 * sc, 14 * sc), QPointF(22 * sc, 14 * sc));
        p.drawLine(QPointF(10 * sc, 18 * sc), QPointF(17 * sc, 18 * sc));
    });
    return icon;
}

// 11. Generic document icon (txt, md, json, xml, cfg): Neutral slate document
QIcon get_doc_icon() {
    static QIcon icon = create_multi_res_icon([](QPainter& p, int s) {
        const float sc = s / 32.0f;
        QRectF doc(6 * sc, 4 * sc, 20 * sc, 24 * sc);
        p.setPen(QPen(QColor("#94A3B8"), 1.5f * sc));
        p.setBrush(QColor(30, 35, 45));
        p.drawRoundedRect(doc, 3 * sc, 3 * sc);

        p.setPen(QPen(QColor("#CBD5E1"), 1.2f * sc, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(10 * sc, 11 * sc), QPointF(21 * sc, 11 * sc));
        p.drawLine(QPointF(10 * sc, 16 * sc), QPointF(21 * sc, 16 * sc));
        p.drawLine(QPointF(10 * sc, 21 * sc), QPointF(17 * sc, 21 * sc));
    });
    return icon;
}

// Predefined extension to icon mapping
QIcon get_file_icon(const QString& ext) {
    if (ext == "pod" || ext == "glb" || ext == "gltf" || ext == "obj" || ext == "fbx")
        return get_pod_icon();
    if (ext == "scene" || ext == "scn")
        return get_scene_icon();
    if (ext == "scl")
        return get_scl_icon();
    if (ext == "pvr" || ext == "tex")
        return get_pvr_icon();
    if (ext == "fnt")
        return get_fnt_icon();
    if (ext == "lua")
        return get_lua_icon();
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp")
        return get_image_icon();
    if (ext == "rbm" || ext == "swdm" || ext == "gmesh")
        return get_mesh_icon();
    if (ext == "c" || ext == "cpp" || ext == "cc" || ext == "cxx" ||
        ext == "h" || ext == "hpp" || ext == "hxx" || ext == "js" ||
        ext == "ts" || ext == "glsl" || ext == "vert" || ext == "frag")
        return get_code_icon();
    return get_doc_icon();
}

class SwordigoAssetModel final : public QFileSystemModel {
public:
    using QFileSystemModel::QFileSystemModel;

    QVariant data(const QModelIndex& index, int role) const override {
        // Intercept icon decoration: use our predefined custom controlled icons (never OS auto-decide)
        if (role == Qt::DecorationRole && index.column() == 0) {
            if (isDir(index)) return get_folder_icon();
            const QString ext = QFileInfo(filePath(index)).suffix().toLower();
            return get_file_icon(ext);
        }
        if (role == Qt::DecorationRole && index.column() != 0) {
            return QVariant();
        }

        if (role == Qt::DisplayRole && index.column() == 2) {
            if (isDir(index)) return QStringLiteral("Folder");
            const QString ext = QFileInfo(filePath(index)).suffix().toLower();
            if (ext == "pod") return QStringLiteral("POD Model");
            if (ext == "pvr" || ext == "tex") return QStringLiteral("PVR Texture");
            if (ext == "glb" || ext == "gltf") return QStringLiteral("glTF Model");
            if (ext == "obj") return QStringLiteral("Wavefront Model");
            if (ext == "fbx") return QStringLiteral("FBX Model");
            if (ext == "lua") return QStringLiteral("Lua Script");
            if (ext == "scl") return QStringLiteral("Object Library");
            if (ext == "scene" || ext == "scn") return QStringLiteral("Scene");
            if (ext == "swdm" || ext == "gmesh") return QStringLiteral("Mesh Data");
            if (ext == "pvr.png" || ext == "png" || ext == "jpg" || ext == "jpeg") return QStringLiteral("Texture Image");
            if (ext == "rbm") return QStringLiteral("RubyMesh");
            if (ext == "fnt") return QStringLiteral("Font");
            if (ext == "c" || ext == "cpp" || ext == "cc" || ext == "cxx") return QStringLiteral("C/C++ Source");
            if (ext == "h" || ext == "hpp" || ext == "hxx") return QStringLiteral("Header");
            if (ext == "js" || ext == "ts") return QStringLiteral("JavaScript");
            if (ext == "json") return QStringLiteral("JSON");
            if (ext == "xml") return QStringLiteral("XML");
            if (ext == "html" || ext == "htm" || ext == "css") return QStringLiteral("Markup");
            if (ext == "txt" || ext == "md") return QStringLiteral("Document");
            if (ext == "glsl" || ext == "vert" || ext == "frag" || ext == "geom") return QStringLiteral("Shader");
            if (ext == "ini" || ext == "cfg" || ext == "yaml" || ext == "yml") return QStringLiteral("Config");
            return QStringLiteral("File");
        }
        return QFileSystemModel::data(index, role);
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
            if (section == 0) return QStringLiteral("Name");
            if (section == 1) return QStringLiteral("Size");
            if (section == 2) return QStringLiteral("Asset Type");
        }
        return QFileSystemModel::headerData(section, orientation, role);
    }
};

} // namespace

namespace ruby::panels {

AssetBrowserPanel::AssetBrowserPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_search_box = new QLineEdit(this);
    m_search_box->setPlaceholderText("Filter files (e.g. *.scl, pod, main)...");
    connect(m_search_box, &QLineEdit::textChanged, this, &AssetBrowserPanel::onFilterChanged);
    layout->addWidget(m_search_box);

    m_file_model = new SwordigoAssetModel(this);
    m_file_model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::AllDirs);
    m_file_model->setNameFilterDisables(false);
    m_file_model->setNameFilters({});

    m_tree_view = new QTreeView(this);
    m_tree_view->setModel(m_file_model);
    m_tree_view->setHeaderHidden(false);
    m_tree_view->setAnimated(true);
    m_tree_view->setIndentation(16);
    m_tree_view->setSortingEnabled(true);
    m_tree_view->setTextElideMode(Qt::ElideRight);

    // Hide Size (column 1) and Date Modified (column 3) completely
    m_tree_view->setColumnHidden(1, true);
    m_tree_view->setColumnHidden(3, true);

    // Prioritize Name column: section 0 stretches to fill panel width.
    // Section 2 (Asset Type) is interactive and dynamically contracts when panel shrinks.
    m_tree_view->header()->setStretchLastSection(false);
    m_tree_view->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree_view->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_tree_view->header()->setMinimumSectionSize(35);
    m_tree_view->setColumnWidth(2, 110);

    m_tree_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree_view->setDragEnabled(true);
    m_tree_view->setDragDropMode(QAbstractItemView::DragOnly);
    connect(m_tree_view, &QTreeView::customContextMenuRequested, this, &AssetBrowserPanel::onCustomContextMenu);
    connect(m_tree_view, &QTreeView::doubleClicked, this, &AssetBrowserPanel::onItemDoubleClicked);
    layout->addWidget(m_tree_view);

    // ── Quick-sync watcher ─────────────────────────────────────────────
    // QFileSystemModel refreshes directories it is currently showing, but it
    // can lag behind rapid external writes and never recovers if the root
    // path did not exist yet at set_root_path() time. An explicit watcher +
    // debounced forced refresh makes brand-new files appear immediately.
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &AssetBrowserPanel::onDirectoryChanged);

    m_refresh_timer = new QTimer(this);
    m_refresh_timer->setSingleShot(true);
    m_refresh_timer->setInterval(120);   // coalesce save = write+rename bursts
    connect(m_refresh_timer, &QTimer::timeout, this, &AssetBrowserPanel::onDebouncedRefresh);

    m_root_poll = new QTimer(this);
    m_root_poll->setSingleShot(false);
    m_root_poll->setInterval(1500);      // keep retrying a late-appearing root
    connect(m_root_poll, &QTimer::timeout, this, &AssetBrowserPanel::onRootPoll);
}

void AssetBrowserPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    update_column_widths();
}

void AssetBrowserPanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    update_column_widths();
}

void AssetBrowserPanel::update_column_widths() {
    if (!m_tree_view || !m_tree_view->header()) return;
    const int total_w = m_tree_view->viewport()->width();
    if (total_w <= 0) return;

    // Give primary priority to Name column.
    // When the panel is contracted (< 250px), aggressively shrink Asset Type down to 40px
    // so the Name column remains comfortably visible.
    int type_w = 110;
    if (total_w < 250) {
        type_w = std::clamp(total_w - 110, 40, 110);
    }
    m_tree_view->setColumnWidth(2, type_w);
}

void AssetBrowserPanel::set_root_path(const QString& path) {
    m_file_model->setRootPath(path);
    m_tree_view->setRootIndex(m_file_model->index(path));
    rewatch_root();
}

// Re-point the OS watcher at the model root. If the root does not exist yet
// (e.g. a project whose assets/ tree is created on first boot), poll until it
// shows up, then re-apply the root so the model starts listing it.
void AssetBrowserPanel::rewatch_root() {
    if (!m_watcher) return;
    const QString root = m_file_model->rootPath();
    // Manage only the root path here — subdirectory watches added by
    // onDirectoryChanged() must survive refreshes.
    if (!root.isEmpty() && m_watcher->directories().contains(root))
        m_watcher->removePath(root);
    if (QFileInfo::exists(root) && QDir(root).isReadable()) {
        m_watcher->addPath(root);
        m_root_poll->stop();
    } else if (!root.isEmpty()) {
        m_root_poll->start();
    }
}

void AssetBrowserPanel::onRootPoll() {
    const QString root = m_file_model->rootPath();
    if (root.isEmpty()) {
        m_root_poll->stop();
        return;
    }
    if (!QFileInfo::exists(root) || !QDir(root).isReadable()) return;
    // Root appeared — start watching and force the model to list it now.
    m_root_poll->stop();
    m_watcher->addPath(root);
    refresh_now();
}

void AssetBrowserPanel::schedule_refresh(const QString& path) {
    m_pending_refresh = path;
    if (m_refresh_timer) m_refresh_timer->start();
}

void AssetBrowserPanel::onDirectoryChanged(const QString& path) {
    // Keep an eye on any brand-new subdirectories so files dropped into them
    // are noticed too (the model only watches what it has listed).
    if (QFileInfo::exists(path) && QDir(path).isReadable()) {
        const QFileInfoList dirs = QDir(path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& fi : dirs) {
            if (fi.isDir() && !m_watcher->directories().contains(fi.absoluteFilePath()))
                m_watcher->addPath(fi.absoluteFilePath());
        }
    }
    schedule_refresh(path);
}

void AssetBrowserPanel::onDebouncedRefresh() {
    const QString path = m_pending_refresh;
    m_pending_refresh.clear();
    if (path.isEmpty()) return;
    // The model auto-syncs subdirectories it is showing; force only the root
    // listing (re-applying the root re-populates it and is safe: model
    // indices stay stable, so selection and expansion are preserved).
    if (path == m_file_model->rootPath() || path.isEmpty())
        refresh_now();
}

void AssetBrowserPanel::refresh_now() {
    if (!m_file_model) return;
    const QString root = m_file_model->rootPath();
    if (root.isEmpty()) return;
    m_file_model->setRootPath(root);
    m_tree_view->setRootIndex(m_file_model->index(root));
    rewatch_root();
}

QString AssetBrowserPanel::current_selected_folder() const {
    QModelIndex idx = m_tree_view->currentIndex();
    if (idx.isValid()) {
        QString p = m_file_model->filePath(idx);
        if (m_file_model->isDir(idx)) return p;
        return QFileInfo(p).absolutePath();
    }
    return m_file_model->rootPath();
}

void AssetBrowserPanel::onItemDoubleClicked(const QModelIndex& index) {
    if (!m_file_model->isDir(index)) {
        emit fileSelected(m_file_model->filePath(index));
    }
}

void AssetBrowserPanel::onFilterChanged(const QString& filter) {
    const QString trimmed = filter.trimmed();
    if (trimmed.isEmpty()) {
        m_file_model->setNameFilters({});
    } else {
        if (trimmed.contains('*')) {
            m_file_model->setNameFilters({ trimmed });
        } else {
            m_file_model->setNameFilters({ "*" + trimmed + "*" });
        }
    }
}

void AssetBrowserPanel::onCustomContextMenu(const QPoint& pt) {
    QModelIndex idx = m_tree_view->indexAt(pt);
    QString selected_path = idx.isValid() ? m_file_model->filePath(idx) : QString();
    QString target_folder = current_selected_folder();

    auto* menu = new QMenu(this);

    auto* new_file_act = menu->addAction("New File...");
    connect(new_file_act, &QAction::triggered, this, [this, target_folder]() {
        emit newFileRequested(target_folder);
    });

    if (idx.isValid() && !m_file_model->isDir(idx)) {
        auto* open_act = menu->addAction("Open in Editor");
        connect(open_act, &QAction::triggered, this, [this, selected_path]() {
            emit fileSelected(selected_path);
        });

        const QString ext = QFileInfo(selected_path).suffix().toLower();
        if (ext == "pod") {
            auto* add_to_scene_act = menu->addAction("Add Model to Scene");
            connect(add_to_scene_act, &QAction::triggered, this, [this, selected_path]() {
                emit addModelToSceneRequested(selected_path);
            });
        }
        if (ext == "glb" || ext == "gltf" || ext == "fbx" || ext == "obj" || ext == "pod") {
            auto* conv_act = menu->addAction("Convert to Game POD...");
            connect(conv_act, &QAction::triggered, this, [this, selected_path]() {
                emit convertModelRequested(selected_path);
            });
        }
    }

    menu->addSeparator();

    if (!selected_path.isEmpty()) {
        auto* copy_path_act = menu->addAction("Copy Full Path");
        connect(copy_path_act, &QAction::triggered, [selected_path]() {
            QGuiApplication::clipboard()->setText(selected_path);
        });
    }

    auto* reveal_act = menu->addAction("Reveal in System File Manager");
    connect(reveal_act, &QAction::triggered, [target_folder, selected_path]() {
        QString to_open = selected_path.isEmpty() ? target_folder : selected_path;
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(to_open).isDir() ? to_open : QFileInfo(to_open).absolutePath()));
    });

    menu->exec(m_tree_view->viewport()->mapToGlobal(pt));
    delete menu;
}

} // namespace ruby::panels
