// ============================================================================
// ruby_main_window.cpp — Main Studio Window Implementation
// ============================================================================

#include "ruby_main_window.h"
#include "ruby/viewport/viewport_3d_widget.h"
#include "ruby/editor/script_ide_widget.h"
#include "ruby/panels/asset_browser_panel.h"
#include "ruby/panels/inspector_panel.h"
#include "ruby/panels/animation_control_bar.h"
#include "ruby/panels/texture_viewer_panel.h"
#include "ruby/panels/audio_viewer_panel.h"
#include "ruby/panels/scene_hierarchy_panel.h"
#include "ruby/panels/template_palette_panel.h"
#include "ruby/panels/template_inspector_panel.h"
#include "ruby/panels/console_panel.h"
#include "ruby/panels/lighting_panel.h"
#include "ruby/panels/local_history_panel.h"
#include "ruby/git/ruby_git.h"
#include "ruby/editor/ruby_title_bar.h"
#include "ruby/core/project_context.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include "ruby/tools/ruby_tools_workspace.h"
#include "ruby/editor/doc_viewer_dialog.h"
#include "ruby/editor/new_file_dialog.h"
#include "ruby/editor/model_convert_dialog.h"
#include "ruby/editor/studio_idle_widget.h"
#include "ruby/editor/apk_session_panel.h"
#include "tools/scene_loader.h"      // av::scene_serialize / scene_save (structured edits)
#include "tools/scene_workspace.h"   // swk::recompute_ground_mesh_geometry
#include "tools/filerift.h"          // decode_protobuf — structured RAM → FileRift text
#include "ruby/emulator/engine_preview_panel.h"
#include "ruby/emulator/engine_pod.h"
#include "ruby/emulator/workspace_detect.h"
#include "ruby/graph/graphy_canvas.h"
#include <fstream>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QSaveFile>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QApplication>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWindow>
#include <thread>
#include <QStyle>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QScrollArea>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QTimer>
#include <QPolygonF>

namespace ruby {

RubyMainWindow::RubyMainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setWindowTitle("Ruby Studio");
    resize(1440, 900);
    // Rich docking: tabs, nesting, animation and grouped dragging so every
    // panel can be torn off, floated anywhere, and re-docked freely.
    setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks |
                   QMainWindow::AnimatedDocks | QMainWindow::GroupedDragging);

    // ── IDE-style hierarchy ────────────────────────────────────────────────
    // Document bar (one tab per open file) sits ABOVE the viewing-mode tabs.
    m_central = new QWidget(this);
    auto* central_layout = new QVBoxLayout(m_central);
    central_layout->setContentsMargins(0, 0, 0, 0);
    central_layout->setSpacing(0);

    m_doc_tabs = new QTabBar(m_central);
    m_doc_tabs->setTabsClosable(true);
    m_doc_tabs->setMovable(true);
    m_doc_tabs->setExpanding(false);
    m_doc_tabs->setDocumentMode(true);
    m_doc_tabs->setUsesScrollButtons(true);
    m_doc_tabs->setVisible(false);
    central_layout->addWidget(m_doc_tabs);

    m_mode_tabs = new QTabWidget(m_central);
    m_mode_tabs->setTabsClosable(false);
    m_mode_tabs->setMovable(true);
    m_mode_tabs->setDocumentMode(true);

    // Mode pages use small stacks so an "unsupported here" notice can replace
    // the viewer content when the active document can't open in that mode.
    m_viewport_3d = new ruby::viewport::Viewport3DWidget(m_central);
    auto* page_3d = new QWidget(m_central);
    {
        auto* layout = new QVBoxLayout(page_3d);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        // ── Model Top Bar (visible when viewing a 3D model/POD in the viewport) ──
        m_model_top_bar = new QWidget(page_3d);
        m_model_top_bar->setObjectName(QStringLiteral("modelTopBar"));
        m_model_top_bar->setStyleSheet(QStringLiteral(
            "QWidget#modelTopBar { background: #16181d; border-bottom: 1px solid #282c34; min-height: 34px; max-height: 34px; }"
            "QLabel { color: #abb2bf; font-size: 11px; }"
            "QPushButton#modelConvertBtn { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #2962ff, stop:1 #00b0ff); "
            "  color: #ffffff; border: none; border-radius: 4px; padding: 4px 12px; font-weight: bold; font-size: 11px; }"
            "QPushButton#modelConvertBtn:hover { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #3d74ff, stop:1 #1ec0ff); }"
            "QPushButton#modelResetCamBtn { background: #282c34; color: #abb2bf; border: 1px solid #3e4451; "
            "  border-radius: 4px; padding: 4px 10px; font-size: 11px; }"
            "QPushButton#modelResetCamBtn:hover { background: #323742; color: #e5e9f0; }"));

        auto* bar_layout = new QHBoxLayout(m_model_top_bar);
        bar_layout->setContentsMargins(12, 3, 12, 3);
        bar_layout->setSpacing(10);

        m_model_name_label = new QLabel(m_model_top_bar);
        m_model_name_label->setStyleSheet(QStringLiteral("font-weight: bold; color: #e5e9f0; font-size: 12px;"));

        m_model_format_badge = new QLabel(m_model_top_bar);
        m_model_format_badge->setStyleSheet(QStringLiteral("background: #282c34; color: #61afef; border-radius: 3px; padding: 1px 6px; font-size: 10px; font-weight: bold;"));

        m_model_stats_label = new QLabel(m_model_top_bar);
        m_model_stats_label->setStyleSheet(QStringLiteral("color: #7d8492; font-size: 11px;"));

        bar_layout->addWidget(m_model_name_label);
        bar_layout->addWidget(m_model_format_badge);
        bar_layout->addWidget(m_model_stats_label);
        bar_layout->addStretch(1);

        auto* reset_cam_btn = new QPushButton(QStringLiteral("⟲ Reset Camera"), m_model_top_bar);
        reset_cam_btn->setObjectName(QStringLiteral("modelResetCamBtn"));
        reset_cam_btn->setCursor(Qt::PointingHandCursor);
        connect(reset_cam_btn, &QPushButton::clicked, m_viewport_3d, &ruby::viewport::Viewport3DWidget::reset_camera);
        bar_layout->addWidget(reset_cam_btn);

        m_model_convert_btn = new QPushButton(QStringLiteral("✦ Convert to Game POD..."), m_model_top_bar);
        m_model_convert_btn->setObjectName(QStringLiteral("modelConvertBtn"));
        m_model_convert_btn->setCursor(Qt::PointingHandCursor);
        m_model_convert_btn->setToolTip(QStringLiteral("Open conversion dialog to scale and export this 3D model with ETC1 compressed PVR textures."));
        connect(m_model_convert_btn, &QPushButton::clicked, this, [this]() {
            QString src_path;
            if (m_active_doc >= 0 && m_active_doc < m_docs.size()) {
                src_path = m_docs[m_active_doc].path;
            }
            onConvertModel(src_path);
        });
        bar_layout->addWidget(m_model_convert_btn);

        m_model_top_bar->setVisible(false);
        layout->addWidget(m_model_top_bar);

        m_3d_stack = new QStackedWidget(page_3d);
        m_3d_notice = new QLabel(page_3d);
        m_3d_notice->setAlignment(Qt::AlignCenter);
        m_3d_notice->setWordWrap(true);
        m_3d_notice->setStyleSheet("QLabel { color:#9aa3b2; font-size:14px; padding:24px; }");
        m_3d_stack->addWidget(m_viewport_3d);   // StackView
        m_3d_stack->addWidget(m_3d_notice);     // StackNotice
        layout->addWidget(m_3d_stack, 1);
    }
    m_mode_tabs->addTab(page_3d, "3D Viewport");

    m_script_ide = new ruby::editor::ScriptIDEWidget(m_central);
    auto* page_ide = new QWidget(m_central);
    {
        auto* layout = new QVBoxLayout(page_ide);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        m_ide_stack = new QStackedWidget(page_ide);
        m_ide_notice = new QLabel(page_ide);
        m_ide_notice->setAlignment(Qt::AlignCenter);
        m_ide_notice->setWordWrap(true);
        m_ide_notice->setStyleSheet("QLabel { color:#9aa3b2; font-size:14px; padding:24px; }");
        m_ide_stack->addWidget(m_script_ide);   // StackView
        m_ide_stack->addWidget(m_ide_notice);   // StackNotice
        layout->addWidget(m_ide_stack, 1);

        auto* bottom_bar = new QWidget(page_ide);
        bottom_bar->setStyleSheet("QWidget { background:#14171d; border-top:1px solid #262b34; }");
        auto* bar_layout = new QHBoxLayout(bottom_bar);
        bar_layout->setContentsMargins(8, 2, 10, 2);
        bar_layout->setSpacing(12);

        m_ide_status = new QLabel("No file open", bottom_bar);
        m_ide_status->setStyleSheet("QLabel { color:#7d8492; border:none; background:transparent; font-size:12px; }");
        bar_layout->addWidget(m_ide_status, 1);

        m_filerift_toggle = new QCheckBox("FileRift: Encode to Binary on Save", bottom_bar);
        m_filerift_toggle->setChecked(true);
        m_filerift_toggle->setCursor(Qt::PointingHandCursor);
        m_filerift_toggle->setStyleSheet(
            "QCheckBox { color:#abb2bf; font-size:12px; spacing:6px; border:none; background:transparent; }"
            "QCheckBox:disabled { color:#5c6370; }"
            "QCheckBox::indicator { width:13px; height:13px; border-radius:3px; border:1px solid #3e4451; background:#1e2227; }"
            "QCheckBox::indicator:checked { background:#61afef; border-color:#61afef; }"
            "QCheckBox::indicator:disabled { background:#181a1f; border-color:#282c34; }"
        );
        connect(m_filerift_toggle, &QCheckBox::toggled, this, [this](bool checked) {
            if (m_active_doc >= 0 && m_active_doc < m_docs.size()) {
                RubyDocEntry& doc = m_docs[m_active_doc];
                doc.encode_filerift = checked;
                m_script_ide->set_filerift_encode_on_save(checked);
                update_filerift_toggle_ui();
                // Binary-format types can never be saved as raw text — warn
                // immediately so the user doesn't discover it at save time.
                if (!checked) {
                    const QString ext = QFileInfo(doc.path).suffix().toLower();
                    const bool binary_format = doc.kind == "scene" || ext == "scl" ||
                                               ext == "swdm" || ext == "gmesh";
                    if (binary_format) {
                        ruby::core::ProjectContext::instance().set_status(
                            "Warning: Raw Text saves are blocked for " +
                            QFileInfo(doc.path).fileName() +
                            " — it is a binary format the engine must load.");
                    }
                }
            }
        });
        bar_layout->addWidget(m_filerift_toggle);

        layout->addWidget(bottom_bar);
    }
    m_mode_tabs->addTab(page_ide, "Script IDE");

    m_texture_viewer = new ruby::panels::TextureViewerPanel(m_central);
    auto* page_texture = new QWidget(m_central);
    {
        auto* layout = new QVBoxLayout(page_texture);
        layout->setContentsMargins(0, 0, 0, 0);
        m_tex_stack = new QStackedWidget(page_texture);
        m_tex_notice = new QLabel(page_texture);
        m_tex_notice->setAlignment(Qt::AlignCenter);
        m_tex_notice->setWordWrap(true);
        m_tex_notice->setStyleSheet("QLabel { color:#9aa3b2; font-size:14px; padding:24px; }");
        m_tex_stack->addWidget(m_texture_viewer); // StackView
        m_tex_stack->addWidget(m_tex_notice);     // StackNotice
        layout->addWidget(m_tex_stack);
    }
    m_mode_tabs->addTab(page_texture, "Texture Viewer");

    // Authoring tools become a full central page (not a cramped dock).
    m_tools = new ruby::tools::RubyToolsWorkspace(m_central);
    m_mode_tabs->addTab(m_tools, "Scene Tools");

    // Audio Viewer: WAV / MP3 / OGG playback with waveform (ImGui asset
    // viewer parity). Lives at the end so the 3D/IDE/texture/tools indices
    // above never shift.
    m_audio_viewer = new ruby::panels::AudioViewerPanel(m_central);
    auto* page_audio = new QWidget(m_central);
    {
        auto* layout = new QVBoxLayout(page_audio);
        layout->setContentsMargins(0, 0, 0, 0);
        m_audio_stack = new QStackedWidget(page_audio);
        m_audio_notice = new QLabel(page_audio);
        m_audio_notice->setAlignment(Qt::AlignCenter);
        m_audio_notice->setWordWrap(true);
        m_audio_notice->setStyleSheet("QLabel { color:#9aa3b2; font-size:14px; padding:24px; }");
        m_audio_stack->addWidget(m_audio_viewer); // StackView
        m_audio_stack->addWidget(m_audio_notice); // StackNotice
        layout->addWidget(m_audio_stack);
    }
    m_mode_tabs->addTab(page_audio, "Audio Viewer");
    // Decode results feed the status bar / console.
    connect(m_audio_viewer, &ruby::panels::AudioViewerPanel::loadFinished,
            this, [this](const QString& path, bool ok, const QString& detail) {
                const QString msg = ok
                    ? "Loaded audio: " + QFileInfo(path).fileName() +
                      (detail.isEmpty() ? QString() : " \u2014 " + detail)
                    : "Could not load audio: " + QFileInfo(path).fileName() +
                      (detail.isEmpty() ? QString() : " \u2014 " + detail);
                ruby::core::ProjectContext::instance().set_status(msg);
            });

    // Node Graph Editor (Graphy Unreal-style visual node editor)
    m_graph_canvas = new ruby::graph::GraphyCanvas(m_central);
    m_mode_tabs->addTab(m_graph_canvas, "Node Graph");

    // Central stack: displays m_idle_widget when no documents are open, or m_mode_tabs when files are active
    m_central_stack = new QStackedWidget(m_central);
    m_idle_widget = new ruby::editor::StudioIdleWidget(m_central);
    m_central_stack->addWidget(m_idle_widget); // Index 0: Idle / No file open
    m_central_stack->addWidget(m_mode_tabs);   // Index 1: Active document viewers
    m_central_stack->setCurrentIndex(0);

    central_layout->addWidget(m_central_stack, 1);
    setCentralWidget(m_central);

    connect(m_doc_tabs, &QTabBar::currentChanged,
            this, [this](int index) { activate_document(index); });
    connect(m_doc_tabs, &QTabBar::tabCloseRequested,
            this, [this](int index) { close_document(index); });
    // Switching modes never shows stale content: re-sync what each mode page
    // displays for the ACTIVE document.
    connect(m_mode_tabs, &QTabWidget::currentChanged,
            this, [this](int tab_idx) {
                if (m_active_doc >= 0 && m_active_doc < m_docs.size()) {
                    m_docs[m_active_doc].active_mode_tab = tab_idx;
                }
                if (tab_idx == 3 && m_viewport_3d && m_viewport_3d->has_scene()) {
                    const auto cam = m_viewport_3d->camera_state();
                    if (m_tools) m_tools->set_scene_camera_focus(cam.target[0], cam.target[1]);
                }
                if (tab_idx == 1 && m_active_doc >= 0 && m_active_doc < m_docs.size()) {
                    if (m_script_ide->current_file_path() != m_docs[m_active_doc].path) {
                        load_doc_into_ide(m_active_doc);
                    }
                } else if (tab_idx != 1) {
                    snapshot_active_script();
                }
                // SCL scene sync: flipping from FileRift text to the 3D
                // viewport shows unsaved text edits (re-encoded in memory,
                // never saved to disk) so the visual editor never goes stale.
                if (tab_idx == 0 && m_active_doc >= 0 && m_active_doc < m_docs.size() &&
                    m_docs[m_active_doc].kind == "scene") {
                    apply_unsaved_scene_text_to_viewport(m_docs[m_active_doc].path);
                }
                // Audio Viewer: (re)load the active audio document when the
                // tab is entered; leaving it silences playback (hideEvent is
                // a safety net for occluded/minimized windows).
                if (tab_idx == 4 && m_active_doc >= 0 && m_active_doc < m_docs.size() &&
                    m_docs[m_active_doc].kind == "audio") {
                    m_audio_viewer->load_audio(m_docs[m_active_doc].path);
                } else if (tab_idx != 4 && m_audio_viewer) {
                    m_audio_viewer->stop();
                }
                sync_views();
            });
    // Leaving the Script IDE content (notice page shown) preserves unsaved
    // text in the per-document buffer before the editor is re-pointed.
    connect(m_ide_stack, &QStackedWidget::currentChanged,
            this, [this](int) { snapshot_active_script(); });
    // Real-time dirty state tracking for editor tabs
    connect(m_script_ide->document(), &QTextDocument::modificationChanged,
            this, [this](bool modified) {
                if (m_loading_doc) return;
                const QString path = m_script_ide->current_file_path();
                if (path.isEmpty()) return;
                if (modified) {
                    m_dirty_scripts.insert(path);
                } else {
                    m_dirty_scripts.remove(path);
                }
                refresh_tab_labels();
            });
    connect(m_script_ide, &QPlainTextEdit::textChanged,
            this, [this]() {
                if (m_loading_doc) return;
                const QString path = m_script_ide->current_file_path();
                if (path.isEmpty()) return;
                if (m_script_ide->document()->isModified()) {
                    if (!m_dirty_scripts.contains(path)) {
                        m_dirty_scripts.insert(path);
                        refresh_tab_labels();
                    }
                }
            });
    // File loads are asynchronous (charter rule 1): cache the RAM buffer into
    // the per-document store the moment the widget finishes adopting content.
    connect(m_script_ide, &ruby::editor::ScriptIDEWidget::documentLoaded,
            this, [this](const QString& path) {
                if (path.isEmpty()) return;
                if (m_script_buffers.contains(path)) return;  // already cached
                m_script_buffers[path] = m_script_ide->full_text();
            });
    // Cursor position & live FileRift diagnostics feed the IDE status line
    auto update_ide_status = [this]() {
        if (!m_ide_status) return;
        QTextCursor c = m_script_ide->textCursor();
        const QString file = QFileInfo(m_script_ide->current_file_path()).fileName();
        int errors = 0, warnings = 0;
        for (const auto& d : m_script_ide->diagnostics()) {
            if (d.severity == ruby::filerift::Diagnostic::Error) ++errors;
            else if (d.severity == ruby::filerift::Diagnostic::Warning) ++warnings;
        }
        QString diag_str;
        if (errors > 0 || warnings > 0) {
            diag_str = QString("   ·   <span style='color:%1'>%2 Errors</span>, <span style='color:%3'>%4 Warnings</span>")
                .arg(errors > 0 ? "#e06c75" : "#5c6370")
                .arg(errors)
                .arg(warnings > 0 ? "#e5c07b" : "#5c6370")
                .arg(warnings);
        } else {
            diag_str = QStringLiteral("   ·   <span style='color:#98c379'>✓ FileRift Verified</span>");
        }
        if (m_script_ide->has_lua_chunks()) {
            diag_str += QStringLiteral("   ·   <span style='color:#61afef'>$ Lua Chunks</span>");
        }
        m_ide_status->setText(QString("%1   ·   Ln %2, Col %3%4   ·   UTF-8   ·   Ctrl+Space Autocomplete")
            .arg(file.isEmpty() ? "untitled" : file)
            .arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1)
            .arg(diag_str));
    };
    connect(m_script_ide, &QPlainTextEdit::cursorPositionChanged, this, update_ide_status);
    connect(m_script_ide, &ruby::editor::ScriptIDEWidget::analysisCompleted, this, update_ide_status);

    auto* menu_bar = new QMenuBar(this);
    setup_menus(menu_bar);

    // APK session badge (master TODO 4.1a): sits permanently in the status
    // bar, hidden until an APK is imported. Created before the menus so the
    // File ▸ Import APK… action can hand straight to it.
    m_apk_session = new ruby::editor::ApkSessionPanel(this);
    statusBar()->addPermanentWidget(m_apk_session);
    connect(m_apk_session, &ruby::editor::ApkSessionPanel::sessionChanged,
            this, [this](bool active) {
                if (m_export_apk_act) m_export_apk_act->setEnabled(active);
            });
    connect(m_apk_session, &ruby::editor::ApkSessionPanel::statusMessage,
            this, [this](const QString& msg, int ms) {
                ruby::core::ProjectContext::instance().set_status(msg, ms);
            });

    auto* title_bar = new ruby::editor::RubyTitleBar(this, menu_bar);
    setMenuWidget(title_bar);

    // Top panel is intentionally kept free (only the slim title bar above the
    // central area). Every action lives on the left vertical rail instead.
    setup_left_rail();
    setup_dock_panels();

    // The 3D viewport must give way when side docks are widened — never the
    // inspector / preview column. Small minimums make Qt shrink the central
    // widget first instead of squeezing the right rail.
    m_central->setMinimumSize(180, 120);
    m_mode_tabs->setMinimumSize(180, 120);

    // View menu is built before the docks exist; attach the Panels section now.
    // Every dock gets a respawn toggle here, so a panel closed by accident can
    // always be brought back (and re-attached) from View > Panels, plus a
    // one-click reset that re-docks everything to the canonical layout.
    if (menu_bar) {
        for (QMenu* m : menu_bar->findChildren<QMenu*>()) {
            if (m->title() != "&View") continue;
            build_panels_menu(m);
            break;
        }
    }

    QSettings settings;
    restoreGeometry(settings.value("ruby_gg/windowGeometry").toByteArray());
    restoreState(settings.value("ruby_gg/dockState").toByteArray());

    // Connect model loading to inspector
    connect(m_viewport_3d, &ruby::viewport::Viewport3DWidget::modelLoaded,
            this, &RubyMainWindow::onModelLoaded);

    // Debounced structured-edit sync (perf charter rule 1): gizmo commits and
    // inspector changes used to re-encode the whole scene + decode the FileRift
    // text + reload the editor + rebuild the hierarchy synchronously — seconds
    // of freeze per change. The dirty flag is set immediately; the expensive
    // resync coalesces into one pass after the burst settles.
    m_scene_sync_timer = new QTimer(this);
    m_scene_sync_timer->setSingleShot(true);
    m_scene_sync_timer->setInterval(250);
    connect(m_scene_sync_timer, &QTimer::timeout, this, &RubyMainWindow::flush_pending_scene_sync);
    // App-wide Ctrl+Z / Ctrl+Y: scene undo from any panel (inspector, hierarchy,
    // docks) — the viewport-only key handling made undo feel broken everywhere
    // else. The Script IDE keeps its own Qt text undo.
    if (QApplication::instance())
        QApplication::instance()->installEventFilter(this);

    statusBar()->showMessage("Ruby GG Studio Ready.");
    sync_views();
    update_engine_scene_context();
}

RubyMainWindow::~RubyMainWindow() {
    QSettings settings;
    settings.setValue("ruby_gg/windowGeometry", saveGeometry());
    settings.setValue("ruby_gg/dockState", saveState());
}

void RubyMainWindow::setup_menus(QMenuBar* menu_bar) {
    if (!menu_bar) return;
    auto* file_menu = menu_bar->addMenu("&File");
    auto* new_act = file_menu->addAction("&New File...", QKeySequence::New, this, [this]() { onNewFile(); });
    new_act->setToolTip("Create a new game asset or script (Ctrl+N)");
    file_menu->addAction("&Open Project Folder...", QKeySequence::Open, this, &RubyMainWindow::onOpenProject);
    file_menu->addAction("&Open File...", this, &RubyMainWindow::onOpenFile);
    file_menu->addAction("&Save Active Script", QKeySequence::Save, this, &RubyMainWindow::onSaveFile);
    file_menu->addSeparator();

    // APK session workflow (master TODO 4.1a): Import extracts the whole APK
    // into ~/.ruby/apk-sessions/<id>/ and points the workspace at it; Export
    // (4.1b repack + 4.1c signer) is only reachable once a session is live.
    file_menu->addAction("&Import APK…", this, &RubyMainWindow::onImportApk);
    m_export_apk_act = file_menu->addAction("Export as &APK…", this, &RubyMainWindow::onExportApk);
    m_export_apk_act->setEnabled(false);
    m_export_apk_act->setToolTip(
        "Repack the active APK session back into an .apk "
        "(available after File ▸ Import APK…)");
    file_menu->addSeparator();
    file_menu->addAction("&Exit", QKeySequence::Quit, this, &QWidget::close);

    auto* view_menu = menu_bar->addMenu("&View");
    view_menu->addAction("Reset 3D Camera", QKeySequence(Qt::Key_F), m_viewport_3d, &ruby::viewport::Viewport3DWidget::reset_camera);
    auto* act_effects = view_menu->addAction("In-Game Dynamic Effects (Water, Particles, Portals)", QKeySequence(Qt::Key_P), [this]() {
        m_viewport_3d->set_render_effects(!m_viewport_3d->render_effects());
    });
    act_effects->setCheckable(true);
    act_effects->setChecked(m_viewport_3d->render_effects());
    view_menu->addAction("Authoring & Production Tools", this, &RubyMainWindow::onOpenTools);
    view_menu->addSeparator();
    if (m_console_dock) view_menu->addAction(m_console_dock->toggleViewAction());

    auto* filerift_menu = menu_bar->addMenu("&FileRift");
    filerift_menu->addAction("Decode Binary Protobuf to Markup", [this]() {
        if (m_script_ide->decode_current_binary()) {
            ruby::core::ProjectContext::instance().set_status("Decoded binary file to FileRift markup.");
        } else {
            ruby::core::ProjectContext::instance().set_status("Failed to decode binary file.");
        }
    });
    filerift_menu->addAction("Recode Markup to Binary Protobuf", [this]() {
        if (m_script_ide->recode_current_markup()) {
            ruby::core::ProjectContext::instance().set_status("Recoded FileRift markup to binary file.");
        } else {
            ruby::core::ProjectContext::instance().set_status("Failed to recode markup.");
        }
    });
    filerift_menu->addSeparator();
    filerift_menu->addAction("Re-run Semantic Diagnostics", [this]() {
        m_script_ide->trigger_analysis();
    });

    auto* tools_menu = menu_bar->addMenu("&Tools");
    tools_menu->addAction("✦ &Convert 3D Model to Game POD...", this, [this]() {
        QString src_path;
        if (m_active_doc >= 0 && m_active_doc < m_docs.size()) {
            if (m_docs[m_active_doc].kind == "model") src_path = m_docs[m_active_doc].path;
        }
        onConvertModel(src_path);
    });
    tools_menu->addAction("Authoring & Production Tools...", this, &RubyMainWindow::onOpenTools);

    auto* help_menu = menu_bar->addMenu("&Help");
    help_menu->addAction("&Engine & FileRift Documentation...", QKeySequence::HelpContents, this, &RubyMainWindow::onOpenDocumentation);
    help_menu->addSeparator();
    help_menu->addAction("&About Ruby Studio", [this]() {
        QMessageBox::about(this, "About Ruby Studio GG",
            "<h3>Ruby Studio (Qt6 Edition)</h3>"
            "<p>Next-generation high-performance editor and IDE for Swordigo reverse-engineering and modding.</p>"
            "<p>Featuring native 3D OpenGL viewport, virtualized code editor, FileRift binary markup, "
            "and a multi-document workspace where every viewer points at the active file.</p>");
    });
}

void RubyMainWindow::setup_left_rail() {
    // Android-Studio-style left rail. The top panel stays free; every studio
    // action (New / Open Project / Save / Frame / Tools / Docs) plus the
    // phone ▶ boot button for the inline engine live here as icon buttons.
    auto* rail = addToolBar("Left Rail");
    rail->setObjectName("StudioLeftRail");
    rail->setMovable(false);
    rail->setOrientation(Qt::Vertical);
    rail->setToolButtonStyle(Qt::ToolButtonIconOnly);
    rail->setIconSize(QSize(18, 18));
    addToolBar(Qt::LeftToolBarArea, rail);

    // ── Phone ▶ button (runs / focuses the inline 1.4.13 engine) ──────
    QPixmap pm(22, 22);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(QColor(0xe5, 0xe9, 0xf0), 1.6);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(4.5, 1.5, 13, 19), 3.0, 3.0);
        p.drawLine(QPointF(9.5, 2.4), QPointF(12.5, 2.4));
        p.drawLine(QPointF(9.0, 17.6), QPointF(13.0, 17.6));
        QColor accent(0x61, 0xaf, 0xef);
        pen.setColor(accent); pen.setWidthF(1.4);
        p.setPen(pen);
        QPolygonF tri;
        tri << QPointF(9.2, 8.0) << QPointF(9.2, 14.0) << QPointF(14.6, 11.0);
        p.setBrush(accent);
        p.drawPolygon(tri);
    }
    auto* run = rail->addAction(QIcon(pm), "Run 1.4.13 (inline engine)",
                                this, &RubyMainWindow::onEngineBootClicked);
    run->setToolTip("Boot / focus the inline Swordigo 1.4.13 emulator\n(Engine Preview dock)");
    rail->addSeparator();

    // ── Studio file / view actions ──────────────────────────────────────
    auto* new_btn = rail->addAction(style()->standardIcon(QStyle::SP_FileIcon),
                                    "New", this, [this]() { onNewFile(); });
    new_btn->setToolTip("Create New Asset or Script (Ctrl+N)");
    auto* open = rail->addAction(style()->standardIcon(QStyle::SP_DirOpenIcon),
                                 "Open Project", this, &RubyMainWindow::onOpenProject);
    open->setToolTip("Open Project Folder (Ctrl+O)");
    auto* save = rail->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton),
                                 "Save", this, &RubyMainWindow::onSaveFile);
    save->setToolTip("Save Active Script (Ctrl+S)");
    rail->addSeparator();
    auto* frame = rail->addAction(style()->standardIcon(QStyle::SP_BrowserReload),
                                  "Frame", m_viewport_3d,
                                  &ruby::viewport::Viewport3DWidget::reset_camera);
    frame->setToolTip("Frame selection / reset camera (F)");
    auto* tools = rail->addAction(style()->standardIcon(QStyle::SP_ComputerIcon),
                                  "Tools", this, &RubyMainWindow::onOpenTools);
    tools->setToolTip("Authoring & production tools");
    auto* docs_btn = rail->addAction(style()->standardIcon(QStyle::SP_FileDialogContentsView),
                                     "Docs", this, &RubyMainWindow::onOpenDocumentation);
    docs_btn->setToolTip("Open Engine & FileRift Documentation (F1)");
}

void RubyMainWindow::setup_dock_panels() {
    // Left: Asset Browser
    m_asset_dock = new QDockWidget("Asset Browser", this);
    m_asset_dock->setObjectName("AssetBrowserDock");
    m_asset_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_asset_browser = new ruby::panels::AssetBrowserPanel(m_asset_dock);
    m_asset_dock->setWidget(m_asset_browser);
    addDockWidget(Qt::LeftDockWidgetArea, m_asset_dock);

    connect(m_asset_browser, &ruby::panels::AssetBrowserPanel::fileSelected,
            this, &RubyMainWindow::onFileSelectedInBrowser);
    connect(m_asset_browser, &ruby::panels::AssetBrowserPanel::newFileRequested,
            this, &RubyMainWindow::onNewFile);
    connect(m_asset_browser, &ruby::panels::AssetBrowserPanel::convertModelRequested,
            this, &RubyMainWindow::onConvertModel);

    // Right: Properties Inspector (wrapped in scroll area to prevent overlapping)
    m_inspector_dock = new QDockWidget("Inspector", this);
    m_inspector_dock->setObjectName("InspectorDock");
    m_inspector_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_inspector = new ruby::panels::InspectorPanel(m_inspector_dock);
    auto* inspector_scroll = new QScrollArea(m_inspector_dock);
    inspector_scroll->setWidgetResizable(true);
    inspector_scroll->setFrameShape(QFrame::NoFrame);
    inspector_scroll->setWidget(m_inspector);
    m_inspector_dock->setWidget(inspector_scroll);
    addDockWidget(Qt::RightDockWidgetArea, m_inspector_dock);

    // Lighting rig dock (live sun/fill/ambient/fog for the 3D viewers, wrapped in scroll area).
    m_lighting_dock = new QDockWidget("Lighting", this);
    m_lighting_dock->setObjectName("LightingDock");
    m_lighting_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_lighting = new ruby::panels::LightingPanel(m_lighting_dock);
    auto* lighting_scroll = new QScrollArea(m_lighting_dock);
    lighting_scroll->setWidgetResizable(true);
    lighting_scroll->setFrameShape(QFrame::NoFrame);
    lighting_scroll->setWidget(m_lighting);
    m_lighting_dock->setWidget(lighting_scroll);
    addDockWidget(Qt::RightDockWidgetArea, m_lighting_dock);
    tabifyDockWidget(m_inspector_dock, m_lighting_dock);
    connect(m_lighting, &ruby::panels::LightingPanel::lightingChanged,
            m_viewport_3d, &ruby::viewport::Viewport3DWidget::set_lighting);
    // The panel emits at construction before this connect exists — push now.
    m_viewport_3d->set_lighting(m_lighting->lighting());

    // Bottom: Animation Bar (visible only in POD / Model viewer)
    m_animation_dock = new QDockWidget("Animation", this);
    m_animation_dock->setObjectName("AnimationDock");
    m_animation_dock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    m_animation_controls = new ruby::panels::AnimationControlBar(m_animation_dock);
    m_animation_dock->setWidget(m_animation_controls);
    addDockWidget(Qt::BottomDockWidgetArea, m_animation_dock);
    m_animation_dock->setVisible(false);
    connect(m_animation_controls, &ruby::panels::AnimationControlBar::frameChanged,
            m_viewport_3d, &ruby::viewport::Viewport3DWidget::set_frame);

    m_scene_dock = new QDockWidget("Scene Outliner", this);
    m_scene_dock->setObjectName("SceneOutlinerDock");
    m_scene_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_scene_hierarchy = new ruby::panels::SceneHierarchyPanel(m_scene_dock);
    m_scene_dock->setWidget(m_scene_hierarchy);
    addDockWidget(Qt::LeftDockWidgetArea, m_scene_dock);
    tabifyDockWidget(m_asset_dock, m_scene_dock);

    // Template Palette (master TODO 2.3): scene templates + model assets you
    // can drop into the open scene. Models get a measured LocalAABB on add.
    m_template_palette_dock = new QDockWidget("Template Palette", this);
    m_template_palette_dock->setObjectName("TemplatePaletteDock");
    m_template_palette_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_template_palette = new ruby::panels::TemplatePalettePanel(m_template_palette_dock);
    m_template_palette_dock->setWidget(m_template_palette);
    addDockWidget(Qt::LeftDockWidgetArea, m_template_palette_dock);
    tabifyDockWidget(m_scene_dock, m_template_palette_dock);
    connect(m_template_palette, &ruby::panels::TemplatePalettePanel::addTemplateRequested,
            this, &RubyMainWindow::on_template_add_requested);
    connect(m_template_palette, &ruby::panels::TemplatePalettePanel::addModelRequested,
            this, &RubyMainWindow::on_model_add_requested);

    // Template Inspector (master TODO 2.4): the selected object's template
    // hierarchy — inherited vs local components, retarget/unlink/materialize.
    m_template_inspector_dock = new QDockWidget("Template", this);
    m_template_inspector_dock->setObjectName("TemplateInspectorDock");
    m_template_inspector_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_template_inspector = new ruby::panels::TemplateInspectorPanel(m_template_inspector_dock);
    m_template_inspector_dock->setWidget(m_template_inspector);
    addDockWidget(Qt::RightDockWidgetArea, m_template_inspector_dock);
    tabifyDockWidget(m_inspector_dock, m_template_inspector_dock);
    connect(m_template_inspector, &ruby::panels::TemplateInspectorPanel::templateChanged,
            this, &RubyMainWindow::on_template_changed);
    connect(m_template_inspector, &ruby::panels::TemplateInspectorPanel::materializeRequested,
            this, &RubyMainWindow::on_template_materialize);
    connect(m_template_inspector, &ruby::panels::TemplateInspectorPanel::resetToTemplateRequested,
            this, &RubyMainWindow::on_template_reset);
    connect(m_template_inspector, &ruby::panels::TemplateInspectorPanel::overrideComponentRequested,
            this, &RubyMainWindow::on_template_override);
    connect(m_scene_hierarchy, &ruby::panels::SceneHierarchyPanel::objectSelected,
            [](int index) { ruby::core::ProjectContext::instance().set_selected_object(index); });
    connect(m_viewport_3d, &ruby::viewport::Viewport3DWidget::sceneObjectSelected,
            this, [](int index) {
                ruby::core::ProjectContext::instance().set_selected_object(index);
            });
    connect(m_viewport_3d, &ruby::viewport::Viewport3DWidget::sceneEdited,
            this, &RubyMainWindow::on_viewport_scene_edited);

    // Selection changes propagate to viewport, hierarchy outliner, and inspector
    connect(&ruby::core::ProjectContext::instance(), &ruby::core::ProjectContext::selectionChanged,
            this, [this](int index) {
                m_viewport_3d->set_selected_object(index);
                m_scene_hierarchy->select_object(index);
                if (m_viewport_3d->has_scene()) {
                    m_inspector->inspect_scene_object(m_viewport_3d->scene(), index);
                    refresh_scene_templates(/*rescan=*/false);   // cheap: cached catalog
                }
            });

    // Real-time gizmo dragging syncs inspector spinboxes live
    connect(m_viewport_3d, &ruby::viewport::Viewport3DWidget::sceneObjectTransformed,
            this, [this](int idx, float px, float py, float pz,
                                float rx, float rz, float ry,
                                float sx, float sy, float sz) {
                if (m_inspector->current_object_index() == idx) {
                    m_inspector->update_transform(px, py, pz, rx, rz, ry, sx, sy, sz);
                }
            });

    // Scene hierarchy actions
    connect(m_scene_hierarchy, &ruby::panels::SceneHierarchyPanel::objectVisibilityChanged,
            m_viewport_3d, &ruby::viewport::Viewport3DWidget::set_scene_object_visibility);
    connect(m_scene_hierarchy, &ruby::panels::SceneHierarchyPanel::objectFocusRequested,
            m_viewport_3d, &ruby::viewport::Viewport3DWidget::focus_object);
    connect(m_scene_hierarchy, &ruby::panels::SceneHierarchyPanel::objectCreated,
            m_viewport_3d, &ruby::viewport::Viewport3DWidget::add_scene_object);
    connect(m_scene_hierarchy, &ruby::panels::SceneHierarchyPanel::objectDuplicated,
            m_viewport_3d, &ruby::viewport::Viewport3DWidget::duplicate_scene_object);
    connect(m_scene_hierarchy, &ruby::panels::SceneHierarchyPanel::objectDeleted,
            m_viewport_3d, &ruby::viewport::Viewport3DWidget::delete_scene_object);

    // Clipboard keyboard shortcuts (ImGui asset_viewer parity): Del/Backspace
    // deletes, Ctrl+C copies, Ctrl+V pastes (fresh identifier + nudge, cross-
    // scene safe), Ctrl+D duplicates (copy + paste), Alt+Up/Down reorders.
    connect(m_scene_hierarchy, &ruby::panels::SceneHierarchyPanel::copyRequested,
            m_viewport_3d, &ruby::viewport::Viewport3DWidget::copy_scene_selection);
    connect(m_scene_hierarchy, &ruby::panels::SceneHierarchyPanel::pasteRequested,
            m_viewport_3d, &ruby::viewport::Viewport3DWidget::paste_scene_selection);
    connect(m_scene_hierarchy, &ruby::panels::SceneHierarchyPanel::duplicateRequested,
            m_viewport_3d, &ruby::viewport::Viewport3DWidget::duplicate_scene_selection);
    connect(m_scene_hierarchy, &ruby::panels::SceneHierarchyPanel::deleteRequested,
            m_viewport_3d, &ruby::viewport::Viewport3DWidget::delete_scene_selection);
    connect(m_scene_hierarchy, &ruby::panels::SceneHierarchyPanel::moveRequested,
            m_viewport_3d, &ruby::viewport::Viewport3DWidget::move_scene_object);

    // Inspector object property editing actions. Spinboxes fire valueChanged on
    // EVERY keystroke; the RAM mutation + viewport refresh below are cheap and
    // stay per-keystroke (the object follows the cursor), but the heavy sync
    // and the undo entry are coalesced: one undo step per edit burst instead of
    // one per digit (which made Ctrl+Z walk through every intermediate value
    // and the whole chain freezed for seconds per keystroke).
    connect(m_inspector, &ruby::panels::InspectorPanel::objectTransformChanged,
            this, [this](int idx, float px, float py, float pz,
                                float rx, float rz, float ry,
                                float sx, float sy, float sz) {
                if (!m_viewport_3d->has_scene()) return;
                auto& sc = m_viewport_3d->editable_scene();
                if (idx < 0 || idx >= static_cast<int>(sc.objects.size())) return;
                // One snapshot per burst, captured at the FIRST change.
                if (!m_scene_sync_undo_armed) {
                    m_scene_sync_undo_before = m_viewport_3d->capture_scene_snapshot();
                    m_scene_sync_undo_armed = true;
                    m_scene_sync_undo_path = m_viewport_3d->scene().filepath.empty()
                        ? QString() : QString::fromStdString(m_viewport_3d->scene().filepath);
                }
                auto& o = sc.objects[idx];
                o.pos_x = px; o.pos_y = py; o.pos_z = pz;
                o.rot_x = rx; o.rot_z = rz; o.rot_y = ry;
                o.scale_x = sx; o.scale_y = sy; o.scale_z = sz;
                m_viewport_3d->refresh_edited_object();
                m_viewport_3d->update();
                // Skip a full inspector rebuild when the selection did not move.
                if (m_viewport_3d->selected_object() != idx)
                    m_viewport_3d->set_selected_object(idx);
                on_viewport_scene_edited();
            });
    connect(m_inspector, &ruby::panels::InspectorPanel::objectIdentityChanged,
            this, [this](int idx, const QString& name, const QString& tmpl) {
                if (!m_viewport_3d->has_scene()) return;
                auto& sc = m_viewport_3d->editable_scene();
                if (idx >= 0 && idx < static_cast<int>(sc.objects.size())) {
                    sc.objects[idx].name = name.toStdString();
                    sc.objects[idx].template_name = tmpl.toStdString();
                    m_scene_hierarchy->set_scene(sc);
                    m_scene_hierarchy->select_object(idx);
                    on_viewport_scene_edited();
                }
            });
    connect(m_inspector, &ruby::panels::InspectorPanel::objectHiddenChanged,
            m_viewport_3d, &ruby::viewport::Viewport3DWidget::set_scene_object_visibility);
    connect(m_inspector, &ruby::panels::InspectorPanel::componentAdded,
            this, [this](int idx, const QString& comp_type) {
                if (!m_viewport_3d->has_scene()) return;
                auto& sc = m_viewport_3d->editable_scene();
                const std::string before = m_viewport_3d->capture_scene_snapshot();
                if (av::scene_add_component(sc, static_cast<size_t>(idx), comp_type.toStdString())) {
                    m_viewport_3d->refresh_edited_object();
                    // Bridge Gap 3: append the new component widget incrementally
                    // instead of tearing down and rebuilding the entire list.
                    if (m_inspector->current_object_index() == idx &&
                        !sc.objects[idx].components.empty()) {
                        m_inspector->append_component(sc.objects[idx].components.back());
                    } else {
                        m_inspector->inspect_scene_object(sc, idx);
                    }
                    on_viewport_scene_edited();
                    m_viewport_3d->push_scene_snapshot_undo(before, "Add Component");
                }
            });
    connect(m_inspector, &ruby::panels::InspectorPanel::componentRemoved,
            this, [this](int idx, int comp_idx) {
                if (!m_viewport_3d->has_scene()) return;
                auto& sc = m_viewport_3d->editable_scene();
                const std::string before = m_viewport_3d->capture_scene_snapshot();
                if (av::scene_remove_component(sc, static_cast<size_t>(idx), static_cast<size_t>(comp_idx))) {
                    m_viewport_3d->refresh_edited_object();
                    m_inspector->inspect_scene_object(sc, idx);
                    on_viewport_scene_edited();
                    m_viewport_3d->push_scene_snapshot_undo(before, "Remove Component");
                }
            });
    connect(m_inspector, &ruby::panels::InspectorPanel::componentPasted,
            this, [this](int idx, const av::SceneComponent& comp) {
                if (!m_viewport_3d->has_scene()) return;
                auto& sc = m_viewport_3d->editable_scene();
                const std::string before = m_viewport_3d->capture_scene_snapshot();
                size_t new_index = 0;
                if (av::scene_paste_component(sc, static_cast<size_t>(idx), comp, &new_index)) {
                    m_viewport_3d->refresh_edited_object();
                    // Bridge Gap 3: append the pasted component incrementally.
                    if (m_inspector->current_object_index() == idx &&
                        new_index < sc.objects[idx].components.size()) {
                        m_inspector->append_component(sc.objects[idx].components[new_index]);
                    } else {
                        m_inspector->inspect_scene_object(sc, idx);
                    }
                    on_viewport_scene_edited();
                    m_viewport_3d->push_scene_snapshot_undo(before, "Paste Component");
                }
            });
    connect(m_inspector, &ruby::panels::InspectorPanel::componentFieldChanged,
            this, [this](int idx, int comp_idx, const av::SceneComponentField& field) {
                if (!m_viewport_3d->has_scene()) return;
                auto& sc = m_viewport_3d->editable_scene();
                const std::string before = m_viewport_3d->capture_scene_snapshot();
                if (idx >= 0 && idx < static_cast<int>(sc.objects.size())) {
                    auto& comps = sc.objects[idx].components;
                    if (comp_idx >= 0 && comp_idx < static_cast<int>(comps.size())) {
                        if (av::scene_set_component_field(comps[comp_idx], field)) {
                            m_viewport_3d->refresh_edited_object();
                            // Bridge: re-inspect so the panel shows any derived
                            // values or layout changes the field edit caused.
                            if (m_inspector->current_object_index() == idx)
                                m_inspector->inspect_scene_object(sc, idx);
                            on_viewport_scene_edited();
                            m_viewport_3d->push_scene_snapshot_undo(before, "Edit Component Field");
                        }
                    }
                }
            });
    connect(m_inspector, &ruby::panels::InspectorPanel::groundMeshRegenNormals,
            this, [this](int idx) {
                if (!m_viewport_3d->has_scene()) return;
                auto& sc = m_viewport_3d->editable_scene();
                const std::string before = m_viewport_3d->capture_scene_snapshot();
                if (idx >= 0 && idx < static_cast<int>(sc.objects.size())) {
                    for (auto& gm : sc.objects[idx].ground_meshes) {
                        swk::recompute_ground_mesh_geometry(gm);
                    }
                    m_viewport_3d->refresh_edited_object();
                    m_viewport_3d->update();
                    // Bridge: re-inspect so ground mesh stats refresh.
                    if (m_inspector->current_object_index() == idx)
                        m_inspector->inspect_scene_object(sc, idx);
                    on_viewport_scene_edited();
                    m_viewport_3d->push_scene_snapshot_undo(before, "Regen Ground Normals");
                }
            });

    connect(m_viewport_3d, &ruby::viewport::Viewport3DWidget::sceneLoaded,
            m_scene_hierarchy, &ruby::panels::SceneHierarchyPanel::set_scene);
    connect(m_viewport_3d, &ruby::viewport::Viewport3DWidget::sceneLoaded,
            this, [this](const av::SceneData& sc) {
                const bool perf = std::getenv("RUBY_GG_PERF") != nullptr;
                auto pt0 = std::chrono::steady_clock::now();
                auto perf_ms = [&](const char* tag) {
                    if (!perf) return;
                    std::fprintf(stderr, "[perf sceneLoaded] %s: %.1f ms\n", tag,
                        std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - pt0).count());
                    pt0 = std::chrono::steady_clock::now();
                };
                int sel = m_viewport_3d->selected_object();
                if (sel >= 0 && sel < static_cast<int>(sc.objects.size())) {
                    m_inspector->inspect_scene_object(sc, sel);
                    m_scene_hierarchy->select_object(sel);
                } else {
                    m_inspector->clear_inspection();
                }
                perf_ms("inspector");
                refresh_scene_templates();
                perf_ms("refresh_scene_templates");
                QString status = QString("Scene loaded: %1 objects, %2 libraries")
                    .arg(sc.objects.size())
                    .arg(sc.imported_library_names.size());
                if (!sc.missing_libraries.empty()) {
                    status += QString(" (%1 missing: %2)")
                        .arg(sc.missing_libraries.size())
                        .arg(QString::fromStdString(sc.missing_libraries[0]));
                }
                ruby::core::ProjectContext::instance().set_status(status);
                perf_ms("status");
            });
    connect(m_viewport_3d, &ruby::viewport::Viewport3DWidget::sceneLoadingFailed,
            this, [this](const QString& err) {
                ruby::core::ProjectContext::instance().set_status("Scene load error: " + err);
            });

    // ── Inspector ↔ 3D viewport sync bridge ──────────────────────────────────
    // Bridge Gap 5: undo/redo refreshes the inspector so spinboxes always
    // match the restored scene state. Every scene snapshot undo/redo calls
    // restore_scene_snapshot → apply_scene_data → emit sceneEdited, but the
    // inspector is only rebuilt on explicit selection change. Connecting to the
    // per-scene undo stack's indexChanged ensures the inspector stays live
    // across Ctrl+Z / Ctrl+Y.
    connect(m_viewport_3d, &ruby::viewport::Viewport3DWidget::sceneObjectRenderStateChanged,
            this, [this](int object_index) {
                if (!m_viewport_3d->has_scene() || !m_inspector) return;
                // Only refresh if the affected object is the one being inspected.
                if (m_inspector->current_object_index() == object_index) {
                    m_inspector->inspect_scene_object(m_viewport_3d->scene(), object_index);
                }
            });
    // The viewport owns per-scene QUndoStack instances; the active stack changes
    // when switching documents. We re-connect on each scene load to wire the
    // current stack's indexChanged to the inspector refresh.
    connect(m_viewport_3d, &ruby::viewport::Viewport3DWidget::sceneLoaded,
            this, [this](const av::SceneData&) {
                // Disconnect any previous undo stack connection (safe if none).
                disconnect(m_undo_stack_conn);
                QUndoStack* stack = m_viewport_3d->undo_stack();
                if (stack) {
                    m_undo_stack_conn = connect(stack, &QUndoStack::indexChanged,
                        this, [this](int) {
                            sync_inspector_from_viewport();
                        });
                }
            });

    // Bottom: Terminal / Console (VS Code style).
    m_console_dock = new QDockWidget("Terminal", this);
    m_console_dock->setObjectName("ConsoleDock");
    m_console_dock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    m_console = new ruby::panels::ConsolePanel(m_console_dock);
    m_console_dock->setWidget(m_console);
    addDockWidget(Qt::BottomDockWidgetArea, m_console_dock);
    tabifyDockWidget(m_animation_dock, m_console_dock);
    connect(m_console, &ruby::panels::ConsolePanel::openFileRequested,
            this, [this](const QString& path) { open_document(path); });
    connect(&ruby::core::ProjectContext::instance(), &ruby::core::ProjectContext::statusMessage,
            this, [this](const QString& msg, int) {
                if (m_console) m_console->append_line(msg);
                statusBar()->showMessage(msg, 5000);
            });

    // Bottom: Local History (Git timeline & FileRift semantic diff)
    m_history_dock = new QDockWidget("Local History", this);
    m_history_dock->setObjectName("LocalHistoryDock");
    m_history_dock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::RightDockWidgetArea);
    m_history = new ruby::panels::LocalHistoryPanel(m_history_dock);
    m_history_dock->setWidget(m_history);
    addDockWidget(Qt::BottomDockWidgetArea, m_history_dock);
    tabifyDockWidget(m_console_dock, m_history_dock);
    connect(m_history, &ruby::panels::LocalHistoryPanel::fileRestored,
            this, [this](const QString& path) {
                int idx = find_document(path);
                if (idx >= 0) close_document(idx);
                open_document(path);
            });

    connect(m_tools, &ruby::tools::RubyToolsWorkspace::objectImported,
            this, &RubyMainWindow::onFileSelectedInBrowser);
    connect(m_tools, &ruby::tools::RubyToolsWorkspace::groundMeshApplied,
            this, [this](const QString& path) {
                ruby::core::ProjectContext::instance().set_status("Ground mesh written: " + path);
            });
    connect(m_tools, &ruby::tools::RubyToolsWorkspace::groundMeshAddToScene,
            this, &RubyMainWindow::on_ground_mesh_add_to_scene);
    connect(m_tools, &ruby::tools::RubyToolsWorkspace::collisionApplied,
            this, [this](const QString& path) {
                ruby::core::ProjectContext::instance().set_status("RubyMesh apply requested: " + path);
            });
    connect(m_tools, &ruby::tools::RubyToolsWorkspace::rendererQualityChanged,
            this, [this](int, bool wireframe, bool grid) {
                m_viewport_3d->set_wireframe(wireframe);
                m_viewport_3d->set_grid_visible(grid);
            });

    // Right rail: inline Engine Preview — the phone-sized 1.4.13 mini
    // emulator dock (Android-Studio style). Stacked below the Inspector.
    m_engine_dock = new QDockWidget("Engine Preview", this);
    m_engine_dock->setObjectName("EnginePreviewDock");
    m_engine_dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea | Qt::BottomDockWidgetArea);
    m_engine_dock->setMinimumWidth(340);
    m_engine_panel = new ruby::emulator::EnginePreviewPanel(m_engine_dock);
    m_engine_dock->setWidget(m_engine_panel);
    addDockWidget(Qt::RightDockWidgetArea, m_engine_dock);
    splitDockWidget(m_inspector_dock, m_engine_dock, Qt::Vertical);
    // Give the column a sensible landscape-first default; fit_engine_preview_dock()
    // re-derives the exact 16:9 geometry when a session actually starts.
    resizeDocks({ m_inspector_dock, m_engine_dock }, { 460, 460 }, Qt::Horizontal);
    resizeDocks({ m_inspector_dock, m_engine_dock }, { 220, 420 }, Qt::Vertical);
    // When the engine session goes live, reshape the dock so the preview is a
    // landscape 16:9 area (image fills it, no giant black portrait void). The
    // space comes from the central 3D viewport / wider window, not by closing
    // or starving the Inspector above it.
    connect(m_engine_panel->pod(), &ruby::emulator::EnginePod::stateChanged,
            this, [this](int s) {
                if ((s == ruby::emulator::EnginePod::kRunning ||
                     s == ruby::emulator::EnginePod::kPaused) &&
                    m_engine_dock && m_engine_dock->isVisible()) {
                    QTimer::singleShot(0, this,
                                       &RubyMainWindow::fit_engine_preview_dock);
                }
            });
    connect(m_engine_panel, &ruby::emulator::EnginePreviewPanel::statusMessage,
            this, [this](const QString& msg, int ms) {
                if (m_console) m_console->append_line(msg);
                statusBar()->showMessage(msg, ms);
            });
    // "▶ Run Scene" in the engine dock: save the FileRift scene edits
    // (re-encode to the binary .scene), then dispatch through the SRE Scene
    // Shifter so the running 1.4.13 engine loads the scene.
    connect(m_engine_panel, &ruby::emulator::EnginePreviewPanel::runSceneRequested,
            this, &RubyMainWindow::on_engine_run_scene);
    // Raijin: connect the bottom console's Lua tab to the engine pod's Lua
    // console whenever a session is live (and disconnect when it stops).
    connect(m_engine_panel->pod(), &ruby::emulator::EnginePod::stateChanged,
            this, [this](int s) {
                if (!m_console) return;
                if (s == ruby::emulator::EnginePod::kRunning ||
                    s == ruby::emulator::EnginePod::kPaused) {
                    m_console->set_lua_port(m_engine_panel->pod()->lua_console_port());
                } else if (s == ruby::emulator::EnginePod::kStopped ||
                           s == ruby::emulator::EnginePod::kExited ||
                           s == ruby::emulator::EnginePod::kCrashed) {
                    m_console->set_lua_port(0);
                }
            });

    // Default project root: the shipped game data lives under
    // ~/.local/share/swordigo-desktop/assets/resources — make that the
    // browser's starting directory (fall back to assets/ itself).
    const QString assets_root = QDir::homePath() + "/.local/share/swordigo-desktop/assets";
    const QString resources_root = assets_root + "/resources";
    m_asset_browser->set_root_path(QFileInfo::exists(resources_root) ? resources_root : assets_root);

    // Quietly detect a good engine-asset workspace in the background and seed
    // the preview dock with it (never blocks startup).
    QTimer::singleShot(250, this, [this]() {
        if (!m_engine_panel) return;
        // Dist deployment first: run_ruby_gg.sh points SWORDIGO_DATA_DIR at
        // the bundled data/ tree, which ships assets*/resources.
        if (const char* sd = getenv("SWORDIGO_DATA_DIR"); sd && sd[0]) {
            const QString data_dir = QString::fromLocal8Bit(sd);
            if (QFileInfo::exists(data_dir + "/assets/resources")) {
                m_engine_panel->set_assets_dir(data_dir + "/assets");
                return;
            }
        }
        if (QFileInfo::exists(QDir::homePath() +
                              "/.local/share/swordigo-desktop/assets/resources")) {
            m_engine_panel->set_assets_dir(QDir::homePath() +
                                           "/.local/share/swordigo-desktop/assets");
            return;
        }
        const auto& proj = ruby::core::ProjectContext::instance().project_dir();
        std::vector<std::string> roots;
        if (!proj.empty()) roots.push_back(proj);
        roots.push_back(QDir::homePath().toStdString());
        auto cands = ruby::emulator::detect_from_roots(roots);
        if (!cands.empty()) {
            m_engine_panel->set_assets_dir(
                QString::fromStdString(cands.front().instance_dir));
        }
    });

    // Guarantee every panel is closable, movable and floatable (torn off into
    // its own draggable window). If a future change ever removes a feature
    // flag, this keeps the rescue UX in View > Panels working.
    const QList<QDockWidget*> all_docks = { m_asset_dock, m_inspector_dock,
                                            m_lighting_dock, m_animation_dock,
                                            m_scene_dock, m_template_palette_dock,
                                            m_template_inspector_dock,
                                            m_console_dock,
                                            m_history_dock, m_engine_dock };
    for (QDockWidget* d : all_docks) {
        if (!d) continue;
        d->setFeatures(QDockWidget::DockWidgetMovable |
                       QDockWidget::DockWidgetFloatable |
                       QDockWidget::DockWidgetClosable);
        d->setMinimumSize(220, 140);   // keep floated panels usable
    }

    // Wayland: Qt moves floating docks via QWidget::move(), which compositors
    // ignore for toplevel windows, so a floated panel's title bar cannot be
    // dragged there (KDE Plasma Wayland and GNOME Wayland alike). Intercept
    // title-strip presses on floating docks and use the interactive move
    // protocol instead. X11/Windows keep Qt's native drag (drag-to-redock
    // included).
    m_wayland_drag_filter =
        QGuiApplication::platformName().startsWith(QLatin1String("wayland"),
                                                   Qt::CaseInsensitive);
    if (m_wayland_drag_filter) {
        for (QDockWidget* d : all_docks) {
            if (d) d->installEventFilter(this);
        }
    }
}

bool RubyMainWindow::eventFilter(QObject* watched, QEvent* event) {
    // App-wide scene undo/redo: Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y from ANY panel
    // (inspector spinboxes, hierarchy, docks) — the viewport-only key handler
    // made undo feel broken everywhere else. The Script IDE keeps its own Qt
    // text undo (route_undo_shortcut decides).
    if (event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        const bool is_undo_key = (ke->key() == Qt::Key_Z || ke->key() == Qt::Key_Y) &&
                                 ke->modifiers().testFlag(Qt::ControlModifier) &&
                                 !ke->modifiers().testFlag(Qt::AltModifier) &&
                                 !ke->modifiers().testFlag(Qt::MetaModifier);
        if (is_undo_key && route_undo_shortcut(ke))
            return true;   // consumed — the focused widget must not re-handle it
        return QMainWindow::eventFilter(watched, event);
    }
    if (!m_wayland_drag_filter || !watched || event->type() != QEvent::MouseButtonPress)
        return QMainWindow::eventFilter(watched, event);

    QDockWidget* dock = qobject_cast<QDockWidget*>(watched);
    if (!dock || !dock->isFloating())
        return QMainWindow::eventFilter(watched, event);

    auto* me = static_cast<QMouseEvent*>(event);
    if (me->button() != Qt::LeftButton)
        return false;

    // Only the title strip starts a drag. Its bottom edge == where the dock's
    // content widget begins (the strip itself is painted on the QDockWidget,
    // so presses there have no child widget under the cursor).
    QWidget* content = dock->widget();
    if (!content) return false;
    const QPoint pos = me->position().toPoint();
    const int titleBottom = content->mapTo(dock, QPoint(0, 0)).y();
    if (pos.y() >= titleBottom || dock->childAt(pos))
        return false;   // content or a button — normal handling

    if (QWindow* wh = dock->windowHandle())
        wh->startSystemMove();
    return true;   // consume so Qt's broken move()-based drag never starts
}

// ── Panels lifecycle: respawn closed docks / reset the layout ───────────────
void RubyMainWindow::build_panels_menu(QMenu* view_menu) {
    if (!view_menu) return;
    auto* panels = view_menu->addMenu("&Panels");
    panels->setToolTip("Show / hide studio panels. Any panel closed by accident "
                       "can be brought straight back here.");
    auto* reset = panels->addAction("&Reset Panel Layout", this,
                                    &RubyMainWindow::reset_panel_layout);
    reset->setToolTip("Close any floating/separated panels and re-attach every "
                      "panel to its default position inside the window");
    panels->addSeparator();

    // One toggle per dock: unchecking hides the panel, checking brings it back
    // and re-attaches it exactly where it was docked before.
    const QList<QDockWidget*> docks = findChildren<QDockWidget*>();
    for (QDockWidget* dock : docks) {
        if (!dock) continue;
        QAction* act = dock->toggleViewAction();
        act->setText(dock->windowTitle().isEmpty() ? "Panel" : dock->windowTitle());
        act->setToolTip(QString("Show or hide the %1 panel").arg(act->text()));
        panels->addAction(act);
    }
}

void RubyMainWindow::reset_panel_layout() {
    const QList<QDockWidget*> all = { m_asset_dock, m_inspector_dock, m_lighting_dock,
                                      m_animation_dock, m_scene_dock, m_template_palette_dock,
                                      m_template_inspector_dock, m_console_dock,
                                      m_engine_dock };
    // Tear down whatever the user did (closed, moved, tabified or floated).
    for (QDockWidget* d : all) {
        if (!d) continue;
        d->setFloating(false);
        removeDockWidget(d);
    }

    // Rebuild the canonical layout — mirrors setup_dock_panels() ordering so
    // tab groups and splitter orientation come back exactly as at startup.
    addDockWidget(Qt::LeftDockWidgetArea, m_asset_dock);
    addDockWidget(Qt::RightDockWidgetArea, m_inspector_dock);
    addDockWidget(Qt::RightDockWidgetArea, m_lighting_dock);
    tabifyDockWidget(m_inspector_dock, m_lighting_dock);

    addDockWidget(Qt::BottomDockWidgetArea, m_animation_dock);
    addDockWidget(Qt::LeftDockWidgetArea, m_scene_dock);
    tabifyDockWidget(m_asset_dock, m_scene_dock);
    addDockWidget(Qt::LeftDockWidgetArea, m_template_palette_dock);
    tabifyDockWidget(m_scene_dock, m_template_palette_dock);

    addDockWidget(Qt::RightDockWidgetArea, m_template_inspector_dock);
    tabifyDockWidget(m_inspector_dock, m_template_inspector_dock);

    addDockWidget(Qt::BottomDockWidgetArea, m_console_dock);
    tabifyDockWidget(m_animation_dock, m_console_dock);

    addDockWidget(Qt::RightDockWidgetArea, m_engine_dock);
    splitDockWidget(m_inspector_dock, m_engine_dock, Qt::Vertical);
    resizeDocks({ m_inspector_dock, m_engine_dock }, { 460, 460 }, Qt::Horizontal);
    resizeDocks({ m_inspector_dock, m_engine_dock }, { 220, 420 }, Qt::Vertical);

    // Visibility defaults: the Animation bar only appears when a model is
    // being viewed; every other panel comes back on screen.
    m_animation_dock->setVisible(false);
    for (QDockWidget* d : all) {
        if (d && d != m_animation_dock && !d->isVisible()) d->show();
    }
    // Pick the canonical front tab inside each tab group.
    m_asset_dock->raise();      // (Asset Browser over Scene Outliner)
    m_inspector_dock->raise();  // (Inspector over Lighting)
    m_console_dock->raise();    // (Terminal over Animation)
    fit_engine_preview_dock();  // landscape 16:9 preview area

    const QString msg = "Panel layout reset — every panel is docked again.";
    ruby::core::ProjectContext::instance().set_status(msg);
    if (m_console) m_console->append_line(msg);
    statusBar()->showMessage(msg, 4000);
}

void RubyMainWindow::onEngineBootClicked() {
    if (!m_engine_dock || !m_engine_panel) return;
    if (m_engine_dock->isHidden()) {
        m_engine_dock->show();
        m_engine_dock->raise();
    }
    m_engine_panel->boot_or_toggle();
    if (m_engine_panel->pod()->is_alive()) {
        QTimer::singleShot(0, this, &RubyMainWindow::fit_engine_preview_dock);
    }
}

// Reshape the Engine Preview dock for a landscape 16:9 video area.
//   * widens the whole right rail a bit (480–620 px) so the game renders
//     noticeably bigger — the central 3D viewport yields the space;
//   * gives the engine dock a height of ~ (width×9/16) + control chrome, so
//     the frame fills the widget instead of floating in a tall black column;
//   * never closes or hides any other panel — the Inspector keeps ≥130 px and
//     keeps its position above the engine.
void RubyMainWindow::fit_engine_preview_dock() {
    if (!m_engine_dock || !m_inspector_dock || !m_engine_panel) return;
    if (m_engine_dock->isHidden()) return;
    // Respect custom layouts: only auto-shape while the preview lives in the
    // default right-column split next to the Inspector.
    if (dockWidgetArea(m_engine_dock) != Qt::RightDockWidgetArea ||
        dockWidgetArea(m_inspector_dock) != Qt::RightDockWidgetArea) {
        return;
    }

    // 1) Column width: a comfortable landscape strip (the central viewport /
    //    IDE shrinks instead of the right-side panels).
    const int win_w = std::max(width(), 640);
    int want_w = qBound(460, win_w * 2 / 5, 620);
    want_w = qMin(want_w, win_w - 240);   // leave the left rail + ≥180 central
    const int eng_w = m_engine_dock->width();
    const int insp_w = m_inspector_dock->width();
    if (want_w > eng_w) {
        resizeDocks({ m_inspector_dock, m_engine_dock },
                    { insp_w + (want_w - eng_w), want_w }, Qt::Horizontal);
    }

    // 2) Column heights: engine dock ≈ 16:9 of its own width (+ ~170 px of
    //    status/transport chrome); the Inspector above keeps everything else.
    const int w = m_engine_dock->width();
    int eng_h = m_engine_dock->height();
    const int insp_h = m_inspector_dock->height();
    const int col_h = eng_h + insp_h;
    int want_h = (int)(w * 9.0f / 16.0f) + 170;
    want_h = qBound(300, want_h, std::max(300, col_h - 130));
    if (col_h > 0 && want_h != eng_h && want_h <= col_h) {
        resizeDocks({ m_inspector_dock, m_engine_dock },
                    { col_h - want_h, want_h }, Qt::Vertical);
    }
}

// ── Engine-pod Scene Shifter context ("▶ Run Scene" in the dock) ───────────
// The engine-preview dock shows its "▶ Run Scene" control while a .scene
// document is the active doc AND an engine session is live. RubyMainWindow
// feeds it the active scene path whenever the document set changes.
void RubyMainWindow::update_engine_scene_context() {
    if (!m_engine_panel) return;
    QString scene_path;
    if (m_active_doc >= 0 && m_active_doc < m_docs.size() &&
        m_docs[m_active_doc].kind == "scene") {
        scene_path = m_docs[m_active_doc].path;
    }
    m_engine_panel->set_scene_doc(scene_path);
}

// Persist the scene's FileRift markup buffer back to the binary .scene file
// so the engine loads the user's latest edits. Returns false when the disk
// file cannot be guaranteed to match the edits (then the shift must not fire).
bool RubyMainWindow::save_scene_doc_for_run(int index) {
    if (index < 0 || index >= m_docs.size()) return false;
    const QString path = m_docs[index].path;
    if (m_docs[index].kind != "scene") return true;   // nothing to encode

    if (!m_dirty_scripts.contains(path)) {
        return true;   // disk already matches the last saved buffer
    }
    if (!m_script_buffers.contains(path)) {
        // No RAM markup buffer exists — the binary on disk is authoritative.
        m_dirty_scripts.remove(path);
        refresh_tab_labels();
        return true;
    }

    // Dirty FileRift scenes must be re-encoded to binary. Ensure the IDE is
    // showing this document, then save (buffer is cached, so load is sync).
    snapshot_active_script();
    if (m_script_ide->current_file_path() != path) {
        load_doc_into_ide(index);
    }
    if (!m_docs[index].encode_filerift) {
        // Markup would overwrite the binary with text the engine can't load.
        ruby::core::ProjectContext::instance().set_status(
            "Run Scene: cannot encode — FileRift 'Save as Raw Text' is on for " +
            QFileInfo(path).fileName() + ". Re-enable 'Encode to Binary on Save'.");
        return false;
    }
    if (m_script_ide->save_file()) {
        m_script_buffers[path] = m_script_ide->full_text();
        m_dirty_scripts.remove(path);
        m_user_text_paths.remove(path);
        m_script_ide->document()->setModified(false);
        refresh_tab_labels();
        ruby::core::ProjectContext::instance().set_status(
            "Run Scene: saved (encoded) " + path);
        return true;
    }
    const QString err = m_script_ide->last_save_error();
    ruby::core::ProjectContext::instance().set_status(
        "Run Scene: failed to save " + path + " — file left untouched: " +
        (err.isEmpty() ? QStringLiteral("unknown error") : err));
    return false;
}

void RubyMainWindow::on_engine_run_scene(const QString& scene_path) {
    if (!m_engine_panel) return;
    const int idx = find_document(scene_path);
    if (idx < 0 || idx >= m_docs.size() || m_docs[idx].kind != "scene") return;

    ruby::emulator::EnginePod* pod = m_engine_panel->pod();
    if (!pod || !pod->is_alive()) {
        ruby::core::ProjectContext::instance().set_status(
            "Start the inline engine (phone ▶) before running a scene.");
        return;
    }
    if (!save_scene_doc_for_run(idx)) return;

    // The engine resolves scenes from ITS asset instance (resources/<stem>.scene).
    const QFileInfo fi(m_docs[idx].path);
    const QString stem = fi.completeBaseName();
    const QString assets_dir = m_engine_panel->assets_dir();
    const QString scene_in_pod = assets_dir + "/resources/" + stem + ".scene";
    if (assets_dir.isEmpty() || !QFileInfo::exists(scene_in_pod)) {
        const QString msg = QString(
            "Run Scene: '%1.scene' is not inside the engine pod's assets "
            "(%2). Set the right assets folder (Assets… in the dock) and "
            "re-boot before running this scene.")
            .arg(stem, assets_dir.isEmpty()
                           ? QStringLiteral("no assets folder set")
                           : scene_in_pod);
        ruby::core::ProjectContext::instance().set_status(msg);
        if (m_console) m_console->append_line(msg);
        return;
    }

    // Guard + sync the pod's copy. The engine resolves scenes from ITS assets
    // dir (resources/<stem>.scene), which may be a DIFFERENT file than the
    // document being edited. Two rules keep the running game safe:
    //   1) never shift in a scene that fails to parse — that is how a corrupted
    //      scene reaches the game and crashes the engine;
    //   2) always copy the freshly-saved binary over the pod's copy so the
    //      engine runs EXACTLY the user's edits, not stale bytes on disk.
    const QString src_path = m_docs[idx].path;
    if (QDir::cleanPath(src_path) != QDir::cleanPath(scene_in_pod)) {
        QFile in_f(src_path);
        if (!in_f.open(QIODevice::ReadOnly)) {
            ruby::core::ProjectContext::instance().set_status(
                "Run Scene: cannot re-read saved scene " + src_path);
            return;
        }
        const QByteArray saved_bytes = in_f.readAll();
        if (saved_bytes.isEmpty()) {
            ruby::core::ProjectContext::instance().set_status(
                "Run Scene: saved scene is empty — refusing to run a corrupt scene.");
            return;
        }
        std::vector<uint8_t> raw(saved_bytes.constData(),
                                 saved_bytes.constData() + saved_bytes.size());
        std::vector<std::string> extra_roots;
        const std::string pdir = ruby::core::ProjectContext::instance().project_dir();
        if (!pdir.empty()) extra_roots.push_back(pdir);
        bool parses = false;
        try {
            parses = !av::scene_load_bytes(raw, src_path.toStdString(), extra_roots).objects.empty();
        } catch (...) {
            parses = false;
        }
        if (!parses) {
            const QString warn = "Run Scene: saved scene failed to parse — not teleporting "
                                 "(refusing to crash the engine with a corrupt scene).";
            ruby::core::ProjectContext::instance().set_status(warn);
            if (m_console) m_console->append_line(warn);
            return;
        }
        QSaveFile pod_out(scene_in_pod);
        if (!pod_out.open(QIODevice::WriteOnly)) {
            ruby::core::ProjectContext::instance().set_status(
                "Run Scene: cannot write the pod's scene copy " + scene_in_pod);
            return;
        }
        if (pod_out.write(saved_bytes) != saved_bytes.size() || !pod_out.commit()) {
            ruby::core::ProjectContext::instance().set_status(
                "Run Scene: failed to sync the pod's scene copy — not teleporting.");
            return;
        }
    } else {
        // Same file — cheap final guard: the exact bytes the engine will load
        // must still parse, otherwise refuse the shift.
        std::vector<uint8_t> raw;
        {
            QFile in_f(src_path);
            if (in_f.open(QIODevice::ReadOnly)) {
                const QByteArray b = in_f.readAll();
                raw.assign(b.constData(), b.constData() + b.size());
            }
        }
        std::vector<std::string> extra_roots;
        const std::string pdir = ruby::core::ProjectContext::instance().project_dir();
        if (!pdir.empty()) extra_roots.push_back(pdir);
        bool parses = false;
        try {
            parses = !raw.empty() &&
                     !av::scene_load_bytes(raw, src_path.toStdString(), extra_roots).objects.empty();
        } catch (...) {
            parses = false;
        }
        if (!parses) {
            const QString warn = "Run Scene: scene failed to parse — not teleporting "
                                 "(refusing to crash the engine with a corrupt scene).";
            ruby::core::ProjectContext::instance().set_status(warn);
            if (m_console) m_console->append_line(warn);
            return;
        }
    }

    // Normal gateway through the Scene Shifter — writes the pending request
    // into the guest; the per-frame sre_scene_shifter_tick performs the
    // GotoLevel (loading screen + BackgroundLoad), exactly like the in-game
    // panel. The dock already resumed the session if it was paused.
    pod->send_scene_shift(stem, QStringLiteral("start"), 1);
    const QString msg = QString("▶ Run Scene: teleporting engine to '%1' "
                                "(saved: %2 → pod: %3)").arg(stem, src_path, scene_in_pod);
    ruby::core::ProjectContext::instance().set_status(msg);
    if (m_console) m_console->append_line(msg);
}

void RubyMainWindow::onOpenTools() {
    if (m_tools) m_mode_tabs->setCurrentWidget(m_tools);
}

void RubyMainWindow::onOpenProject() {
    QString dir = QFileDialog::getExistingDirectory(this, "Open Swordigo Asset Directory");
    if (!dir.isEmpty()) {
        m_asset_browser->set_root_path(dir);
        ruby::core::ProjectContext::instance().set_project_dir(dir.toStdString());
        ruby::git::RubyGit::init_or_open(dir);
        ruby::core::ProjectContext::instance().set_status("Project loaded: " + dir);
        // Feed the engine-preview dock the workspace's asset tree if one is
        // detectable, so ▶ Boot starts instantly with the user's own content.
        auto cands = ruby::emulator::detect_asset_dirs(dir.toStdString());
        if (!cands.empty() && m_engine_panel) {
            m_engine_panel->set_assets_dir(
                QString::fromStdString(cands.front().instance_dir));
        }
    }
}

void RubyMainWindow::onOpenFile() {
    QString file = QFileDialog::getOpenFileName(this, "Open Swordigo Asset", QString(),
        "Swordigo Assets (*.pod *.POD *.glb *.GLB *.gltf *.GLTF *.obj *.OBJ *.pvr *.PVR *.tex *.TEX *.png *.PNG *.jpg *.JPG *.scl *.SCL *.scene *.SCENE *.scn *.SCN *.lua *.LUA *.swdm *.SWDM *.gmesh);;All Files (*)");
    if (!file.isEmpty()) {
        onFileSelectedInBrowser(file);
    }
}

void RubyMainWindow::onImportApk() {
    if (!m_apk_session) return;
    if (!m_apk_session->import_apk(this)) return;   // errors surfaced via status bar

    const apk::Session& s = m_apk_session->session();
    const std::string assets = s.session_dir + "/" + s.assets_subdir;

    // Project + asset roots point at the extraction: the 3D viewport's scene
    // roots (project_dir/assets/resources, …) and the Asset Browser resolve
    // from the session tree, so the user edits loose files as usual.
    ruby::core::ProjectContext::instance().set_project_dir(s.session_dir);
    const QString resources = QString::fromStdString(assets + "/resources");
    m_asset_browser->set_root_path(QFileInfo::exists(resources)
                                       ? resources
                                       : QString::fromStdString(assets));

    // Feed the engine-preview dock the session's asset tree if detectable, so
    // ▶ Boot starts from the imported content (mirrors onOpenProject).
    auto cands = ruby::emulator::detect_asset_dirs(s.session_dir);
    if (!cands.empty() && m_engine_panel) {
        m_engine_panel->set_assets_dir(
            QString::fromStdString(cands.front().instance_dir));
    }

    ruby::core::ProjectContext::instance().set_status(
        QString("APK session active: %1 — Export as APK… is now available.")
            .arg(QString::fromStdString(s.session_id)),
        8000);
}

void RubyMainWindow::onExportApk() {
    if (!m_apk_session || !m_apk_session->has_session()) return;
    // Repack + sign land in master TODO 4.1b/4.1c (shared zip writer is live;
    // the session tree is already a full editable copy). Until then, never
    // silently do nothing: say exactly where the feature is.
    ruby::core::ProjectContext::instance().set_status(
        "APK export arrives with master TODO 4.1b (repack) + 4.1c (native v1 "
        "signer) — the session tree is untouched for now.",
        8000);
}

void RubyMainWindow::onSaveFile() {
    // Ctrl+S saves the ACTIVE document. Scenes can be edited through two RAM
    // surfaces (structured 3D viewport, or FileRift text); each Save runs
    // through the writer that matches where the user actually changed things.
    if (m_active_doc < 0 || m_active_doc >= m_docs.size()) return;
    const int idx = m_active_doc;
    const RubyDocEntry& doc = m_docs[idx];
    const QString target = (doc.kind == "script" || doc.kind == "scene") ? doc.path : QString();
    if (target.isEmpty()) return;
    const bool is_scene = doc.kind == "scene";

    // Ctrl+S while Mesh Edit is armed: commit the session FIRST. The commit
    // regenerates the polygon through boulder, emits sceneEdited (marking the
    // doc structured-dirty and re-syncing the FileRift text), and restores the
    // camera — so the save below writes the edited mesh rather than a stale
    // buffer, and the viewport is never left in the projection-locked view.
    if (is_scene && m_viewport_3d && m_viewport_3d->mesh_edit_active()) {
        m_viewport_3d->set_mesh_edit(false);
        // end_mesh_edit() deliberately keeps the session open when the apply
        // (boulder regenerate) fails so it can be fixed. Saving then would
        // write stale geometry under a locked camera — abort instead.
        if (m_viewport_3d->mesh_edit_active()) {
            ruby::core::ProjectContext::instance().set_status(
                "Save aborted: the mesh commit failed — fix the mesh, then save again.");
            return;
        }
    }

    // Binary-format docs must never be written as raw text — that turns the
    // .scene into FileRift markup the engine and viewport cannot load.
    if (is_scene && !doc.encode_filerift) {
        ruby::core::ProjectContext::instance().set_status(
            "Save blocked: FileRift 'Save as Raw Text' is on for " +
            QFileInfo(doc.path).fileName() +
            " — re-enable 'Encode to Binary on Save'.");
        return;
    }

    // Structured (viewport gizmo) edits → structured writer.
    if (is_scene && m_scene_struct_dirty.contains(target)) {
        // If the user typed NEW changes into the FileRift text after the last
        // viewport sync, those take precedence — save via the text encoder and
        // reload the structured scene so nothing is lost.
        if (scene_has_user_text_edits(target)) {
            ruby::core::ProjectContext::instance().set_status(
                "Saving FileRift text edits; the 3D view reloads from disk after save.");
        } else {
            save_scene_doc_structured(idx);
            return;
        }
    }

    // Default path: persist the FileRift-encoded editor buffer (script or
    // scene markup). Re-encoding writes binary the engine can load.
    if (m_script_ide->current_file_path() != target) {
        snapshot_active_script();
        load_doc_into_ide(idx);
    }
    if (m_script_ide->save_file()) {
        const QString path = m_script_ide->current_file_path();
        m_script_buffers[path] = m_script_ide->full_text();
        m_dirty_scripts.remove(path);
        m_user_text_paths.remove(path);   // typed text is on disk now
        // Drop any in-flight scene-text sync: the buffer just became the
        // on-disk truth, a stale RAM-snapshot decode must not overwrite it.
        ++m_scene_text_sync_seq;
        m_script_ide->document()->setModified(false);
        refresh_tab_labels();
        // A text save rewrote the binary: refresh the viewport's structured RAM
        // so a later gizmo edit starts from the freshly-encoded state. When the
        // object structure is unchanged this swaps in place — no camera reset,
        // no GPU rebuild, no loading flash; only structural changes fall back
        // to a full (camera-preserving) reload.
        if (is_scene && m_viewport_3d && m_viewport_3d->has_scene() &&
            m_viewport_3d->scene().filepath == path.toStdString()) {
            m_scene_struct_dirty.remove(path);
            bool applied = false;
            try {
                std::vector<std::string> extra_roots;
                const std::string pdir = ruby::core::ProjectContext::instance().project_dir();
                if (!pdir.empty()) extra_roots.push_back(pdir);
                const av::SceneData from_disk = av::scene_load(path.toStdString(), extra_roots);
                applied = !from_disk.objects.empty() &&
                          m_viewport_3d->apply_scene_data(from_disk, path.toStdString());
            } catch (...) {
                applied = false;
            }
            if (!applied) {
                // evict() clears the scene flags, so preserve the camera explicitly.
                const auto cam = m_viewport_3d->camera_state();
                m_viewport_3d->evict_scene_cache(path.toStdString());
                m_viewport_3d->load_scene(path.toStdString());
                m_viewport_3d->set_camera_state(cam);
            }
        }
        if (is_scene) m_scene_text_synced.remove(path);
        if (m_scene_sync_timer) m_scene_sync_timer->stop();
        m_scene_sync_pending_path.clear();
        commit_pending_scene_undo();
        // Off-main-thread git checkpoint + history refresh (see
        // save_scene_doc_structured — the libgit2 work froze the UI on save).
        {
            const QString rel = path;
            const QString msg = "Save checkpoint: " + QFileInfo(path).fileName();
            RubyMainWindow* win = this;
            std::thread([win, rel, msg]() {
                ruby::git::RubyGit::commit_file(rel, msg);
                QMetaObject::invokeMethod(win, [win]() {
                    if (win->m_history) win->m_history->refresh();
                }, Qt::QueuedConnection);
            }).detach();
        }
        ruby::core::ProjectContext::instance().set_status("Saved: " + path);
    } else {
        const QString err = m_script_ide->last_save_error();
        ruby::core::ProjectContext::instance().set_status(
            "Save failed — file left untouched: " +
            (err.isEmpty() ? QStringLiteral("could not write ") + target : err));
    }
}

// True when the scene's FileRift text has genuine user-typed changes that are
// not the artifact of our structured→text regeneration.
bool RubyMainWindow::scene_has_user_text_edits(const QString& path) const {
    // (b) explicitly recorded when the user typed and then left the IDE tab
    // (snapshot_active_script) — the only signal that survives tab switches.
    if (m_user_text_paths.contains(path)) return true;
    // While the IDE SHOWS the scene (mode tab active), the reliable
    // discriminator is TEXT: the IDE's own virtual streaming touches the
    // document-modified flag but shows exactly the cached buffer, whereas
    // genuine typing changes it. When the IDE tab is hidden the editor is not
    // being edited, so the live-text comparison must not run against a stale
    // (pre-sync) document.
    if (!m_mode_tabs || m_mode_tabs->currentIndex() != 1) return false;
    if (m_script_ide->current_file_path() != path) return false;
    if (!m_script_buffers.contains(path)) return false;
    const QString buf = m_script_buffers.value(path);
    return !buf.isEmpty() && m_script_ide->full_text() != buf;
}

// Re-encode a scene's unsaved FileRift markup in memory and show the result in
// the 3D viewport WITHOUT writing the file: text edits become visible in the
// visual editor while the disk copy stays untouched. Structure-preserving edits
// swap in place (instant); structural changes take the async in-memory load.
// Returns true when the viewport now reflects the text edits (or none exist).
bool RubyMainWindow::apply_unsaved_scene_text_to_viewport(const QString& path) {
    if (!m_viewport_3d) return false;
    if (!m_script_buffers.contains(path)) return false;
    if (!scene_has_user_text_edits(path)) return true;   // nothing new to show

    QString markup = m_script_buffers.value(path);
    const QString prefix = "## FileRift decoded Swordigo file type: scene";
    // Strip every leading banner line (legacy buffers may carry a double one).
    for (;;) {
        if (!markup.startsWith(prefix)) break;
        const int nl = markup.indexOf('\n');
        if (nl < 0) { markup.clear(); break; }
        markup = markup.mid(nl + 1).trimmed();
    }
    std::string binary;
    try {
        binary = ::filerift::recode_markup(markup.toStdString(), "scene");
        if (binary.empty()) return false;   // never preview an empty re-encode
        const std::string roundtrip = ::filerift::decode_protobuf(binary, "scene");
        if (roundtrip.empty()) return false;
    } catch (...) {
        return false;
    }

    // Structure-preserving path: parse in memory and swap in place.
    {
        std::vector<std::string> extra_roots;
        const std::string pdir = ruby::core::ProjectContext::instance().project_dir();
        if (!pdir.empty()) extra_roots.push_back(pdir);
        const std::string tmp = path.toStdString() + ".ruby-inmem.tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (out) out.write(binary.data(), static_cast<std::streamsize>(binary.size()));
        }
        av::SceneData sd;
        try {
            sd = av::scene_load(tmp, extra_roots);
        } catch (...) {
            sd = av::SceneData{};
        }
        std::error_code ec;
        fs::remove(tmp, ec);
        if (!sd.objects.empty() && m_viewport_3d->apply_scene_data(sd, path.toStdString())) {
            ruby::core::ProjectContext::instance().set_status(
                "Scene text edits applied to the 3D viewport (not saved yet).");
            return true;
        }
    }

    // Structural change or viewport showing another scene: async-load the
    // re-encoded bytes — the file on disk is still untouched.
    m_viewport_3d->load_scene_async(path.toStdString(), &binary);
    ruby::core::ProjectContext::instance().set_status(
        "Scene text edits re-encoded for the 3D viewport (not saved yet).");
    return true;
}

// Tools → Ground Mesh Studio "Add to Scene…": paste a boulder-generated
// ground-mesh object (GroundPolygon + GroundMesh + Generator + CollisionShape
// + TextureMappings) into the open scene's RAM. Nothing touches disk until the
// user saves — the structured scene is the source of truth, exactly like gizmo
// edits.
void RubyMainWindow::on_ground_mesh_add_to_scene(const QString& identifier,
                                                 const QByteArray& scene_bytes,
                                                 double pos_x, double pos_y,
                                                 double depth) {
    if (!m_viewport_3d || !m_viewport_3d->has_scene()) {
        ruby::core::ProjectContext::instance().set_status(
            "Open a .scene in the 3D viewport first, then add the ground mesh.");
        return;
    }
    const int idx = m_active_doc;
    if (idx < 0 || idx >= m_docs.size() || m_docs[idx].kind != "scene") {
        ruby::core::ProjectContext::instance().set_status(
            "Activate a scene document before adding a ground mesh.");
        return;
    }
    const QString path = m_docs[idx].path;
    if (m_viewport_3d->scene().filepath != path.toStdString()) {
        ruby::core::ProjectContext::instance().set_status(
            "The 3D viewport is showing another scene — open " + path + " first.");
        return;
    }
    if (scene_has_user_text_edits(path)) {
        ruby::core::ProjectContext::instance().set_status(
            "Scene has unsaved FileRift text edits — save the text (Ctrl+S) first.");
        return;
    }

    // Decode the boulder-generated object (Scene field 1 = a single Object).
    av::SceneObject obj;
    {
        const std::string tmp = path.toStdString() + ".ruby-gm-inmem.tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (out) out.write(scene_bytes.constData(), scene_bytes.size());
        }
        av::SceneData sd;
        try { sd = av::scene_load(tmp); } catch (...) { sd = av::SceneData{}; }
        std::error_code ec;
        fs::remove(tmp, ec);
        if (sd.objects.empty()) {
            ruby::core::ProjectContext::instance().set_status(
                "Ground mesh object could not be decoded — generator output invalid.");
            return;
        }
        obj = std::move(sd.objects.front());
    }

    // Keep the user's identifier, deduped against the live scene.
    QString name = identifier;
    if (name.isEmpty()) name = "ground_mesh";
    bool taken = false;
    for (const auto& o : m_viewport_3d->scene().objects)
        if (o.name == name.toStdString()) { taken = true; break; }
    int suffix = 1;
    while (taken) {
        const QString cand = QString("%1_%2").arg(name).arg(suffix++);
        taken = false;
        for (const auto& o : m_viewport_3d->scene().objects)
            if (o.name == cand.toStdString()) { taken = true; break; }
        if (!taken) name = cand;
    }
    obj.name = name.toStdString();

    // Spawns in the z = 0 on the place scene camera is actually focused on
    const auto cam = m_viewport_3d->camera_state();
    obj.pos_x = cam.target[0];
    obj.pos_y = cam.target[1];
    obj.pos_z = 0.0f;

    // Append to the open scene's RAM and GPU without touching disk
    int new_idx = m_viewport_3d->add_ground_mesh_object(std::move(obj));
    if (new_idx >= 0) {
        m_scene_struct_dirty.insert(path);
        sync_scene_text_buffer(path, /*from_disk=*/false);
        refresh_tab_labels();
        if (m_scene_hierarchy) {
            m_scene_hierarchy->set_scene(m_viewport_3d->scene());
            m_scene_hierarchy->select_object(new_idx);
        }
        // Auto-switch to 3D Viewport tab so the user immediately sees it selected and can transform it
        m_mode_tabs->setCurrentIndex(0);
        ruby::core::ProjectContext::instance().set_status(
            "Ground mesh '" + name + "' spawned at camera focus (" +
            QString::number(cam.target[0], 'f', 1) + ", " +
            QString::number(cam.target[1], 'f', 1) + ", 0.0) in open scene.");
    }
}

// Gizmo drag committed: the in-RAM structured scene changed. Mark the doc
// dirty and regenerate the FileRift text so both editing surfaces agree.
void RubyMainWindow::on_viewport_scene_edited() {
    if (!m_viewport_3d || !m_viewport_3d->has_scene()) return;
    const int idx = m_active_doc;
    if (idx < 0 || idx >= m_docs.size() || m_docs[idx].kind != "scene") return;
    const QString path = m_docs[idx].path;

    // Guard: never fold structured edits over unsaved FileRift text the user
    // typed before this move — the structured scene may predate that text.
    if (scene_has_user_text_edits(path)) {
        ruby::core::ProjectContext::instance().set_status(
            "Scene has unsaved FileRift text edits — save the text (Ctrl+S) first, "
            "then redo this transform.");
        return;
    }

    // Mark dirty IMMEDIATELY (the save path keys off this) but coalesce the
    // expensive resync — scene re-encode + FileRift decode + editor reload +
    // hierarchy rebuild — into one pass ~250 ms after the last change. This is
    // what made gizmo drags and inspector spinbox typing freeze the UI for
    // seconds per event.
    m_scene_struct_dirty.insert(path);
    m_scene_sync_pending_path = path;
    if (m_scene_sync_timer) m_scene_sync_timer->start();
}

void RubyMainWindow::commit_pending_scene_undo() {
    if (!m_scene_sync_undo_armed) return;
    m_scene_sync_undo_armed = false;
    const QString armed_path = m_scene_sync_undo_path;
    m_scene_sync_undo_path.clear();
    if (!m_viewport_3d || !m_viewport_3d->has_scene() ||
        m_viewport_3d->scene().filepath != armed_path.toStdString()) {
        // The user switched documents mid-burst — drop the entry rather than
        // push an old scene's snapshot onto another scene's undo stack.
        m_scene_sync_undo_before.clear();
        return;
    }
    m_viewport_3d->push_scene_snapshot_undo(std::move(m_scene_sync_undo_before),
                                            QStringLiteral("Edit Transform"));
    m_scene_sync_undo_before.clear();
}

void RubyMainWindow::flush_pending_scene_sync() {
    if (m_scene_sync_timer) m_scene_sync_timer->stop();
    const QString path = m_scene_sync_pending_path;
    m_scene_sync_pending_path.clear();

    // A spinbox burst's single undo entry lands when the burst settles (the
    // "before" snapshot was captured at burst start).
    commit_pending_scene_undo();

    if (path.isEmpty() || !m_viewport_3d || !m_viewport_3d->has_scene()) return;
    // Never clobber text the user typed while the burst was settling.
    if (scene_has_user_text_edits(path)) return;
    if (m_viewport_3d->scene().filepath != path.toStdString()) return;

    sync_scene_text_buffer(path, /*from_disk=*/false);   // RAM scene → text view
    refresh_tab_labels();

    if (m_scene_hierarchy) {
        m_scene_hierarchy->set_scene(m_viewport_3d->scene());
        m_scene_hierarchy->select_object(m_viewport_3d->selected_object());
    }
    // Selection-only: a transform edit cannot change the template catalog, and
    // the full filesystem template scan (~1.3-2.6 s on big asset trees) used to
    // run here on EVERY drag burst — the drag-end freeze.
    refresh_scene_templates(false);
}

bool RubyMainWindow::route_undo_shortcut(QKeyEvent* event) {
    // The Script IDE (FileRift text) keeps its own Qt text undo/redo — never
    // steal Ctrl+Z while the user is typing markup.
    QWidget* focus = QApplication::focusWidget();
    if (focus && m_script_ide &&
        (focus == m_script_ide || m_script_ide->isAncestorOf(focus)))
        return false;
    if (!m_viewport_3d || !m_viewport_3d->has_scene()) return false;

    const bool redo = (event->key() == Qt::Key_Y) ||
                      (event->key() == Qt::Key_Z &&
                       event->modifiers().testFlag(Qt::ShiftModifier));
    // A spinbox burst may still be un-committed on the debounce timer: push its
    // coalesced undo entry now so the very first Ctrl+Z undoes the last edit
    // instead of doing nothing (or stepping through intermediate digits).
    if (!redo && m_scene_sync_timer && m_scene_sync_timer->isActive() &&
        m_scene_sync_undo_armed) {
        m_scene_sync_timer->stop();
        flush_pending_scene_sync();
    }
    if (redo) m_viewport_3d->redo();
    else      m_viewport_3d->undo();
    event->accept();
    return true;
}

// ── Inspector ↔ 3D viewport sync bridge ─────────────────────────────────────
// Refreshes the inspector panel for the currently-selected object whenever the
// viewport scene is mutated externally (undo/redo, component field changes,
// visibility toggles, template mutations, ground-mesh edits). This is the
// central "diff-against-re-inspect" bridge: full inspect_scene_object rebuild
// on each mutation. Component field edits are rare relative to transform
// dragging, so the full rebuild cost is negligible.
void RubyMainWindow::sync_inspector_from_viewport() {
    if (!m_viewport_3d || !m_viewport_3d->has_scene() || !m_inspector) return;
    const int sel = m_viewport_3d->selected_object();
    if (sel >= 0 && sel < static_cast<int>(m_viewport_3d->scene().objects.size())) {
        m_inspector->inspect_scene_object(m_viewport_3d->scene(), sel);
    } else {
        m_inspector->clear_inspection();
    }
}

// ── Template palette + template hierarchy (master TODO 2.3 / 2.4) ───────────

void RubyMainWindow::refresh_scene_templates(bool rescan) {
    if (!m_viewport_3d || !m_viewport_3d->has_scene()) return;
    const av::SceneData& scene = m_viewport_3d->scene();

    // Memoization key: the template SOURCE set is determined by the scene's
    // identity, its library list and its object count. Gizmo commits, saves
    // (in-place applies) and selection changes alter none of these — the full
    // filesystem walk + .scl decode (~1.3-2.6 s with 48 imported-library roots
    // over one big assets tree) only runs when that key actually changes.
    std::string key = scene.filepath + "\x01" + std::to_string(scene.objects.size());
    for (const auto& lib : scene.imported_library_paths) key += "\x01" + lib;

    if (rescan && key != m_template_scan_key) {
        // Roots: project dir + scene dir + imported library dirs + the usual
        // home asset dirs (mirrors load_scene/current_scene_roots so the
        // palette sees exactly what the scene can resolve).
        QStringList roots;
        const std::string project_dir = ruby::core::ProjectContext::instance().project_dir();
        if (!project_dir.empty()) roots << QString::fromStdString(project_dir);
        const QString scene_path = QString::fromStdString(scene.filepath);
        if (!scene_path.isEmpty()) {
            roots << QFileInfo(scene_path).absolutePath();
            roots << QFileInfo(scene_path).absolutePath() + "/resources";
        }
        for (const auto& lib : scene.imported_library_paths) {
            if (lib.empty()) continue;
            roots << QString::fromStdString(fs::path(lib).parent_path().string());
        }
        const QString home = QDir::homePath();
        roots << home + "/resources"
              << home + "/SwordigoDesktop/assets"
              << home + "/SwordigoDesktop/resources"
              << home + "/SwordigoRefresh/assets/resources"
              << home + "/.local/share/swordigo-desktop/assets";

        std::vector<std::string> root_list;
        root_list.reserve(static_cast<size_t>(roots.size()));
        for (const QString& q : roots) root_list.push_back(q.toStdString());

        // Scan ONCE; feed both the palette and the shared catalog (the old
        // code scanned twice — once inside the palette, once here).
        m_template_scan_entries = av::scan_template_sources(root_list, scene);
        if (m_template_palette) m_template_palette->set_entries(m_template_scan_entries);

        // Shared catalog for the inspector: every template from the scene's
        // libraries + every .scl template the scan found on disk.
        m_template_catalog.clear();
        auto absorb = [this](const std::string& bytes) {
            for (auto& e : av::scl_load_templates(bytes)) {
                bool known = false;
                for (const auto& have : m_template_catalog)
                    if (have.name == e.name) { known = true; break; }
                if (!known) m_template_catalog.push_back(std::move(e));
            }
        };
        for (const auto& lib : scene.object_libraries) absorb(lib);
        for (const auto& lib : scene.external_libraries) absorb(lib);
        for (const auto& src : m_template_scan_entries) {
            if (src.kind != av::TemplateSourceEntry::Template || src.source_path.empty())
                continue;
            std::ifstream in(src.source_path, std::ios::binary);
            if (!in) continue;
            std::string bytes((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
            absorb(bytes);
        }
        m_template_scan_key = key;
    }

    if (m_template_inspector) {
        m_template_inspector->set_scene(scene,
                                        m_viewport_3d->selected_object(),
                                        m_template_catalog);
    }
}

void RubyMainWindow::on_template_add_requested(const QString& template_name,
                                               const QString& scl_path) {
    if (!m_viewport_3d || !m_viewport_3d->has_scene()) return;
    // If the template comes from a .scl the scene does not import, carry its
    // components so the object still renders (kept linked by name — the game
    // reads it as a template override).
    const av::SceneObject* tpl = nullptr;
    av::SceneObject carried;
    float scaling = 1.0f;
    if (!scl_path.isEmpty()) {
        std::ifstream in(scl_path.toStdString(), std::ios::binary);
        if (in) {
            std::string bytes((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
            for (const auto& e : av::scl_load_templates(bytes)) {
                if (e.name == template_name.toStdString()) {
                    carried = e.object;
                    scaling = e.scaling;
                    tpl = &carried;
                    break;
                }
            }
        }
    }
    const int idx = m_viewport_3d->add_template_object(template_name, tpl, scaling);
    if (idx >= 0) {
        refresh_scene_templates();
        ruby::core::ProjectContext::instance().set_selected_object(idx);
    }
}

void RubyMainWindow::on_model_add_requested(const QString& pod_path,
                                            const QString& display_name) {
    if (!m_viewport_3d || !m_viewport_3d->has_scene()) return;
    const int idx = m_viewport_3d->add_model_object(pod_path, display_name);
    if (idx >= 0) {
        refresh_scene_templates();
        ruby::core::ProjectContext::instance().set_selected_object(idx);
    }
}

void RubyMainWindow::on_template_changed(int object_index, const QString& template_name) {
    if (!m_viewport_3d || !m_viewport_3d->has_scene()) return;
    if (object_index < 0 ||
        object_index >= static_cast<int>(m_viewport_3d->scene().objects.size()))
        return;
    if (m_viewport_3d->scene().objects[object_index].template_name ==
        template_name.toStdString())
        return;   // no-op (combo re-armed with the same value)
    m_viewport_3d->set_scene_object_template(object_index, template_name);
    refresh_scene_templates();
}

void RubyMainWindow::on_template_materialize(int object_index) {
    if (!m_viewport_3d || !m_viewport_3d->has_scene()) return;
    m_viewport_3d->materialize_scene_object_template(object_index);
    refresh_scene_templates();
}

void RubyMainWindow::on_template_reset(int object_index) {
    if (!m_viewport_3d || !m_viewport_3d->has_scene()) return;
    m_viewport_3d->reset_scene_object_to_template(object_index);
    refresh_scene_templates();
}

void RubyMainWindow::on_template_override(int object_index, const QString& class_name) {
    if (!m_viewport_3d || !m_viewport_3d->has_scene()) return;
    m_viewport_3d->override_inherited_component(object_index, class_name);
    refresh_scene_templates();
}

// Re-decode the scene into FileRift markup. from_disk=false encodes the
// viewport's structured RAM (live gizmo edits); true re-decodes the file bytes
// that were just written (post-save sync).
void RubyMainWindow::sync_scene_text_buffer(const QString& path, bool from_disk) {
    // Perf charter rule 1: decoding a whole scene to FileRift markup takes
    // ~600 ms for a multi-MB scene — it must never run on the UI thread. The
    // encode+decode (plus the disk read for from_disk) runs on a worker; the
    // result is posted back and applied only while it is still the newest sync.
    if (!m_viewport_3d) return;
    const uint64_t seq = ++m_scene_text_sync_seq;

    auto build_content = [](const std::string& binary) -> QString {
        const std::string markup = ::filerift::decode_protobuf(binary, "scene");
        // decode_protobuf already emits the FileRift banner — never prepend a
        // second one. The double-banner was the corruption that turned scenes
        // into unloadable text files.
        QString content = QString::fromUtf8(markup.c_str(),
                                            static_cast<qsizetype>(markup.size()));
        if (!content.startsWith(QLatin1String("## FileRift decoded"))) {
            content = QStringLiteral("## FileRift decoded Swordigo file type: scene\n\n")
                      + content;
        }
        return content;
    };

    if (from_disk) {
        // Worker reads the fresh file + decodes; the disk read of a 25 MB
        // scene alone was ~0.5 s of UI freeze.
        std::thread([this, path, seq, build_content]() {
            try {
                std::string binary;
                QFile f(path);
                if (f.open(QIODevice::ReadOnly)) {
                    const QByteArray b = f.readAll();
                    binary.assign(b.constData(), static_cast<size_t>(b.size()));
                }
                if (binary.empty()) return;
                const QString content = build_content(binary);
                QMetaObject::invokeMethod(this, [this, seq, path, content]() {
                    apply_scene_text_sync_result(seq, path, content, /*from_disk=*/true);
                }, Qt::QueuedConnection);
            } catch (...) {
                ruby::core::ProjectContext::instance().set_status(
                    "Scene text sync skipped (FileRift decode failed).");
            }
        }).detach();
        return;
    }

    // Edit sync: serialize the RAM scene on a worker from a snapshot copy (the
    // live scene keeps mutating on the UI thread during drags). The dirty
    // markers are set NOW so the tab dot appears instantly; the worker only
    // refreshes the buffer text (~0.6 s later).
    if (!m_viewport_3d->has_scene()) return;
    if (m_viewport_3d->scene().filepath != path.toStdString()) return;
    m_dirty_scripts.insert(path);
    m_scene_text_synced.insert(path);
    av::SceneData copy = m_viewport_3d->scene();
    std::thread([this, path, seq, copy = std::move(copy), build_content]() mutable {
        try {
            const std::string binary = av::scene_serialize(copy);
            if (binary.empty()) return;
            const QString content = build_content(binary);
            QMetaObject::invokeMethod(this, [this, seq, path, content]() {
                apply_scene_text_sync_result(seq, path, content, /*from_disk=*/false);
            }, Qt::QueuedConnection);
        } catch (...) {
            ruby::core::ProjectContext::instance().set_status(
                "Scene text sync skipped (FileRift decode failed).");
        }
    }).detach();
}

void RubyMainWindow::apply_scene_text_sync_result(uint64_t seq, const QString& path,
                                                  const QString& content, bool from_disk) {
    // Stale result (a newer sync, a save, or the doc closing superseded it).
    if (seq != m_scene_text_sync_seq) return;
    if (find_document(path) < 0) return;   // doc was closed mid-decode
    if (!from_disk) {
        // The user typed (or the doc was saved) while the decode was running —
        // never clobber live text with a stale RAM-snapshot sync.
        if (scene_has_user_text_edits(path)) return;
    }

    m_script_buffers[path] = content;
    // Only touch the live QPlainTextEdit when the Script IDE actually SHOWS
    // this doc (mode tab active). When the editor is hidden the cached buffer
    // is authoritative — reloading the whole document on a hidden tab was a
    // full re-layout + re-highlight per edit.
    const bool ide_visible = m_mode_tabs && m_mode_tabs->currentIndex() == 1 &&
                             m_script_ide->current_file_path() == path;
    if (!from_disk) {
        m_dirty_scripts.insert(path);       // RAM differs from disk until save
        m_scene_text_synced.insert(path);   // this dirty is our own artifact
        if (ide_visible) {
            // CRITICAL: the IDE must actually SHOW the synced text. If the
            // cached buffer is updated but the visible QPlainTextEdit keeps
            // the pre-edit markup, the next save's scene_has_user_text_edits()
            // compares full_text() (stale) against the buffer (fresh) and
            // misreads our own sync artifact as user typing — routing the
            // save through the stale text writer, which reverts the gizmo /
            // mesh edit (the classic "Ctrl+S undid my change" bug).
            m_loading_doc = true;
            m_script_ide->load_buffer(content, path, QStringLiteral("scene"));
            m_loading_doc = false;
            m_script_ide->document()->setModified(true);
        }
    } else {
        m_dirty_scripts.remove(path);
        m_scene_text_synced.remove(path);
        if (ide_visible)
            m_script_ide->document()->setModified(false);
    }
    refresh_tab_labels();
}

// Structured save: serialize the viewport RAM scene straight to binary via the
// structured writer (av::scene_save), then refresh every view from the new file.
void RubyMainWindow::save_scene_doc_structured(int index) {
    if (index < 0 || index >= m_docs.size()) return;
    const QString path = m_docs[index].path;
    if (!m_viewport_3d || !m_viewport_3d->has_scene()) {
        ruby::core::ProjectContext::instance().set_status(
            "Scene has no 3D viewport state to save.");
        return;
    }
    if (m_viewport_3d->scene().filepath != path.toStdString()) {
        // The viewport is showing another scene — load the target first.
        m_viewport_3d->evict_scene_cache(path.toStdString());
        if (!m_viewport_3d->load_scene(path.toStdString())) {
            ruby::core::ProjectContext::instance().set_status("Could not reload " + path);
            return;
        }
    }
    // A pending debounced sync would re-mark the doc dirty AFTER this save
    // (and re-decode the RAM scene). Commit the coalesced undo entry now, and
    // let the save's own from-disk sync below be the final buffer refresh.
    if (m_scene_sync_timer) m_scene_sync_timer->stop();
    m_scene_sync_pending_path.clear();
    commit_pending_scene_undo();

    const av::SceneData scene = m_viewport_3d->scene();   // copy for the writer
    std::string error;
    if (!av::scene_save(path.toStdString(), scene, &error)) {
        ruby::core::ProjectContext::instance().set_status(
            "Scene save failed: " + QString::fromStdString(error));
        return;
    }
    // Smart save: the RAM scene IS the state that was just written — scene_save
    // serializes this exact SceneData, so re-reading and re-parsing the file
    // would only redo the same work (and froze the UI for seconds on big
    // scenes). Applying the same scene in place is a structure-identical no-op
    // that keeps the camera and GPU state untouched. The evict+reload fallback
    // stays only for the impossible mismatch case.
    bool applied_in_place = m_viewport_3d->apply_scene_data(scene, path.toStdString());
    if (!applied_in_place) {
        // evict() clears the scene flags, so preserve the camera explicitly.
        const auto cam = m_viewport_3d->camera_state();
        m_viewport_3d->evict_scene_cache(path.toStdString());
        m_viewport_3d->load_scene(path.toStdString());
        m_viewport_3d->set_camera_state(cam);
    }
    m_scene_struct_dirty.remove(path);
    // Clear the dirty markers NOW (the tab dot must not linger for the ~0.6 s
    // the async from-disk text sync takes); the worker only refreshes the
    // buffer content afterwards.
    m_dirty_scripts.remove(path);
    m_scene_text_synced.remove(path);
    sync_scene_text_buffer(path, /*from_disk=*/true);   // async buffer refresh
    refresh_tab_labels();
    // Git checkpoint + history refresh run OFF the main thread (libgit2 tree
    // write + revwalk froze the UI for seconds on every save). The history
    // refresh is posted back after the commit finishes.
    {
        const QString rel = path;
        const QString msg = "Save checkpoint: " + QFileInfo(path).fileName();
        RubyMainWindow* win = this;
        std::thread([win, rel, msg]() {
            ruby::git::RubyGit::commit_file(rel, msg);
            QMetaObject::invokeMethod(win, [win]() {
                if (win->m_history) win->m_history->refresh();
            }, Qt::QueuedConnection);
        }).detach();
    }
    ruby::core::ProjectContext::instance().set_status(
        "Saved (3D scene): " + path);
    if (m_console) {
        m_console->append_line("Saved (3D scene): " + path);
    }
}

void RubyMainWindow::onNewFile(const QString& target_dir) {
    QString dir = target_dir;
    if (dir.isEmpty() && m_asset_browser) {
        dir = m_asset_browser->current_selected_folder();
    }
    ruby::editor::NewFileDialog dlg(this, dir);
    if (dlg.exec() == QDialog::Accepted) {
        QString path = dlg.created_file_path();
        if (!path.isEmpty()) {
            open_document(path);
        }
    }
}

void RubyMainWindow::onFileSelectedInBrowser(const QString& file_path) {
    open_document(file_path);
}

// ── IDE-style multi-document layer ─────────────────────────────────────────
QString RubyMainWindow::kind_of_file(const QString& path) const {
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "pod" || ext == "glb" || ext == "gltf" || ext == "obj" || ext == "fbx") return "model";
    // QImage decodes all of these natively (no extra plugins required); .pvr/
    // .tex go through the platform PVR/ETC1/ETC2/ASTC decoder.
    if (ext == "pvr" || ext == "tex" || ext == "png" || ext == "jpg" || ext == "jpeg" ||
        ext == "bmp" || ext == "gif" || ext == "webp") return "texture";
    if (ext == "scene" || ext == "scn") return "scene";
    if (ext == "wav" || ext == "wave" || ext == "mp3" || ext == "ogg" || ext == "oga") return "audio";
    return "script";
}

int RubyMainWindow::find_document(const QString& path) const {
    const QString clean = QDir::cleanPath(path);
    for (int i = 0; i < m_docs.size(); ++i) {
        if (QDir::cleanPath(m_docs[i].path) == clean) return i;
    }
    return -1;
}

void RubyMainWindow::open_document(const QString& raw_path) {
    if (raw_path.isEmpty()) return;
    const QString path = QDir::cleanPath(raw_path);
    const int existing = find_document(path);
    if (existing >= 0) {
        if (m_doc_tabs->currentIndex() != existing) {
            m_doc_tabs->setCurrentIndex(existing);
        } else {
            activate_document(existing);
        }
        return;
    }
    if (m_active_doc >= 0 && m_active_doc < m_docs.size()) {
        m_docs[m_active_doc].active_mode_tab = m_mode_tabs->currentIndex();
        auto cam = m_viewport_3d->camera_state();
        m_docs[m_active_doc].cam_pitch = cam.pitch;
        m_docs[m_active_doc].cam_yaw = cam.yaw;
        m_docs[m_active_doc].cam_dist = cam.dist;
        m_docs[m_active_doc].cam_target[0] = cam.target[0];
        m_docs[m_active_doc].cam_target[1] = cam.target[1];
        m_docs[m_active_doc].cam_target[2] = cam.target[2];
        m_docs[m_active_doc].has_camera_state = true;
    }
    snapshot_active_script();
    RubyDocEntry entry;
    entry.path = path;
    entry.kind = kind_of_file(path);
    const ViewerCaps caps = caps_for(entry.kind);
    entry.active_mode_tab = caps.default_tab;
    const QString ext = QFileInfo(path).suffix().toLower();
    entry.encode_filerift = (entry.kind == "scene" || ext == "scene" || ext == "scn" || ext == "scl" || ext == "swdm" || ext == "gmesh");
    m_docs.append(entry);
    const int index = m_docs.size() - 1;
    m_doc_tabs->addTab(QFileInfo(path).fileName());
    m_doc_tabs->setTabToolTip(index, path);
    m_doc_tabs->setVisible(true);
    m_doc_tabs->setCurrentIndex(index); // fires currentChanged -> activate_document
    refresh_tab_labels();
}

void RubyMainWindow::snapshot_active_script() {
    if (m_loading_doc) return;
    const QString path = m_script_ide->current_file_path();
    if (path.isEmpty()) return;
    for (auto& doc : m_docs) {
        if (doc.path == path) {
            // full_text() returns the RAM canonical text (virtual mode) or the
            // whole document (never a partial paged window).
            m_script_buffers[path] = m_script_ide->full_text();
            doc.cursor_position = m_script_ide->textCursor().position();
            if (m_script_ide->document()->isModified()) {
                m_dirty_scripts.insert(path);
                // The doc's own virtual streaming also sets the modified flag
                // transiently — but streaming shows EXACTLY the cached buffer,
                // so only genuine text divergence is user typing. Record it:
                // once the user leaves the IDE tab the text comparison in
                // scene_has_user_text_edits() can no longer see it, and
                // structured edits must not fold over it.
                const QString cached = m_script_buffers.value(path);
                if (!cached.isEmpty() && m_script_ide->full_text() != cached)
                    m_user_text_paths.insert(path);
            }
            break;
        }
    }
}

void RubyMainWindow::load_doc_into_ide(int index) {
    if (index < 0 || index >= m_docs.size()) return;
    const RubyDocEntry& doc = m_docs[index];
    const bool is_scene = doc.kind == "scene";

    m_script_ide->set_filerift_encode_on_save(doc.encode_filerift);

    if (m_script_ide->current_file_path() == doc.path) {
        m_script_ide->document()->setModified(m_dirty_scripts.contains(doc.path));
        update_filerift_toggle_ui();
        return;
    }

    m_loading_doc = true;
    if (m_script_buffers.contains(doc.path)) {
        m_script_ide->load_buffer(m_script_buffers.value(doc.path), doc.path,
                                  m_script_types.value(doc.path));
    } else {
        QString schema;
        if (is_scene) schema = "scene";
        else {
            const QString ext = QFileInfo(doc.path).suffix().toLower();
            schema = ext == "scl" ? "scl" : (ext == "swdm" || ext == "gmesh" ? "scene" : QString());
        }
        if (m_script_ide->load_file(doc.path, schema)) {
            // Buffer caching happens on documentLoaded (load_file is async);
            // the type map is set now so re-encodes know the schema.
            m_script_types[doc.path] = schema;
            if (!schema.isEmpty())
                ruby::core::ProjectContext::instance().set_status(
                    QString("Decoded %1 via FileRift (%2). Edit then Save to re-encode.")
                        .arg(doc.path, schema));
        } else {
            ruby::core::ProjectContext::instance().set_status("Could not open " + doc.path);
        }
    }
    m_loading_doc = false;
    m_script_ide->document()->setModified(m_dirty_scripts.contains(doc.path));

    if (doc.cursor_position > 0) {
        QTextCursor c = m_script_ide->textCursor();
        const int max_pos = m_script_ide->document()->characterCount() - 1;
        c.setPosition(std::clamp(doc.cursor_position, 0, std::max(0, max_pos)));
        m_script_ide->setTextCursor(c);
        m_script_ide->ensureCursorVisible();
    }
    update_filerift_toggle_ui();
}

RubyMainWindow::ViewerCaps RubyMainWindow::caps_for(const QString& kind) const {
    ViewerCaps caps;
    if (kind == "model") {
        caps.viewport = true;  caps.default_tab = 0;
    } else if (kind == "scene") {
        caps.viewport = true; caps.ide = true; caps.default_tab = 0;
    } else if (kind == "texture") {
        caps.viewport = true; caps.texture = true; caps.default_tab = 2;
    } else if (kind == "audio") {
        caps.audio = true; caps.default_tab = 4;
    } else if (kind == "script") {
        caps.ide = true; caps.default_tab = 1;
    } else {
        caps.default_tab = -1; // leave the current mode alone
    }
    return caps;
}

QString RubyMainWindow::notice_for(const QString& kind, int mode_tab) const {
    const QString ext = m_active_doc >= 0 && m_active_doc < m_docs.size()
        ? QFileInfo(m_docs[m_active_doc].path).suffix().toLower() : QString();
    if (mode_tab == 0) { // 3D Viewport
        if (kind == "script" && ext == "scl")
            return "SCL object libraries are FileRift blueprints, not 3D geometry —\n"
                   "switch to the Script IDE to decode and edit them.";
        if (kind == "script")
            return "This is a text file — it has no 3D representation.\nSwitch to the Script IDE to edit it.";
        if (kind == "texture")
            return "Textures are viewed in the Texture Viewer (default) or as a poster here in 3D space.\n"
                   "This page shows the texture standing on the grid — orbit with the mouse.";
        return "Nothing to show in 3D for this file.\nSwitch to the Script IDE or Texture Viewer.";
    }
    if (mode_tab == 1) { // Script IDE
        if (kind == "model")
            return "POD/OBJ/GLB models are binary 3D assets —\nview them in the 3D Viewport instead.";
        if (kind == "texture")
            return "Binary image data can't be edited as text.\nUse the Texture Viewer (or the 3D poster view).";
        return "Nothing to show in the editor for this file.";
    }
    if (mode_tab == 4) { // Audio Viewer
        if (kind == "audio")
            return QString();   // the viewer itself handles the file
        if (kind == "scene")
            return "Scene binaries aren't audio — open them in the 3D Viewport.";
        if (kind == "model")
            return "Models aren't audio — view them in the 3D Viewport.";
        if (kind == "texture")
            return "Images aren't audio — use the Texture Viewer.";
        return "This file isn't audio.\nUse the Audio Viewer only for WAV / MP3 / OGG assets.";
    }
    // Texture Viewer
    if (kind == "scene")
        return "Scene binaries are spatial data — open them in the 3D Viewport, or inspect the\n"
               "FileRift markup in the Script IDE.";
    if (kind == "model")
        return "Models contain textures, but view them through the 3D Viewport — or open a .pvr/.png directly.";
    if (kind == "script" && ext == "scl")
        return "SCL libraries aren't textures — decode them in the Script IDE.";
    if (kind == "audio")
        return "Audio is played in the Audio Viewer tab (default).";
    return "This file isn't an image.\nUse the Texture Viewer only for PVR/TEX/PNG/JPEG assets.";
}

void RubyMainWindow::sync_views() {
    // If no documents are open, show the animated cyber StudioIdleWidget
    if (m_central_stack) {
        m_central_stack->setCurrentIndex(m_docs.isEmpty() ? 0 : 1);
    }

    ViewerCaps caps;
    QString active_kind = "none";
    if (m_active_doc >= 0 && m_active_doc < m_docs.size()) {
        active_kind = m_docs[m_active_doc].kind;
        caps = caps_for(active_kind);
    } else {
        // Empty studio: show every editor.
        caps.viewport = caps.ide = caps.texture = caps.audio = true;
    }

    m_3d_stack->setCurrentIndex(caps.viewport ? StackView : StackNotice);
    m_ide_stack->setCurrentIndex(caps.ide ? StackView : StackNotice);
    m_tex_stack->setCurrentIndex(caps.texture ? StackView : StackNotice);
    m_audio_stack->setCurrentIndex(caps.audio ? StackView : StackNotice);

    // Tab visibility:
    // Tab 2 (Texture Viewer) is hidden for scenes (scenes have no texture viewer)
    // Tab 3 (Scene Tools) is only visible for scene files
    // Tab 4 (Audio Viewer) is only visible for audio files
    const bool is_scene = (active_kind == "scene");
    m_mode_tabs->setTabVisible(2, !is_scene && caps.texture);
    m_mode_tabs->setTabVisible(3, is_scene);
    m_mode_tabs->setTabVisible(4, !is_scene && caps.audio);

    m_3d_notice->setText(notice_for(active_kind, 0));
    m_ide_notice->setText(notice_for(active_kind, 1));
    m_tex_notice->setText(notice_for(active_kind, 2));
    m_audio_notice->setText(notice_for(active_kind, 4));

    // Animation bar & Model Top Bar are visible when viewing a 3D model in the viewport
    bool is_pod_view = (m_active_doc >= 0 && m_active_doc < m_docs.size()
                        && m_docs[m_active_doc].kind == "model"
                        && m_mode_tabs && m_mode_tabs->currentIndex() == 0);
    if (m_animation_dock) {
        m_animation_dock->setVisible(is_pod_view);
    }
    if (m_model_top_bar) {
        m_model_top_bar->setVisible(is_pod_view);
        if (is_pod_view) {
            const QString path = m_docs[m_active_doc].path;
            const QFileInfo fi(path);
            const QString ext = fi.suffix().toLower();
            if (m_model_name_label) m_model_name_label->setText(fi.fileName());
            if (m_model_format_badge) {
                m_model_format_badge->setText("[" + ext.toUpper() + "]");
                if (ext == "pod") {
                    m_model_format_badge->setStyleSheet(QStringLiteral("background: #23372b; color: #98c379; border-radius: 3px; padding: 1px 6px; font-size: 10px; font-weight: bold;"));
                } else {
                    m_model_format_badge->setStyleSheet(QStringLiteral("background: #213247; color: #61afef; border-radius: 3px; padding: 1px 6px; font-size: 10px; font-weight: bold;"));
                }
            }
            if (m_model_convert_btn) {
                if (ext == "pod") {
                    m_model_convert_btn->setText(QStringLiteral("✦ Convert Model to POD..."));
                } else {
                    m_model_convert_btn->setText(QStringLiteral("✦ Convert to Game POD..."));
                }
            }
            if (m_model_stats_label && m_viewport_3d && m_viewport_3d->has_model()) {
                const auto& m = m_viewport_3d->current_model();
                float w = std::max(0.0f, m.max_x - m.min_x);
                float h = std::max(0.0f, m.max_y - m.min_y);
                float d = std::max(0.0f, m.max_z - m.min_z);
                if (h > 0.001f || w > 0.001f || d > 0.001f) {
                    m_model_stats_label->setText(QString(
                        "Dims: %1 W × %2 H × %3 D units &nbsp;|&nbsp; %4 %5, %6 Verts")
                        .arg(QString::number(w, 'f', 1))
                        .arg(QString::number(h, 'f', 1))
                        .arg(QString::number(d, 'f', 1))
                        .arg(m.meshes.size())
                        .arg(m.meshes.size() == 1 ? "Mesh" : "Meshes")
                        .arg(m.total_vertices));
                }
            }
        }
    }

    if (m_ide_status) {
        QString file = m_active_doc >= 0 && m_active_doc < m_docs.size()
            ? QFileInfo(m_docs[m_active_doc].path).fileName() : QString();
        if (m_ide_status->text().startsWith("No file") || !file.isEmpty())
            m_ide_status->setText(file.isEmpty() ? "No file open"
                : file + "   ·   Script IDE (FileRift binary markup supported for .scene/.scl/.swdm)");
    }
    update_filerift_toggle_ui();
}

void RubyMainWindow::refresh_tab_labels() {
    for (int i = 0; i < m_docs.size(); ++i) {
        const RubyDocEntry& doc = m_docs[i];
        const QString name = QFileInfo(doc.path).fileName();
        const QString marker = m_dirty_scripts.contains(doc.path) ? "● " : QString();
        if (m_doc_tabs->tabText(i) != marker + name)
            m_doc_tabs->setTabText(i, marker + name);
    }
}

void RubyMainWindow::activate_document(int index) {
    if (index < 0 || index >= m_docs.size()) {
        if (index == -1) {
            m_active_doc = -1;
            sync_views();
        }
        return;
    }
    // A debounced structured-edit sync still pending from the outgoing doc:
    // flush it while the viewport still shows that scene, so the coalesced
    // undo entry lands on the right scene's stack and the text view is fresh.
    flush_pending_scene_sync();
    if (m_active_doc >= 0 && m_active_doc < m_docs.size()) {
        m_docs[m_active_doc].active_mode_tab = m_mode_tabs->currentIndex();
        auto cam = m_viewport_3d->camera_state();
        m_docs[m_active_doc].cam_pitch = cam.pitch;
        m_docs[m_active_doc].cam_yaw = cam.yaw;
        m_docs[m_active_doc].cam_dist = cam.dist;
        m_docs[m_active_doc].cam_target[0] = cam.target[0];
        m_docs[m_active_doc].cam_target[1] = cam.target[1];
        m_docs[m_active_doc].cam_target[2] = cam.target[2];
        m_docs[m_active_doc].has_camera_state = true;
    }
    snapshot_active_script();
    m_active_doc = index;
    const RubyDocEntry& doc = m_docs[index];
    const ViewerCaps caps = caps_for(doc.kind);

    // A mesh-edit session is bound to the scene currently loaded in the
    // viewport. Switching to any OTHER document ends it: the projection-locked
    // camera must never survive a doc switch (the "stuck camera" state seen
    // when several files are open). Re-activating the SAME scene keeps the
    // session, and loading a different scene is already guarded inside the
    // viewport's load paths.
    if (m_viewport_3d && m_viewport_3d->mesh_edit_active() &&
        (doc.kind != "scene" ||
         m_viewport_3d->scene().filepath != doc.path.toStdString())) {
        m_viewport_3d->set_mesh_edit(false);
    }

    m_inspector->inspect_file(doc.path);
    ruby::core::ProjectContext::instance().set_active_file(doc.path.toStdString());
    if (m_history) {
        m_history->set_target_file(doc.path);
    }

    const bool is_scene = (doc.kind == "scene");
    m_mode_tabs->setTabVisible(2, !is_scene && caps.texture);
    m_mode_tabs->setTabVisible(3, is_scene);
    m_mode_tabs->setTabVisible(4, !is_scene && caps.audio);

    // Restore the document's last active mode tab, falling back to its kind default
    int target_tab = doc.active_mode_tab;
    if (target_tab < 0 || target_tab >= m_mode_tabs->count()) {
        target_tab = caps.default_tab;
    }
    if (target_tab == 0 && !caps.viewport) target_tab = caps.default_tab;
    else if (target_tab == 1 && !caps.ide) target_tab = caps.default_tab;
    else if (target_tab == 2 && (is_scene || !caps.texture)) target_tab = caps.default_tab;
    else if (target_tab == 3 && !is_scene) target_tab = caps.default_tab;
    else if (target_tab == 4 && (is_scene || !caps.audio)) target_tab = caps.default_tab;

    if (target_tab >= 0 && target_tab < m_mode_tabs->count() &&
        m_mode_tabs->currentIndex() != target_tab) {
        m_mode_tabs->setCurrentIndex(target_tab);
    }

    update_engine_scene_context();

    // ── 3D Viewport ──
    if (caps.viewport) {
        if (doc.kind == "model") {
            if (m_viewport_3d->load_model(doc.path.toStdString())) {
                m_animation_controls->set_frame_count(m_viewport_3d->frame_count());
                ruby::core::ProjectContext::instance().set_status("Loaded 3D Model: " + doc.path);
            } else ruby::core::ProjectContext::instance().set_status("Could not load model: " + doc.path);
        } else if (doc.kind == "scene") {
            if (scene_has_user_text_edits(doc.path)) {
                // Unsaved FileRift edits: preview the re-encoded bytes in the
                // viewport instead of the stale disk state.
                if (!apply_unsaved_scene_text_to_viewport(doc.path)) {
                    m_viewport_3d->load_scene_async(doc.path.toStdString());
                    ruby::core::ProjectContext::instance().set_status(
                        "Loading scene: " + doc.path + "...");
                } else {
                    ruby::core::ProjectContext::instance().set_status(
                        "3D preview of unsaved scene text: " + doc.path);
                }
            } else {
                m_viewport_3d->load_scene_async(doc.path.toStdString());
                ruby::core::ProjectContext::instance().set_status("Loading scene: " + doc.path + "...");
            }
        } else if (doc.kind == "texture") {
            if (m_viewport_3d->load_texture_preview(doc.path.toStdString()))
                ruby::core::ProjectContext::instance().set_status("Texture in 3D: " + doc.path);
            else ruby::core::ProjectContext::instance().set_status("Could not decode texture: " + doc.path);
        }
        if (doc.has_camera_state) {
            ruby::viewport::Viewport3DWidget::CameraState cam;
            cam.pitch = doc.cam_pitch;
            cam.yaw = doc.cam_yaw;
            cam.dist = doc.cam_dist;
            cam.target[0] = doc.cam_target[0];
            cam.target[1] = doc.cam_target[1];
            cam.target[2] = doc.cam_target[2];
            m_viewport_3d->set_camera_state(cam);
        }
    }

    // ── Texture Viewer ──
    if (caps.texture) {
        if (!m_texture_viewer->load_texture(doc.path))
            ruby::core::ProjectContext::instance().set_status("Could not decode texture: " + doc.path);
    }

    // ── Audio Viewer ──
    if (caps.audio) {
        if (m_audio_viewer->load_audio(doc.path)) {
            if (m_mode_tabs->currentIndex() != 4)
                ruby::core::ProjectContext::instance().set_status(
                    "Decoding audio: " + doc.path);
        }
    } else if (m_audio_viewer) {
        m_audio_viewer->stop();
    }

    // ── Script IDE (scene markup via FileRift, scripts, SCL) ──
    if (caps.ide) {
        if (doc.kind != "scene" || m_mode_tabs->currentIndex() == 1) {
            load_doc_into_ide(index);
        }
    }

    sync_views();
    refresh_tab_labels();
}

void RubyMainWindow::close_document(int index) {
    if (index < 0 || index >= m_docs.size()) return;
    const RubyDocEntry doc = m_docs[index];

    // Guard unsaved IDE work (scripts and FileRift scene markup).
    snapshot_active_script();
    if ((doc.kind == "script" || doc.kind == "scene") && m_dirty_scripts.contains(doc.path)) {
        const auto choice = QMessageBox::question(
            this, "Unsaved changes",
            QString("Save changes to %1 before closing?").arg(QFileInfo(doc.path).fileName()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (choice == QMessageBox::Cancel) return;
        if (choice == QMessageBox::Save) {
            if (m_script_ide->current_file_path() != doc.path) {
                load_doc_into_ide(index);
            }
            if (!m_script_ide->save_file()) return;
        }
    }

    m_doc_tabs->removeTab(index);
    m_docs.removeAt(index);
    m_script_buffers.remove(doc.path);
    m_script_types.remove(doc.path);
    m_dirty_scripts.remove(doc.path);
    m_user_text_paths.remove(doc.path);
    // A scene-text decode may be in flight for this doc — its result must not
    // resurrect the closed document's buffer.
    ++m_scene_text_sync_seq;
    // Closing the scene that is currently being mesh-edited ends the session
    // (its projection-locked camera and overlay are bound to this doc).
    if (doc.kind == "scene" && m_viewport_3d &&
        m_viewport_3d->mesh_edit_active() &&
        m_viewport_3d->scene().filepath == doc.path.toStdString()) {
        m_viewport_3d->set_mesh_edit(false);
    }
    if (doc.kind == "scene" && m_viewport_3d) {
        m_viewport_3d->evict_scene_cache(doc.path.toStdString());
    }
    if (m_docs.isEmpty()) {
        m_doc_tabs->setVisible(false);
        m_active_doc = -1;
        sync_views();
    } else {
        // QTabBar promotes a neighbor; currentChanged activates it.
        m_active_doc = m_doc_tabs->currentIndex();
        activate_document(m_active_doc);
    }
    update_engine_scene_context();
    refresh_tab_labels();
}

void RubyMainWindow::onModelLoaded(const QString& name, int meshCount, int vertCount) {
    m_inspector->inspect_model_info(name, meshCount, vertCount);
    if (m_model_stats_label && m_viewport_3d) {
        if (m_viewport_3d->has_model()) {
            const auto& m = m_viewport_3d->current_model();
            float w = std::max(0.0f, m.max_x - m.min_x);
            float h = std::max(0.0f, m.max_y - m.min_y);
            float d = std::max(0.0f, m.max_z - m.min_z);
            if (h > 0.001f || w > 0.001f || d > 0.001f) {
                m_model_stats_label->setText(QString(
                    "Dims: %1 W × %2 H × %3 D units &nbsp;|&nbsp; %4 %5, %6 Verts")
                    .arg(QString::number(w, 'f', 1))
                    .arg(QString::number(h, 'f', 1))
                    .arg(QString::number(d, 'f', 1))
                    .arg(meshCount)
                    .arg(meshCount == 1 ? "Mesh" : "Meshes")
                    .arg(vertCount));
            } else {
                m_model_stats_label->setText(QString("%1 %2, %3 Verts")
                    .arg(meshCount)
                    .arg(meshCount == 1 ? "Mesh" : "Meshes")
                    .arg(vertCount));
            }
        } else {
            m_model_stats_label->setText(QString("%1 %2, %3 Verts")
                .arg(meshCount)
                .arg(meshCount == 1 ? "Mesh" : "Meshes")
                .arg(vertCount));
        }
    }
}

void RubyMainWindow::onConvertModel(const QString& source_path) {
    if (!m_convert_dialog) {
        m_convert_dialog = new ruby::editor::ModelConvertDialog(this);
        connect(m_convert_dialog, &ruby::editor::ModelConvertDialog::conversionSucceeded,
                this, [this](const QString& pod_path) {
            if (m_asset_browser) {
                m_asset_browser->refresh_now();
            }
            open_document(pod_path);
        });
    }

    if (!source_path.isEmpty()) {
        m_convert_dialog->set_source_path(source_path);
    } else if (m_active_doc >= 0 && m_active_doc < m_docs.size()) {
        const RubyDocEntry& doc = m_docs[m_active_doc];
        if (doc.kind == "model") {
            m_convert_dialog->set_source_path(doc.path);
        }
    }

    m_convert_dialog->show();
    m_convert_dialog->raise();
    m_convert_dialog->activateWindow();
}

void RubyMainWindow::onOpenDocumentation() {
    if (!m_doc_viewer) {
        m_doc_viewer = new ruby::editor::DocViewerDialog(this);
    }
    m_doc_viewer->show();
    m_doc_viewer->raise();
    m_doc_viewer->activateWindow();
}

void RubyMainWindow::update_filerift_toggle_ui() {
    if (!m_filerift_toggle) return;
    if (m_active_doc < 0 || m_active_doc >= m_docs.size()) {
        m_filerift_toggle->setEnabled(false);
        m_filerift_toggle->setText("FileRift: N/A");
        m_filerift_toggle->setToolTip("No file open");
        return;
    }
    const RubyDocEntry& doc = m_docs[m_active_doc];
    const QString ext = QFileInfo(doc.path).suffix().toLower();
    const bool is_filerift = (doc.kind == "scene" || ext == "scene" || ext == "scn" ||
                              ext == "scl" || ext == "swdm" || ext == "gmesh" ||
                              !m_script_types.value(doc.path).isEmpty());

    m_filerift_toggle->blockSignals(true);
    if (is_filerift) {
        m_filerift_toggle->setEnabled(true);
        m_filerift_toggle->setChecked(doc.encode_filerift);
        if (doc.encode_filerift) {
            m_filerift_toggle->setText("FileRift: Encode to Binary on Save");
            m_filerift_toggle->setToolTip("FileRift enabled: on save, markup will be compiled to binary protobuf.\nUncheck to save as raw UTF-8 text.");
        } else {
            m_filerift_toggle->setText("FileRift: Save as Raw Text");
            m_filerift_toggle->setToolTip("FileRift disabled: on save, text will be saved directly as plain UTF-8.\nCheck to compile to binary protobuf.");
        }
    } else {
        m_filerift_toggle->setEnabled(false);
        m_filerift_toggle->setChecked(false);
        m_filerift_toggle->setText("FileRift: N/A (Plain UTF-8 text)");
        m_filerift_toggle->setToolTip(QString("Non-FileRift file (%1): always read and saved as raw UTF-8 text.").arg(ext.isEmpty() ? "plain" : ext));
    }
    m_filerift_toggle->blockSignals(false);
}

} // namespace ruby

