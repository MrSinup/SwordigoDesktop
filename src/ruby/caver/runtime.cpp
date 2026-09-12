#include "ruby/caver/runtime.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "ruby/caver/library_manager.h"
#include "ruby/caver/program_host.h"
#include "tools/scene_schemas.h"

namespace caver {

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;

// Nested Vector2/Vector3 payloads are fixed32 triples (X, Y[, Z]).
void read_vec(const std::string& bytes, float* out, int want) {
    for (int i = 0; i < want; ++i) out[i] = 0.0f;
    if (bytes.empty()) return;
    try {
        proto::Reader reader(bytes);
        proto::Field field;
        int n = 0;
        while (reader.read_field(field) && n < want) {
            if (field.wire_type == proto::WIRE_I32)      out[n++] = field.float_val;
            else if (field.wire_type == proto::WIRE_I64) out[n++] = static_cast<float>(field.double_val);
        }
    } catch (...) {}
}

// A payload field is a cross-component reference when the recovered schema names
// it `<something>Id` and it is an integer.
bool is_reference_field(const std::string& name, proto::WireType wire) {
    if (wire != proto::WIRE_VARINT) return false;
    return name.size() > 2 && name.compare(name.size() - 2, 2, "Id") == 0;
}

// Program.String / Program.Bytes — field 1 and field 2 of the Program message.
bool split_program(const std::string& program_bytes, std::string* source, std::string* bytecode) {
    try {
        proto::Reader reader(program_bytes);
        proto::Field field;
        while (reader.read_field(field)) {
            if (field.wire_type != proto::WIRE_LEN) continue;
            if (field.field_number == 1) *source = field.bytes_val;
            else if (field.field_number == 2) *bytecode = field.bytes_val;
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool looks_like_ai_loop(const std::string& source) {
    std::string compact;
    compact.reserve(source.size());
    for (char c : source)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') compact.push_back(c);
    return compact.find("whiletrue") != std::string::npos;
}

} // namespace

// ── RuntimeComponent ────────────────────────────────────────────────────────
bool RuntimeComponent::is(const std::string& cls) const {
    if (cls == class_name) return true;
    if (type && cls == type->class_name) return true;
    if (type && cls == type->short_name) return true;
    return false;
}

const RuntimeField* RuntimeComponent::field(const std::string& name) const {
    for (const auto& f : fields)
        if (f.name == name) return &f;
    return nullptr;
}

float RuntimeComponent::number(const std::string& name, float fallback) const {
    const RuntimeField* f = field(name);
    if (!f) return fallback;
    switch (f->wire) {
        case proto::WIRE_VARINT: return static_cast<float>(f->varint_value);
        case proto::WIRE_I32:    return f->float_value;
        case proto::WIRE_I64:    return static_cast<float>(f->double_value);
        default:                 return fallback;
    }
}

// ── RuntimeObject ───────────────────────────────────────────────────────────
RuntimeComponent* RuntimeObject::find(int32_t component_identifier) {
    for (auto& c : components)
        if (c.identifier == component_identifier) return &c;
    return nullptr;
}

const RuntimeComponent* RuntimeObject::find(int32_t component_identifier) const {
    return const_cast<RuntimeObject*>(this)->find(component_identifier);
}

RuntimeComponent* RuntimeObject::find_class(const std::string& class_name) {
    for (auto& c : components)
        if (c.is(class_name)) return &c;
    return nullptr;
}

const RuntimeComponent* RuntimeObject::find_class(const std::string& class_name) const {
    return const_cast<RuntimeObject*>(this)->find_class(class_name);
}

// ── RuntimeScene ────────────────────────────────────────────────────────────
void RuntimeScene::clear() {
    objects_.clear();
    by_identifier_.clear();
    render_.clear();
    programs_.clear();
    warnings_.clear();
    events_.clear();
    clock_ = 0.0;
}

bool RuntimeScene::load(const av::SceneData& scene) {
    clear();

    // Head-room for scripted spawns (Scene.CreateObject, drops, casts): a growth
    // reallocation mid-program-pass would invalidate object pointers.
    objects_.reserve(scene.objects.size() + 256);
    for (size_t i = 0; i < scene.objects.size(); ++i)
        build_object(scene.objects[i], i);

    for (auto& object : objects_) {
        resolve_references(object);
        // Recovered behaviour state (controllers, physics, health, triggers …).
        behaviour_init(*this, object);
        // Programs are owned by the scene (one ProgramState each), not the object.
        for (const auto& program : object.programs) programs_.push_back(program);
    }

    render_.resize(objects_.size());
    for (size_t i = 0; i < objects_.size(); ++i) {
        ObjectRenderState& state = render_[i];
        RuntimeObject& object = objects_[i];
        state.identifier = object.identifier;
        state.pos[0] = object.pos[0];
        state.pos[1] = object.pos[1];
        state.pos[2] = object.pos[2];
        state.rot_y = object.rot[1];
        state.scale = object.scale[0];
        state.hidden = object.hidden;
        state.facing = object.behaviour.facing;
        state.on_ground = object.behaviour.physics.on_ground;
        state.light_radius = object.behaviour.light_radius;
        state.light_intensity = object.behaviour.light_intensity;
        state.light_color[0] = object.behaviour.light_color[0];
        state.light_color[1] = object.behaviour.light_color[1];
        state.light_color[2] = object.behaviour.light_color[2];
        object.base_pos[0] = object.pos[0];
        object.base_pos[1] = object.pos[1];
        object.base_pos[2] = object.pos[2];
    }

    // Start the scene's Lua programs on the real VM, if a host is attached.
    if (program_host_) program_host_->start(*this);
    return true;
}

void RuntimeScene::reindex() {
    by_identifier_.clear();
    for (size_t i = 0; i < objects_.size(); ++i)
        by_identifier_[objects_[i].identifier] = i;
}

RuntimeObject* RuntimeScene::hero() {
    auto it = by_identifier_.find(hero_identifier_);
    if (it != by_identifier_.end()) return &objects_[it->second];
    // A scene without an object literally named "hero" still has one: the object
    // carrying HeroEntityComponent.
    for (auto& object : objects_)
        if (object.find_class("HeroEntityComponent")) return &object;
    return nullptr;
}

const RuntimeObject* RuntimeScene::hero() const { return const_cast<RuntimeScene*>(this)->hero(); }

RuntimeObject* RuntimeScene::spawn(const std::string& template_name, const std::string& identifier,
                                   const float position[3]) {
    if (!libraries_) {
        warnings_.push_back("spawn('" + template_name + "') without an ObjectLibrary registry");
        return nullptr;
    }
    av::SclTemplateEntry entry;
    std::string owner;
    if (!libraries_->find_template(template_name, &entry, &owner)) {
        warnings_.push_back("spawn('" + template_name + "'): no template in any loaded library");
        return nullptr;
    }

    RuntimeObject object;
    object.identifier = identifier;
    object.template_name = template_name;
    object.template_scaling = entry.scaling;
    if (position) {
        object.pos[0] = position[0];
        object.pos[1] = position[1];
        object.pos[2] = position[2];
    }
    instantiate(entry.object, object);
    object.activated = !object.components.empty();

    if (by_identifier_.count(object.identifier)) {
        warnings_.push_back("spawn('" + template_name + "'): duplicate identifier '" + identifier + "'");
        return nullptr;
    }
    objects_.push_back(std::move(object));
    const size_t index = objects_.size() - 1;
    by_identifier_[objects_[index].identifier] = index;
    resolve_references(objects_[index]);
    behaviour_init(*this, objects_[index]);
    for (const auto& program : objects_[index].programs) programs_.push_back(program);
    render_.resize(objects_.size());
    render_[index].identifier = objects_[index].identifier;
    if (program_host_) program_host_->start_object(*this, objects_[index]);
    return &objects_[index];
}

void RuntimeScene::instantiate(const av::SceneObject& src, RuntimeObject& object) {
    // Same reconcile as build_object: the instance's own components with template
    // components folded in (SceneObject::InitWithTemplate).
    const auto& source = src.resolved_components.empty() ? src.components : src.resolved_components;
    for (const auto& component : source)
        decode_component(component, object);
    if (object.pos[0] == 0.0f && object.pos[1] == 0.0f && object.pos[2] == 0.0f) {
        object.pos[0] = src.pos_x; object.pos[1] = src.pos_y; object.pos[2] = src.pos_z;
        object.rot[1] = src.rot_y;
        object.scale[0] = src.scale_x;
    }
}

void RuntimeScene::build_object(const av::SceneObject& src, size_t index) {
    RuntimeObject object;
    object.identifier = src.name.empty() ? ("obj#" + std::to_string(index)) : src.name;
    object.template_name = src.template_name;
    object.pos[0] = src.pos_x; object.pos[1] = src.pos_y; object.pos[2] = src.pos_z;
    object.rot[0] = src.rot_x; object.rot[1] = src.rot_y; object.rot[2] = src.rot_z;
    object.scale[0] = src.scale_x; object.scale[1] = src.scale_y; object.scale[2] = src.scale_z;
    object.template_scaling = src.template_scaling;
    object.hidden = src.hidden;
    object.base_pos[0] = object.pos[0];
    object.base_pos[1] = object.pos[1];
    object.base_pos[2] = object.pos[2];

    instantiate(src, object);

    // An object with a template link is active once it has components; objects
    // whose template failed to resolve stay inactive (never ticked), which is
    // how the engine treats a missing ObjectLibrary.
    object.activated = !object.components.empty();
    if (object.components.empty() && !src.template_name.empty())
        warnings_.push_back("object '" + object.identifier + "': template '" +
                            src.template_name + "' resolved to no components");

    if (by_identifier_.count(object.identifier))
        warnings_.push_back("duplicate object identifier '" + object.identifier + "'");
    by_identifier_[object.identifier] = objects_.size();
    objects_.push_back(std::move(object));
}

void RuntimeScene::decode_component(const av::SceneComponent& src, RuntimeObject& owner) {
    RuntimeComponent component;
    component.class_name = src.type_name;
    component.raw = src.raw_data;
    component.type = src.payload_field ? component_type_by_field(static_cast<uint32_t>(src.payload_field))
                                       : component_type_by_short(src.type_name);

    const uint32_t payload_field = src.payload_field ? static_cast<uint32_t>(src.payload_field) : 0;

    try {
        proto::Reader reader(src.raw_data);
        proto::Field field;
        while (reader.read_field(field)) {
            if (field.wire_type != proto::WIRE_LEN) {
                if (field.field_number == 2) component.identifier = static_cast<int32_t>(field.varint_val);
                else if (field.field_number == 4) component.parent_identifier = static_cast<int32_t>(field.varint_val);
                continue;
            }
            if (field.field_number == 1)      component.class_name = field.bytes_val;
            else if (field.field_number == 3) component.label = field.bytes_val;
            else if (payload_field && field.field_number == payload_field) component.payload = field.bytes_val;
        }
    } catch (...) {
        warnings_.push_back("object '" + owner.identifier + "': malformed component bytes");
    }

    // Decode the payload against the recovered schema so refs and program fields
    // are named. Unknown fields stay in `fields` with an empty name.
    const std::string schema_class = component.type ? component.type->class_name
                                                    : src.type_name + "Component";
    const av::SchemaClass* schema = nullptr;
    {
        auto it = av::g_schemas.find(schema_class);
        if (it != av::g_schemas.end()) schema = &it->second;
    }

    if (!component.payload.empty()) {
        try {
            proto::Reader reader(component.payload);
            proto::Field field;
            while (reader.read_field(field)) {
                RuntimeField decoded;
                decoded.tag = (field.field_number << 3) | static_cast<uint32_t>(field.wire_type);
                decoded.wire = field.wire_type;
                decoded.varint_value = field.varint_val;
                decoded.double_value = field.double_val;
                decoded.float_value = field.float_val;
                decoded.bytes_value = field.bytes_val;
                if (schema) {
                    auto it = schema->fields.find(decoded.tag);
                    if (it != schema->fields.end()) decoded.name = it->second.name;
                }
                if (is_reference_field(decoded.name, field.wire_type)) {
                    ComponentRef ref;
                    ref.tag = decoded.tag;
                    ref.field_name = decoded.name;
                    ref.target = static_cast<int32_t>(field.varint_val);
                    component.refs.push_back(ref);
                }
                component.fields.push_back(std::move(decoded));
            }
        } catch (...) {
            warnings_.push_back("object '" + owner.identifier + "': malformed " + schema_class + " payload");
        }
    }

    // Collect embedded Lua: ProgramComponent's `Program`, plus every handler
    // field (OnKill / OnHurt / OnActivate / OnCollide / OnLoad / OnCast …).
    for (const auto& decoded : component.fields) {
        if (decoded.name.empty()) continue;
        if (schema) {
            auto it = schema->fields.find(decoded.tag);
            if (it == schema->fields.end() || it->second.class_name != "Program") continue;
        } else {
            continue;
        }
        collect_program(owner, component, decoded.name, decoded.bytes_value);
    }

    // The object's own OnLoad program (SceneObject field 10) is handled by the
    // loader, not the component decode; nothing to do here.

    if (!component.type)
        warnings_.push_back("object '" + owner.identifier + "': unknown component class '" +
                            component.class_name + "' (newer game version?)");

    owner.components.push_back(std::move(component));
}

void RuntimeScene::collect_program(RuntimeObject& owner, const RuntimeComponent& comp,
                                   const std::string& field_name, const std::string& program_bytes) {
    RuntimeProgram program;
    program.owner_identifier = comp.identifier;
    program.owner_class = comp.class_name;
    program.field_name = field_name;
    if (!split_program(program_bytes, &program.source, &program.bytecode)) return;
    if (program.source.empty() && program.bytecode.empty()) return;
    program.keep_active = looks_like_ai_loop(program.source);
    owner.programs.push_back(std::move(program));
}

void RuntimeScene::resolve_references(RuntimeObject& object) {
    for (auto& component : object.components) {
        for (auto& ref : component.refs) {
            ref.resolved = object.find(ref.target) != nullptr;
            if (!ref.resolved)
                warnings_.push_back("object '" + object.identifier + "': " + component.class_name +
                                    "." + ref.field_name + " -> " + std::to_string(ref.target) +
                                    " is dangling");
        }
    }
}

void RuntimeScene::tick(RuntimeObject& object, float dt) {
    if (!object.activated) return;

    // The recovered behaviours (behaviour.cpp) own every per-component clock and
    // transition; this function only drives them and republishes render state.
    // Scripts run in a separate pass (RuntimeScene::update) so this loop can
    // never be re-entered by a spawn.
    behaviour_tick(*this, object, dt);

    ObjectRenderState* state = nullptr;
    for (auto& s : render_)
        if (s.identifier == object.identifier) { state = &s; break; }
    if (!state) return;
    state->pos[0] = object.pos[0];
    state->pos[1] = object.pos[1];
    state->pos[2] = object.pos[2];
    state->rot_y = object.rot[1];
    state->scale = object.scale[0] * object.template_scaling;
    state->hidden = object.hidden;
    state->facing = object.behaviour.facing;
    state->on_ground = object.behaviour.physics.on_ground;
    state->anim_name = object.behaviour.anim.current;
    if (object.behaviour.anim.running) state->anim_time = object.behaviour.anim.time;
    state->emitter_time = object.behaviour.emitter.time;
    state->water_time = object.behaviour.water_time;
    state->light_radius = object.behaviour.light_radius;
    state->light_intensity = object.behaviour.light_intensity;
    for (int i = 0; i < 3; ++i) state->light_color[i] = object.behaviour.light_color[i];
    for (int i = 0; i < 3; ++i) state->light_offset[i] = object.behaviour.light_offset[i];

    // SimpleGlow pulse: 1 + PulseAmount * sin(2π t / PulseTime) at PulseTime > 0.
    const float amount = object.behaviour.glow_pulse_amount;
    const float period = object.behaviour.glow_pulse_time;
    state->light_pulse = (amount != 0.0f && period > 0.0f)
        ? 1.0f + amount * std::sin(kTwoPi * object.behaviour.emitter.time / period)
        : 1.0f;
}

void RuntimeScene::update(float dt) {
    clock_ += dt;

    // Pass 1 — every C++ component behaviour (behaviour.cpp). No scripting runs
    // here, so the object collection cannot be mutated mid-iteration.
    for (size_t i = 0; i < objects_.size(); ++i)
        tick(objects_[i], dt);

    // Pass 2 — PROGRAM_UPDATE: the real Lua VM. `count` is snapshotted, so an
    // object spawned by a script is only scripted from the next frame on (the
    // same rule the engine uses for freshly created objects).
    if (program_host_) {
        program_host_->sync_clock(clock_);
        const size_t count = objects_.size();
        for (size_t i = 0; i < count; ++i) {
            RuntimeObject* object = this->object(objects_[i].identifier);
            if (object) program_host_->update_object(*this, *object, dt);
        }
        // Pass 3 — events raised by the C++ behaviours this frame, then the
        // spawns (Scene.CreateObject, drops, casts) the handlers requested.
        program_host_->pump(*this);
    }
}

RuntimeObject* RuntimeScene::object(const std::string& identifier) {
    auto it = by_identifier_.find(identifier);
    return it == by_identifier_.end() ? nullptr : &objects_[it->second];
}

const RuntimeObject* RuntimeScene::object(const std::string& identifier) const {
    return const_cast<RuntimeScene*>(this)->object(identifier);
}

size_t RuntimeScene::component_count() const {
    size_t total = 0;
    for (const auto& object : objects_) total += object.components.size();
    return total;
}

std::vector<std::string> RuntimeScene::unrecovered_classes() const {
    std::unordered_set<std::string> missing;
    for (const auto& object : objects_) {
        for (const auto& component : object.components) {
            if (!component.type) { missing.insert(component.class_name); continue; }
            if (!has(component.type->stages, Stage::Updated))
                missing.insert(component.type->class_name);
        }
    }
    std::vector<std::string> out(missing.begin(), missing.end());
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace caver
