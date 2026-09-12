#pragma once
/* apk_session.h — APK import→edit→export session registry (master TODO 4.1a).
 *
 * A session is: the source APK (apk_origin), a session id, and the extracted
 * tree under ~/.ruby/apk-sessions/<id>/ (override with $RUBY_SESSIONS_DIR,
 * which the tests use to stay hermetic). Importing extracts EVERYTHING (the
 * whole APK, not just assets/) so the session tree is a full, editable copy
 * and the original APK is never mutated until export (4.1b).
 *
 * Each session directory carries a session.json registry:
 *   { "session_id": …, "apk_origin": …, "imported_at": …, "assets_subdir": "assets" }
 * written by save_session() and read by load_session().
 */

#include <string>
#include <vector>

namespace apk {

struct Session {
    std::string session_id;    // "<apk-stem>-<yyyymmdd-HHMMSS>-<rand4>"
    std::string apk_origin;    // absolute path of the source .apk
    std::string session_dir;   // absolute extraction dir (sessions_root()/id)
    std::string assets_subdir; // "assets" — the tree the engine reads
    std::string imported_at;   // ISO-ish timestamp
};

// Root of all sessions (~/.ruby/apk-sessions, or $RUBY_SESSIONS_DIR).
// The directory is NOT created here; import_apk creates it.
std::string sessions_root();

// Import `apk_path` into a fresh session dir: validates it is a ZIP with
// AndroidManifest.xml + assets/, extracts every entry, writes session.json.
// On success `out` is filled and true is returned; on failure `error`
// explains why (partial extraction is cleaned up).
bool import_apk(const std::string& apk_path, Session& out, std::string& error);

// Write/read the session.json registry inside s.session_dir.
bool save_session(const Session& s, std::string& error);
bool load_session(const std::string& session_dir, Session& out, std::string& error);

// All sessions currently present under sessions_root() (dirs with a readable
// session.json). Broken/partial dirs are skipped.
std::vector<Session> list_sessions();

// Delete a session's directory tree (export cleanup / "clear sessions").
// Returns false + error when the session is unknown or removal fails.
bool remove_session(const std::string& session_id, std::string& error);

} // namespace apk