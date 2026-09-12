#pragma once
// ============================================================================
// console_panel.h — VS Code-style Terminal / Console dock
//
// Two tabs (like VS Code's terminal switcher):
//   * "Terminal"  — interactive bash child + a log stream that project
//                   messages, tool actions and script results echo into.
//   * "Raijin"    — the GAME's Lua console. Connects over TCP to the engine
//                   pod's Raijin Lua SDK server (SWORDFARE_TCP_PORT) so you
//                   can evaluate Lua live inside the running 1.4.13 game.
// ============================================================================

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QStringList>
#include <QTabWidget>
#include <QTcpSocket>
#include <QWidget>

class QLabel;

namespace ruby::panels {

// Input box that reports Up/Down so a console can walk its command history.
class ConsoleInput final : public QLineEdit {
    Q_OBJECT
public:
    explicit ConsoleInput(QWidget* parent = nullptr);

signals:
    void history_up();
    void history_down();

protected:
    void keyPressEvent(QKeyEvent* event) override;
};

class ConsolePanel final : public QWidget {
    Q_OBJECT

public:
    explicit ConsolePanel(QWidget* parent = nullptr);
    ~ConsolePanel() override;

    // Echo a message into the Ruby system logger stream (status / errors / notifications).
    void append_line(const QString& text, const QString& color = QString());
    // Run one command line in the bash tab (builtins first, then the shell).
    void run_command(const QString& command);

    // Point the SwLua tab at the engine pod's Raijin Lua console (port from
    // EnginePod::lua_console_port()). 0 disconnects (engine stopped).
    void set_lua_port(quint16 port);

signals:
    void openFileRequested(const QString& path);

private slots:
    void submit_input();
    void handle_shell_output();
    void handle_shell_finished(int exit_code, QProcess::ExitStatus status);
    void walk_history_up();
    void walk_history_down();

    void submit_lua();
    void walk_lua_history_up();
    void walk_lua_history_down();
    void on_lua_ready();
    void on_lua_connected();
    void on_lua_disconnected();
    void on_lua_error();
    void on_tab_changed(int index);

    void clear_current_tab();

private:
    void ensure_shell();
    void append_echo(const QString& command);
    void append_lua(const QString& text, const QString& color = QString());
    static QString strip_ansi(const QString& text);

    QTabWidget* m_tabs = nullptr;
    QLabel*     m_hint = nullptr;
    QLabel*     m_status_badge = nullptr;

    // ── Terminal (bash) tab ──────────────────────────────────────────────
    QPlainTextEdit* m_output = nullptr;
    ConsoleInput* m_input = nullptr;
    QProcess* m_shell = nullptr;
    QStringList m_history;
    int m_history_index = -1;
    bool m_running_builtin = false;

    // ── SwLua (game Lua console, internal Raijin SDK) tab ────────────────
    QPlainTextEdit* m_lua_output = nullptr;
    ConsoleInput* m_lua_input = nullptr;
    QTcpSocket* m_lua_sock = nullptr;
    QStringList m_lua_history;
    int m_lua_history_index = -1;
    quint16 m_lua_port = 0;
    bool m_lua_connected = false;
    bool m_lua_hint_shown = false;

    // ── Ruby System Logger tab (separate from bash shell) ────────────────
    QPlainTextEdit* m_log_output = nullptr;
};

} // namespace ruby::panels