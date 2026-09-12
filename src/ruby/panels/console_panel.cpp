#include "console_panel.h"

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextCursor>
#include <QVBoxLayout>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace ruby::panels {

ConsoleInput::ConsoleInput(QWidget* parent) : QLineEdit(parent) {}

void ConsoleInput::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Up) { emit history_up(); event->accept(); return; }
    if (event->key() == Qt::Key_Down) { emit history_down(); event->accept(); return; }
    QLineEdit::keyPressEvent(event);
}

// Remove ANSI colour/control escape sequences (the Raijin server colours its
// prompt). Handles standard ESC [...] and raw CSI sequences, preserving normal text.
QString ConsolePanel::strip_ansi(const QString& s) {
    // Replace standard ESC CSI sequence: \x1b\[[0-9;]*[a-zA-Z]
    static const QRegularExpression ansi_esc(QStringLiteral("\\x1b\\[[0-9;]*[a-zA-Z]"));
    // Also clean up any naked ANSI sequences like "1;36m", "0m", "1;32m", "33m"
    static const QRegularExpression raw_csi(QStringLiteral("(?:\\b|(?<=[\\]>;=\\s]))\\d+(?:;\\d+)*m"));

    QString clean = s;
    clean.remove(ansi_esc);
    clean.remove(raw_csi);
    return clean;
}

ConsolePanel::ConsolePanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    // ── Header: Title, hint badge, status indicator, and quick actions ────
    auto* header = new QHBoxLayout();
    header->setSpacing(8);

    auto* title = new QLabel("Terminal", this);
    title->setStyleSheet("font-weight: 700; font-size: 11px; color: #9aa3b2; letter-spacing: 0.5px; text-transform: uppercase;");
    header->addWidget(title);

    m_status_badge = new QLabel("OFFLINE", this);
    m_status_badge->setStyleSheet("background-color: #21252b; color: #7d8492; border-radius: 3px; padding: 1px 6px; font-size: 10px; font-weight: 700;");
    header->addWidget(m_status_badge);

    m_hint = new QLabel("bash — local environment shell", this);
    m_hint->setStyleSheet("color: #6b7280; font-size: 11px;");
    header->addWidget(m_hint, 1);

    auto* btn_clear = new QPushButton("Clear", this);
    btn_clear->setCursor(Qt::PointingHandCursor);
    btn_clear->setToolTip("Clear active terminal/log output");
    btn_clear->setStyleSheet(
        "QPushButton { background-color: #1a1d23; color: #abb2bf; border: 1px solid #2d313b; border-radius: 4px; padding: 2px 8px; font-size: 11px; }"
        "QPushButton:hover { background-color: #282c34; color: #ffffff; border-color: #4b5263; }"
        "QPushButton:pressed { background-color: #121417; }"
    );
    connect(btn_clear, &QPushButton::clicked, this, &ConsolePanel::clear_current_tab);
    header->addWidget(btn_clear);

    layout->addLayout(header);

    // ── Tab bar (VS Code / Terminal style) ─────────────────────────────────
    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    m_tabs->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #21252b; background-color: #0b0d10; border-radius: 4px; }"
        "QTabBar::tab { background: #14171d; color: #7d8492; padding: 5px 14px; margin-right: 2px; border-top-left-radius: 4px; border-top-right-radius: 4px; font-weight: 600; font-size: 11px; }"
        "QTabBar::tab:selected { background: #0b0d10; color: #e5e9f0; border-top: 2px solid #61afef; }"
        "QTabBar::tab:hover:!selected { background: #1c2027; color: #abb2bf; }"
    );

    // Common terminal font & styling
    QFont mono_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono_font.setPointSize(10);

    // ── Tab 0: Bash Terminal ──────────────────────────────────────────────
    auto* shell_page = new QWidget(this);
    auto* shell_layout = new QVBoxLayout(shell_page);
    shell_layout->setContentsMargins(4, 4, 4, 4);
    shell_layout->setSpacing(4);

    m_output = new QPlainTextEdit(shell_page);
    m_output->setReadOnly(true);
    m_output->setFont(mono_font);
    m_output->setStyleSheet("QPlainTextEdit { background-color: #0b0d10; color: #abb2bf; border: none; padding: 6px; selection-background-color: #264f78; }");
    m_output->setMaximumBlockCount(30000);
    shell_layout->addWidget(m_output, 1);

    auto* row = new QHBoxLayout();
    row->setContentsMargins(4, 2, 4, 2);
    row->setSpacing(6);
    auto* prompt = new QLabel("user@swordigo:~$", shell_page);
    prompt->setStyleSheet("color: #98c379; font-weight: 700; font-family: monospace; font-size: 11px;");
    auto* arrow = new QLabel("❯", shell_page);
    arrow->setStyleSheet("color: #61afef; font-weight: 700; font-size: 11px;");
    m_input = new ConsoleInput(shell_page);
    m_input->setFont(mono_font);
    m_input->setPlaceholderText("Enter shell command… (e.g. ls, pwd, git status)");
    m_input->setStyleSheet(
        "QLineEdit { background-color: #12151b; color: #e5e9f0; border: 1px solid #21252b; border-radius: 3px; padding: 5px 8px; }"
        "QLineEdit:focus { border: 1px solid #61afef; background-color: #161921; }"
    );
    row->addWidget(prompt);
    row->addWidget(arrow);
    row->addWidget(m_input, 1);
    shell_layout->addLayout(row);

    m_tabs->addTab(shell_page, "Bash");

    // ── Tab 1: SwLua (Game Lua Console) ──────────────────────────────────
    auto* lua_page = new QWidget(this);
    auto* lua_layout = new QVBoxLayout(lua_page);
    lua_layout->setContentsMargins(4, 4, 4, 4);
    lua_layout->setSpacing(4);

    m_lua_output = new QPlainTextEdit(lua_page);
    m_lua_output->setReadOnly(true);
    m_lua_output->setFont(mono_font);
    m_lua_output->setStyleSheet("QPlainTextEdit { background-color: #080a0d; color: #98c379; border: none; padding: 6px; selection-background-color: #22384a; }");
    m_lua_output->setMaximumBlockCount(30000);
    lua_layout->addWidget(m_lua_output, 1);

    auto* lua_row = new QHBoxLayout();
    lua_row->setContentsMargins(4, 2, 4, 2);
    lua_row->setSpacing(6);
    auto* lua_badge = new QLabel("SwLua", lua_page);
    lua_badge->setStyleSheet("background-color: #1e3a5f; color: #61afef; font-weight: 700; font-size: 10px; padding: 2px 6px; border-radius: 3px;");
    auto* lua_prompt = new QLabel("❯", lua_page);
    lua_prompt->setStyleSheet("color: #e5c07b; font-weight: 700; font-size: 12px;");
    m_lua_input = new ConsoleInput(lua_page);
    m_lua_input->setFont(mono_font);
    m_lua_input->setPlaceholderText("Live game Lua — e.g. hero = Scene.Find(\"hero\"); return hero:position()");
    m_lua_input->setStyleSheet(
        "QLineEdit { background-color: #0d1117; color: #61afef; border: 1px solid #1f2d3d; border-radius: 3px; padding: 5px 8px; }"
        "QLineEdit:focus { border: 1px solid #528bff; background-color: #111722; }"
    );
    lua_row->addWidget(lua_badge);
    lua_row->addWidget(lua_prompt);
    lua_row->addWidget(m_lua_input, 1);
    lua_layout->addLayout(lua_row);

    m_tabs->addTab(lua_page, "SwLua");

    // ── Tab 2: Ruby System Logger ─────────────────────────────────────────
    auto* log_page = new QWidget(this);
    auto* log_layout = new QVBoxLayout(log_page);
    log_layout->setContentsMargins(4, 4, 4, 4);
    log_layout->setSpacing(4);

    m_log_output = new QPlainTextEdit(log_page);
    m_log_output->setReadOnly(true);
    m_log_output->setFont(mono_font);
    m_log_output->setStyleSheet("QPlainTextEdit { background-color: #0f1115; color: #d5dbe5; border: none; padding: 6px; selection-background-color: #3e4451; }");
    m_log_output->setMaximumBlockCount(30000);
    log_layout->addWidget(m_log_output, 1);

    m_tabs->addTab(log_page, "Ruby Log");

    layout->addWidget(m_tabs, 1);

    // ── Wiring ───────────────────────────────────────────────────────────
    connect(m_tabs, &QTabWidget::currentChanged, this, &ConsolePanel::on_tab_changed);

    connect(m_input, &ConsoleInput::returnPressed, this, &ConsolePanel::submit_input);
    connect(m_input, &ConsoleInput::history_up, this, &ConsolePanel::walk_history_up);
    connect(m_input, &ConsoleInput::history_down, this, &ConsolePanel::walk_history_down);

    connect(m_lua_input, &ConsoleInput::returnPressed, this, &ConsolePanel::submit_lua);
    connect(m_lua_input, &ConsoleInput::history_up, this, &ConsolePanel::walk_lua_history_up);
    connect(m_lua_input, &ConsoleInput::history_down, this, &ConsolePanel::walk_lua_history_down);

    m_lua_sock = new QTcpSocket(this);
    connect(m_lua_sock, &QTcpSocket::readyRead, this, &ConsolePanel::on_lua_ready);
    connect(m_lua_sock, &QTcpSocket::connected, this, &ConsolePanel::on_lua_connected);
    connect(m_lua_sock, &QTcpSocket::disconnected, this, &ConsolePanel::on_lua_disconnected);
    connect(m_lua_sock, &QTcpSocket::errorOccurred, this, &ConsolePanel::on_lua_error);

    append_lua("[SwLua] Swordigo Live Lua Terminal (powered by Raijin SDK)\n"
               "Connected directly to running game engine. Type any Lua expressions or commands.",
               "#61afef");

    append_line("[Ruby Studio] Logger initialized. Engine and project messages appear here.", "#98c379");
}

void ConsolePanel::clear_current_tab() {
    int idx = m_tabs ? m_tabs->currentIndex() : 0;
    if (idx == 0 && m_output) m_output->clear();
    else if (idx == 1 && m_lua_output) m_lua_output->clear();
    else if (idx == 2 && m_log_output) m_log_output->clear();
}

ConsolePanel::~ConsolePanel() {
    if (m_lua_sock) m_lua_sock->abort();
    if (m_shell) {
        m_shell->closeWriteChannel();
        if (m_shell->state() != QProcess::NotRunning) {
            m_shell->terminate();
            if (!m_shell->waitForFinished(800)) m_shell->kill();
        }
    }
}

void ConsolePanel::append_line(const QString& text, const QString& color) {
    if (!m_log_output) return;
    if (color.isEmpty()) {
        m_log_output->appendPlainText(text);
    } else {
        m_log_output->appendHtml(QString("<span style=\"color:%1\">%2</span>")
                                     .arg(color, text.toHtmlEscaped()));
    }
    QScrollBar* bar = m_log_output->verticalScrollBar();
    if (bar) bar->setValue(bar->maximum());
}

void ConsolePanel::append_lua(const QString& text, const QString& color) {
    if (!m_lua_output) return;
    if (color.isEmpty()) {
        m_lua_output->appendPlainText(text);
    } else {
        m_lua_output->appendHtml(QString("<span style=\"color:%1\">%2</span>")
                                     .arg(color, text.toHtmlEscaped()));
    }
    QScrollBar* bar = m_lua_output->verticalScrollBar();
    if (bar) bar->setValue(bar->maximum());
}

void ConsolePanel::on_tab_changed(int index) {
    if (!m_hint) return;
    if (index == 0) {
        m_hint->setText("bash — local environment shell · builtins: clear, fps/perf, open <file>");
        if (m_status_badge) {
            m_status_badge->setText("LOCAL");
            m_status_badge->setStyleSheet("background-color: #1e3a29; color: #98c379; border-radius: 3px; padding: 1px 6px; font-size: 10px; font-weight: 700;");
        }
        m_input->setFocus();
    } else if (index == 1) {
        m_hint->setText("SwLua — live Swordigo engine Lua (Raijin SDK backend) · runs in game world");
        if (m_status_badge) {
            if (m_lua_connected) {
                m_status_badge->setText("CONNECTED");
                m_status_badge->setStyleSheet("background-color: #1e3a29; color: #98c379; border-radius: 3px; padding: 1px 6px; font-size: 10px; font-weight: 700;");
            } else {
                m_status_badge->setText("DISCONNECTED");
                m_status_badge->setStyleSheet("background-color: #3e2428; color: #e06c75; border-radius: 3px; padding: 1px 6px; font-size: 10px; font-weight: 700;");
            }
        }
        m_lua_input->setFocus();
    } else {
        m_hint->setText("Ruby System Logger — engine lifecycle, scene transitions, and diagnostics");
        if (m_status_badge) {
            m_status_badge->setText("LOGGING");
            m_status_badge->setStyleSheet("background-color: #2c313a; color: #61afef; border-radius: 3px; padding: 1px 6px; font-size: 10px; font-weight: 700;");
        }
    }
}

// ---------------------------------------------------------------------------
// Terminal (bash)
// ---------------------------------------------------------------------------
void ConsolePanel::ensure_shell() {
    if (m_shell && m_shell->state() != QProcess::NotRunning) return;
    if (!m_shell) {
        m_shell = new QProcess(this);
        m_shell->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_shell, &QProcess::readyReadStandardOutput,
                this, &ConsolePanel::handle_shell_output);
        connect(m_shell, &QProcess::finished,
                this, &ConsolePanel::handle_shell_finished);
    }
    m_shell->setWorkingDirectory(QDir::homePath());
    m_shell->start("bash", {"--norc", "--noprofile"});
}

void ConsolePanel::handle_shell_output() {
    if (!m_shell) return;
    const QString text = QString::fromUtf8(m_shell->readAllStandardOutput());
    m_output->moveCursor(QTextCursor::End);
    m_output->insertPlainText(text);
    QScrollBar* bar = m_output->verticalScrollBar();
    if (bar) bar->setValue(bar->maximum());
}

void ConsolePanel::handle_shell_finished(int, QProcess::ExitStatus) {
    if (m_running_builtin) return;
    append_line("[shell exited — next command restarts it]", "#7d8492");
}

void ConsolePanel::append_echo(const QString& command) {
    m_output->appendHtml(QString("<span style=\"color:#35d07a\">❯</span> ") + command.toHtmlEscaped());
    QScrollBar* bar = m_output->verticalScrollBar();
    if (bar) bar->setValue(bar->maximum());
}

void ConsolePanel::run_command(const QString& command) {
    const QString cmd = command.trimmed();
    if (cmd.isEmpty()) return;
    m_history.append(command);
    m_history_index = -1;
    m_input->clear();

    // ── Builtins ──────────────────────────────────────────────────────────
    if (cmd == "clear" || cmd == "cls") { m_output->clear(); return; }
    if (cmd == "help") {
        append_line("Ruby Terminal builtins:\n"
                    "  clear            clear this console\n"
                    "  perf / fps       print current memory (RAM RSS), thread count & perf status\n"
                    "  open <file>      open an asset/script as a document tab\n"
                    "  help             this text\n"
                    "Everything else runs through bash (working dir: your home).", "#9aa3b2");
        return;
    }
    if (cmd == "perf" || cmd == "fps") {
        append_echo(cmd);
        double rss_mb = 0.0, vms_mb = 0.0;
        int threads = 1;
#if defined(__linux__)
        std::ifstream statm("/proc/self/statm");
        if (statm.is_open()) {
            unsigned long size_pages = 0, resident_pages = 0;
            if (statm >> size_pages >> resident_pages) {
                long page_size = sysconf(_SC_PAGESIZE);
                vms_mb = (size_pages * page_size) / (1024.0 * 1024.0);
                rss_mb = (resident_pages * page_size) / (1024.0 * 1024.0);
            }
        }
        std::ifstream status("/proc/self/status");
        if (status.is_open()) {
            std::string line;
            while (std::getline(status, line)) {
                if (line.rfind("Threads:", 0) == 0) {
                    std::istringstream iss(line.substr(8));
                    iss >> threads;
                    break;
                }
            }
        }
#endif
        append_line(QString("[Ruby Performance Status]\n"
                            "  Resident Memory (RAM RSS): %1 MB\n"
                            "  Virtual Memory (VMS):      %2 MB\n"
                            "  Active Process Threads:    %3\n"
                            "  Stutter Monitoring:        ACTIVE (threshold > 33.3ms / <30 FPS)")
                        .arg(rss_mb, 0, 'f', 1)
                        .arg(vms_mb, 0, 'f', 1)
                        .arg(threads), "#61afef");
        return;
    }
    if (cmd.startsWith("open ")) {
        QString path = cmd.mid(5).trimmed();
        if (path.startsWith('~')) path = QDir::homePath() + path.mid(1);
        QFileInfo info(path);
        if (!info.exists()) {
            append_line("No such file: " + path, "#e06c75");
            return;
        }
        append_echo(cmd);
        emit openFileRequested(info.absoluteFilePath());
        return;
    }

    // ── Live shell ────────────────────────────────────────────────────────
    append_echo(cmd);
    ensure_shell();
    if (m_shell->state() != QProcess::Running) {
        append_line("Could not start bash.", "#e06c75");
        return;
    }
    m_shell->write((cmd + "\n").toUtf8());
    m_shell->waitForBytesWritten(200);
}

void ConsolePanel::submit_input() {
    run_command(m_input->text());
}

void ConsolePanel::walk_history_up() {
    if (m_history.isEmpty()) return;
    if (m_history_index < 0) m_history_index = m_history.size();
    if (m_history_index > 0) --m_history_index;
    m_input->setText(m_history[m_history_index]);
    m_input->setCursorPosition(m_input->text().size());
}

void ConsolePanel::walk_history_down() {
    if (m_history.isEmpty() || m_history_index < 0) return;
    ++m_history_index;
    if (m_history_index >= m_history.size()) {
        m_history_index = -1;
        m_input->clear();
        return;
    }
    m_input->setText(m_history[m_history_index]);
    m_input->setCursorPosition(m_input->text().size());
}

// ---------------------------------------------------------------------------
// Raijin — the game's Lua console (TCP client to the engine pod)
// ---------------------------------------------------------------------------
void ConsolePanel::set_lua_port(quint16 port) {
    if (m_lua_port == port) return;
    m_lua_port = port;
    if (port == 0) {
        m_lua_sock->abort();
        m_lua_connected = false;
        m_lua_hint_shown = false;
        if (m_status_badge && m_tabs && m_tabs->currentIndex() == 1) {
            m_status_badge->setText("DISCONNECTED");
            m_status_badge->setStyleSheet("background-color: #3e2428; color: #e06c75; border-radius: 3px; padding: 1px 6px; font-size: 10px; font-weight: 700;");
        }
        return;
    }
    m_lua_sock->abort();
    m_lua_connected = false;
    m_lua_sock->connectToHost(QHostAddress::LocalHost, port);
    append_lua(QString("[SwLua] Connecting to engine Lua console on 127.0.0.1:%1…").arg(port),
               "#61afef");
}

void ConsolePanel::submit_lua() {
    const QString cmd = m_lua_input->text();
    if (cmd.isEmpty()) {
        if (m_lua_connected) {
            // An empty line makes the server re-emit its prompt.
            m_lua_sock->write("\n");
        }
        m_lua_input->clear();
        return;
    }
    m_lua_history.append(cmd);
    m_lua_history_index = -1;
    m_lua_input->clear();
    append_lua(QString("\u25B8 ") + cmd, "#e5c07b");   // echo the command in warm amber

    if (!m_lua_connected || m_lua_sock->state() != QTcpSocket::ConnectedState) {
        append_lua("[SwLua] Engine is not connected — boot the inline engine (phone ▶) and try again.", "#e06c75");
        return;
    }
    m_lua_sock->write((cmd + "\n").toUtf8());
}

void ConsolePanel::walk_lua_history_up() {
    if (m_lua_history.isEmpty()) return;
    if (m_lua_history_index < 0) m_lua_history_index = m_lua_history.size();
    if (m_lua_history_index > 0) --m_lua_history_index;
    m_lua_input->setText(m_lua_history[m_lua_history_index]);
    m_lua_input->setCursorPosition(m_lua_input->text().size());
}

void ConsolePanel::walk_lua_history_down() {
    if (m_lua_history.isEmpty() || m_lua_history_index < 0) return;
    ++m_lua_history_index;
    if (m_lua_history_index >= m_lua_history.size()) {
        m_lua_history_index = -1;
        m_lua_input->clear();
        return;
    }
    m_lua_input->setText(m_lua_history[m_lua_history_index]);
    m_lua_input->setCursorPosition(m_lua_input->text().size());
}

void ConsolePanel::on_lua_ready() {
    QByteArray bytes = m_lua_sock->readAll();
    if (bytes.contains("\x1b[2J")) {
        // The server's "clear" sent an ANSI clear-screen.
        m_lua_output->clear();
    }
    const QString text = strip_ansi(QString::fromUtf8(bytes));
    if (text.isEmpty()) return;
    m_lua_output->moveCursor(QTextCursor::End);
    m_lua_output->insertPlainText(text);
    QScrollBar* bar = m_lua_output->verticalScrollBar();
    if (bar) bar->setValue(bar->maximum());
}

void ConsolePanel::on_lua_connected() {
    m_lua_connected = true;
    if (m_status_badge && m_tabs && m_tabs->currentIndex() == 1) {
        m_status_badge->setText("CONNECTED");
        m_status_badge->setStyleSheet("background-color: #1e3a29; color: #98c379; border-radius: 3px; padding: 1px 6px; font-size: 10px; font-weight: 700;");
    }
    append_lua("[SwLua] Connected to engine Lua console.", "#98c379");
}

void ConsolePanel::on_lua_disconnected() {
    if (!m_lua_connected) return;   // ignore aborts during port switches
    m_lua_connected = false;
    if (m_status_badge && m_tabs && m_tabs->currentIndex() == 1) {
        m_status_badge->setText("DISCONNECTED");
        m_status_badge->setStyleSheet("background-color: #3e2428; color: #e06c75; border-radius: 3px; padding: 1px 6px; font-size: 10px; font-weight: 700;");
    }
    append_lua("[SwLua] Engine Lua console connection closed.", "#7d8492");
}

void ConsolePanel::on_lua_error() {
    if (m_lua_port == 0) return;
    if (m_lua_sock->state() != QTcpSocket::ConnectedState) {
        if (m_status_badge && m_tabs && m_tabs->currentIndex() == 1) {
            m_status_badge->setText("ERROR");
            m_status_badge->setStyleSheet("background-color: #3e2428; color: #e06c75; border-radius: 3px; padding: 1px 6px; font-size: 10px; font-weight: 700;");
        }
        append_lua("[SwLua] Connection error: " + m_lua_sock->errorString(), "#e06c75");
    }
}

} // namespace ruby::panels