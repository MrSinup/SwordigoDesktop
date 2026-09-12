// component_field_editors_test.cpp — schema-driven component field editing
// (master TODO 2.1). Exercises the exact code path the inspector's field
// editors use: scene_component_fields() enumerates the payload, the editor
// mutates a copy, scene_set_component_field() writes it back, and the
// component re-encodes with nothing lost — including an unknown binary field
// (a stand-in for fields a newer game version added).
//
// Also covers the Program (Lua) field path: scene_program_source() /
// scene_set_program_source() must round-trip the embedded source AND
// regenerate field 2 (compiled bytecode), because the shipped engine's
// Program::LoadIntoState() only ever loads field 2.
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

#include "tools/scene_loader.h"
#include "platform/protobuf_reader.h"

using av::SceneComponent;
using av::SceneComponentField;
using av::SceneData;
using av::SceneObject;

std::string g_instance_assets_dir = "assets";   // required by libswcore

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}
static void check_eq(long long got, long long want, const char* what) {
    if (got != want) {
        std::printf("FAIL: %s (got %lld want %lld)\n", what, got, want);
        ++failures;
    }
}
static void check_f32(float got, float want, const char* what) {
    if (std::fabs(got - want) > 1e-3f) {
        std::printf("FAIL: %s (got %.3f want %.3f)\n", what, (double)got, (double)want);
        ++failures;
    }
}

// CollisionShapeComponent wrapper (payload field 121) with a spread of field
// types: VARINT bool (IsGround=2), VARINT int (Collides=3), I32 float
// (MinDepth=6), printable LEN (Field 5), nested LEN (Field 4 = program-like),
// and an UNKNOWN binary LEN (Field 200 — not in any schema).
static SceneComponent make_component() {
    SceneComponent comp;
    comp.type_name = "CollisionShapeComponent";
    proto::Writer payload;
    payload.write_varint_field(2, 1);                 // IsGround
    payload.write_varint_field(3, 1);                 // Collides
    payload.write_float_field(6, 10.0f);              // MinDepth
    payload.write_string_field(5, "ground");          // printable LEN
    std::string prog;                                 // nested LEN (program-like)
    av::scene_set_program_source(prog, "print('hi')");
    payload.write_bytes_field(4, prog);
    payload.write_bytes_field(200, std::string("\x00\x01\xFF", 3));   // unknown raw
    proto::Writer wrap;
    wrap.write_bytes_field(121, payload.to_string());
    comp.raw_data = wrap.to_string();
    return comp;
}

static SceneComponentField find_field(const SceneComponent& comp, uint32_t number) {
    for (const auto& f : av::scene_component_fields(comp))
        if (f.field_number == number) return f;
    return {};
}

int main() {
    // ── Program source round trip ───────────────────────────────────────────
    {
        std::string prog;
        check(av::scene_set_program_source(prog, "print('hello')"), "program set");
        check_eq(static_cast<long long>(av::scene_program_source(prog).size()), 14LL, "program source extracted");
        check(av::scene_program_source(prog) == "print('hello')", "program source exact");
        // Field 2 is what the engine runs: it must be (re)generated, not stale.
        check(av::scene_program_bytes(prog).rfind("\x1bLua", 0) == 0,
              "program bytecode regenerated with the Lua signature");
        std::string prog2;
        av::scene_set_program_source(prog2, "x = 1");
        av::scene_set_program_source(prog2, "x = 2");   // replace, not append
        check(av::scene_program_source(prog2) == "x = 2", "program source replaced");
        check(av::scene_program_bytes(prog2).rfind("\x1bLua", 0) == 0,
              "replacement edit regenerates the bytecode");
    }

    // ── Lua compile failure: keep the source, never keep stale bytecode ─────
    {
        std::string prog;
        check(av::scene_set_program_source(prog, "print('ok')"), "valid program set");
        check(!av::scene_program_bytes(prog).empty(), "valid program has bytecode");
        std::string err;
        check(!av::scene_set_program_source(prog, "function(", &err), "syntax error is reported");
        check(!err.empty(), "syntax error message present");
        check(av::scene_program_source(prog) == "function(", "source kept on compile error");
        check(av::scene_program_bytes(prog).empty(), "stale bytecode dropped on compile error");
    }

    // ── Field enumeration + schema names ────────────────────────────────────
    SceneComponent comp = make_component();
    const auto all = av::scene_component_fields(comp);
    check(all.size() == 6, "all 6 payload fields enumerated");
    {
        bool saw_unknown = false;
        for (const auto& f : all) if (f.field_number == 200) saw_unknown = true;
        check(saw_unknown, "unknown field 200 enumerated (newer-version field)");
    }

    // ── Set every field through the editor path, then re-read ───────────────
    {
        SceneComponentField f = find_field(comp, 2);
        f.varint_value = 0;                                   // IsGround → false
        check(av::scene_set_component_field(comp, f), "set IsGround");
    }
    {
        SceneComponentField f = find_field(comp, 3);
        f.varint_value = 5;                                   // Collides → 5
        check(av::scene_set_component_field(comp, f), "set Collides");
    }
    {
        SceneComponentField f = find_field(comp, 6);
        f.float_value = 42.5f;                                // MinDepth
        check(av::scene_set_component_field(comp, f), "set MinDepth");
    }
    {
        SceneComponentField f = find_field(comp, 5);
        f.bytes_value = "lava";                               // printable LEN
        check(av::scene_set_component_field(comp, f), "set Field 5 string");
    }
    {
        SceneComponentField f = find_field(comp, 200);
        f.bytes_value = std::string("\xDE\xAD\xBE\xEF", 4);   // unknown raw edit
        check(av::scene_set_component_field(comp, f), "set unknown field 200");
    }

    {
        const auto again = av::scene_component_fields(comp);
        for (const auto& f : again) {
            switch (f.field_number) {
                case 2: check_eq(static_cast<long long>(f.varint_value), 0LL, "IsGround re-read"); break;
                case 3: check_eq(static_cast<long long>(f.varint_value), 5LL, "Collides re-read"); break;
                case 6: check_f32(f.float_value, 42.5f, "MinDepth re-read"); break;
                case 5: check(f.bytes_value == "lava", "Field 5 re-read"); break;
                case 200: check(f.bytes_value == std::string("\xDE\xAD\xBE\xEF", 4), "field 200 re-read"); break;
                default: break;
            }
        }
    }

    // ── Full round trip: scene → serialize → parse → byte-identical component
    {
        SceneData scene;
        SceneObject obj;
        obj.name = "test";
        obj.components.push_back(comp);
        scene.objects.push_back(obj);
        const std::string bytes = av::scene_serialize(scene);
        check(!bytes.empty(), "scene serialized");
        SceneData reparsed = av::scene_load_bytes(
            std::vector<uint8_t>(bytes.begin(), bytes.end()), "mem.scene");
        check(reparsed.objects.size() == 1, "round-trip keeps the object");
        if (reparsed.objects.size() == 1) {
            check(reparsed.objects[0].components.size() == 1, "round-trip keeps the component");
            if (!reparsed.objects[0].components.empty())
                check(reparsed.objects[0].components[0].raw_data == comp.raw_data,
                      "component bytes survive serialize → parse byte-for-byte");
        }
    }

    std::printf(failures == 0 ? "PASS: component field editors round-trip\n"
                              : "FAIL: %d assertion(s)\n", failures);
    return failures == 0 ? 0 : 1;
}