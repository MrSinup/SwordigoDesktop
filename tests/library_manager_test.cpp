// library_manager_test.cpp — caver::LibraryManager (C1), no Qt/GL.
//
// Covers the contract the runtime depends on:
//   * load by name through the search path, identity taken from the
//     ObjectLibrary's own Name field (not the file name),
//   * template lookup across libraries with the owning library reported,
//   * the content-hash fast path: re-loading unchanged bytes does NOT bump the
//     generation or re-report a change,
//   * hot swap: editing an .scl on disk is picked up by refresh_changed(),
//     which reports exactly which templates appeared/disappeared,
//   * in-memory load_bytes for editor swaps, and
//   * missing-library diagnostics.
//
// The wire layout mirrors what the game emits:
//   ObjectLibrary { Name = 1 (string), Template = 2, ImportedLibrary = 3 (string) }
//   Template      { Object = 1, Scaling = 2 (float) }
//   Object        { Name = 2 (string) }
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ruby/caver/library_manager.h"
#include "platform/protobuf_reader.h"

// Required by libswcore (asset resolution global; other tests define it too).
std::string g_instance_assets_dir = "assets";

namespace fs = std::filesystem;
using caver::LibraryManager;
using caver::LibraryChange;

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}

// ── fixture builders ────────────────────────────────────────────────────────
static std::string object_bytes(const std::string& name) {
    proto::Writer o;
    o.write_string_field(2, name);
    return o.to_string();
}

static std::string template_entry(const std::string& object_name) {
    proto::Writer t;
    t.write_bytes_field(1, object_bytes(object_name));
    t.write_float_field(2, 1.0f);
    return t.to_string();
}

static std::string library_bytes(const std::string& library_name,
                                 const std::vector<std::string>& template_names,
                                 const std::vector<std::string>& imports = {}) {
    proto::Writer lib;
    lib.write_string_field(1, library_name);          // ObjectLibrary.Name
    for (const auto& n : template_names) lib.write_bytes_field(2, template_entry(n));
    for (const auto& imp : imports) lib.write_string_field(3, imp);
    return lib.to_string();
}

static void write_file(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

static bool has_name(const std::vector<std::string>& v, const std::string& name) {
    for (const auto& s : v) if (s == name) return true;
    return false;
}

static bool has_missing(const LibraryManager& m, const std::string& name) {
    for (const auto& s : m.missing()) if (s == name) return true;
    return false;
}

int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() / ("caver_lib_test_" + std::to_string(stamp));
    std::error_code ec;
    fs::create_directories(dir, ec);

    write_file(dir / "alpha.scl", library_bytes("alpha", {"alpha", "torch_cave"}));
    write_file(dir / "child.scl", library_bytes("child", {"child_thing"}, {"parent"}));

    LibraryManager manager;
    manager.add_search_path(dir.string());

    // ── load by name / identity / templates ────────────────────────────────
    {
        LibraryChange change;
        check(manager.load_by_name("alpha", &change), "load alpha by name");
        check(change.added && change.bytes_changed, "alpha reported as added");
        check(manager.has("alpha"), "alpha registered under its ObjectLibrary.Name");
        const caver::Library* lib = manager.get("alpha");
        check(lib != nullptr, "alpha retrievable");
        check(lib && lib->templates.size() == 2, "alpha has 2 templates");
        check(manager.generation() == 1, "generation bumped once");
    }

    // Extension is optional, and the object identity is the library Name.
    check(manager.load_by_name("alpha.scl"), "load alpha.scl resolves the same library");

    {
        av::SclTemplateEntry entry;
        std::string owner;
        check(manager.find_template("torch_cave", &entry, &owner), "find torch_cave");
        check(entry.name == "torch_cave", "found the right template");
        check(owner == "alpha", "owner library reported");
        check(!manager.find_template("does_not_exist", nullptr, nullptr), "unknown template not found");
    }

    // ── content-hash fast path: re-loading unchanged bytes is a no-op ──────
    {
        LibraryChange change;
        check(manager.load_by_name("alpha", &change), "re-load alpha succeeds");
        check(!change.bytes_changed, "unchanged bytes are not a change");
        check(manager.generation() == 1, "generation unchanged on identical reload");
        check(manager.refresh_changed().empty(), "no spurious changes on refresh");
    }

    // ── imports are parsed from ImportedLibrary (field 3) ──────────────────
    {
        check(manager.load_by_name("child"), "load child");
        const caver::Library* child = manager.get("child");
        check(child && child->imports.size() == 1 && child->imports[0] == "parent",
              "child imports parent");
        check(manager.find_template("child_thing", nullptr, nullptr), "child template resolvable");
    }

    // ── missing diagnostics ────────────────────────────────────────────────
    {
        check(!manager.load_by_name("nope"), "missing library fails to load");
        check(has_missing(manager, "nope"), "missing library recorded");
    }

    // ── hot swap: edit the file on disk ────────────────────────────────────
    {
        check(manager.find_template("torch_cave", nullptr, nullptr), "torch_cave present before edit");
        write_file(dir / "alpha.scl", library_bytes("alpha", {"alpha", "gamma"}));

        const std::vector<LibraryChange> changes = manager.refresh_changed();
        check(changes.size() == 1, "exactly one library changed");
        if (changes.size() == 1) {
            check(changes[0].name == "alpha", "the changed library is alpha");
            check(changes[0].replaced && changes[0].bytes_changed, "reported as replaced");
            check(has_name(changes[0].added_templates, "gamma"), "gamma reported as added");
            check(has_name(changes[0].removed_templates, "torch_cave"), "torch_cave reported as removed");
        }
        check(manager.generation() == 3, "generation bumped by the edit");
        check(manager.find_template("gamma", nullptr, nullptr), "gamma resolvable after edit");
        check(!manager.find_template("torch_cave", nullptr, nullptr), "torch_cave gone after edit");
        check(manager.refresh_changed().empty(), "no further changes once settled");
    }

    // ── in-memory swap (editor path, no disk round trip) ───────────────────
    {
        const std::string mem = library_bytes("mem_lib", {"mem_thing"});
        LibraryChange change;
        check(manager.load_bytes(mem, std::string(), "mem:lib", &change), "load_bytes");
        check(change.added, "in-memory library reported as added");
        check(manager.has("mem_lib"), "in-memory library keyed by its Name field");
        check(manager.find_template("mem_thing", nullptr, nullptr), "in-memory template resolvable");

        LibraryChange again;
        check(manager.load_bytes(mem, std::string(), "mem:lib", &again), "re-load identical bytes");
        check(!again.bytes_changed, "identical in-memory bytes are a no-op");
        check(manager.refresh_changed().empty(), "in-memory libraries are not disk-watched");
    }

    // ── raw byte helpers ───────────────────────────────────────────────────
    {
        const std::string bytes = library_bytes("helper", {"x"}, {"dep_a", "dep_b"});
        check(caver::library_name_from_bytes(bytes) == "helper", "library_name_from_bytes");
        const auto imports = caver::library_imports_from_bytes(bytes);
        check(imports.size() == 2 && imports[0] == "dep_a" && imports[1] == "dep_b",
              "library_imports_from_bytes");
    }

    // ── clear ──────────────────────────────────────────────────────────────
    {
        manager.clear();
        check(manager.names().empty(), "clear drops libraries");
        check(manager.generation() == 0, "clear resets generation");
        check(!manager.find_template("alpha", nullptr, nullptr), "clear drops the index");
    }

    // ── real corpus (SKIP when the assets are not installed) ───────────────
    {
        const char* home = std::getenv("HOME");
        const fs::path assets = home ? fs::path(home) / ".local/share/swordigo-desktop/assets/resources"
                                     : fs::path();
        std::error_code exists_ec;
        if (!assets.empty() && fs::is_directory(assets, exists_ec)) {
            LibraryManager corpus;
            corpus.add_search_path(assets.string());
            check(corpus.load_by_name("crypt"), "corpus: load crypt.scl");
            check(corpus.has("crypt"), "corpus: identity is the ObjectLibrary Name");
            check(corpus.find_template("crypt_torch", nullptr, nullptr), "corpus: crypt_torch resolves");
            std::printf("corpus: %zu libraries loaded, %zu templates\n",
                        corpus.names().size(), corpus.template_count());
        } else {
            std::printf("SKIP: corpus assets not present (%s)\n", assets.string().c_str());
        }
    }

    fs::remove_all(dir, ec);
    std::printf(failures == 0 ? "PASS: LibraryManager\n" : "FAIL: %d assertion(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
