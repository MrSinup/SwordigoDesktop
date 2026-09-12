#include "ruby/caver/library_manager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <unordered_set>

#include "platform/protobuf_reader.h"

namespace fs = std::filesystem;

namespace caver {

namespace {

uint64_t fnv1a(const std::string& bytes) {
    uint64_t hash = 1469598103934665603ULL;   // FNV offset basis
    for (unsigned char c : bytes) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;             // FNV prime
    }
    return hash;
}

std::string lowercase(std::string value) {
    for (char& ch : value)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

std::string strip_scl_extension(std::string name) {
    if (name.size() > 4) {
        const std::string tail = lowercase(name.substr(name.size() - 4));
        if (tail == ".scl") name.resize(name.size() - 4);
    }
    return name;
}

bool read_file(const fs::path& path, std::string* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// A directory we should not descend into while resolving a library by name.
bool skip_directory(const std::string& filename) {
    if (filename.empty()) return false;
    if (filename[0] == '.') return true;
    return filename == "build" || filename == "bin" || filename == "binw" ||
           filename == "CMakeFiles" || filename == "node_modules" ||
           filename == "deps" || filename == "dist";
}

} // namespace

// ── ObjectLibrary byte parsing ──────────────────────────────────────────────
// ObjectLibrary { Name = 1 (string), Template = 2 (repeated), ImportedLibrary = 3 }
std::string library_name_from_bytes(const std::string& scl_bytes) {
    try {
        proto::Reader reader(scl_bytes);
        proto::Field field;
        while (reader.read_field(field)) {
            if (field.field_number == 1 && field.wire_type == proto::WIRE_LEN)
                return field.bytes_val;
        }
    } catch (...) {}
    return {};
}

std::vector<std::string> library_imports_from_bytes(const std::string& scl_bytes) {
    std::vector<std::string> names;
    try {
        proto::Reader reader(scl_bytes);
        proto::Field field;
        while (reader.read_field(field)) {
            if (field.field_number == 3 && field.wire_type == proto::WIRE_LEN)
                names.push_back(field.bytes_val);
        }
    } catch (...) {}
    return names;
}

// ── search paths ────────────────────────────────────────────────────────────
void LibraryManager::add_search_path(const std::string& dir) {
    if (dir.empty()) return;
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(dir, ec);
    if (ec || canonical.empty()) canonical = fs::path(dir);
    const std::string value = canonical.string();
    if (std::find(search_paths_.begin(), search_paths_.end(), value) == search_paths_.end())
        search_paths_.push_back(value);
}

std::string LibraryManager::resolve_path(const std::string& raw_name) const {
    const std::string name = strip_scl_extension(raw_name);
    if (name.empty()) return {};

    // A cached path that still exists wins (and is order-stable across calls).
    auto cached = path_cache_.find(name);
    if (cached != path_cache_.end()) {
        std::error_code ec;
        if (fs::is_regular_file(cached->second, ec)) return cached->second;
    }

    const std::string target = lowercase(name + ".scl");

    // 1. Direct hit inside a search root.
    for (const auto& root : search_paths_) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        for (const char* suffix : {".scl", ".SCL"}) {
            fs::path candidate = fs::path(root) / (name + suffix);
            if (fs::is_regular_file(candidate, ec)) {
                const_cast<LibraryManager*>(this)->path_cache_[name] = candidate.string();
                return candidate.string();
            }
        }
    }

    // 2. Bounded recursive scan.
    for (const auto& root : search_paths_) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             it != end && !ec; it.increment(ec)) {
            if (it.depth() > 4) { it.disable_recursion_pending(); continue; }
            if (it->is_directory(ec)) {
                if (skip_directory(it->path().filename().string()))
                    it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(ec)) continue;
            const fs::path& p = it->path();
            if (lowercase(p.extension().string()) == ".scl" &&
                lowercase(p.filename().string()) == target) {
                const_cast<LibraryManager*>(this)->path_cache_[name] = p.string();
                return p.string();
            }
        }
    }
    return {};
}

// ── loading ─────────────────────────────────────────────────────────────────
bool LibraryManager::apply(const std::string& requested_name, const std::string& bytes,
                           const std::string& path, LibraryChange* change) {
    const uint64_t hash = fnv1a(bytes);

    // Identity: the ObjectLibrary's own Name wins; the file stem / caller hint is
    // the fallback. This is what makes `import "crypt.scl"` and `import crypt`
    // resolve to the same library.
    std::string name = library_name_from_bytes(bytes);
    if (name.empty()) name = requested_name;
    if (name.empty() && !path.empty()) name = fs::path(path).stem().string();
    if (name.empty()) return false;

    auto it = index_.find(name);
    if (it != index_.end()) {
        Library& existing = libraries_[it->second];
        if (existing.content_hash == hash && existing.path == path) {
            if (change) {
                change->name = name;
                change->added = false;
                change->replaced = false;
                change->bytes_changed = false;
            }
            return true;   // nothing to do — this is the fast path
        }

        std::vector<std::string> before;
        before.reserve(existing.templates.size());
        for (const auto& t : existing.templates) before.push_back(t.name);

        existing.path = path;
        existing.bytes = bytes;
        existing.content_hash = hash;
        existing.imports = library_imports_from_bytes(bytes);
        existing.templates = av::scl_load_templates(bytes);

        ++generation_;
        if (change) {
            change->name = name;
            change->added = false;
            change->replaced = true;
            change->bytes_changed = true;
            for (const auto& t : existing.templates)
                if (std::find(before.begin(), before.end(), t.name) == before.end())
                    change->added_templates.push_back(t.name);
            for (const auto& old_name : before) {
                bool still_there = false;
                for (const auto& t : existing.templates)
                    if (t.name == old_name) { still_there = true; break; }
                if (!still_there) change->removed_templates.push_back(old_name);
            }
        }
        return true;
    }

    Library library;
    library.name = name;
    library.path = path;
    library.bytes = bytes;
    library.content_hash = hash;
    library.imports = library_imports_from_bytes(bytes);
    library.templates = av::scl_load_templates(bytes);

    index_[name] = libraries_.size();
    libraries_.push_back(std::move(library));
    ++generation_;

    if (change) {
        change->name = name;
        change->added = true;
        change->replaced = false;
        change->bytes_changed = true;
        for (const auto& t : libraries_.back().templates)
            change->added_templates.push_back(t.name);
    }
    return true;
}

bool LibraryManager::load_bytes(const std::string& bytes, const std::string& name,
                                const std::string& path, LibraryChange* change) {
    if (bytes.empty()) return false;
    return apply(name, bytes, path, change);
}

bool LibraryManager::load_file(const std::string& path, LibraryChange* change) {
    std::string bytes;
    if (!read_file(path, &bytes) || bytes.empty()) return false;
    return apply(fs::path(path).stem().string(), bytes, path, change);
}

bool LibraryManager::load_by_name(const std::string& raw_name, LibraryChange* change) {
    const std::string name = strip_scl_extension(raw_name);
    if (name.empty()) return false;

    const std::string path = resolve_path(name);
    if (path.empty()) {
        if (std::find(missing_.begin(), missing_.end(), name) == missing_.end())
            missing_.push_back(name);
        if (change) { change->name = name; }
        return false;
    }

    std::string bytes;
    if (!read_file(path, &bytes) || bytes.empty()) return false;
    return apply(name, bytes, path, change);
}

std::vector<LibraryChange> LibraryManager::refresh_changed() {
    std::vector<LibraryChange> changes;
    // Re-read every file-backed library. Reading is cheap; the content hash gate
    // means the (expensive) template re-parse only runs on a real byte change.
    for (size_t i = 0; i < libraries_.size(); ++i) {
        const size_t slot = i;
        if (libraries_[slot].path.empty()) continue;   // in-memory: nothing to watch
        std::string bytes;
        if (!read_file(libraries_[slot].path, &bytes) || bytes.empty()) continue;
        if (fnv1a(bytes) == libraries_[slot].content_hash) continue;

        const std::string name = libraries_[slot].name;
        const std::string path = libraries_[slot].path;
        LibraryChange change;
        if (apply(name, bytes, path, &change) && change.bytes_changed)
            changes.push_back(std::move(change));
    }
    return changes;
}

// ── queries ─────────────────────────────────────────────────────────────────
bool LibraryManager::has(const std::string& name) const {
    return index_.find(name) != index_.end();
}

const Library* LibraryManager::get(const std::string& name) const {
    auto it = index_.find(name);
    return it == index_.end() ? nullptr : &libraries_[it->second];
}

std::vector<std::string> LibraryManager::names() const {
    std::vector<std::string> out;
    out.reserve(libraries_.size());
    for (const auto& lib : libraries_) out.push_back(lib.name);
    return out;
}

size_t LibraryManager::template_count() const {
    size_t total = 0;
    for (const auto& lib : libraries_) total += lib.templates.size();
    return total;
}

bool LibraryManager::find_template(const std::string& template_name,
                                   av::SclTemplateEntry* out,
                                   std::string* owner) const {
    if (template_name.empty()) return false;
    for (const auto& lib : libraries_) {
        for (const auto& entry : lib.templates) {
            if (entry.name != template_name) continue;
            if (out) *out = entry;
            if (owner) *owner = lib.name;
            return true;
        }
    }
    return false;
}

void LibraryManager::clear() {
    search_paths_.clear();
    libraries_.clear();
    index_.clear();
    path_cache_.clear();
    missing_.clear();
    generation_ = 0;
}

} // namespace caver
