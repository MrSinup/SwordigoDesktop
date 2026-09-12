// ============================================================================
// engine_pod.cpp — see engine_pod.h
// ============================================================================
#include "ruby/emulator/engine_pod.h"

#include "platform/pod_ipc.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QDateTime>
#include <QStandardPaths>
#include <QTcpServer>
#include <QHostAddress>
#include <QProcessEnvironment>

#include <cstring>
#include <algorithm>

namespace ruby::emulator {

namespace {
QString home_dir() {
    return QDir::homePath();
}

// ~/.local/share/swordigo-desktop (Linux) — same data root as swordfare.
QString swordigo_data_dir() {
    QString base;
    const char* xdg = getenv("XDG_DATA_HOME");
    if (xdg && xdg[0]) {
        base = QString::fromLocal8Bit(xdg);
    } else {
        base = home_dir() + "/.local/share";
    }
    return base + "/swordigo-desktop";
}
} // namespace

EnginePod::EnginePod(QObject* parent) : QObject(parent) {
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &EnginePod::on_stdout);
    connect(m_proc, &QProcess::readyReadStandardError, this, &EnginePod::on_stderr);
    // finished is (re)connected per session in start() so a queued exit
    // signal from a previous session can be identified and ignored.
    connect(m_proc, &QProcess::errorOccurred, this, &EnginePod::on_error);

    m_poll_timer = new QTimer(this);
    connect(m_poll_timer, &QTimer::timeout, this, &EnginePod::poll);
    m_poll_timer->setInterval(16);   // ~60 Hz UI-side poll
}

EnginePod::~EnginePod() {
    stop();
}

QString EnginePod::default_engine_lib_path() {
    // 1.4.13 ARM64 engine always for the inline mini emulator.
    QString rel = "engine/v1.4.13/arm64-v8a/libswordigo.so";
    // Dist deployment: run_ruby_gg.sh sets SWORDIGO_DATA_DIR to the bundled
    // data/ tree. Honor it FIRST so distributed builds boot without requiring
    // a ~/.local/share/swordigo-desktop user-data install (the most common
    // reason "Boot always fails" on a fresh KDE/GNOME machine).
    const char* env = getenv("SWORDIGO_DATA_DIR");
    if (env && env[0]) {
        QString from_env = QString::fromLocal8Bit(env) + "/" + rel;
        if (QFileInfo::exists(from_env)) return from_env;
    }
    QString data = swordigo_data_dir();
    if (QFileInfo::exists(data + "/" + rel)) return data + "/" + rel;
    // Dev fallback: repo cwd contains engine/.
    QString dev = QDir::current().absoluteFilePath(rel);
    if (QFileInfo::exists(dev)) return dev;
    // ~/.local/share/swordigo-desktop/assets13 instance style fallback:
    // engine binaries under the launcher's registry are keyed there too.
    // If SWORDIGO_DATA_DIR points at a dist data/ tree, keep that path even
    // when the file is missing so the error message shows the real location.
    if (env && env[0]) return QString::fromLocal8Bit(env) + "/" + rel;
    return data + "/" + rel;
}

QString EnginePod::find_swordfare_binary() {
    QStringList candidates;
    const QString app_dir = QCoreApplication::applicationDirPath();
    candidates << app_dir + "/swordfare"
               << app_dir + "/swordfare.exe";
    // When ruby_gg runs from a build dir instead of bin/:
    candidates << app_dir + "/../bin/swordfare"
               << QDir::current().absoluteFilePath("bin/swordfare");
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c)) return QDir(c).absolutePath();
    }
    return QString();
}

void EnginePod::set_state(SessionState s) {
    if (m_state == s) return;
    m_state = s;
    emit stateChanged((int)s);
}

bool EnginePod::start(const QString& asset_instance_dir, int res_w, int res_h,
                      const QString& engine_lib_path, const QString& swordfare_bin) {
    stop();
    m_assets = asset_instance_dir;
    m_lib_path = engine_lib_path;
    m_bin_path = swordfare_bin;
    m_req_w = res_w;
    m_req_h = res_h;
    m_last_error.clear();
    m_ring_timeout_fired = false;
    m_user_stopped = false;
    m_recent_log.clear();
    close_ring();

    if (m_bin_path.isEmpty() || !QFileInfo::exists(m_bin_path)) {
        m_last_error = QString("swordfare binary not found (looked for '%1')")
                           .arg(m_bin_path.isEmpty() ? find_swordfare_binary()
                                                     : m_bin_path);
        emit bootFailed(m_last_error);
        set_state(kCrashed);
        return false;
    }
    if (m_lib_path.isEmpty() || !QFileInfo::exists(m_lib_path)) {
        m_last_error = QString("engine lib not found: %1").arg(m_lib_path);
        emit bootFailed(m_last_error);
        set_state(kCrashed);
        return false;
    }
    if (!QFileInfo::exists(asset_instance_dir + "/resources")) {
        m_last_error = QString("assets dir missing resources/: %1")
                           .arg(asset_instance_dir);
        emit bootFailed(m_last_error);
        set_state(kCrashed);
        return false;
    }

    // Unique shm name per session (child sanitizes further).
    m_shm_name = QString("pod%1_%2")
                     .arg(QCoreApplication::applicationPid())
                     .arg(QDateTime::currentMSecsSinceEpoch() % 1000000);

    QStringList args;
    args << "--no-launcher"
         << "--pod-preview"
         << QString("%1:%2x%3").arg(m_shm_name).arg(res_w).arg(res_h)
         << "--lib" << m_lib_path
         << "--assets" << asset_instance_dir
         << "--engine=dynarmic"
         << "--sre";

    // Reserve a TCP port for the pod's Raijin Lua console (SWORDFARE_TCP_PORT).
    // The pod auto-starts its TCP Lua server on that port during SRE init; Ruby
    // connects to it so the bottom console can talk to the game's Lua.
    m_lua_port = 0;
    {
        QTcpServer probe;
        if (probe.listen(QHostAddress::LocalHost, 0)) {
            m_lua_port = probe.serverPort();
        }
    }
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (m_lua_port > 0) {
        env.insert(QStringLiteral("SWORDFARE_TCP_PORT"),
                   QString::number(m_lua_port));
    }

    m_stdout_acc.clear();
    m_stderr_acc.clear();
    m_last_seq = 0;
    m_attempts = 0;

    // Re-arm the finished handler for THIS session (see m_session_gen).
    const quint64 gen = ++m_session_gen;
    disconnect(m_proc, &QProcess::finished, this, nullptr);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, gen](int code, QProcess::ExitStatus status) {
                on_finished(code, status, gen);
            });

    m_proc->setWorkingDirectory(QFileInfo(m_bin_path).absolutePath());
    m_proc->setProcessEnvironment(env);
    m_proc->start(m_bin_path, args);
    if (!m_proc->waitForStarted(5000)) {
        m_last_error = QString("failed to start swordfare: %1").arg(m_proc->errorString());
        emit bootFailed(m_last_error);
        set_state(kCrashed);
        return false;
    }

    pod::shm_unlink_name(m_shm_name.toStdString());   // remove stale leftovers
    set_state(kBooting);
    m_poll_timer->start();
    emit logLine(QString("[EnginePod] started %1 (%2x%3) assets=%4")
                     .arg(m_bin_path).arg(res_w).arg(res_h)
                     .arg(asset_instance_dir));
    return true;
}

void EnginePod::stop() {
    m_user_stopped = true;
    if (is_alive()) {
        send_quit();
        if (!m_proc->waitForFinished(2500)) {
            kill();
            m_proc->waitForFinished(1000);
        }
    }
    close_ring();
    pod::shm_unlink_name(m_shm_name.toStdString());
    m_poll_timer->stop();
    m_lua_port = 0;
    if (m_state != kCrashed) set_state(kStopped);
}

void EnginePod::kill() {
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
    }
}

// ── control messages ────────────────────────────────────────────────────────
void EnginePod::send(const pod::PodMsg& msg) {
    if (!m_proc || m_proc->state() == QProcess::NotRunning) return;
    std::string bytes;
    pod::MsgStream stream(&bytes);
    stream.push(msg);
    m_proc->write(bytes.data(), (qint64)bytes.size());
}

void EnginePod::send_pause(bool paused) {
    pod::PodMsg m;
    m.kind = pod::kMsgPause;
    m.a = paused ? 1 : 0;
    send(m);
}

void EnginePod::send_mute_toggle() {
    pod::PodMsg m;
    m.kind = pod::kMsgMute;
    send(m);
}

void EnginePod::send_quit() {
    pod::PodMsg m;
    m.kind = pod::kMsgQuit;
    send(m);
}

void EnginePod::send_key(unsigned sdl_keycode, unsigned sdl_scancode, unsigned mods,
                         bool down, bool repeat) {
    pod::PodMsg m;
    m.kind = pod::kMsgKey;
    m.a = down ? 1 : 0;
    m.key = sdl_keycode;
    m.scancode = sdl_scancode;
    m.mods = mods;
    m.x = repeat ? 1 : 0;
    send(m);
}

void EnginePod::send_text(const QString& text) {
    pod::PodMsg m;
    m.kind = pod::kMsgText;
    QByteArray utf8 = text.toUtf8();
    std::strncpy(m.text, utf8.constData(), sizeof(m.text) - 1);
    send(m);
}

void EnginePod::send_mouse_button(float nx, float ny, int button, bool down) {
    pod::PodMsg m;
    m.kind = pod::kMsgMouseBtn;
    m.a = (uint32_t)button;
    m.mods = down ? 1u : 0u;
    m.f[0] = nx;
    m.f[1] = ny;
    send(m);
}

void EnginePod::send_mouse_move(float nx, float ny) {
    pod::PodMsg m;
    m.kind = pod::kMsgMouseMove;
    m.f[0] = nx;
    m.f[1] = ny;
    send(m);
}

void EnginePod::send_wheel(int steps) {
    pod::PodMsg m;
    m.kind = pod::kMsgWheel;
    m.a = (uint32_t)steps;
    send(m);
}

void EnginePod::send_touch(int action, int id, float nx, float ny, float dx, float dy) {
    pod::PodMsg m;
    m.kind = pod::kMsgTouch;
    m.a = (uint32_t)action;
    m.mods = (uint32_t)id;
    m.f[0] = nx;
    m.f[1] = ny;
    m.f[2] = dx;
    m.f[3] = dy;
    send(m);
}

void EnginePod::send_scene_shift(const QString& target, const QString& spawn, int mode) {
    if (target.isEmpty()) return;
    pod::PodMsg m;
    m.kind = pod::kMsgShiftScene;
    m.a = mode == 2 ? 2u : 1u;
    QByteArray t = target.toUtf8();
    QByteArray s = spawn.isEmpty() ? QByteArray("start") : spawn.toUtf8();
    std::strncpy(m.target, t.constData(), sizeof(m.target) - 1);
    std::strncpy(m.spawn, s.constData(), sizeof(m.spawn) - 1);
    send(m);
}

// ── stdout / stderr → log ───────────────────────────────────────────────────
void EnginePod::append_log_line(const QString& line) {
    m_recent_log.append(line);
    while (m_recent_log.size() > 120) m_recent_log.removeFirst();
}

QString EnginePod::log_tail(int max_lines) const {
    if (m_recent_log.isEmpty()) return QString();
    const int start = std::max(0, int(m_recent_log.size()) - max_lines);
    return m_recent_log.mid(start).join('\n');
}

static void flush_lines(QByteArray& acc, const QByteArray& chunk,
                        EnginePod* pod) {
    acc += chunk;
    int nl;
    while ((nl = acc.indexOf('\n')) >= 0) {
        QString line = QString::fromUtf8(acc.left(nl));
        acc.remove(0, nl + 1);
        if (!line.trimmed().isEmpty()) {
            pod->logLine(line);
            pod->append_log_line(line);
        }
        if (acc.size() > 65536) acc.clear();
    }
}

void EnginePod::on_stdout() {
    flush_lines(m_stdout_acc, m_proc->readAllStandardOutput(), this);
}

void EnginePod::on_stderr() {
    flush_lines(m_stderr_acc, m_proc->readAllStandardError(), this);
}

void EnginePod::try_open_ring() {
    if (m_ring || m_shm_name.isEmpty()) return;
    m_ring = pod::FrameRing::open(m_shm_name.toStdString());
    if (m_ring) {
        pod::FrameLayout fl = m_ring->layout();
        if (fl.width > 0 && fl.height > 0 && fl.width <= 4096 && fl.height <= 4096) {
            m_frame = QImage(fl.width, fl.height, QImage::Format_RGBA8888);
        } else {
            close_ring();   // header not fully initialized yet
        }
    }
}

void EnginePod::poll() {
    // Child gone: stop polling and let on_finished() classify the exit. It
    // must do that classification while m_state is still kBooting/kRunning so
    // a boot-time crash surfaces bootFailed with the child's log tail (racing
    // this with a premature kExited here would swallow that error).
    if (!m_proc || m_proc->state() == QProcess::NotRunning) {
        m_poll_timer->stop();
        return;
    }

    try_open_ring();
    if (!m_ring) {
        // ~10 s at 60 Hz polling with a 16 ms timer. Fire ONCE and surface the
        // child's own log tail so a platform failure (e.g. SDL video init on a
        // KDE Wayland setup) shows the real error instead of a bare timeout.
        if (!m_ring_timeout_fired && ++m_attempts > 10 * 60) {
            m_ring_timeout_fired = true;
            m_last_error = "engine pod did not create its frame ring";
            const QString tail = log_tail();
            if (!tail.isEmpty()) m_last_error += "\n--- child log ---\n" + tail;
            m_poll_timer->stop();
            set_state(kCrashed);
            emit bootFailed(m_last_error);
        }
        return;
    }

    if (m_state == kBooting && m_ring) {
        uint32_t s = m_ring->state();
        if (s == pod::kStateRunning || s == pod::kStatePaused) {
            set_state(s == pod::kStateRunning ? kRunning : kPaused);
        }
    }

    pod::FrameLayout fl = m_ring->layout();
    if (fl.width != (uint32_t)m_frame.width() ||
        fl.height != (uint32_t)m_frame.height()) {
        m_frame = QImage(fl.width, fl.height, QImage::Format_RGBA8888);
    }
    if (m_frame.isNull() || m_frame.bytesPerLine() != (int)fl.stride) return;

    uint64_t seq = m_ring->grab(m_frame.bits());
    if (seq) {
        m_last_seq = seq;
        // Reflect pod-side pause/running state from shm.
        uint32_t s = m_ring->state();
        SessionState want = (s == pod::kStatePaused) ? kPaused
                          : (s == pod::kStateRunning) ? kRunning : m_state;
        set_state(want);
        int fps = (int)m_ring->fps();
        if (++m_fps_emit_counter % 8 == 0) emit fpsChanged(fps);
        emit frameAvailable(seq);   // seq lets the viewer reject stale frames
    }
}

void EnginePod::close_ring() {
    if (m_ring) {
        pod::FrameRing* r = m_ring;
        m_ring = nullptr;
        delete r;
    }
    m_frame = QImage();
    m_last_seq = 0;
}

void EnginePod::on_finished(int code, QProcess::ExitStatus status, quint64 session_gen) {
    // Ignore exit signals from a previous session (queued finished signals
    // can arrive after a new start() has already begun).
    if (session_gen != m_session_gen) return;
    m_poll_timer->stop();
    close_ring();
    const bool was_booting = (m_state == kBooting);
    if (was_booting || m_state == kRunning || m_state == kPaused) {
        SessionState s = (status == QProcess::CrashExit || code != 0) ? kCrashed : kExited;
        set_state(s);
        emit logLine(QString("[EnginePod] child exited code=%1 status=%2")
                         .arg(code)
                         .arg(status == QProcess::NormalExit ? "normal" : "crash"));
    }
    // The engine never reached its frame loop — tell the user WHY instead of
    // leaving the dock stuck on "Booting…" / a silent "Stopped". Attach the
    // child's own log tail (SDL / ELF / JIT errors are almost always there).
    if (was_booting && !m_user_stopped) {
        const QString tail = log_tail();
        if (!tail.isEmpty()) {
            m_last_error = QString("engine pod exited during boot (code=%1)\n--- child log ---\n%2")
                               .arg(code)
                               .arg(tail);
        } else {
            m_last_error = QString("engine pod exited during boot (code=%1, no output)")
                               .arg(code);
        }
        set_state(kCrashed);
        emit bootFailed(m_last_error);
    }
}

void EnginePod::on_error(QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
        m_last_error = m_proc->errorString();
        emit bootFailed(m_last_error);
        set_state(kCrashed);
    }
}

} // namespace ruby::emulator
