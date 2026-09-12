#include "ruby/caver/visual.h"

#include <cmath>

#include "ruby/caver/runtime.h"

namespace caver {

namespace {

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

void read_vec_field(const RuntimeComponent& c, const char* name, float* out, int want) {
    if (const RuntimeField* f = c.field(name)) read_vec(f->bytes_value, out, want);
}

std::string bytes_field(const RuntimeComponent& c, const char* name) {
    const RuntimeField* f = c.field(name);
    return f ? f->bytes_value : std::string();
}

void read_color(const RuntimeComponent& c, const char* name, float* rgba) {
    rgba[0] = rgba[1] = rgba[2] = 1.0f;
    rgba[3] = 1.0f;
    if (const RuntimeField* f = c.field(name)) {
        float rgb[3] = {1, 1, 1};
        read_vec(f->bytes_value, rgb, 3);
        rgba[0] = rgb[0]; rgba[1] = rgb[1]; rgba[2] = rgb[2];
    }
}

VisualItem base_item(const RuntimeObject& object, const RuntimeComponent& c, VisualKind kind) {
    VisualItem item;
    item.kind = kind;
    item.owner = object.identifier;
    item.component = c.identifier;
    item.hidden = object.hidden;
    item.anim_time = object.behaviour.anim.time;
    item.water_time = object.behaviour.water_time;
    return item;
}

} // namespace

std::vector<VisualItem> extract_visuals(const RuntimeObject& object, const ObjectRenderState* state) {
    std::vector<VisualItem> items;

    for (const RuntimeComponent& c : object.components) {
        if (c.is("ModelComponent")) {
            VisualItem item = base_item(object, c, VisualKind::Model);
            item.asset = bytes_field(c, "Name");
            read_color(c, "DiffuseColor", item.color);
            read_vec_field(c, "Origin", item.pos, 3);
            item.rot[1] = c.number("YRotation", 0.0f) + c.number("XRotation", 0.0f);
            item.transparent = [&] {
                const RuntimeField* f = c.field("Transparent");
                return f && f->varint_value != 0;
            }();
            items.push_back(std::move(item));
        } else if (c.is("SpriteComponent")) {
            VisualItem item = base_item(object, c, VisualKind::Sprite);
            item.asset = bytes_field(c, "TextureName");
            item.depth = object.pos[2];
            items.push_back(std::move(item));
        } else if (c.is("ParticleComponent")) {
            VisualItem item = base_item(object, c, VisualKind::Particle);
            item.asset = bytes_field(c, "TextureName");
            item.scale[0] = item.scale[1] = c.number("Size", 1.0f);
            items.push_back(std::move(item));
        } else if (c.is("ParticleEmitterComponent") || c.is("FireEmitterComponent") ||
                   c.is("FireBreathComponent")) {
            VisualItem item = base_item(object, c, VisualKind::Particle);
            item.asset = bytes_field(c, "TextureName");
            if (const RuntimeField* f = c.field("ParticleId")) {
                const RuntimeComponent* particle = object.find(static_cast<int32_t>(f->varint_value));
                if (particle) {
                    item.asset = bytes_field(*particle, "TextureName");
                    item.scale[0] = item.scale[1] = particle->number("Size", 1.0f);
                }
            }
            read_color(c, "Color", item.color);
            read_vec_field(c, "Origin", item.pos, 3);
            if (item.pos[0] == 0.0f && item.pos[1] == 0.0f && item.pos[2] == 0.0f)
                read_vec_field(c, "Origin3", item.pos, 3);
            item.additive = true;
            item.radius = c.number("ParticleSpread", 1.0f);
            item.anim_time = object.behaviour.emitter.time;
            items.push_back(std::move(item));
        } else if (c.is("LightComponent")) {
            VisualItem item = base_item(object, c, VisualKind::Light);
            item.intensity = c.number("Intensity", 1.0f);
            item.radius = c.number("Radius", 0.0f);
            item.pulse = state ? state->light_pulse : 1.0f;
            read_color(c, "Color", item.color);
            read_vec_field(c, "Offset", item.pos, 3);
            items.push_back(std::move(item));
        } else if (c.is("SimpleGlowComponent")) {
            VisualItem item = base_item(object, c, VisualKind::Glow);
            item.radius = c.number("Size", 1.0f);
            item.depth = c.number("Depth", 0.0f);
            item.additive = true;
            item.pulse = state ? state->light_pulse : 1.0f;
            read_color(c, "Color", item.color);
            read_vec_field(c, "Offset", item.pos, 2);
            items.push_back(std::move(item));
        } else if (c.is("ShadowComponent")) {
            VisualItem item = base_item(object, c, VisualKind::Shadow);
            item.scale[0] = c.number("WidthRadius", 0.0f);
            item.scale[1] = c.number("DepthRadius", 0.0f);
            read_vec_field(c, "Offset", item.pos, 3);
            items.push_back(std::move(item));
        } else if (c.is("GroundPolygonComponent")) {
            VisualItem item = base_item(object, c, VisualKind::Ground);
            item.asset = bytes_field(c, "TextureName");
            items.push_back(std::move(item));
        } else if (c.is("GroundMeshComponent")) {
            VisualItem item = base_item(object, c, VisualKind::Ground);
            item.asset = bytes_field(c, "Mesh");
            read_color(c, "Color", item.color);
            // GroundMesh carries its mesh inline in the scene data (Mesh field).
            item.transparent = [&] {
                const RuntimeField* f = c.field("Transparent");
                return f && f->varint_value != 0;
            }();
            items.push_back(std::move(item));
        } else if (c.is("WaterMeshComponent")) {
            VisualItem item = base_item(object, c, VisualKind::Water);
            read_color(c, "SurfaceColor", item.color);
            item.water_time = object.behaviour.water_time;
            item.transparent = true;
            items.push_back(std::move(item));
        } else if (c.is("BackgroundComponent")) {
            VisualItem item = base_item(object, c, VisualKind::Background);
            item.asset = bytes_field(c, "TextureName");
            item.depth = object.pos[2];
            items.push_back(std::move(item));
        } else if (c.is("OverlayTextComponent")) {
            VisualItem item = base_item(object, c, VisualKind::Overlay);
            item.asset = bytes_field(c, "Text");
            read_vec_field(c, "TextOffset", item.pos, 2);
            items.push_back(std::move(item));
        } else if (c.is("ShapeComponent") || c.is("CollisionShapeComponent")) {
            VisualItem item = base_item(object, c, VisualKind::Shape);
            if (const RuntimeField* rect = c.field("Rectangle")) {
                float r[4] = {0, 0, 0, 0};
                read_vec(rect->bytes_value, r, 4);
                for (int i = 0; i < 4; ++i) item.shape_rect[i] = r[i];
            }
            if (const RuntimeField* circle = c.field("Circle")) {
                float r[3] = {0, 0, 0};
                read_vec(circle->bytes_value, r, 3);
                for (int i = 0; i < 3; ++i) item.shape_circle[i] = r[i];
            }
            // Disabled shapes draw dimmed so the studio can see the difference.
            if (!object.behaviour.health.shape_enabled) item.color[3] = 0.35f;
            items.push_back(std::move(item));
        } else if (c.is("PortalEffectComponent")) {
            VisualItem item = base_item(object, c, VisualKind::Glow);
            read_color(c, "Color", item.color);
            item.additive = true;
            item.radius = 1.5f;
            items.push_back(std::move(item));
        } else if (c.is("WeaponGlowComponent")) {
            VisualItem item = base_item(object, c, VisualKind::WeaponTrail);
            item.scale[0] = c.number("Width", 0.0f);
            item.additive = true;
            read_color(c, "ParticleColor", item.color);
            items.push_back(std::move(item));
        } else if (c.is("WeaponTrailComponent")) {
            VisualItem item = base_item(object, c, VisualKind::WeaponTrail);
            read_color(c, "Color", item.color);
            item.additive = true;
            items.push_back(std::move(item));
        }
    }

    // The runtime's own summary state (set by the behaviour layer) is the source
    // of truth for the animated values, so a scene with no components but a live
    // state still renders correctly.
    if (state) {
        for (VisualItem& item : items) {
            if (item.kind == VisualKind::Model || item.kind == VisualKind::Particle)
                item.anim_time = state->anim_time;
            if (item.kind == VisualKind::Water) item.water_time = state->water_time;
            if (item.kind == VisualKind::Light || item.kind == VisualKind::Glow)
                item.pulse = state->light_pulse;
        }
    }
    return items;
}

bool visual_has_item(const std::string& class_name) {
    static const char* const kVisualClasses[] = {
        "ModelComponent", "Model", "SpriteComponent", "Sprite", "ParticleComponent",
        "Particle", "ParticleEmitterComponent", "ParticleEmitter", "FireEmitterComponent",
        "FireEmitter", "FireBreathComponent", "FireBreath", "LightComponent", "Light",
        "SimpleGlowComponent", "SimpleGlow", "Glow", "ShadowComponent", "Shadow",
        "GroundPolygonComponent", "GroundPolygon", "GroundMeshComponent", "GroundMesh",
        "GroundMeshGeneratorComponent", "GroundMeshGenerator", "WaterMeshComponent",
        "WaterMesh", "BackgroundComponent", "Background", "OverlayTextComponent",
        "OverlayText", "ShapeComponent", "Shape", "UtilityShape", "CollisionShapeComponent",
        "CollisionShape", "PortalEffectComponent", "PortalEffect", "WeaponGlowComponent",
        "WeaponGlow", "WeaponTrailComponent", "WeaponTrail", "TextureMappingComponent",
        "TextureMapping", "ParticleObjectComponent", "ParticleObject",
    };
    for (const char* name : kVisualClasses)
        if (class_name == name) return true;
    return false;
}

std::vector<VisualItem> extract_scene_visuals(const RuntimeScene& scene) {
    std::vector<VisualItem> items;
    const auto& states = scene.render_state();
    for (const RuntimeObject& object : scene.objects()) {
        const ObjectRenderState* state = nullptr;
        for (const ObjectRenderState& candidate : states)
            if (candidate.identifier == object.identifier) { state = &candidate; break; }
        std::vector<VisualItem> local = extract_visuals(object, state);
        items.insert(items.end(), local.begin(), local.end());
    }
    return items;
}

std::string visual_summary(const RuntimeObject& object) {
    std::string summary;
    for (const VisualItem& item : extract_visuals(object)) {
        const char* label = "?";
        switch (item.kind) {
            case VisualKind::Model:       label = "Model"; break;
            case VisualKind::Sprite:      label = "Sprite"; break;
            case VisualKind::Particle:    label = "Particles"; break;
            case VisualKind::Light:       label = "Light"; break;
            case VisualKind::Glow:        label = "Glow"; break;
            case VisualKind::Shadow:      label = "Shadow"; break;
            case VisualKind::Ground:      label = "Ground"; break;
            case VisualKind::Water:       label = "Water"; break;
            case VisualKind::Background:  label = "Background"; break;
            case VisualKind::ShadowMap:   label = "ShadowMap"; break;
            case VisualKind::Shape:       label = "Shape"; break;
            case VisualKind::WeaponTrail: label = "Trail"; break;
            case VisualKind::Overlay:     label = "Text"; break;
        }
        if (!summary.empty()) summary += " \xC2\xB7 ";
        summary += label;
        if (item.component) summary += " #" + std::to_string(item.component);
        if (!item.asset.empty() && item.asset.size() < 40) summary += " " + item.asset;
    }
    return summary;
}

} // namespace caver
