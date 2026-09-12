#pragma once
// apk_session_panel.h — status-bar APK session badge + Import/Export plumbing
// (master TODO 4.1a). A tiny permanent widget in the status bar that shows the
// active APK session ("APK session: <origin>.apk") with an End button, and
// hosts the File ▸ Import APK… handler so the main window stays thin.
//
// Importing an APK (apk::import_apk) extracts the WHOLE archive into
// ~/.ruby/apk-sessions/<id>/ and writes the session.json registry; the host
// then points the project/asset roots at the extraction. The session object
// stays here (single source of truth for "what are we exporting?"), and the
// Export-as-APK flow (4.1b repack + 4.1c signer) reads it back.
//
// Ending a session only clears the badge/state — the extracted tree is kept so
// the user can keep working; deleting it is the export cleanup ask (4.1b).

#include <QWidget>

#include "tools/apk_session.h"

class QLabel;
class QToolButton;

namespace ruby::editor {

class ApkSessionPanel final : public QWidget {
    Q_OBJECT
public:
    explicit ApkSessionPanel(QWidget* parent = nullptr);

    bool has_session() const { return !m_session.session_id.empty(); }
    const apk::Session& session() const { return m_session; }

    // File ▸ Import APK…: file dialog → apk::import_apk → adopt the session.
    // Returns true on success; errors are surfaced via statusMessage.
    bool import_apk(QWidget* parent);

    // Clear the session state (keeps the extracted files on disk).
    void end_session();

signals:
    void sessionChanged(bool active);
    void statusMessage(const QString& message, int timeout_ms);

private:
    void adopt_session(const apk::Session& s);
    void refresh_ui();

    apk::Session m_session;
    QLabel* m_label = nullptr;
    QToolButton* m_end_btn = nullptr;
};

} // namespace ruby::editor