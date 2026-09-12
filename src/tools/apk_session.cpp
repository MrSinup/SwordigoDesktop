/* apk_session.cpp — APK session import/registry (see apk_session.h).
 *
 * Pure std C++17 + the shared zip_archive library (no Qt) so the CLI, the
 * GUI and the tests all use the same import code path.
 */

#include "tools/apk_session.h"

#include "platform/zip_archive.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace apk {

// ─── Session root ────────────────────────────────────────────────────────────

std::string sessions_root() {
    if (const char* env = std::getenv("RUBY_SESSIONS_DIR"); env && env[0])
        return env;
    const char* home = std::getenv("HOME");
    const std::string base = (home && home[0]) ? home : ".";
    return base + "/.ruby/apk-sessions";
}

// ─── Tiny flat-JSON helpers (session.json only holds string fields) ─────────

static std::string json_escape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += (char)c;
        }
    }
    return out;
}

// Find the JSON string value of `key` at the top level of `text`. Handles the
// standard escapes (incl. \uXXXX → UTF-8). Returns false when absent.
static bool json_string_value(const std::string& text, const std::string& key,
                              std::string& out) {
    const std::string needle = "\"" + key + "\"";
    size_t p = text.find(needle);
    while (p != std::string::npos) {
        // A real key is `"key"` immediately followed by optional space and ':'. A
        // colon deeper in some value string (e.g. an origin path containing the
        // key text) must not match.
        size_t colon = p + needle.size();
        while (colon < text.size() && (text[colon] == ' ' || text[colon] == '\t')) ++colon;
        if (colon < text.size() && text[colon] == ':') {
            size_t q = colon + 1;
            while (q < text.size() && (text[q] == ' ' || text[q] == '\t')) ++q;
            if (q < text.size() && text[q] == '"') {
                ++q;
                std::string val;
                while (q < text.size()) {
                    char c = text[q];
                    if (c == '"') { out = val; return true; }
                    if (c == '\\' && q + 1 < text.size()) {
                        char e = text[q + 1];
                        switch (e) {
                            case '"':  val += '"';  q += 2; continue;
                            case '\\': val += '\\'; q += 2; continue;
                            case '/':  val += '/';  q += 2; continue;
                            case 'n':  val += '\n'; q += 2; continue;
                            case 'r':  val += '\r'; q += 2; continue;
                            case 't':  val += '\t'; q += 2; continue;
                            case 'b':  val += '\b'; q += 2; continue;
                            case 'f':  val += '\f'; q += 2; continue;
                            case 'u': {
                                // \uXXXX → UTF-8 (BMP only; good enough for paths)
                                if (q + 6 <= text.size()) {
                                    unsigned cp = 0;
                                    for (int i = 0; i < 4; ++i) {
                                        char h = text[q + 2 + (size_t)i];
                                        cp <<= 4;
                                        if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                                        else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                                        else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                                        else { cp = 0xFFFD; break; }
                                    }
                                    if (cp < 0x80) val += (char)cp;
                                    else if (cp < 0x800) {
                                        val += (char)(0xC0 | (cp >> 6));
                                        val += (char)(0x80 | (cp & 0x3F));
                                    } else {
                                        val += (char)(0xE0 | (cp >> 12));
                                        val += (char)(0x80 | ((cp >> 6) & 0x3F));
                                        val += (char)(0x80 | (cp & 0x3F));
                                    }
                                    q += 6;
                                    continue;
                                }
                                val += e;
                                q += 2;
                                continue;
                            }
                            default: val += e; q += 2; continue;
                        }
                    }
                    val += c;
                    ++q;
                }
                return false; // unterminated string
            }
        }
        p = text.find(needle, p + needle.size());
    }
    return false;
}

// ─── Session id + timestamp ─────────────────────────────────────────────────

static std::string sanitize_stem(const std::string& stem) {
    std::string out;
    for (unsigned char c : stem) {
        if (std::isalnum(c) || c == '.' || c == '_' || c == '-') out += (char)c;
        else out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "apk" : out;
}

static std::string now_stamp() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return buf;
}

static std::string rand4() {
    static bool seeded = false;
    if (!seeded) {
        std::srand((unsigned)std::time(nullptr) ^ (unsigned)std::clock());
        seeded = true;
    }
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04x", (unsigned)(std::rand() & 0xFFFF));
    return buf;
}

// ─── Import ─────────────────────────────────────────────────────────────────

static bool file_exists(const std::string& p) {
    std::error_code ec;
    return fs::exists(p, ec) && !fs::is_directory(p, ec);
}

bool import_apk(const std::string& apk_path, Session& out, std::string& error) {
    error.clear();
    if (!file_exists(apk_path)) {
        error = "file not found: " + apk_path;
        return false;
    }

    // Validate it is a ZIP and carries the APK markers before extracting
    // anything (fail fast, per the design doc §2.4).
    std::vector<zip::Entry> entries;
    if (!zip::read_entries(apk_path, entries)) {
        error = "not a valid ZIP/APK";
        return false;
    }
    bool has_manifest = false, has_assets = false;
    for (const auto& e : entries) {
        if (e.name == "AndroidManifest.xml") has_manifest = true;
        if (e.name.rfind("assets/", 0) == 0) has_assets = true;
    }
    if (!has_manifest) {
        error = "not an APK: no AndroidManifest.xml in the archive";
        return false;
    }
    if (!has_assets) {
        error = "not a moddable APK: no assets/ directory in the archive";
        return false;
    }

    // Session id: <apk-stem>-<yyyymmdd-HHMMSS>-<rand4>
    fs::path src(apk_path);
    std::string stem = sanitize_stem(src.stem().string());
    const std::string session_id = stem + "-" + now_stamp() + "-" + rand4();
    const std::string root = sessions_root();
    const std::string session_dir = (fs::path(root) / session_id).string();

    std::error_code ec;
    fs::create_directories(session_dir, ec);
    if (ec) {
        error = "cannot create session directory " + session_dir + ": " + ec.message();
        return false;
    }

    const zip::ExtractResult res = zip::extract_all(apk_path, session_dir);
    if (!res.ok) {
        fs::remove_all(session_dir, ec); // clean up the partial tree
        error = "extraction failed: " +
                (res.error.empty() ? std::string("unknown zip error") : res.error);
        return false;
    }

    Session s;
    s.session_id = session_id;
    s.apk_origin = fs::absolute(src).lexically_normal().string();
    s.session_dir = session_dir;
    s.assets_subdir = "assets";
    s.imported_at = now_stamp();

    if (!save_session(s, error)) {
        fs::remove_all(session_dir, ec);
        return false;
    }
    out = std::move(s);
    return true;
}

// ─── Registry I/O ───────────────────────────────────────────────────────────

bool save_session(const Session& s, std::string& error) {
    error.clear();
    std::error_code ec;
    if (s.session_dir.empty() || !fs::is_directory(s.session_dir, ec)) {
        error = "session directory missing: " + s.session_dir;
        return false;
    }
    std::ostringstream json;
    json << "{\n"
         << "  \"session_id\": \""   << json_escape(s.session_id) << "\",\n"
         << "  \"apk_origin\": \""   << json_escape(s.apk_origin) << "\",\n"
         << "  \"imported_at\": \""  << json_escape(s.imported_at) << "\",\n"
         << "  \"assets_subdir\": \"" << json_escape(s.assets_subdir) << "\"\n"
         << "}\n";
    std::ofstream f((fs::path(s.session_dir) / "session.json").string(),
                    std::ios::binary | std::ios::trunc);
    if (!f) {
        error = "cannot write session.json in " + s.session_dir;
        return false;
    }
    f << json.str();
    return f.good();
}

bool load_session(const std::string& session_dir, Session& out, std::string& error) {
    error.clear();
    std::ifstream f((fs::path(session_dir) / "session.json").string(),
                    std::ios::binary);
    if (!f) {
        error = "no session.json in " + session_dir;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();

    Session s;
    if (!json_string_value(text, "session_id", s.session_id) ||
        !json_string_value(text, "apk_origin", s.apk_origin)) {
        error = "session.json in " + session_dir + " is missing required fields";
        return false;
    }
    json_string_value(text, "imported_at", s.imported_at);
    if (!json_string_value(text, "assets_subdir", s.assets_subdir))
        s.assets_subdir = "assets";
    s.session_dir = fs::path(session_dir).lexically_normal().string();
    out = std::move(s);
    return true;
}

std::vector<Session> list_sessions() {
    std::vector<Session> out;
    std::error_code ec;
    for (auto& de : fs::directory_iterator(sessions_root(),
                                           fs::directory_options::skip_permission_denied, ec)) {
        if (!de.is_directory(ec)) continue;
        Session s;
        std::string err;
        if (load_session(de.path().string(), s, err)) out.push_back(std::move(s));
    }
    return out;
}

bool remove_session(const std::string& session_id, std::string& error) {
    error.clear();
    const std::string dir = (fs::path(sessions_root()) / session_id).string();
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        error = "no such session: " + session_id;
        return false;
    }
    fs::remove_all(dir, ec);
    if (ec) {
        error = "cannot remove " + dir + ": " + ec.message();
        return false;
    }
    return true;
}

} // namespace apk