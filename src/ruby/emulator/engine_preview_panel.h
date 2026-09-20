// ============================================================================
// engine_preview_panel.h — "Engine Preview" dock for Ruby GG
//
// A phone-sized Swordigo 1.4.13 mini emulator living in the dock sidebar:
//   ┌────────────────────────────────────────────┐
//   │ ● Running  · 60 fps   · assets13           │
//   │ ┌────────────────────────────────────────┐ │
//   │ │                                        │ │  ← PreviewView (zoom/pan)
//   │ │          Swordigo 1.4.13               │ │
//   │ └────────────────────────────────────────┘ │
//   │ [Boot] [Pause]  [Mute]  720p ▼             │
//   │ [−] 100% [+] [Fit]  Assets…  Snap  Log     │
//   └────────────────────────────────────────────┘
// ============================================================================
#pragma once

#include <QWidget>
#include <QString>

class QLabel;
class QPushButton;
class QComboBox;
class QPlainTextEdit;
class QToolButton;

namespace ruby::emulator {

class EnginePod;
class PreviewView;

class EnginePreviewPanel : public QWidget {
    Q_OBJECT

public:
    explicit EnginePreviewPanel(QWidget* parent = nullptr);

    EnginePod* pod() const { return m_pod; }
    PreviewView* view() const { return m_view; }

    // Asset convenience used by the launcher phone button / project open.
    bool has_assets() const;
    void set_assets_dir(const QString& dir);
    QString assets_dir() const { return m_assets_dir; }

    // Phone-button behaviour: boot if idle, focus otherwise.
    void boot_or_toggle();
    void stop();
    void set_resolution(int w, int h);

    // Scene-shifter context. RubyMainWindow calls this whenever the active
    // document changes: pass the open .scene path, or an empty string when the
    // active doc is not a scene (or no doc is open). While a scene is active
    // AND an engine session is live, the panel shows a "▶ Run Scene" control
    // that emits runSceneRequested(path) when clicked.
    void set_scene_doc(const QString& scene_path);

signals:
    void statusMessage(const QString& text, int timeout_ms);
    // Emitted when the user clicks "▶ Run Scene": sceneFilePath is the open
    // .scene document. The host (RubyMainWindow) saves the FileRift buffer
    // (re-encode to binary), then sends the shift via EnginePod.
    void runSceneRequested(const QString& sceneFilePath);

private slots:
    void on_boot();
    void on_pause_toggled(bool paused);
    void on_mute_toggled(bool muted);
    void on_res_changed(int index);
    void on_snapshot();
    void on_choose_assets();
    void on_state(int state);
    void on_fps(int fps);
    void on_log(const QString& line);
    void toggle_log();
    void zoom_by(int delta);
    void on_run_scene();

private:
    QString pick_assets_dir();          // auto-detect → user choice
    void    refresh_state_ui();
    void    refresh_scene_ui();          // show/hide ▶ Run Scene control
    static QString state_label(int s);

    EnginePod*   m_pod = nullptr;
    PreviewView* m_view = nullptr;

    QLabel*       m_status = nullptr;
    QLabel*       m_fps = nullptr;
    QLabel*       m_assets_label = nullptr;
    QPushButton*  m_boot_btn = nullptr;
    QPushButton*  m_stop_btn = nullptr;
    QPushButton*  m_pause_btn = nullptr;
    QPushButton*  m_mute_btn = nullptr;
    QComboBox*    m_res_combo = nullptr;
    QLabel*       m_zoom_label = nullptr;
    QPushButton*  m_log_btn = nullptr;
    QPlainTextEdit* m_log = nullptr;

    // Scene-shifter context UI (hidden until a scene doc is open and the pod
    // session is live).
    QWidget*      m_scene_bar = nullptr;
    QPushButton*  m_run_scene_btn = nullptr;
    QLabel*       m_scene_name = nullptr;
    QString       m_scene_doc_path;

    QString m_assets_dir;
    bool    m_muted = false;
    int     m_last_state = 0;
};

} // namespace ruby::emulator
