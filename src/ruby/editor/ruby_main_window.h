#pragma once
// ============================================================================
// ruby_main_window.h — Ruby GG Main Studio Window
//   Unifies 3D Viewport, Code IDE, Asset Browser, and Properties Inspector
//   into a cohesive Blender/UE5-style dark studio workspace.
//
//   Central layout (IDE style):
//     [ document tabs (one per open file) ]
//     [ 3D Viewport | Script IDE | Texture Viewer | Scene Tools ]
//   Every viewer re-points to the ACTIVE document: a .scene shows in both the
//   viewport and the FileRift-backed Script IDE; a .pvr shows in the Texture
//   Viewer and as a 3D poster; unsupported combos get an explainer page.
// ============================================================================

#include <QMainWindow>
#include <QDockWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QStackedWidget>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>
#include <vector>
#include "tools/scene_loader.h"   // av::SclTemplateEntry (template catalog)
#include "ruby/panels/template_palette_panel.h"
#include "ruby/panels/template_inspector_panel.h"

class QLabel;
class QPushButton;
class QCheckBox;
namespace ruby::editor { class StudioIdleWidget; class ApkSessionPanel; }

namespace ruby::viewport { class Viewport3DWidget; }
namespace ruby::editor   { class ScriptIDEWidget; class DocViewerDialog; class ModelConvertDialog; }
namespace ruby::panels   { class AssetBrowserPanel; class InspectorPanel; class AnimationControlBar; class TextureViewerPanel; class AudioViewerPanel; class SceneHierarchyPanel; class ConsolePanel; class LightingPanel; class LocalHistoryPanel; }
namespace ruby::tools { class RubyToolsWorkspace; }
namespace ruby::emulator { class EnginePreviewPanel; }
namespace ruby::graph { class GraphyCanvas; }

namespace ruby {

// One entry in the IDE-style document bar. A document is any file opened from
// the asset browser / File menu; its owning editor (3D viewport, texture
// viewer, script IDE) is resolved from `kind` when the tab is activated.
struct RubyDocEntry {
    QString path;
    QString kind;
    int active_mode_tab = -1;
    bool encode_filerift = true;
    int cursor_position = 0;
    float cam_pitch = 15.0f;
    float cam_yaw   = -45.0f;
    float cam_dist  = 120.0f;
    float cam_target[3] = {0.0f, 20.0f, 0.0f};
    bool has_camera_state = false;
};

class RubyMainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit RubyMainWindow(QWidget* parent = nullptr);
    ~RubyMainWindow() override;

    // Wayland-only: Qt moves floating QDockWidgets with QWidget::move(), a
    // no-op for toplevels on Wayland — so floating dock title bars cannot be
    // dragged there. This filter routes title-strip presses through the
    // interactive move protocol instead (see eventFilter()).
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onNewFile(const QString& target_dir = QString());
    void onOpenProject();
    void onOpenFile();
    void onImportApk();    // File ▸ Import APK… (master TODO 4.1a)
    void onExportApk();    // File ▸ Export as APK… (gated; repack lands in 4.1b)
    void onSaveFile();
    void onFileSelectedInBrowser(const QString& file_path);
    void onModelLoaded(const QString& name, int meshCount, int vertCount);
    void onConvertModel(const QString& source_path = QString());
    void onOpenTools();
    void onOpenDocumentation();
    void onEngineBootClicked();       // phone button → show dock + boot
    void on_engine_run_scene(const QString& scene_path); // ▶ Run Scene in dock
    void on_viewport_scene_edited();  // gizmo/mesh commit → mark doc dirty, sync text
    void flush_pending_scene_sync();  // debounced heavy sync after edits settle

    // Template palette + template hierarchy (master TODO 2.3/2.4).
    void on_template_add_requested(const QString& template_name, const QString& scl_path);
    void on_model_add_requested(const QString& pod_path, const QString& display_name);
    void on_template_changed(int object_index, const QString& template_name);
    void on_template_materialize(int object_index);
    void on_template_reset(int object_index);
    void on_template_override(int object_index, const QString& class_name);

private:
    void setup_menus(QMenuBar* menu_bar);
    void setup_left_rail();           // left vertical rail: run + file/tool actions
    void setup_dock_panels();
    void build_panels_menu(QMenu* view_menu);  // per-dock respawn toggles
    void reset_panel_layout();        // re-dock everything to the default layout
    void fit_engine_preview_dock();   // landscape 16:9 dock for the engine pod

    // ── IDE-style multi-document layer ──
    void open_document(const QString& path);       // add or focus a document tab
    void activate_document(int index);             // route active doc to every viewer
    void close_document(int index);                // close tab (asks to save edits)
    void snapshot_active_script();                 // keep unsaved IDE text per file
    void load_doc_into_ide(int index);             // (re)load a doc's text into the IDE
    QString kind_of_file(const QString& path) const;
    void refresh_tab_labels();                     // dirty-marker tab titles
    int  find_document(const QString& path) const;

    // Engine-preview dock scene-shifter context.
    void update_engine_scene_context();   // active scene doc → ▶ Run Scene control
    bool save_scene_doc_for_run(int index); // FileRift re-encode; true = disk fresh

    // Floating-dock Wayland drag workaround state.
    bool m_wayland_drag_filter = false;

    // Structured (3D viewport) scene editing — see todo_scene_editor.md.
    void save_scene_doc_structured(int index); // scene_save from the viewport RAM scene
    void sync_scene_text_buffer(const QString& path, bool from_disk);
    // App-wide Ctrl+Z / Ctrl+Y routing (scene undo unless the Script IDE has
    // focus, where Qt text undo must win). Returns true when handled.
    bool route_undo_shortcut(QKeyEvent* event);
    // Bridge: refresh the inspector panel for the currently selected object.
    // Called after undo/redo, component field changes, visibility toggles,
    // template mutations, and ground-mesh edits to keep the inspector in sync
    // with the live viewport scene data.
    void sync_inspector_from_viewport();
    // Refresh the Template Palette + Template Inspector from the live scene:
    // roots = scene dir + imported libraries + project + home asset dirs; the
    // catalog (every known template) is shared by both panels. rescan=true
    // re-walks the filesystem for .scl/.pod (scene load + edits); false only
    // re-renders the inspector with the cached catalog (selection changes).
    void refresh_scene_templates(bool rescan = true);
    bool scene_has_user_text_edits(const QString& path) const;
    // Re-encode a scene's unsaved FileRift markup in memory and show it in the
    // 3D viewport WITHOUT saving to disk (structure-preserving edits swap in
    // place; structural changes take the async in-memory load path).
    bool apply_unsaved_scene_text_to_viewport(const QString& path);
    // Tools → Ground Mesh Studio "Add to Scene…": paste a generated ground-mesh
    // object (binary, Scene field 1 = one Object) into the open scene's RAM.
    void on_ground_mesh_add_to_scene(const QString& identifier,
                                     const QByteArray& scene_bytes,
                                     double pos_x, double pos_y, double depth);
    QSet<QString> m_scene_struct_dirty;   // docs with in-RAM 3D-viewport edits
    QSet<QString> m_scene_text_synced;    // docs whose text-dirty came from our sync
    // Debounced structured-edit sync: every gizmo commit / inspector change
    // used to run a full scene re-encode + FileRift decode + editor reload +
    // hierarchy rebuild synchronously (seconds of freeze per change). Edits
    // now mark the doc dirty immediately and coalesce the expensive resync
    // into one pass ~250 ms after the last change. The undo entry for a
    // spinbox burst is pushed when the burst settles.
    QTimer* m_scene_sync_timer = nullptr;
    QString m_scene_sync_pending_path;
    std::string m_scene_sync_undo_before;   // snapshot captured at burst start
    bool m_scene_sync_undo_armed = false;
    QString m_scene_sync_undo_path;         // scene the armed snapshot belongs to
    // Inspector ↔ 3D viewport bridge: QMetaObject::Connection to the active
    // scene's QUndoStack::indexChanged, re-wired on each scene load so the
    // inspector refreshes after every undo/redo step.
    QMetaObject::Connection m_undo_stack_conn;
    // Push the coalesced spinbox-burst undo entry (guarded against doc switches
    // mid-burst: it must land on the SAME scene's undo stack or be dropped).
    void commit_pending_scene_undo();
    // Scene→FileRift-text sync is dispatched to a worker thread (the decode of
    // a multi-MB scene took ~600 ms on the UI thread per edit/save). The
    // generation counter drops stale results (newer sync, save, or doc close).
    uint64_t m_scene_text_sync_seq = 0;
    void apply_scene_text_sync_result(uint64_t seq, const QString& path,
                                      const QString& content, bool from_disk);
    // Docs where the user typed in the Script IDE and then left the tab. The
    // snapshot preserved their text in m_script_buffers, but the IDE's own
    // virtual streaming also touches the document-modified flag — so the only
    // reliable "user edited" signals are (a) text differing from the cached
    // buffer while the IDE shows the doc, or (b) this explicit marker.
    QSet<QString> m_user_text_paths;

    // Per-kind viewer capability model: which mode pages can show this file,
    // which mode is the default, and what to say when a combo is unsupported.
    struct ViewerCaps {
        bool viewport = false;
        bool ide = false;
        bool texture = false;
        bool audio = false;
        int default_tab = 0; // 0 viewport, 1 IDE, 2 texture, 3 tools, 4 audio
    };
    ViewerCaps caps_for(const QString& kind) const;
    QString notice_for(const QString& kind, int mode_tab) const;
    void sync_views();               // push notices / show viewers for active doc
    void update_filerift_toggle_ui(); // update FileRift encode checkbox state

    // Document tab bar (above the viewing-mode tabs) + per-document state.
    QTabBar* m_doc_tabs = nullptr;
    QVector<RubyDocEntry> m_docs;
    QHash<QString, QString> m_script_buffers;  // path -> decoded editor text
    QHash<QString, QString> m_script_types;    // path -> filerift schema (scl, scene…)
    QSet<QString> m_dirty_scripts;             // paths whose buffer differs from disk
    int m_active_doc = -1;
    bool m_loading_doc = false;

    // Central widget: document tab bar stacked over the viewing-mode tabs.
    QWidget* m_central = nullptr;
    QStackedWidget* m_central_stack = nullptr;
    QTabWidget* m_mode_tabs = nullptr;

    // Mode pages: each hosts a stack whose "notice" page explains why the
    // current document can't open there (e.g. SCL in the 3D viewport).
    QStackedWidget* m_3d_stack = nullptr;
    QStackedWidget* m_ide_stack = nullptr;
    QStackedWidget* m_tex_stack = nullptr;
    QStackedWidget* m_audio_stack = nullptr;
    QLabel* m_3d_notice = nullptr;
    QLabel* m_ide_notice = nullptr;
    QLabel* m_tex_notice = nullptr;
    QLabel* m_audio_notice = nullptr;
    QLabel* m_ide_status = nullptr;
    QCheckBox* m_filerift_toggle = nullptr;
    enum { StackView = 0, StackNotice = 1 };

    ruby::viewport::Viewport3DWidget* m_viewport_3d = nullptr;
    ruby::editor::ScriptIDEWidget*    m_script_ide = nullptr;
    ruby::panels::TextureViewerPanel* m_texture_viewer = nullptr;
    ruby::panels::AudioViewerPanel*   m_audio_viewer = nullptr;
    ruby::graph::GraphyCanvas*        m_graph_canvas = nullptr;
    ruby::panels::SceneHierarchyPanel* m_scene_hierarchy = nullptr;
    ruby::editor::StudioIdleWidget*   m_idle_widget = nullptr;
    QDockWidget* m_scene_dock = nullptr;

    // Dock panels
    QDockWidget* m_asset_dock = nullptr;
    ruby::panels::AssetBrowserPanel* m_asset_browser = nullptr;

    QDockWidget* m_inspector_dock = nullptr;
    ruby::panels::InspectorPanel* m_inspector = nullptr;
    QDockWidget* m_lighting_dock = nullptr;
    ruby::panels::LightingPanel* m_lighting = nullptr;
    QDockWidget* m_animation_dock = nullptr;
    ruby::panels::AnimationControlBar* m_animation_controls = nullptr;

    // Template palette (add objects) + template hierarchy inspector.
    QDockWidget* m_template_palette_dock = nullptr;
    ruby::panels::TemplatePalettePanel* m_template_palette = nullptr;
    QDockWidget* m_template_inspector_dock = nullptr;
    ruby::panels::TemplateInspectorPanel* m_template_inspector = nullptr;
    // Every known template (scene-embedded + scanned .scl), shared by the
    // palette and the inspector's combo + inherited-component list.
    std::vector<av::SclTemplateEntry> m_template_catalog;
    // Memoization for refresh_scene_templates: the full filesystem template
    // scan (every imported-library dir + home asset dirs) took ~1.3-2.6 s per
    // drag-end flush / save / in-place apply. The source set only changes when
    // the scene identity, library list or object count changes — key on that
    // and skip the walk otherwise (selection-only refresh).
    std::string m_template_scan_key;
    std::vector<av::TemplateSourceEntry> m_template_scan_entries;

    // VS Code-style terminal.
    QDockWidget* m_console_dock = nullptr;
    ruby::panels::ConsolePanel* m_console = nullptr;

    // Git-backed Local History panel.
    QDockWidget* m_history_dock = nullptr;
    ruby::panels::LocalHistoryPanel* m_history = nullptr;

    ruby::tools::RubyToolsWorkspace* m_tools = nullptr;
    ruby::editor::DocViewerDialog* m_doc_viewer = nullptr;

    // Inline engine pod (1.4.13 mini emulator dock).
    QDockWidget* m_engine_dock = nullptr;
    ruby::emulator::EnginePreviewPanel* m_engine_panel = nullptr;

    // APK session (master TODO 4.1a): status-bar badge + Import handler.
    // The active apk::Session lives inside the panel (single source of truth
    // for 4.1b's Export flow); the menu action below is enabled only while a
    // session is active.
    ruby::editor::ApkSessionPanel* m_apk_session = nullptr;
    QAction* m_export_apk_act = nullptr;

    // 3D Viewport model top bar & conversion
    QWidget* m_model_top_bar = nullptr;
    QLabel*  m_model_name_label = nullptr;
    QLabel*  m_model_format_badge = nullptr;
    QLabel*  m_model_stats_label = nullptr;
    QPushButton* m_model_convert_btn = nullptr;
    ruby::editor::ModelConvertDialog* m_convert_dialog = nullptr;
};

} // namespace ruby
