// ============================================================================
// engine_pod.h — Ruby GG engine-pod client (1.4.13 mini emulator)
//
// EnginePod owns one child `swordfare --pod-preview` process and displays its
// shared-memory frames as a QImage. It is the Qt-side counterpart of
// platform/pod_ipc.{h,cpp}:
//   * spawns the child with the 1.4.13 ARM64 engine + libsre13 + a detected
//     assets directory
//   * pulls the newest triple-buffered RGBA frame on a timer and emits it
//   * sends pause / mute / quit / keyboard / mouse / text control messages
//     over the child's stdin
//   * reports session state (booting / running / paused / stopped / crashed)
// ============================================================================
#pragma once

#include <QObject>
#include <QProcess>
#include <QImage>
#include <QTimer>
#include <QString>
#include <QStringList>

#include <memory>
#include <string>

namespace pod { class FrameRing; struct PodMsg; }

namespace ruby::emulator {

class EnginePod : public QObject {
    Q_OBJECT

public:
    // Mirrors pod::PodState plus local terminal states.
    enum SessionState {
        kStopped = 0,
        kBooting = 1,
        kRunning = 2,
        kPaused  = 3,
        kExited  = 4,
        kCrashed = 5,
    };
    Q_ENUM(SessionState)

    explicit EnginePod(QObject* parent = nullptr);
    ~EnginePod() override;

    SessionState state() const { return m_state; }
    bool is_alive() const { return m_proc && m_proc->state() != QProcess::NotRunning; }
    const QImage& frame() const { return m_frame; }
    // Monotonic frame sequence of the newest grabbed frame (0 = none yet).
    quint64 frame_seq() const { return m_last_seq; }
    QString assets_dir() const { return m_assets; }
    int  requested_width() const { return m_req_w; }
    int  requested_height() const { return m_req_h; }
    QString last_error() const { return m_last_error; }
    // Raijin Lua console TCP port (0 = not available). The pod auto-starts its
    // TCP Lua console on this port (SWORDFARE_TCP_PORT); Ruby's console panel
    // connects to it to expose the game's Lua console.
    quint16 lua_console_port() const { return m_lua_port; }

    // Recent child stdout/stderr lines (used to attach the real failure reason
    // to bootFailed).
    QString log_tail(int max_lines = 30) const;
    void append_log_line(const QString& line);

    // Boot the 1.4.13 engine at `asset_instance_dir` (folder containing
    // resources/) at the given preview resolution (logical request).
    bool start(const QString& asset_instance_dir, int res_w, int res_h,
               const QString& engine_lib_path, const QString& swordfare_bin);

    void stop();            // polite quit message, then kill after timeout
    void kill();            // immediate

    // ── Control helpers (ruby → pod) ─────────────────────────────────────
    void send_pause(bool paused);
    void send_mute_toggle();
    void send_quit();
    void send_key(unsigned sdl_keycode, unsigned sdl_scancode, unsigned mods,
                  bool down, bool repeat);
    void send_text(const QString& text);
    // x,y are normalized 0..1 over the full engine frame (top-left origin).
    void send_mouse_button(float nx, float ny, int button, bool down);
    void send_mouse_move(float nx, float ny);
    void send_wheel(int steps);
    void send_touch(int action, int id, float nx, float ny, float dx, float dy);
    void send(const pod::PodMsg& msg);

    // Ruby "▶ Run scene" — dispatch through the engine's Scene Shifter.
    // target is the scene stem (no .scene extension), e.g. "town_part1".
    void send_scene_shift(const QString& target, const QString& spawn = QStringLiteral("start"),
                          int mode = 1);

    // Resolve engine/library paths with sane fallbacks.
    static QString default_engine_lib_path();
    static QString find_swordfare_binary();

signals:
    void frameAvailable(quint64 seq);      // new frame QImage ready (GUI thread)
    void stateChanged(int state);          // SessionState
    void fpsChanged(int fps);
    void logLine(const QString& line);     // stdout/stderr from the child
    void bootFailed(const QString& reason);

private slots:
    void poll();                           // frame pull + liveness watchdog
    void on_stdout();
    void on_stderr();
    void on_finished(int code, QProcess::ExitStatus status, quint64 session_gen);
    void on_error(QProcess::ProcessError error);

private:
    void set_state(SessionState s);
    void try_open_ring();
    void close_ring();
    QString m_assets;
    QString m_lib_path;
    QString m_bin_path;
    QString m_shm_name;
    QString m_last_error;

    QProcess* m_proc = nullptr;
    QTimer*   m_poll_timer = nullptr;
    pod::FrameRing* m_ring = nullptr;
    SessionState m_state = kStopped;
    int m_req_w = 1280;
    int m_req_h = 720;
    quint16 m_lua_port = 0;
    QImage m_frame;
    uint64_t m_last_seq = 0;
    uint64_t m_attempts = 0;
    QByteArray m_stdout_acc;
    QByteArray m_stderr_acc;
    // Ring buffer of the child's recent stdout/stderr lines — attached to
    // bootFailed so a crash/failure shows WHY instead of a bare "did not
    // create its frame ring" (e.g. SDL Wayland init errors on other
    // compositors than the dev machine).
    QStringList m_recent_log;
    bool m_ring_timeout_fired = false;
    bool m_user_stopped = false;   // stop() initiated the child exit
    // Monotonic session id: each start() bumps it and re-connects the
    // finished handler with the new id, so a stale child-exit signal from a
    // previous session can never be misattributed to the current one.
    quint64 m_session_gen = 0;
    int m_fps_emit_counter = 0;
};

} // namespace ruby::emulator
