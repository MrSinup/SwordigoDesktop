// template_sources_test.cpp — Template Palette + template hierarchy (master
// TODO 2.3/2.4) unit tests, no Qt/GL:
//
//  * scan_template_sources finds .scl templates (name + scaling + source) and
//    .pod model assets under the roots, deduped with scene-embedded templates.
//  * scene_find_template resolves a template's object + scaling from the
//    scene's embedded ObjectLibrary bytes.
//  * scene_make_model_component builds a Model component whose Name field
//    round-trips, and scene_build_local_aabb emits Rectangle {X,Y,W,H}.
//  * scene_set_object_template / scene_override_inherited_component /
//    scene_materialize_object_template / scene_apply_clean_template implement
//    the link → override → unlink → reset lifecycle.
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "tools/scene_loader.h"
#include "tools/template_sources.h"
#include "platform/protobuf_reader.h"

using av::SceneData;
using av::SceneObject;
using av::SceneComponent;

// Required by libswcore (asset resolution global; other tests define it too).
std::string g_instance_assets_dir = "assets";

// ── wire helpers (tag>>3 field numbers, floats are WIRE_I32) ────────────────
static std::string component_bytes(const std::string& class_name, int id,
                                   const std::string& payload_bytes = {}) {
    proto::Writer w;
    w.write_string_field(1, class_name);
    w.write_varint_field(2, static_cast<uint64_t>(id));
    if (!payload_bytes.empty())
        w.write_bytes_field(101, payload_bytes);   // Model payload slot (810>>3)
    return w.to_string();
}

static std::string template_object_bytes(const std::string& name,
                                         const std::string& comp_bytes) {
    proto::Writer o;
    o.write_string_field(2, name);
    o.write_bytes_field(3, comp_bytes);
    return o.to_string();
}

// ObjectLibrary { Template { Object{...} = 1, Scaling = 2 } = 2 }
static std::string scl_bytes(const std::string& tpl_name, float scaling,
                             const std::string& object_bytes) {
    proto::Writer tpl;
    tpl.write_bytes_field(1, object_bytes);
    tpl.write_float_field(2, scaling);
    proto::Writer lib;
    lib.write_bytes_field(2, tpl.to_string());
    return lib.to_string();
}

static float f32(const std::string& bytes, uint32_t field_number) {
    proto::Reader r(bytes);
    proto::Field f;
    while (r.read_field(f))
        if (f.field_number == field_number && f.wire_type == proto::WIRE_I32)
            return f.float_val;
    return 0.0f;
}

static std::string str_field(const std::string& bytes, uint32_t field_number) {
    proto::Reader r(bytes);
    proto::Field f;
    while (r.read_field(f))
        if (f.field_number == field_number && f.wire_type == proto::WIRE_LEN)
            return f.bytes_val;
    return {};
}

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

int main() {
    // ── Fixture: an ObjectLibrary with one Model template + a pod file ─────
    const std::string root = "/tmp/ruby_template_palette_test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root + "/models", ec);

    proto::Writer model_payload;
    model_payload.write_string_field(1, "hero");   // ModelComponent.Name
    const std::string scl = scl_bytes("test_tpl", 1.5f,
                                      template_object_bytes("test_tpl",
                                          component_bytes("Model", 101, model_payload.to_string())));
    {
        std::ofstream out(root + "/handle.scl", std::ios::binary);
        out << scl;
    }
    {
        std::ofstream out(root + "/models/hero.pod", std::ios::binary);
        out << "fake-pod";
    }


    // ── scan_template_sources: finds the template AND the model ────────────
    {
        SceneData empty_scene;
        const auto entries = av::scan_template_sources({root}, empty_scene);
        bool found_tpl = false, found_model = false;
        float tpl_scaling = 0.0f;
        for (const auto& e : entries) {
            if (e.kind == av::TemplateSourceEntry::Template && e.name == "test_tpl") {
                found_tpl = true;
                tpl_scaling = e.scaling;
                check(e.source_path.find("handle.scl") != std::string::npos,
                      "template source path points at the .scl");
            }
            if (e.kind == av::TemplateSourceEntry::Model && e.name == "hero") {
                found_model = true;
                check(e.source_path.find("hero.pod") != std::string::npos,
                      "model source path points at the .pod");
            }
        }
        check(found_tpl, "palette scan found the .scl template");
        check(found_model, "palette scan found the .pod model");
        check_f32(tpl_scaling, 1.5f, "template scaling carried");
    }


    // ── scene_find_template resolves object + scaling from library bytes ───
    SceneData scene;
    scene.object_libraries.push_back(scl);
    {
        SceneObject tpl_obj;
        float scaling = 0.0f;
        check(av::scene_find_template(scene, "test_tpl", &tpl_obj, &scaling),
              "scene_find_template finds the embedded template");
        check_f32(scaling, 1.5f, "found scaling matches");
        check(tpl_obj.components.size() == 1, "template object has 1 component");
        if (tpl_obj.components.size() == 1) {
            check(av::scene_component_class_name(tpl_obj.components[0]) == "ModelComponent",
                  "template component class resolves via the Component schema");
        }
        check(!av::scene_find_template(scene, "missing_tpl", nullptr, nullptr),
              "unknown template not found");
    }

    // ── scene_make_model_component + scene_build_local_aabb ────────────────
    {
        const SceneComponent model = av::scene_make_model_component("hero");
        check(av::scene_component_class_name(model) == "ModelComponent",
              "made component class resolves via the Component schema");
        // ModelComponent.Name lives at payload field 101 → field 1 of payload.
        const std::string payload = str_field(model.raw_data, 101);
        check(str_field(payload, 1) == "hero", "made component Name round-trips");

        const std::string aabb = av::scene_build_local_aabb(-1.0f, -2.0f, 3.0f, 4.0f);
        check_f32(f32(aabb, 1), -1.0f, "aabb X = min_x");
        check_f32(f32(aabb, 2), -2.0f, "aabb Y = min_y");
        check_f32(f32(aabb, 3), 4.0f, "aabb W = max_x - min_x");
        check_f32(f32(aabb, 4), 6.0f, "aabb H = max_y - min_y");
    }

    // ── template lifecycle: link → resolve → override → materialize → reset ─
    {
        SceneObject obj;
        obj.name = "obj1";
        scene.objects.push_back(obj);

        // Link: template_name set → scene_refresh resolves its components.
        check(av::scene_set_object_template(scene, 0, "test_tpl"),
              "set_object_template accepted");
        check(scene.objects[0].template_name == "test_tpl", "template_name set");
        check(scene.objects[0].resolved_components.size() == 1,
              "resolved components pulled from the template");

        // Override one inherited component locally (keeps link + type id).
        check(av::scene_override_inherited_component(scene, 0, "ModelComponent"),
              "override inherited component");
        check(scene.objects[0].components.size() == 1, "override copied locally");
        check(scene.objects[0].components[0].type_id == 101,
              "override kept the template type id");
        check(scene.objects[0].template_name == "test_tpl",
              "link survives the override");
        check(!av::scene_override_inherited_component(scene, 0, "ModelComponent"),
              "second override rejected (already local)");

        // Materialize: unlink + full resolved set becomes local.
        check(av::scene_materialize_object_template(scene, 0),
              "materialize accepted");
        check(scene.objects[0].template_name.empty(), "template reference cleared");
        check(scene.objects[0].components.size() == 1,
              "materialized components carried locally");

        // Re-link then reset-to-template drops the override.
        check(av::scene_set_object_template(scene, 0, "test_tpl"),
              "re-linked after materialize");
        check(scene.objects[0].components.size() == 1, "override present before reset");
        check(av::scene_apply_clean_template(scene, 0), "reset accepted");
        check(scene.objects[0].components.empty(), "reset dropped local overrides");
        check(scene.objects[0].template_name == "test_tpl", "reset kept the link");

        // Materializing a clean link is legal: it copies the template's
        // components locally and drops the reference (web `unlinkTemplate`).
        check(av::scene_materialize_object_template(scene, 0),
              "materialize on clean link copies components");
        check(scene.objects[0].template_name.empty(),
              "materialize cleared the link");
        check(scene.objects[0].components.size() == 1,
              "materialize carried the template component locally");
        SceneData bare;
        bare.objects.push_back(SceneObject{});
        check(!av::scene_apply_clean_template(bare, 0),
              "reset on unlinked object returns false");
        check(!av::scene_set_object_template(bare, 99, "x"),
              "out-of-range object rejected");
    }

    std::filesystem::remove_all(root, ec);
    std::printf(failures == 0 ? "PASS: template palette scan + link/materialize helpers work\n"
                              : "FAIL: %d assertion(s)\n", failures);
    return failures == 0 ? 0 : 1;
}