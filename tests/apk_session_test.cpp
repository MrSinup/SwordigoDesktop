// apk_session_test.cpp — APK session import + registry (master TODO 4.1a).
//
// Builds a tiny fake APK with the shared zip writer, imports it through the
// same apk::import_apk path the GUI uses, then asserts:
//   * validation fails fast for non-zips / zips without AndroidManifest.xml /
//     zips without assets/,
//   * extraction is byte-identical for every entry,
//   * path-traversal and absolute entry names never escape the session dir,
//   * session.json round-trips (save → load → list → remove).
//
// $RUBY_SESSIONS_DIR points at a temp dir so nothing touches ~/.ruby.
// Pure std C++17 + zlib; no Qt, no GL.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "platform/zip_archive.h"
#include "tools/apk_session.h"

namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}

static void set_env(const char* key, const char* value) {
#ifdef _WIN32
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}

static std::string read_all(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    const std::string tmp = (fs::temp_directory_path() / "ruby_apk_session_test").string();
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);

    const std::string sessions = tmp + "/sessions";
    set_env("RUBY_SESSIONS_DIR", sessions.c_str());
    check(apk::sessions_root() == sessions, "sessions_root honors RUBY_SESSIONS_DIR");

    // ── Fixture: a fake Swordigo-shaped APK ─────────────────────────────────
    const std::string manifest = "<?xml version=\"1.0\"?><manifest package=\"com.touchfoo.swordigo\"/>";
    const std::string dex = "\x64\x65\x78\n035\0";                 // binary junk
    const std::string scene = "fake-scene-bytes-0123456789";
    const std::string model = "FAKE-POD-HEADER";
    const std::string lua = "function onLevelStart() end";
    const std::string meta_inf = "Manifest-Version: 1.0\n";

    std::vector<zip::OutEntry> apk_entries = {
        {"AndroidManifest.xml", manifest, 8, 0},
        {"classes.dex", dex, 0, 0},                       // stored (mmap-friendly)
        {"assets/", "", 0, 0},
        {"assets/resources/menu.scene", scene, 8, 0},
        {"assets/models/hero.pod", model, 8, 0},
        {"assets/scripts/main.lua", lua, 8, 0},
        {"META-INF/MANIFEST.MF", meta_inf, 8, 0},
        {"../evil.txt", "must-not-escape", 8, 0},         // traversal
        {"/abs.txt", "must-not-escape", 8, 0},            // absolute
    };
    const std::string fake_apk = tmp + "/swordigo_1.4.13.apk";
    check(zip::write_archive(fake_apk, apk_entries), "fixture APK written");

    // The reader round-trips what the writer wrote (incl. external_attr).
    {
        std::vector<zip::Entry> entries;
        check(zip::read_entries(fake_apk, entries), "read_entries on fixture");
        check(entries.size() == apk_entries.size(), "entry count matches");
        std::string out;
        check(zip::read_entry(fake_apk, entries[0], out) && out == manifest,
              "stored/deflated payload round-trips");
    }

    // ── Validation fails fast ───────────────────────────────────────────────
    {
        const std::string not_zip = tmp + "/not_an_apk.bin";
        {
            std::ofstream f(not_zip, std::ios::binary);
            f << "this is not a zip archive at all";
        }
        apk::Session s;
        std::string err;
        check(!apk::import_apk(not_zip, s, err), "non-zip rejected");
        check(err.find("ZIP") != std::string::npos, "non-zip error mentions ZIP");

        // Zip but no AndroidManifest.xml → not an APK.
        const std::string no_manifest = tmp + "/no_manifest.apk";
        std::vector<zip::OutEntry> bare = {{"assets/x.txt", "x", 8, 0}};
        check(zip::write_archive(no_manifest, bare), "no-manifest fixture written");
        check(!apk::import_apk(no_manifest, s, err), "zip without manifest rejected");
        check(err.find("AndroidManifest") != std::string::npos,
              "missing-manifest error names AndroidManifest.xml");

        // Manifest but no assets/ → not moddable.
        const std::string no_assets = tmp + "/no_assets.apk";
        std::vector<zip::OutEntry> noa = {{"AndroidManifest.xml", manifest, 8, 0}};
        check(zip::write_archive(no_assets, noa), "no-assets fixture written");
        check(!apk::import_apk(no_assets, s, err), "zip without assets rejected");
        check(err.find("assets") != std::string::npos,
              "missing-assets error mentions assets/");
    }

    // ── Import: full extraction + registry ──────────────────────────────────
    apk::Session session;
    std::string error;
    check(apk::import_apk(fake_apk, session, error), "import_apk succeeded");
    if (failures == 0 || !session.session_id.empty()) {
        check(session.session_id.find("swordigo_1.4.13-") == 0,
              "session id is <stem>-<stamp>-<rand>");
        check(session.apk_origin.find("swordigo_1.4.13.apk") != std::string::npos,
              "apk_origin recorded");
        check(fs::exists(session.session_dir), "session dir exists");

        const std::string dir = session.session_dir;
        check(read_all(dir + "/AndroidManifest.xml") == manifest, "manifest byte-identical");
        check(read_all(dir + "/classes.dex") == dex, "stored classes.dex byte-identical");
        check(read_all(dir + "/assets/resources/menu.scene") == scene, "scene byte-identical");
        check(read_all(dir + "/assets/models/hero.pod") == model, "pod byte-identical");
        check(read_all(dir + "/assets/scripts/main.lua") == lua, "lua byte-identical");
        check(read_all(dir + "/META-INF/MANIFEST.MF") == meta_inf, "META-INF byte-identical");

        // Traversal / absolute names stay inside the session tree.
        check(!fs::exists(tmp + "/evil.txt"), "traversal name did not escape sessions root");
        check(fs::exists(dir + "/evil.txt"), "traversal name sanitized into session dir");
        check(!fs::exists(dir + "/abs.txt"), "absolute entry name skipped");

        // Registry round trip.
        const std::string json = read_all(dir + "/session.json");
        check(json.find("\"session_id\"") != std::string::npos, "session.json has session_id");
        check(json.find("\"apk_origin\"") != std::string::npos, "session.json has apk_origin");
        check(json.find("\"assets_subdir\": \"assets\"") != std::string::npos,
              "session.json has assets_subdir");

        apk::Session loaded;
        std::string lerr;
        check(apk::load_session(dir, loaded, lerr), "load_session round-trips");
        check(loaded.session_id == session.session_id, "loaded session id matches");
        check(loaded.apk_origin == session.apk_origin, "loaded origin matches");
        check(loaded.assets_subdir == "assets", "loaded assets_subdir defaults correctly");

        const auto sessions_list = apk::list_sessions();
        bool found = false;
        for (const auto& s : sessions_list)
            if (s.session_id == session.session_id) found = true;
        check(found, "list_sessions finds the imported session");

        // ── Remove ──────────────────────────────────────────────────────────
        std::string rerr;
        check(apk::remove_session(session.session_id, rerr), "remove_session succeeds");
        check(!fs::exists(dir), "session dir deleted");
        bool gone = true;
        for (const auto& s : apk::list_sessions())
            if (s.session_id == session.session_id) gone = false;
        check(gone, "session gone from the registry after remove");
        check(!apk::remove_session(session.session_id, rerr),
              "removing an unknown session fails");
    }

    fs::remove_all(tmp, ec);
    std::printf(failures == 0 ? "PASS: apk import + session registry works\n"
                              : "FAIL: %d assertion(s)\n", failures);
    return failures == 0 ? 0 : 1;
}