// ============================================================================
// engine_preview_panel.cpp — see engine_preview_panel.h
// ============================================================================
#include "ruby/emulator/engine_preview_panel.h"
#include "ruby/emulator/engine_pod.h"
#include "ruby/emulator/preview_view.h"
#include "ruby/emulator/workspace_detect.h"
#include "ruby/core/project_context.h"
#include "ruby/theme/ruby_theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QSettings>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QMenu>

namespace ruby::emulator {

namespace {
constexpr int kResPresets[][2] = {
    { 960, 540 }, { 1280, 720 }, { 1600, 900 }, { 1920, 1080 },
};
const char* kResNames[] = { "960×540", "1280×720 (720p)", "1600×900", "1920×1080 (1080p)" };

QString state_dot_color(int s) {
    switch (s) {
        case EnginePod::kRunning: return "#98c379";
        case EnginePod::kPaused:  return "#e5c07b";
        case EnginePod::kBooting: return "#61afef";
        default:                  return "#be5059";
    }
}
} // namespace

EnginePreviewPanel::EnginePreviewPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("enginePreviewPanel");
    setStyleSheet(QString("QWidget#enginePreviewPanel { background:#121316; }"));
    // The engine renders 16:9 landscape. RubyMainWindow::fit_engine_preview_dock()
    // gives this dock a landscape ~16:9 area when a session runs; the small
    // minimum here keeps other panels alive on narrow windows.
    setMinimumSize(340, 220);

    m_pod = new EnginePod(this);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // ── Header: status dot + label + fps ─────────────────────────────────
    auto* head = new QWidget(this);
    auto* hlay = new QHBoxLayout(head);
    hlay->setContentsMargins(2, 0, 2, 0);
    hlay->setSpacing(6);
    m_status = new QLabel("● Stopped", head);
    m_status->setStyleSheet("QLabel { color:#9aa2b1; font-size:12px; font-weight:600; }");
    m_fps = new QLabel("", head);
    m_fps->setStyleSheet("QLabel { color:#5c6370; font-size:11px; }");
    m_assets_label = new QLabel("no assets", head);
    m_assets_label->setStyleSheet("QLabel { color:#5c6370; font-size:10px; }");
    m_assets_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    hlay->addWidget(m_status);
    hlay->addWidget(m_fps);
    hlay->addStretch();
    hlay->addWidget(m_assets_label, 1, Qt::AlignRight);
    root->addWidget(head);

    // ── Scene-shifter row (▶ Run Scene) ─────────────────────────────────
    // Visible only while a scene document is open in Ruby AND an engine
    // session is live. Clicking saves the FileRift edits (re-encoded to the
    // binary .scene) and teleports the running engine into that scene via the
    // SRE Scene Shifter (same infra the SwordfareGUI scene panel uses).
    m_scene_bar = new QWidget(this);
    m_scene_bar->setObjectName("engineSceneBar");
    m_scene_bar->setStyleSheet(
        "QWidget#engineSceneBar { background:#17212b; border:1px solid #2a3b4c;"
        " border-radius:4px; }");
    auto* slay = new QHBoxLayout(m_scene_bar);
    slay->setContentsMargins(6, 3, 6, 3);
    slay->setSpacing(6);
    m_run_scene_btn = new QPushButton("▶ Run Scene", m_scene_bar);
    m_run_scene_btn->setCursor(Qt::PointingHandCursor);
    m_run_scene_btn->setToolTip(
        "Save this scene (FileRift re-encode to binary) and teleport the "
        "running engine into it via the Scene Shifter");
    m_run_scene_btn->setStyleSheet(
        "QPushButton { background:#3a9d5d; color:#0b1510; font-weight:700;"
        " border:none; border-radius:3px; padding:3px 10px; font-size:12px; }"
        "QPushButton:hover { background:#46bb6e; }"
        "QPushButton:pressed { background:#2e854c; }");
    m_scene_name = new QLabel(m_scene_bar);
    m_scene_name->setStyleSheet("QLabel { color:#7fdb9a; font-size:11px; }");
    m_scene_name->setTextInteractionFlags(Qt::TextSelectableByMouse);
    slay->addWidget(m_run_scene_btn);
    slay->addWidget(m_scene_name, 1);
    m_scene_bar->setVisible(false);
    root->addWidget(m_scene_bar);

    // ── The viewport ─────────────────────────────────────────────────────
    m_view = new PreviewView(this);
    m_view->bind(m_pod);
    root->addWidget(m_view, 1);

    // ── Transport row ────────────────────────────────────────────────────
    auto* transport = new QWidget(this);
    auto* tlay = new QHBoxLayout(transport);
    tlay->setContentsMargins(0, 0, 0, 0);
    tlay->setSpacing(4);

    m_boot_btn = new QPushButton("▶ Boot", transport);
    m_boot_btn->setCursor(Qt::PointingHandCursor);
    m_boot_btn->setToolTip("Boot Swordigo 1.4.13 (engine + libsre13) with the detected assets");
    m_stop_btn = new QPushButton("■", transport);
    m_stop_btn->setCursor(Qt::PointingHandCursor);
    m_stop_btn->setToolTip("Stop the engine pod");
    m_pause_btn = new QPushButton("⏸", transport);
    m_pause_btn->setCheckable(true);
    m_pause_btn->setCursor(Qt::PointingHandCursor);
    m_pause_btn->setToolTip("Freeze / resume the emulator");
    m_mute_btn = new QPushButton("🔊", transport);
    m_mute_btn->setCheckable(true);
    m_mute_btn->setCursor(Qt::PointingHandCursor);
    m_mute_btn->setToolTip("Mute / unmute game audio");

    m_res_combo = new QComboBox(transport);
    for (int i = 0; i < 4; ++i) m_res_combo->addItem(kResNames[i]);
    m_res_combo->setCurrentIndex(1);   // 720p default
    m_res_combo->setToolTip("Preview resolution (applies on next boot)");

    tlay->addWidget(m_boot_btn);
    tlay->addWidget(m_stop_btn);
    tlay->addWidget(m_pause_btn);
    tlay->addWidget(m_mute_btn);
    tlay->addStretch();
    tlay->addWidget(m_res_combo);
    root->addWidget(transport);

    // ── View / assets row ────────────────────────────────────────────────
    auto* viewbar = new QWidget(this);
    auto* vlay = new QHBoxLayout(viewbar);
    vlay->setContentsMargins(0, 0, 0, 0);
    vlay->setSpacing(4);

    auto make_btn = [this, viewbar](const QString& t, const QString& tip) {
        auto* b = new QPushButton(t, viewbar);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        b->setFixedHeight(24);
        return b;
    };
    QPushButton* zoom_out = make_btn("−", "Zoom out");
    m_zoom_label = new QLabel("100%", viewbar);
    m_zoom_label->setStyleSheet("QLabel { color:#9aa2b1; font-size:11px; }");
    m_zoom_label->setMinimumWidth(38);
    m_zoom_label->setAlignment(Qt::AlignCenter);
    QPushButton* zoom_in = make_btn("+", "Zoom in");
    QPushButton* zoom_fit = make_btn("Fit", "Fit preview to panel");
    QPushButton* assets = make_btn("Assets…", "Choose the Swordigo asset folder (contains resources/)");
    QPushButton* snap = make_btn("📷", "Save a PNG snapshot of the preview");
    m_log_btn = make_btn("Log", "Show engine log");

    vlay->addWidget(zoom_out);
    vlay->addWidget(m_zoom_label);
    vlay->addWidget(zoom_in);
    vlay->addWidget(zoom_fit);
    vlay->addWidget(assets);
    vlay->addWidget(snap);
    vlay->addWidget(m_log_btn);
    root->addWidget(viewbar);

    // ── Collapsible log ──────────────────────────────────────────────────
    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(600);
    m_log->setStyleSheet("QPlainTextEdit { background:#0d0e11; color:#98a3b0;"
                         " font-family:monospace; font-size:10px; border:1px solid #2d313b; }");
    m_log->setVisible(false);
    m_log->setFixedHeight(150);
    root->addWidget(m_log);

    // ── Wiring ───────────────────────────────────────────────────────────
    connect(m_boot_btn, &QPushButton::clicked, this, &EnginePreviewPanel::on_boot);
    connect(m_run_scene_btn, &QPushButton::clicked, this,
            &EnginePreviewPanel::on_run_scene);
    connect(m_stop_btn, &QPushButton::clicked, this, &EnginePreviewPanel::stop);
    connect(m_pause_btn, &QPushButton::toggled, this, &EnginePreviewPanel::on_pause_toggled);
    connect(m_mute_btn, &QPushButton::toggled, this, &EnginePreviewPanel::on_mute_toggled);
    connect(m_res_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EnginePreviewPanel::on_res_changed);
    connect(zoom_out, &QPushButton::clicked, this, [this] { zoom_by(-25); });
    connect(zoom_in, &QPushButton::clicked, this, [this] { zoom_by(25); });
    connect(zoom_fit, &QPushButton::clicked, m_view, &PreviewView::reset_view);
    connect(m_view, &PreviewView::zoomChanged, this, [this](int pct) {
        m_zoom_label->setText(QString("%1%").arg(pct));
    });
    connect(assets, &QPushButton::clicked, this, &EnginePreviewPanel::on_choose_assets);
    connect(snap, &QPushButton::clicked, this, &EnginePreviewPanel::on_snapshot);
    connect(m_log_btn, &QPushButton::clicked, this, &EnginePreviewPanel::toggle_log);

    connect(m_pod, &EnginePod::stateChanged, this, &EnginePreviewPanel::on_state);
    connect(m_pod, &EnginePod::fpsChanged, this, &EnginePreviewPanel::on_fps);
    connect(m_pod, &EnginePod::logLine, this, &EnginePreviewPanel::on_log);
    connect(m_pod, &EnginePod::bootFailed, this, [this](const QString& why) {
        m_status->setText("● Failed");
        on_log("boot failed: " + why);
    });

    // Restore persisted choices.
    QSettings s;
    m_assets_dir = s.value("ruby_gg/enginePod/assetsDir").toString();
    int res_idx = s.value("ruby_gg/enginePod/resIndex", 1).toInt();
    m_res_combo->setCurrentIndex(qBound(0, res_idx, 3));
    if (!m_assets_dir.isEmpty())
        m_assets_label->setText(QFileInfo(m_assets_dir).fileName());
    refresh_state_ui();
    refresh_scene_ui();
}

// ---------------------------------------------------------------------------
// Asset flow
// ---------------------------------------------------------------------------
bool EnginePreviewPanel::has_assets() const {
    return !m_assets_dir.isEmpty() &&
           QFileInfo::exists(m_assets_dir + "/resources");
}

void EnginePreviewPanel::set_assets_dir(const QString& dir) {
    if (dir.isEmpty()) return;
    m_assets_dir = dir;
    QSettings().setValue("ruby_gg/enginePod/assetsDir", dir);
    m_assets_label->setText(QFileInfo(dir).fileName());
    m_assets_label->setToolTip(dir);
}

QString EnginePreviewPanel::pick_assets_dir() {
    // 1) Existing selection still valid.
    if (has_assets()) return m_assets_dir;

    // 2) Auto-detect from likely roots.
    std::vector<std::string> roots;
    // Dist deployment: run_ruby_gg.sh sets SWORDIGO_DATA_DIR to the bundled
    // data/ tree (assets*, engine/, …). Scan it BEFORE user paths so
    // distributed builds find bootable assets on a fresh machine.
    if (const char* sd = getenv("SWORDIGO_DATA_DIR"); sd && sd[0])
        roots.push_back(sd);
    const auto& proj = ruby::core::ProjectContext::instance().project_dir();
    if (!proj.empty()) roots.push_back(proj);
    roots.push_back(m_assets_dir.toStdString());           // may be a stale path
    QString home = QDir::homePath();
    roots.push_back(home.toStdString());
    roots.push_back(QDir::current().absolutePath().toStdString());

    auto cands = detect_from_roots(roots);
    if (!cands.empty()) {
        // Prefer highest marker score.
        AssetCandidate best = cands.front();
        if (cands.size() > 1) {
            QStringList names;
            for (auto& c : cands) names << QString("%1  (%2)").arg(
                                            QString::fromStdString(c.name),
                                            QString::fromStdString(c.instance_dir));
            bool ok = false;
            QString chosen = QInputDialog::getItem(
                this, "Pick Swordigo assets", "Multiple asset folders found:",
                names, 0, false, &ok);
            if (ok) {
                int idx = names.indexOf(chosen);
                if (idx >= 0 && idx < (int)cands.size()) best = cands[idx];
            }
        }
        return QString::fromStdString(best.instance_dir);
    }

    // 3) Manual folder pick (parent folder containing resources/).
    QString start = has_assets() ? m_assets_dir : home;
    QString dir = QFileDialog::getExistingDirectory(this, "Pick Swordigo assets folder (contains resources/)", start);
    if (dir.isEmpty()) return QString();
    // The user may have picked a parent (workspace) — resolve the instance dir.
    auto here = detect_asset_dirs(dir.toStdString());
    if (!here.empty()) return QString::fromStdString(here.front().instance_dir);
    if (looks_like_instance_dir(dir.toStdString())) return dir;
    QMessageBox::warning(this, "Not an asset folder",
                         QString("'%1' does not contain a bootable resources/ tree.")
                             .arg(dir));
    return QString();
}

void EnginePreviewPanel::on_choose_assets() {
    QString dir = pick_assets_dir();
    if (!dir.isEmpty()) {
        set_assets_dir(dir);
        emit statusMessage(QString("Engine assets: %1").arg(dir), 3000);
    }
}

// ---------------------------------------------------------------------------
// Scene-shifter context ("Run scene" from Ruby)
// ---------------------------------------------------------------------------
void EnginePreviewPanel::set_scene_doc(const QString& scene_path) {
    m_scene_doc_path = scene_path;
    if (m_scene_name) {
        if (scene_path.isEmpty()) {
            m_scene_name->clear();
            m_scene_name->setToolTip(QString());
        } else {
            m_scene_name->setText(QFileInfo(scene_path).fileName());
            m_scene_name->setToolTip(scene_path);
        }
    }
    refresh_scene_ui();
}

void EnginePreviewPanel::refresh_scene_ui() {
    if (!m_scene_bar) return;
    // ▶ Run Scene is offered only when a scene document is active in Ruby AND
    // the engine pod session is live (running or paused).
    const bool session_live = (m_last_state == EnginePod::kRunning ||
                               m_last_state == EnginePod::kPaused);
    const bool show = session_live && !m_scene_doc_path.isEmpty();
    m_scene_bar->setVisible(show);
    if (m_run_scene_btn) {
        m_run_scene_btn->setEnabled(session_live && !m_scene_doc_path.isEmpty());
    }
}

void EnginePreviewPanel::on_run_scene() {
    if (m_scene_doc_path.isEmpty()) return;
    if (!m_pod || !m_pod->is_alive()) return;
    if (m_last_state == EnginePod::kPaused) {
        // The Scene Shifter only runs on a live frame loop — resume first so
        // the pending shift actually dispatches.
        m_pod->send_pause(false);
    }
    if (m_last_state == EnginePod::kRunning || m_last_state == EnginePod::kPaused) {
        emit runSceneRequested(m_scene_doc_path);
    }
}

// ---------------------------------------------------------------------------
// Session controls
// ---------------------------------------------------------------------------
void EnginePreviewPanel::on_boot() {
    if (m_pod->is_alive()) {
        m_view->setFocus();
        return;
    }
    QString dir = pick_assets_dir();
    if (dir.isEmpty()) return;
    set_assets_dir(dir);

    int idx = m_res_combo->currentIndex();
    int w = kResPresets[idx][0], h = kResPresets[idx][1];
    QString lib = EnginePod::default_engine_lib_path();
    QString bin = EnginePod::find_swordfare_binary();
    if (!QFileInfo::exists(bin)) {
        QMessageBox::critical(this, "Engine pod",
            "swordfare binary not found next to ruby_gg. Build it first:\n"
            "  cmake --build build-cmake --target swordfare_boot\n\n"
            "Looked at: " + bin);
        return;
    }
    m_log->clear();
    m_pod->start(dir, w, h, lib, bin);
    m_view->setFocus();
}

void EnginePreviewPanel::stop() {
    m_pod->stop();
    refresh_state_ui();
}

void EnginePreviewPanel::set_resolution(int w, int h) {
    for (int i = 0; i < 4; ++i) {
        if (kResPresets[i][0] == w && kResPresets[i][1] == h) {
            m_res_combo->setCurrentIndex(i);
            return;
        }
    }
}

void EnginePreviewPanel::on_pause_toggled(bool paused) {
    if (m_pod->is_alive()) m_pod->send_pause(paused);
    refresh_state_ui();
}

void EnginePreviewPanel::on_mute_toggled(bool muted) {
    m_muted = muted;
    m_mute_btn->setText(muted ? "🔇" : "🔊");
    if (m_pod->is_alive()) m_pod->send_mute_toggle();
}

void EnginePreviewPanel::on_res_changed(int index) {
    QSettings().setValue("ruby_gg/enginePod/resIndex", index);
    if (m_pod->is_alive()) {
        // Takes effect on the next boot; tell the user.
        m_status->setText(QString("● Restart to apply %1").arg(kResNames[index]));
    }
}

void EnginePreviewPanel::on_snapshot() {
    if (m_view->isVisible() && !m_pod->frame().isNull()) {
        QString file = QFileDialog::getSaveFileName(
            this, "Save preview snapshot",
            QDir::homePath() + "/swordigo_preview_" +
                QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".png",
            "PNG (*.png)");
        if (!file.isEmpty()) m_pod->frame().save(file);
    }
}

// ---------------------------------------------------------------------------
// State / log updates
// ---------------------------------------------------------------------------
QString EnginePreviewPanel::state_label(int s) {
    switch (s) {
        case EnginePod::kBooting: return "Booting engine…";
        case EnginePod::kRunning: return "Running";
        case EnginePod::kPaused:  return "Paused";
        case EnginePod::kExited:  return "Stopped";
        case EnginePod::kCrashed: return "Crashed";
        default:                  return "Stopped";
    }
}

void EnginePreviewPanel::on_state(int s) {
    m_last_state = s;
    m_pause_btn->setEnabled(s == EnginePod::kRunning || s == EnginePod::kPaused);
    m_pause_btn->setChecked(s == EnginePod::kPaused);
    m_stop_btn->setEnabled(s != EnginePod::kStopped && s != EnginePod::kExited);
    refresh_state_ui();
    refresh_scene_ui();
    if (s == EnginePod::kRunning && m_mute_btn->isChecked()) {
        // Re-apply mute after boot.
        m_pod->send_mute_toggle();
    }
    if (s == EnginePod::kCrashed) {
        on_log("[EnginePod] session crashed — check the log above.");
    }
}

void EnginePreviewPanel::on_fps(int fps) {
    m_fps->setText(fps > 0 ? QString("· %1 fps").arg(fps) : QString());
}

void EnginePreviewPanel::on_log(const QString& line) {
    m_log->appendPlainText(line);
}

void EnginePreviewPanel::toggle_log() {
    m_log->setVisible(!m_log->isVisible());
    m_log_btn->setText(m_log->isVisible() ? "Log ▲" : "Log");
}

void EnginePreviewPanel::zoom_by(int delta) {
    m_view->set_zoom_percent(m_view->zoom_percent() + delta);
}

void EnginePreviewPanel::refresh_state_ui() {
    int s = m_last_state;
    m_status->setText(QString("● %1").arg(state_label(s)));
    m_status->setStyleSheet(QString("QLabel { color:%1; font-size:12px; font-weight:600; }")
                                .arg(state_dot_color(s)));
}

// ---------------------------------------------------------------------------
// Public "phone button" behaviour
// ---------------------------------------------------------------------------
void EnginePreviewPanel::boot_or_toggle() {
    if (m_pod->is_alive()) {
        m_view->setFocus();
        return;
    }
    on_boot();
}

} // namespace ruby::emulator
