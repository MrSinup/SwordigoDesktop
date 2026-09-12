// scl_studio_test.cpp — .scl (ObjectLibrary) studio mutation tests (master
// TODO 2.4a), no Qt/GL:
//
//  * scl_load_templates lists an ObjectLibrary's templates in order with their
//    scaling.
//  * scl_add_template appends a new template and leaves every existing byte
//    untouched (the original library is a strict prefix of the result) — and
//    refuses a duplicate name without modifying the bytes.
//  * scl_rename_template patches only the template object's Name field, so the
//    scaling and the object's unknown fields survive byte-exact — and refuses
//    a colliding name.
//  * scl_remove_template drops exactly the named entry, keeping the library's
//    own unknown fields.
//  * The library round-trips byte-exact through scl_save_to_file.
//
// Wire layout used throughout (matches what the game emits):
//   ObjectLibrary { string = 9, Template = 2 }
//   Template      { Object = 1, Scaling = 2 (float), extra = 5 }
//   Object        { string Name = 2, extra = 7 }
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "tools/scene_loader.h"
#include "platform/protobuf_reader.h"

using av::SceneData;
using av::SceneObject;
using av::SclTemplateEntry;

// Required by libswcore (asset resolution global; other tests define it too).
std::string g_instance_assets_dir = "assets";

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}
static void check_f32(float got, float want, const char* what) {
    if (std::fabs(got - want) > 1e-3f) {
        std::printf("FAIL: %s (got %.3f want %.3f)\n", what, (double)got, (double)want);
        ++failures;
    }
}

// ── fixture builders ────────────────────────────────────────────────────────
static std::string object_bytes(const std::string& name,
                                uint32_t extra_field = 0, uint64_t extra_value = 0) {
    proto::Writer o;
    o.write_string_field(2, name);
    if (extra_field) o.write_varint_field(extra_field, extra_value);
    return o.to_string();
}

static std::string template_entry(const std::string& obj_bytes, float scaling,
                                  uint32_t extra_field = 0, uint64_t extra_value = 0) {
    proto::Writer t;
    t.write_bytes_field(1, obj_bytes);
    t.write_float_field(2, scaling);
    if (extra_field) t.write_varint_field(extra_field, extra_value);
    return t.to_string();
}

// The library-level unknown field is written FIRST on purpose: it makes the
// "append leaves the original as a strict prefix" assertion in the add test
// meaningful (an append is only prefix-preserving if it lands at the end).
static std::string library_bytes(const std::vector<std::string>& entries,
                                 const std::string& library_extra = {}) {
    proto::Writer lib;
    if (!library_extra.empty()) lib.write_string_field(9, library_extra);
    for (const auto& e : entries) lib.write_bytes_field(2, e);
    return lib.to_string();
}

static std::string str_field(const std::string& bytes, uint32_t field_number) {
    proto::Reader r(bytes);
    proto::Field f;
    while (r.read_field(f))
        if (f.field_number == field_number && f.wire_type == proto::WIRE_LEN)
            return f.bytes_val;
    return {};
}

static bool has_field(const std::string& bytes, uint32_t field_number) {
    proto::Reader r(bytes);
    proto::Field f;
    while (r.read_field(f))
        if (f.field_number == field_number) return true;
    return false;
}

static std::vector<std::string> names_of(const std::vector<SclTemplateEntry>& entries) {
    std::vector<std::string> names;
    for (const auto& e : entries) names.push_back(e.name);
    return names;
}

static std::string join(const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += ",";
        out += v[i];
    }
    return out;
}

int main() {
    // ── Fixture: alpha(1.5) + beta(2.0); beta's object carries an unknown ──
    //    field (7), alpha's template entry carries an unknown field (5), and
    //    the library carries an unknown field (9).
    const std::string alpha_obj = object_bytes("alpha");
    const std::string beta_obj  = object_bytes("beta", /*extra_field=*/7, /*extra_value=*/7);
    const std::string alpha_tpl = template_entry(alpha_obj, 1.5f, /*extra_field=*/5, /*extra_value=*/42);
    const std::string beta_tpl  = template_entry(beta_obj, 2.0f);
    const std::string original  = library_bytes({alpha_tpl, beta_tpl}, "keepme");

    {
        const auto entries = av::scl_load_templates(original);
        check(entries.size() == 2, "library lists two templates");
        check(join(names_of(entries)) == "alpha,beta", "template order preserved");
        if (entries.size() == 2) {
            check_f32(entries[0].scaling, 1.5f, "alpha scaling");
            check_f32(entries[1].scaling, 2.0f, "beta scaling");
            check(entries[1].raw_object_bytes == beta_obj,
                  "beta raw object bytes match the fixture");
        }
        check(str_field(original, 9) == "keepme", "library unknown field present");
    }

    // ── add: append gamma(3.0) — existing bytes must be a strict prefix ────
    {
        std::string bytes = original;
        SceneObject gamma;
        gamma.name = "ignore-me";          // the helper renames to the arg
        check(av::scl_add_template(bytes, "gamma", gamma, 3.0f),
              "add_template accepted");
        check(bytes.size() > original.size(), "add_template grew the library");
        check(bytes.compare(0, original.size(), original) == 0,
              "add_template left every existing byte untouched (prefix)");

        const auto entries = av::scl_load_templates(bytes);
        check(entries.size() == 3, "three templates after add");
        check(join(names_of(entries)) == "alpha,beta,gamma", "gamma appended last");
        if (entries.size() == 3) {
            check_f32(entries[2].scaling, 3.0f, "gamma scaling carried");
            check_f32(entries[0].scaling, 1.5f, "alpha scaling survived add");
            check_f32(entries[1].scaling, 2.0f, "beta scaling survived add");
        }
        check(str_field(bytes, 9) == "keepme", "library unknown field survived add");

        // Duplicate names are refused and leave the bytes alone.
        std::string dup = bytes;
        check(!av::scl_add_template(dup, "alpha", gamma, 1.0f),
              "duplicate add_template rejected");
        check(dup == bytes, "rejected add left the bytes untouched");
    }

    // ── add onto empty bytes creates a fresh valid library ─────────────────
    {
        std::string empty;
        SceneObject fresh;
        check(av::scl_add_template(empty, "fresh", fresh, 1.0f),
              "add_template onto empty bytes accepted");
        const auto entries = av::scl_load_templates(empty);
        check(entries.size() == 1, "fresh library has one template");
        if (entries.size() == 1) {
            check(entries[0].name == "fresh", "fresh template name set");
            check_f32(entries[0].scaling, 1.0f, "fresh template default scaling");
        }
    }

    // ── rename: beta → beta2 keeps scaling + the object's unknown field ────
    {
        std::string bytes = original;
        check(av::scl_rename_template(bytes, "beta", "beta2"),
              "rename_template accepted");

        const auto entries = av::scl_load_templates(bytes);
        check(entries.size() == 2, "rename kept the template count");
        check(join(names_of(entries)) == "alpha,beta2", "rename updated the name");
        if (entries.size() == 2) {
            check_f32(entries[1].scaling, 2.0f, "rename kept the scaling");
            check(has_field(entries[1].raw_object_bytes, 7),
                  "rename kept the object's unknown field");
            check(str_field(entries[1].raw_object_bytes, 2) == "beta2",
                  "renamed object carries the new name");
            check(entries[0].raw_object_bytes == alpha_obj,
                  "rename left the other template byte-identical");
        }
        check(str_field(bytes, 9) == "keepme", "library unknown field survived rename");

        // Collision + missing name are refused, bytes untouched.
        std::string collision = bytes;
        check(!av::scl_rename_template(collision, "beta2", "alpha"),
              "rename onto an existing name rejected");
        check(collision == bytes, "rejected rename left the bytes untouched");
        std::string missing = bytes;
        check(!av::scl_rename_template(missing, "nope", "x"),
              "rename of an unknown template rejected");
        check(missing == bytes, "unknown rename left the bytes untouched");
    }

    // ── remove: drop alpha, keep beta + the library's unknown field ────────
    {
        std::string bytes = original;
        check(av::scl_remove_template(bytes, "alpha"), "remove_template accepted");

        const auto entries = av::scl_load_templates(bytes);
        check(entries.size() == 1, "one template after remove");
        check(join(names_of(entries)) == "beta", "removed the right template");
        if (entries.size() == 1) {
            check_f32(entries[0].scaling, 2.0f, "surviving template kept its scaling");
            check(entries[0].raw_object_bytes == beta_obj,
                  "surviving template kept its exact object bytes");
        }
        check(str_field(bytes, 9) == "keepme", "library unknown field survived remove");

        std::string missing = bytes;
        check(!av::scl_remove_template(missing, "ghost"),
              "remove of an unknown template rejected");
        check(missing == bytes, "unknown remove left the bytes untouched");
    }

    // ── full studio round trip: add + rename + save + reload ───────────────
    {
        std::string bytes = original;
        SceneObject delta;
        check(av::scl_add_template(bytes, "delta", delta, 0.5f), "studio add");
        check(av::scl_rename_template(bytes, "alpha", "alpha_renamed"), "studio rename");

        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / "ruby_scl_studio_test";
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        const std::string path = (dir / "studio.scl").string();

        std::string err;
        check(av::scl_save_to_file(path, bytes, &err), "scl_save_to_file wrote");

        std::ifstream in(path, std::ios::binary);
        std::string on_disk((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        check(on_disk == bytes, "saved file is byte-identical to the buffer");

        const auto reloaded = av::scl_load_templates(on_disk);
        check(join(names_of(reloaded)) == "alpha_renamed,beta,delta",
              "studio edits survive a save/reload round trip");
        if (reloaded.size() == 3) {
            check_f32(reloaded[0].scaling, 1.5f, "renamed template scaling survives");
            check_f32(reloaded[2].scaling, 0.5f, "added template scaling survives");
        }

        std::filesystem::remove_all(dir, ec);
    }

    std::printf(failures == 0
                    ? "PASS: SCL studio add/rename/remove preserve bytes and round trip\n"
                    : "FAIL: %d assertion(s)\n",
                failures);
    return failures == 0 ? 0 : 1;
}
