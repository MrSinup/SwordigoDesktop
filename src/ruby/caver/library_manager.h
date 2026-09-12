#pragma once
// library_manager.h — runtime registry of ObjectLibraries (.scl archetype
// files), modelled on Caver::ObjectLibrary's load/import surface.
//
// WHY THIS EXISTS
// ---------------
// In the shipped engine, spawning anything from a Lua program
// (`Scene.CreateObject("firespray", ...)`), item drops, spell spawns and weapon
// creation all resolve through `ObjectLibrary::TemplateForName` +
// `SceneObject::InitWithTemplate`. A persistent library registry is therefore a
// *gameplay* requirement, not an editor convenience.
//
// The editor previously re-parsed every .scl from scratch on each scene load /
// template refresh (scene_loader.cpp `load_external_libraries` +
// template_sources.cpp), with a name->path cache that never invalidated. This
// class replaces that with:
//   * a name-keyed registry (ObjectLibrary.Name),
//   * a bounded search-path resolver that caches the resolved path,
//   * content-hash gating: files are re-read cheaply, but templates are only
//     re-parsed when the bytes actually changed (the expensive step), and
//   * a generation counter so clients can invalidate derived state.
//
// Qt-free / GL-free on purpose: it is unit-testable on the real corpus.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "tools/scene_loader.h"

namespace caver {

// One loaded .scl, cached.
struct Library {
    std::string name;                                // ObjectLibrary.Name (field 1); falls back to the file stem
    std::string path;                                // resolved source path ("" for in-memory loads)
    std::string bytes;                               // raw .scl bytes, preserved verbatim
    uint64_t    content_hash = 0;                    // FNV-1a over `bytes`
    std::vector<std::string> imports;                // ImportedLibrary (field 3), in order
    std::vector<av::SclTemplateEntry> templates;     // parsed archetypes, in file order
};

// What a load/replace actually did — used to drive hot-swap side effects.
struct LibraryChange {
    bool added = false;             // library was not loaded before
    bool replaced = false;          // library existed, bytes changed
    bool bytes_changed = false;     // added || replaced
    std::string name;
    std::vector<std::string> added_templates;
    std::vector<std::string> removed_templates;
};

class LibraryManager {
public:
    // ── search paths ────────────────────────────────────────────────────────
    void add_search_path(const std::string& dir);
    const std::vector<std::string>& search_paths() const { return search_paths_; }

    // ── loading ─────────────────────────────────────────────────────────────
    // Resolve `name` via the search paths and load/refresh it. `name` may be
    // given with or without the .scl extension. Returns false when the file
    // cannot be found (the name is recorded in missing()).
    bool load_by_name(const std::string& name, LibraryChange* change = nullptr);

    // Load/refresh an explicit path. The library's own Name field wins; the file
    // stem is the fallback.
    bool load_file(const std::string& path, LibraryChange* change = nullptr);

    // Load/replace from memory (editor hot swap without touching disk). When
    // `name` is empty the ObjectLibrary's own Name field is used.
    bool load_bytes(const std::string& bytes, const std::string& name = std::string(),
                    const std::string& path = std::string(),
                    LibraryChange* change = nullptr);

    // Re-read every file-backed library and re-parse only those whose bytes
    // changed. This is the "edit an .scl on disk" hot-swap entry point.
    std::vector<LibraryChange> refresh_changed();

    // ── queries ─────────────────────────────────────────────────────────────
    bool has(const std::string& name) const;
    const Library* get(const std::string& name) const;
    std::vector<std::string> names() const;                 // insertion order
    size_t template_count() const;

    // Template lookup across loaded libraries (ObjectLibrary::TemplateForName
    // parity). Returns false when no library defines the archetype. `owner` (when
    // non-null) receives the defining library's name.
    bool find_template(const std::string& template_name, av::SclTemplateEntry* out,
                       std::string* owner = nullptr) const;

    // Monotonic counter bumped on every content change (add or replace). Clients
    // cache derived state against it.
    uint64_t generation() const { return generation_; }

    // Names that failed to resolve, in encounter order (diagnostics).
    const std::vector<std::string>& missing() const { return missing_; }

    void clear();

private:
    bool apply(const std::string& name, const std::string& bytes,
               const std::string& path, LibraryChange* change);
    std::string resolve_path(const std::string& name) const;

    std::vector<std::string> search_paths_;
    std::vector<Library>     libraries_;                 // insertion order
    std::unordered_map<std::string, size_t> index_;      // name -> libraries_ slot
    std::unordered_map<std::string, std::string> path_cache_;   // name -> resolved path
    std::vector<std::string> missing_;
    uint64_t generation_ = 0;
};

// ── free helpers (also useful to callers parsing raw .scl bytes) ────────────
std::string library_name_from_bytes(const std::string& scl_bytes);
std::vector<std::string> library_imports_from_bytes(const std::string& scl_bytes);

} // namespace caver
