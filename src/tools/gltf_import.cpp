// gltf_import.cpp — Remastered glTF 2.0 / GLB importer for POD models.
// Uses tiny_gltf_v3 for robust, 100% spec-compliant glTF parsing,
// and maps assets into Swordigo PowerVR POD models and PVR textures.

#include "gltf_glb.h"
#include "pod_loader.h"
#include "tiny_gltf_v3.h"
#include "tinygltf_json_c.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include <unordered_set>
#include <map>

namespace av {
namespace {

// ─── Accessor Unpacking Helper ─────────────────────────────────────────
static bool read_accessor_floats(const tg3_model* model, int32_t acc_idx, std::vector<float>& out) {
    out.clear();
    if (!model || acc_idx < 0 || acc_idx >= (int32_t)model->accessors_count) return false;
    const tg3_accessor* acc = &model->accessors[acc_idx];
    if (acc->count == 0) return true;
    if (acc->buffer_view < 0 || acc->buffer_view >= (int32_t)model->buffer_views_count) return false;
    const tg3_buffer_view* bv = &model->buffer_views[acc->buffer_view];
    if (bv->buffer < 0 || bv->buffer >= (int32_t)model->buffers_count) return false;
    const tg3_buffer* buf = &model->buffers[bv->buffer];
    if (!buf->data.data || buf->data.count == 0) return false;

    int num_comps = 1;
    switch (acc->type) {
        case TG3_TYPE_SCALAR: num_comps = 1; break;
        case TG3_TYPE_VEC2:   num_comps = 2; break;
        case TG3_TYPE_VEC3:   num_comps = 3; break;
        case TG3_TYPE_VEC4:   num_comps = 4; break;
        case TG3_TYPE_MAT2:   num_comps = 4; break;
        case TG3_TYPE_MAT3:   num_comps = 9; break;
        case TG3_TYPE_MAT4:   num_comps = 16; break;
        default: num_comps = 1; break;
    }

    size_t comp_size = 4;
    switch (acc->component_type) {
        case TG3_COMPONENT_TYPE_BYTE:
        case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:  comp_size = 1; break;
        case TG3_COMPONENT_TYPE_SHORT:
        case TG3_COMPONENT_TYPE_UNSIGNED_SHORT: comp_size = 2; break;
        case TG3_COMPONENT_TYPE_INT:
        case TG3_COMPONENT_TYPE_UNSIGNED_INT:
        case TG3_COMPONENT_TYPE_FLOAT:          comp_size = 4; break;
        case TG3_COMPONENT_TYPE_DOUBLE:         comp_size = 8; break;
        default: comp_size = 4; break;
    }

    size_t elem_size = (size_t)num_comps * comp_size;
    size_t stride = (bv->byte_stride > 0) ? bv->byte_stride : elem_size;
    uint64_t start_offset = bv->byte_offset + acc->byte_offset;

    if (start_offset + (acc->count - 1) * stride + elem_size > buf->data.count) {
        return false;
    }

    out.resize(acc->count * num_comps);
    const uint8_t* base_ptr = buf->data.data + start_offset;

    for (uint64_t i = 0; i < acc->count; ++i) {
        const uint8_t* elem_ptr = base_ptr + i * stride;
        float* dst = &out[i * num_comps];

        for (int c = 0; c < num_comps; ++c) {
            const uint8_t* cp = elem_ptr + c * comp_size;
            float val = 0.0f;
            switch (acc->component_type) {
                case TG3_COMPONENT_TYPE_BYTE:
                    val = acc->normalized ? std::max(-1.0f, (float)*(const int8_t*)cp / 127.0f) : (float)*(const int8_t*)cp;
                    break;
                case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
                    val = acc->normalized ? (float)*(const uint8_t*)cp / 255.0f : (float)*(const uint8_t*)cp;
                    break;
                case TG3_COMPONENT_TYPE_SHORT:
                    val = acc->normalized ? std::max(-1.0f, (float)*(const int16_t*)cp / 32767.0f) : (float)*(const int16_t*)cp;
                    break;
                case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
                    val = acc->normalized ? (float)*(const uint16_t*)cp / 65535.0f : (float)*(const uint16_t*)cp;
                    break;
                case TG3_COMPONENT_TYPE_INT:
                    val = (float)*(const int32_t*)cp;
                    break;
                case TG3_COMPONENT_TYPE_UNSIGNED_INT:
                    val = (float)*(const uint32_t*)cp;
                    break;
                case TG3_COMPONENT_TYPE_FLOAT:
                    val = *(const float*)cp;
                    break;
                case TG3_COMPONENT_TYPE_DOUBLE:
                    val = (float)*(const double*)cp;
                    break;
            }
            dst[c] = val;
        }
    }
    return true;
}

static int find_attribute_accessor(const tg3_primitive* prim, const char* name) {
    if (!prim || !prim->attributes || !name) return -1;
    size_t name_len = std::strlen(name);
    for (uint32_t i = 0; i < prim->attributes_count; ++i) {
        if (prim->attributes[i].key.len == name_len &&
            std::strncmp(prim->attributes[i].key.data, name, name_len) == 0) {
            return prim->attributes[i].value;
        }
    }
    return -1;
}

// ─── KHR_materials_pbrSpecularGlossiness fallback ──────────────────────
// Many DCC exports (Substance/Marmoset-era, terrain kits, Sketchfab
// downloads) author the albedo through the pbrSpecularGlossiness extension
// instead of the core metallic-roughness block. In those files
// `pbr_metallic_roughness.base_color_texture.index` is -1 and the real
// diffuse map lives at extensions.KHR_materials_pbrSpecularGlossiness
// .diffuseTexture. tiny_gltf_v3 does not surface this in a typed field, but
// it DOES materialise the extension value tree (skip_extras_values defaults
// off), so we read it out of mat->ext.extensions[]. Without this the model
// renders untextured. (terrain_dristibute_gn.glb — all 3 materials.)
struct SpecGlossInfo {
    int32_t diffuse_texture = -1;   // glTF texture index, -1 if absent
    int32_t diffuse_texcoord = 0;
    bool    has_diffuse_factor = false;
    float   diffuse_factor[4] = {1, 1, 1, 1};
    bool    present = false;         // the extension itself exists
};

static const tg3_value* tg3_obj_get(const tg3_value* obj, const char* key) {
    if (!obj || obj->type != TG3_VALUE_OBJECT) return nullptr;
    const size_t klen = std::strlen(key);
    for (uint32_t i = 0; i < obj->object_count; ++i) {
        const tg3_kv_pair* kv = &obj->object_data[i];
        if (kv->key.len == klen && std::strncmp(kv->key.data, key, klen) == 0)
            return &kv->value;
    }
    return nullptr;
}

// The image a texture points at, honouring EXT_texture_webp.
//
// EXT_texture_webp is a REPLACEMENT mechanism, not a fallback: when a file
// requires it, the texture may carry no core `source` field at all and name its
// image only inside extensions.EXT_texture_webp.source. Reading
// textures[i].source alone then yields -1 and the material silently loses its
// albedo — pilot.glb converts with "0 textures" for exactly this reason. Prefer
// the core field (the spec's own fallback), then the extension.
static int32_t texture_source_image(const tg3_model* model, int32_t tex_idx) {
    if (!model || tex_idx < 0 || tex_idx >= (int32_t)model->textures_count) return -1;
    const tg3_texture& t = model->textures[tex_idx];
    if (t.source >= 0) return t.source;
    for (uint32_t e = 0; e < t.ext.extensions_count; ++e) {
        const tg3_extension& ex = t.ext.extensions[e];
        if (!ex.name.data) continue;
        if (std::string(ex.name.data, ex.name.len) != "EXT_texture_webp") continue;
        const tg3_value* src = tg3_obj_get(&ex.value, "source");
        if (src && src->type == TG3_VALUE_INT) return (int32_t)src->int_val;
    }
    return -1;
}

static SpecGlossInfo read_spec_gloss(const tg3_material* mat) {
    SpecGlossInfo out;
    if (!mat) return out;
    for (uint32_t e = 0; e < mat->ext.extensions_count; ++e) {
        const tg3_extension* ex = &mat->ext.extensions[e];
        static const char* kName = "KHR_materials_pbrSpecularGlossiness";
        if (ex->name.len != std::strlen(kName) ||
            std::strncmp(ex->name.data, kName, ex->name.len) != 0)
            continue;
        out.present = true;
        const tg3_value* sg = &ex->value;
        if (const tg3_value* dt = tg3_obj_get(sg, "diffuseTexture")) {
            if (const tg3_value* idx = tg3_obj_get(dt, "index"))
                if (idx->type == TG3_VALUE_INT) out.diffuse_texture = (int32_t)idx->int_val;
            if (const tg3_value* tc = tg3_obj_get(dt, "texCoord"))
                if (tc->type == TG3_VALUE_INT) out.diffuse_texcoord = (int32_t)tc->int_val;
        }
        if (const tg3_value* df = tg3_obj_get(sg, "diffuseFactor")) {
            if (df->type == TG3_VALUE_ARRAY && df->array_count >= 3) {
                out.has_diffuse_factor = true;
                for (uint32_t k = 0; k < 4 && k < df->array_count; ++k) {
                    const tg3_value* c = &df->array_data[k];
                    out.diffuse_factor[k] = (c->type == TG3_VALUE_REAL) ? (float)c->real_val
                                          : (c->type == TG3_VALUE_INT)  ? (float)c->int_val
                                          : out.diffuse_factor[k];
                }
            }
        }
        break;
    }
    return out;
}

// ─── KHR_texture_transform (TODO edge E15) ────────────────────────────
// UV offset/rotation/scale attached to a textureInfo (almost always the
// base-colour map). tiny_gltf_v3 materialises textureInfo extensions into
// ti->ext (tg3__parse_texture_info calls tg3__parse_extras_and_extensions),
// so we read the value tree directly — same approach as the spec-gloss
// fallback above. glTF spec mapping (column-vector):
//   uv' = T' · R(angle) · S · uv
// POD has a single UV channel, so only the base-colour / spec-gloss diffuse
// transform is baked; other maps' transforms (rare) are ignored.
// NOTE: this bakes in glTF UV space; the game-convention V flip is applied
// afterwards by the converter (--flip-v), which composes correctly
// (game_uv = F(T(uv)) is the ground-truth mapping).
struct TextureTransform {
    float offset[2] = {0, 0};
    float rotation  = 0;    // radians, CCW in UV space
    float scale[2]  = {1, 1};
    bool  present   = false;
};

static TextureTransform parse_texture_transform(const tg3_value* tt) {
    TextureTransform out;
    if (!tt || tt->type != TG3_VALUE_OBJECT) return out;
    auto num = [](const tg3_value* v, float def) -> float {
        if (!v) return def;
        if (v->type == TG3_VALUE_REAL) return (float)v->real_val;
        if (v->type == TG3_VALUE_INT)  return (float)v->int_val;
        return def;
    };
    if (const tg3_value* v = tg3_obj_get(tt, "offset")) {
        if (v->type == TG3_VALUE_ARRAY && v->array_count >= 2) {
            out.offset[0] = num(&v->array_data[0], 0.0f);
            out.offset[1] = num(&v->array_data[1], 0.0f);
        }
    }
    if (const tg3_value* v = tg3_obj_get(tt, "rotation"))
        out.rotation = num(v, 0.0f);
    if (const tg3_value* v = tg3_obj_get(tt, "scale")) {
        if (v->type == TG3_VALUE_ARRAY && v->array_count >= 2) {
            out.scale[0] = num(&v->array_data[0], 1.0f);
            out.scale[1] = num(&v->array_data[1], 1.0f);
        }
    }
    out.present = true;
    return out;
}

static TextureTransform read_texture_transform(const tg3_texture_info* ti) {
    TextureTransform out;
    if (!ti) return out;
    for (uint32_t e = 0; e < ti->ext.extensions_count; ++e) {
        const tg3_extension* ex = &ti->ext.extensions[e];
        static const char* kName = "KHR_texture_transform";
        if (ex->name.len != std::strlen(kName) ||
            std::strncmp(ex->name.data, kName, ex->name.len) != 0)
            continue;
        out = parse_texture_transform(&ex->value);
        break;
    }
    return out;
}

// SpecGloss path: the diffuseTexture is a raw tg3_value (textureInfo-shaped),
// so its extensions object is read out of the value tree instead of ti->ext.
static TextureTransform read_texture_transform_sg(const tg3_value* diffuse_tex) {
    TextureTransform out;
    if (!diffuse_tex) return out;
    const tg3_value* exts = tg3_obj_get(diffuse_tex, "extensions");
    if (!exts) return out;
    if (const tg3_value* tt = tg3_obj_get(exts, "KHR_texture_transform"))
        out = parse_texture_transform(tt);
    return out;
}

static void apply_texture_transform(std::vector<float>& uvs, const TextureTransform& tt) {
    if (!tt.present || uvs.empty()) return;
    const float s = std::sin(tt.rotation), c = std::cos(tt.rotation);
    for (size_t i = 0; i + 1 < uvs.size(); i += 2) {
        const float u = uvs[i]     * tt.scale[0];
        const float v = uvs[i + 1] * tt.scale[1];
        uvs[i]     = c * u - s * v + tt.offset[0];
        uvs[i + 1] = s * u + c * v + tt.offset[1];
    }
}

static int find_uv_accessor(const tg3_model* model, const tg3_primitive* prim, int mat_idx) {
    if (mat_idx >= 0 && mat_idx < (int32_t)model->materials_count) {
        const tg3_material* mat = &model->materials[mat_idx];
        int tc = mat->pbr_metallic_roughness.base_color_texture.tex_coord;
        // SpecGloss fallback: use the diffuseTexture's texCoord when the core
        // base-color texture is absent.
        if (mat->pbr_metallic_roughness.base_color_texture.index < 0) {
            SpecGlossInfo sg = read_spec_gloss(mat);
            if (sg.diffuse_texture >= 0) tc = sg.diffuse_texcoord;
        }
        if (tc > 0) {
            std::string attr = "TEXCOORD_" + std::to_string(tc);
            int acc = find_attribute_accessor(prim, attr.c_str());
            if (acc >= 0) return acc;
        }
    }
    int acc = find_attribute_accessor(prim, "TEXCOORD_0");
    if (acc < 0) acc = find_attribute_accessor(prim, "TEXCOORD");
    if (acc < 0) acc = find_attribute_accessor(prim, "TEXCOORD_1");
    if (acc < 0) acc = find_attribute_accessor(prim, "TEXCOORD_2");
    return acc;
}

static std::string tg3_to_string(const tg3_str& s) {
    if (!s.data || s.len == 0) return "";
    return std::string(s.data, s.len);
}

// General 4x4 inverse (Gauss-Jordan, column-major) — used to convert glTF
// inverseBindMatrices into bind-pose world matrices for PODNode::bind_matrix.
static bool gltf_mat4_inverse(const float in[16], float out[16]) {
    float a[16];
    std::memcpy(a, in, sizeof(a));
    std::memset(out, 0, 16 * sizeof(float));
    out[0] = out[5] = out[10] = out[15] = 1.0f;
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 4; ++row)
            if (std::fabs(a[col * 4 + row]) > std::fabs(a[col * 4 + pivot])) pivot = row;
        if (std::fabs(a[col * 4 + pivot]) < 1e-10f) return false;
        if (pivot != col) {
            for (int c = 0; c < 4; ++c) {
                std::swap(a[c * 4 + col], a[c * 4 + pivot]);
                std::swap(out[c * 4 + col], out[c * 4 + pivot]);
            }
        }
        const float scale = 1.0f / a[col * 4 + col];
        for (int c = 0; c < 4; ++c) {
            a[c * 4 + col] *= scale;
            out[c * 4 + col] *= scale;
        }
        for (int row = 0; row < 4; ++row) {
            if (row == col) continue;
            const float factor = a[col * 4 + row];
            for (int c = 0; c < 4; ++c) {
                a[c * 4 + row] -= factor * a[c * 4 + col];
                out[c * 4 + row] -= factor * out[c * 4 + col];
            }
        }
    }
    return true;
}

static void gltf_mat4_mul(const float a[16], const float b[16], float out[16]) {
    float t[16];
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            t[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1]
                         + a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
        }
    }
    std::memcpy(out, t, sizeof(t));
}

static void quat_mul(const float q1[4], const float q2[4], float out[4]) {
    out[0] = q1[3]*q2[0] + q1[0]*q2[3] + q1[1]*q2[2] - q1[2]*q2[1];
    out[1] = q1[3]*q2[1] - q1[0]*q2[2] + q1[1]*q2[3] + q1[2]*q2[0];
    out[2] = q1[3]*q2[2] + q1[0]*q2[1] - q1[1]*q2[0] + q1[2]*q2[3];
    out[3] = q1[3]*q2[3] - q1[0]*q2[0] - q1[1]*q2[1] - q1[2]*q2[2];
}

static void quat_rotate_vec(const float q[4], float vx, float vy, float vz, float& ox, float& oy, float& oz) {
    float rx = q[0], ry = q[1], rz = q[2], w = q[3];
    float tx = 2.0f * (ry * vz - rz * vy + w * vx);
    float ty = 2.0f * (rz * vx - rx * vz + w * vy);
    float tz = 2.0f * (rx * vy - ry * vx + w * vz);
    ox = vx + (ry * tz - rz * ty);
    oy = vy + (rz * tx - rx * tz);
    oz = vz + (rx * ty - ry * tx);
}

static void mat3_to_quat(const float m[9], float q[4]) {
    float trace = m[0] + m[4] + m[8];
    if (trace > 0.0f) {
        float s = 0.5f / std::sqrt(trace + 1.0f);
        q[3] = 0.25f / s;
        q[0] = (m[7] - m[5]) * s;
        q[1] = (m[2] - m[6]) * s;
        q[2] = (m[3] - m[1]) * s;
    } else {
        if (m[0] > m[4] && m[0] > m[8]) {
            float s = 2.0f * std::sqrt(1.0f + m[0] - m[4] - m[8]);
            q[3] = (m[7] - m[5]) / s;
            q[0] = 0.25f * s;
            q[1] = (m[1] + m[3]) / s;
            q[2] = (m[2] + m[6]) / s;
        } else if (m[4] > m[8]) {
            float s = 2.0f * std::sqrt(1.0f + m[4] - m[0] - m[8]);
            q[3] = (m[2] - m[6]) / s;
            q[0] = (m[1] + m[3]) / s;
            q[1] = 0.25f * s;
            q[2] = (m[5] + m[7]) / s;
        } else {
            float s = 2.0f * std::sqrt(1.0f + m[8] - m[0] - m[4]);
            q[3] = (m[3] - m[1]) / s;
            q[0] = (m[2] + m[6]) / s;
            q[1] = (m[5] + m[7]) / s;
            q[2] = 0.25f * s;
        }
    }
    float len = std::sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    if (len > 1e-8f) { q[0] /= len; q[1] /= len; q[2] /= len; q[3] /= len; }
    else { q[0] = 0; q[1] = 0; q[2] = 0; q[3] = 1; }
}

struct GltfPodMapping {
    std::vector<int> gltf_to_pod_node;     // gltf node idx -> pod node idx (primary/prim0)
    std::vector<int> pod_mesh_of_gltf_mesh; // gltf mesh idx -> first pod mesh idx
    int num_mesh_nodes = 0;
    int total_nodes = 0;
};

static GltfPodMapping compute_gltf_pod_mapping(const tg3_model* model) {
    GltfPodMapping m;
    m.pod_mesh_of_gltf_mesh.assign(model->meshes_count, -1);
    int cur_mesh = 0;
    for (uint32_t mi = 0; mi < model->meshes_count; ++mi) {
        m.pod_mesh_of_gltf_mesh[mi] = cur_mesh;
        cur_mesh += (int)model->meshes[mi].primitives_count;
    }

    std::vector<int> mesh_gltf_nodes;
    std::vector<int> other_gltf_nodes;
    for (uint32_t i = 0; i < model->nodes_count; ++i) {
        if (model->nodes[i].mesh >= 0 && model->nodes[i].mesh < (int32_t)model->meshes_count) {
            mesh_gltf_nodes.push_back(static_cast<int>(i));
        } else {
            other_gltf_nodes.push_back(static_cast<int>(i));
        }
    }

    m.gltf_to_pod_node.assign(model->nodes_count, -1);
    int cur_node = 0;
    for (int gn_idx : mesh_gltf_nodes) {
        m.gltf_to_pod_node[gn_idx] = cur_node;
        int n_prims = (int)model->meshes[model->nodes[gn_idx].mesh].primitives_count;
        cur_node += (n_prims > 0 ? n_prims : 1);
    }
    m.num_mesh_nodes = cur_node;

    for (int gn_idx : other_gltf_nodes) {
        m.gltf_to_pod_node[gn_idx] = cur_node++;
    }
    m.total_nodes = cur_node;
    return m;
}

static void gltf_local_static_matrix(const tg3_node* gn, float m[16]) {
    if (gn->has_matrix) {
        for (int k = 0; k < 16; ++k) m[k] = (float)gn->matrix[k];
        return;
    }
    float S[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float R[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float T[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const float x = (float)gn->rotation[0], y = (float)gn->rotation[1],
                z = (float)gn->rotation[2], w = (float)gn->rotation[3];
    R[0]=1-2*(y*y+z*z); R[4]=2*(x*y-z*w);   R[8]=2*(x*z+y*w);
    R[1]=2*(x*y+z*w);   R[5]=1-2*(x*x+z*z); R[9]=2*(y*z-x*w);
    R[2]=2*(x*z-y*w);   R[6]=2*(y*z+x*w);   R[10]=1-2*(x*x+y*y);
    S[0]=(float)gn->scale[0]; S[5]=(float)gn->scale[1]; S[10]=(float)gn->scale[2];
    T[12]=(float)gn->translation[0]; T[13]=(float)gn->translation[1]; T[14]=(float)gn->translation[2];
    float tr[16];
    gltf_mat4_mul(T, R, tr);
    gltf_mat4_mul(tr, S, m);
}

static void gltf_rest_world_matrix(const tg3_model* model, const std::vector<int>& parent_of,
                                   int idx, float out16[16], int depth = 0) {
    if (idx < 0 || idx >= (int)model->nodes_count || depth > 128) {
        std::memset(out16, 0, sizeof(float) * 16);
        out16[0] = out16[5] = out16[10] = out16[15] = 1.0f;
        return;
    }
    float local[16];
    gltf_local_static_matrix(&model->nodes[idx], local);
    int p = parent_of[idx];
    if (p >= 0 && p != idx) {
        float pw[16];
        gltf_rest_world_matrix(model, parent_of, p, pw, depth + 1);
        gltf_mat4_mul(pw, local, out16);
    } else {
        std::memcpy(out16, local, sizeof(float) * 16);
    }
}

struct GltfSkeletonInfo {
    std::vector<int> parent_of;
    std::vector<bool> is_joint;
    std::vector<bool> is_root_joint;
    std::vector<float> skel_scale_of_joint;
    std::vector<std::array<float, 4>> q_wrapper_of_joint;
    std::vector<std::array<float, 3>> p_wrapper_of_joint;
    std::vector<float> s_wrapper_of_joint;
};

static GltfSkeletonInfo analyze_skeleton(const tg3_model* model, float scale) {
    GltfSkeletonInfo skel;
    skel.parent_of.assign(model->nodes_count, -1);
    for (uint32_t i = 0; i < model->nodes_count; ++i) {
        const tg3_node* n = &model->nodes[i];
        for (uint32_t c = 0; c < n->children_count; ++c) {
            int32_t child_idx = n->children[c];
            if (child_idx >= 0 && child_idx < (int32_t)model->nodes_count) {
                skel.parent_of[child_idx] = (int)i;
            }
        }
    }

    skel.is_joint.assign(model->nodes_count, false);
    for (uint32_t si = 0; si < model->skins_count; ++si) {
        const tg3_skin* skin = &model->skins[si];
        for (uint32_t j = 0; j < skin->joints_count; ++j) {
            int32_t ji = skin->joints[j];
            if (ji >= 0 && ji < (int32_t)skel.is_joint.size()) skel.is_joint[ji] = true;
        }
    }

    skel.is_root_joint.assign(model->nodes_count, false);
    for (uint32_t i = 0; i < model->nodes_count; ++i) {
        if (!skel.is_joint[i]) continue;
        int p = skel.parent_of[i];
        if (p < 0 || !skel.is_joint[p]) skel.is_root_joint[i] = true;
    }

    skel.skel_scale_of_joint.assign(model->nodes_count, scale);
    skel.q_wrapper_of_joint.assign(model->nodes_count, {0,0,0,1});
    skel.p_wrapper_of_joint.assign(model->nodes_count, {0,0,0});
    skel.s_wrapper_of_joint.assign(model->nodes_count, 1.0f);

    for (uint32_t i = 0; i < model->nodes_count; ++i) {
        if (!skel.is_root_joint[i]) continue;
        int p = skel.parent_of[i];
        float S_wrap = 1.0f;
        std::array<float, 4> Q_wrap = {0, 0, 0, 1};
        std::array<float, 3> P_wrap = {0, 0, 0};
        if (p >= 0) {
            float wrap_w[16];
            gltf_rest_world_matrix(model, skel.parent_of, p, wrap_w, 0);
            P_wrap = {wrap_w[12], wrap_w[13], wrap_w[14]};
            float sx = std::sqrt(wrap_w[0]*wrap_w[0] + wrap_w[1]*wrap_w[1] + wrap_w[2]*wrap_w[2]);
            float sy = std::sqrt(wrap_w[4]*wrap_w[4] + wrap_w[5]*wrap_w[5] + wrap_w[6]*wrap_w[6]);
            float sz = std::sqrt(wrap_w[8]*wrap_w[8] + wrap_w[9]*wrap_w[9] + wrap_w[10]*wrap_w[10]);
            S_wrap = (sx + sy + sz) / 3.0f;
            if (S_wrap < 1e-6f) S_wrap = 1.0f;
            float R[9] = {
                wrap_w[0]/sx, wrap_w[1]/sx, wrap_w[2]/sx,
                wrap_w[4]/sy, wrap_w[5]/sy, wrap_w[6]/sy,
                wrap_w[8]/sz, wrap_w[9]/sz, wrap_w[10]/sz
            };
            mat3_to_quat(R, Q_wrap.data());
        }
        skel.s_wrapper_of_joint[i] = S_wrap;
        skel.skel_scale_of_joint[i] = S_wrap * scale;
        skel.q_wrapper_of_joint[i] = Q_wrap;
        skel.p_wrapper_of_joint[i] = P_wrap;
    }

    for (uint32_t i = 0; i < model->nodes_count; ++i) {
        if (!skel.is_joint[i] || skel.is_root_joint[i]) continue;
        int cur = skel.parent_of[i];
        while (cur >= 0 && skel.is_joint[cur] && !skel.is_root_joint[cur]) {
            cur = skel.parent_of[cur];
        }
        if (cur >= 0 && skel.is_root_joint[cur]) {
            skel.s_wrapper_of_joint[i] = skel.s_wrapper_of_joint[cur];
            skel.skel_scale_of_joint[i] = skel.skel_scale_of_joint[cur];
            skel.q_wrapper_of_joint[i] = skel.q_wrapper_of_joint[cur];
            skel.p_wrapper_of_joint[i] = skel.p_wrapper_of_joint[cur];
        }
    }

    return skel;
}

// ─── Rebuild PODModel from tg3_model ───────────────────────────────────
static bool build_pod_from_tg3(const tg3_model* model,
                               PODModel& out, std::vector<GLTFImageBuffer>& images,
                               GLTFPBRInfo* pbr, std::string* err, float scale = 1.0f,
                               bool rigid_skin = false) {
    (void)err;
    if (!model) return false;

    std::vector<int> parent_of(model->nodes_count, -1);
    for (uint32_t i = 0; i < model->nodes_count; ++i) {
        const tg3_node* n = &model->nodes[i];
        for (uint32_t c = 0; c < n->children_count; ++c) {
            int32_t child_idx = n->children[c];
            if (child_idx >= 0 && child_idx < (int32_t)model->nodes_count) {
                parent_of[child_idx] = (int)i;
            }
        }
    }

    // MUST return by value. This previously handed back a reference to ONE
    // shared scratch buffer, so any two results held at once aliased each
    // other:
    //
    //     const std::vector<float>& joints  = load(joints_acc);
    //     const std::vector<float>& weights = load(weights_acc);  // clobbers joints!
    //
    // `joints` silently became the WEIGHTS_0 data, so the rigid-skin bake below
    // bound every vertex to the joint index equal to its own normalised weight
    // (0 or 1). The result still looked perfect at the BIND POSE — all skin
    // matrices are identity there — but any animated frame dragged 89% of the
    // mesh with a single bone, tearing the model into long stretched sheets.
    // Returning by value makes every call site own its data; the per-attribute
    // copy is irrelevant for a one-shot asset conversion.
    auto load = [&](int32_t acc_idx) -> std::vector<float> {
        std::vector<float> buf;
        if (!read_accessor_floats(model, acc_idx, buf)) buf.clear();
        return buf;
    };

    out.meshes.clear();
    for (uint32_t mi = 0; mi < model->meshes_count; ++mi) {
        const tg3_mesh* gm = &model->meshes[mi];
        for (uint32_t pi = 0; pi < gm->primitives_count; ++pi) {
            const tg3_primitive* p = &gm->primitives[pi];
            PODMesh m;

            int pos_acc = find_attribute_accessor(p, "POSITION");
            const std::vector<float>& pos = load(pos_acc);
            if (!pos.empty()) {
                m.positions = pos;
                m.num_vertices = (int)(pos.size() / 3);
            }

            int nrm_acc = find_attribute_accessor(p, "NORMAL");
            const std::vector<float>& nrm = load(nrm_acc);
            if (!nrm.empty()) m.normals = nrm;

            int uv_acc = find_uv_accessor(model, p, p->material);
            const std::vector<float>& uv = load(uv_acc);
            if (!uv.empty()) {
                m.uvs = uv;
                // E15: bake KHR_texture_transform into the UVs (the game and the
                // legacy POD pipeline have no notion of per-texture UV transforms).
                if (p->material >= 0 && p->material < (int32_t)model->materials_count) {
                    const tg3_material* mat = &model->materials[p->material];
                    TextureTransform tt;
                    if (mat->pbr_metallic_roughness.base_color_texture.index >= 0) {
                        tt = read_texture_transform(&mat->pbr_metallic_roughness.base_color_texture);
                    } else {
                        SpecGlossInfo sg = read_spec_gloss(mat);
                        if (sg.diffuse_texture >= 0) {
                            // diffuseTexture lives in the spec-gloss value tree.
                            for (uint32_t e = 0; e < mat->ext.extensions_count; ++e) {
                                const tg3_extension* ex = &mat->ext.extensions[e];
                                static const char* kSG = "KHR_materials_pbrSpecularGlossiness";
                                if (ex->name.len != std::strlen(kSG) ||
                                    std::strncmp(ex->name.data, kSG, ex->name.len) != 0)
                                    continue;
                                tt = read_texture_transform_sg(tg3_obj_get(&ex->value, "diffuseTexture"));
                                break;
                            }
                        }
                    }
                    if (tt.present) {
                        apply_texture_transform(m.uvs, tt);
                        std::fprintf(stderr,
                                     "[gltf] KHR_texture_transform baked into UVs (material %u: offset %.3g,%.3g rot %.3g scale %.3g,%.3g)\n",
                                     p->material, tt.offset[0], tt.offset[1], tt.rotation,
                                     tt.scale[0], tt.scale[1]);
                    }
                }
            }

            int tng_acc = find_attribute_accessor(p, "TANGENT");
            const std::vector<float>& tng = load(tng_acc);
            if (!tng.empty()) m.tangents = tng;

            const std::vector<float>& idx = load(p->indices);
            if (!idx.empty()) {
                m.indices.reserve(idx.size());
                for (float x : idx) m.indices.push_back(static_cast<uint32_t>(x));
                m.num_faces = static_cast<int>(m.indices.size() / 3);
            }

            int joints_acc = find_attribute_accessor(p, "JOINTS_0");
            int weights_acc = find_attribute_accessor(p, "WEIGHTS_0");
            const std::vector<float>& joints = load(joints_acc);
            const std::vector<float>& weights = load(weights_acc);

            if (!joints.empty() && m.num_vertices > 0) {
                int jcomps = 4;
                if (joints_acc >= 0 && joints_acc < (int32_t)model->accessors_count) {
                    if (model->accessors[joints_acc].type == TG3_TYPE_VEC4) jcomps = 4;
                    else if (model->accessors[joints_acc].type == TG3_TYPE_VEC3) jcomps = 3;
                    else if (model->accessors[joints_acc].type == TG3_TYPE_VEC2) jcomps = 2;
                    else jcomps = 1;
                }
                int nverts = m.num_vertices;
                int max_inf = 0;
                for (int v = 0; v < nverts; ++v) {
                    int infl = 0;
                    for (int k = 0; k < jcomps; ++k) {
                        size_t i = (size_t)v * jcomps + k;
                        float w = (weights.size() > i) ? weights[i] : 0.0f;
                        if (w > 0.0f) infl = k + 1;
                    }
                    max_inf = std::max(max_inf, infl);
                }
                max_inf = std::clamp(max_inf, 1, 4);

                if (rigid_skin) {
                    // S1 (TODO.md): dominant-bone (rigid) bake. The game's
                    // C_Matrix4Vector3ArraySkin reads ONE bone index per vertex
                    // and ignores weights (more_model_research.md §6/§9 —
                    // "strictly 1 bone per vertex"), so smooth-skinned rigs with
                    // 2-4 influences deform wrong in-game. Pre-bake each vertex
                    // to its max-weight joint at weight 1.0 (ties → lowest joint
                    // slot). The viewer keeps full weights for accurate preview
                    // (rigid_skin defaults to false on the raw import APIs; the
                    // game-destined converter opts in via --rigid-skin).
                    m.bones_per_vertex = 1;
                    m.bone_indices.assign((size_t)nverts, 0.0f);
                    m.bone_weights.assign((size_t)nverts, 1.0f);
                    for (int v = 0; v < nverts; ++v) {
                        int   best_k = 0;
                        float best_w = -1.0f;
                        for (int k = 0; k < jcomps; ++k) {
                            size_t i = (size_t)v * jcomps + k;
                            float w = (weights.size() > i) ? weights[i] : 0.0f;
                            if (w > best_w) { best_w = w; best_k = k; }
                        }
                        size_t src = (size_t)v * jcomps + best_k;
                        // Still RAW skin.joints[]-space (see the note below) —
                        // bone_batches.indices[] does the POD-node remapping.
                        m.bone_indices[(size_t)v] = (joints.size() > src) ? joints[src] : 0.0f;
                    }
                } else {
                    m.bones_per_vertex = max_inf;
                    m.bone_indices.assign((size_t)nverts * max_inf, 0.0f);
                    m.bone_weights.assign((size_t)nverts * max_inf, 0.0f);
                m.bones_per_vertex = max_inf;
                m.bone_indices.assign((size_t)nverts * max_inf, 0.0f);
                m.bone_weights.assign((size_t)nverts * max_inf, 0.0f);
                // Per-vertex bone indices are stored RAW here — they are glTF
                // JOINTS_0 values, i.e. indices into skin.joints[] (the bone
                // BATCH's logical space), NOT POD node indices. The engine's
                // skin_mesh() resolves them in two levels: per-vertex index ->
                // bone_batches.indices[] -> POD node. The batch table is filled
                // later (see the skin loop below) by remapping skin.joints[j]
                // through gltf_to_pod_node[]. So do NOT remap here — remapping
                // twice would corrupt the binding. (pod_master/05 §2; verified
                // against pod_loader.cpp skin_mesh indirection.)
                    for (int v = 0; v < nverts; ++v) {
                        for (int k = 0; k < max_inf; ++k) {
                            size_t src = (size_t)v * jcomps + k;
                            size_t dst = (size_t)v * max_inf + k;
                            m.bone_indices[dst] = (joints.size() > src) ? joints[src] : 0.0f;
                            m.bone_weights[dst] = (weights.size() > src) ? weights[src] : 0.0f;
                        }
                    }
                }
                // Per-vertex bone indices are stored RAW here — they are glTF
                // JOINTS_0 values, i.e. indices into skin.joints[] (the bone
                // BATCH's logical space), NOT POD node indices. The engine's
                // skin_mesh() resolves them in two levels: per-vertex index ->
                // bone_batches.indices[] -> POD node. The batch table is filled
                // later (see the skin loop below) by remapping skin.joints[j]
                // through gltf_to_pod_node[]. So do NOT remap here — remapping
                // twice would corrupt the binding. (pod_master/05 §2; verified
                // against pod_loader.cpp skin_mesh indirection.)
                m.has_bone_batches = true;
                m.bone_batches.count = 1;
                m.bone_batches.offsets = {0};
                m.bone_batches.counts = {1};
                m.bone_batches.max_bones = 1;
            }

            if (scale != 1.0f && scale > 0.0f && m.bones_per_vertex <= 0) {
                for (float& pv : m.positions) pv *= scale;
            }

            m.num_vertices = m.num_vertices ? m.num_vertices : (int)(m.positions.size() / 3);
            if (m.num_vertices == 0 && !m.positions.empty()) m.num_vertices = (int)(m.positions.size() / 3);
            if (m.num_faces == 0) m.num_faces = m.indices.empty()
                ? (m.num_vertices > 0 ? m.num_vertices / 3 : 0)
                : (int)(m.indices.size() / 3);

            float m_minx =  1e30f, m_miny =  1e30f, m_minz =  1e30f;
            float m_maxx = -1e30f, m_maxy = -1e30f, m_maxz = -1e30f;
            for (size_t i = 0; i + 2 < m.positions.size(); i += 3) {
                float px = m.positions[i + 0];
                float py = m.positions[i + 1];
                float pz = m.positions[i + 2];
                m_minx = std::min(m_minx, px); m_maxx = std::max(m_maxx, px);
                m_miny = std::min(m_miny, py); m_maxy = std::max(m_maxy, py);
                m_minz = std::min(m_minz, pz); m_maxz = std::max(m_maxz, pz);
            }
            if (m.num_vertices > 0) {
                m.min_x = m_minx; m.max_x = m_maxx;
                m.min_y = m_miny; m.max_y = m_maxy;
                m.min_z = m_minz; m.max_z = m_maxz;
            }

            out.meshes.push_back(std::move(m));
        }
    }

    out.nodes.clear();
    GltfPodMapping mapping = compute_gltf_pod_mapping(model);
    GltfSkeletonInfo skel = analyze_skeleton(model, scale);

    out.nodes.resize(mapping.total_nodes);
    out.num_mesh_nodes = mapping.num_mesh_nodes;

    for (uint32_t i = 0; i < model->nodes_count; ++i) {
        if (model->nodes[i].mesh < 0 || model->nodes[i].mesh >= (int32_t)model->meshes_count) continue;
        const tg3_node* gn = &model->nodes[i];
        const tg3_mesh* gm = &model->meshes[gn->mesh];
        int num_prims = (int)gm->primitives_count;
        int first_pod_mesh = mapping.pod_mesh_of_gltf_mesh[gn->mesh];
        int base_pod_idx = mapping.gltf_to_pod_node[i];
        std::string base_name = tg3_to_string(gn->name);
        for (char& c : base_name) { if (c == ':' || c == '/' || c == '\\') c = '_'; }

        bool is_skinned = (gn->skin >= 0 && gn->skin < (int32_t)model->skins_count);

        for (int p = 0; p < std::max(1, num_prims); ++p) {
            int pod_idx = base_pod_idx + p;
            PODNode n;
            n.name = (p == 0) ? base_name : (base_name + "_" + std::to_string(p));
            n.object_index = (first_pod_mesh >= 0 && p < num_prims) ? (first_pod_mesh + p) : -1;
            n.material_index = (p < num_prims) ? gm->primitives[p].material : -1;

            if (is_skinned) {
                n.parent_index = -1;
                n.translation[0] = n.translation[1] = n.translation[2] = 0.0f;
                n.rotation[0] = n.rotation[1] = n.rotation[2] = 0.0f;
                n.rotation[3] = 1.0f;
                n.scale[0] = n.scale[1] = n.scale[2] = 1.0f;
                n.has_translation = n.has_rotation = n.has_scale = true;
                n.has_matrix = false;
            } else {
                int orig_parent = skel.parent_of[i];
                n.parent_index = (orig_parent >= 0 && orig_parent < (int)mapping.gltf_to_pod_node.size())
                                 ? mapping.gltf_to_pod_node[orig_parent] : -1;
                if (gn->has_matrix) {
                    n.has_matrix = true;
                    for (int k = 0; k < 16; ++k) n.matrix[k] = (float)gn->matrix[k];
                    if (scale != 1.0f && orig_parent == -1) {
                        n.matrix[12] *= scale;
                        n.matrix[13] *= scale;
                        n.matrix[14] *= scale;
                    }
                } else {
                    n.has_translation = true;
                    n.translation[0] = (float)gn->translation[0];
                    n.translation[1] = (float)gn->translation[1];
                    n.translation[2] = (float)gn->translation[2];
                    n.has_rotation = true;
                    n.rotation[0] = -(float)gn->rotation[0];
                    n.rotation[1] = -(float)gn->rotation[1];
                    n.rotation[2] = -(float)gn->rotation[2];
                    n.rotation[3] = (float)gn->rotation[3];
                    n.has_scale = true;
                    n.scale[0] = (float)gn->scale[0];
                    n.scale[1] = (float)gn->scale[1];
                    n.scale[2] = (float)gn->scale[2];
                    if (scale != 1.0f && orig_parent == -1) {
                        n.translation[0] *= scale;
                        n.translation[1] *= scale;
                        n.translation[2] *= scale;
                    }
                }
            }
            out.nodes[pod_idx] = std::move(n);
        }
    }

    for (uint32_t i = 0; i < model->nodes_count; ++i) {
        if (model->nodes[i].mesh >= 0 && model->nodes[i].mesh < (int32_t)model->meshes_count) continue;
        const tg3_node* gn = &model->nodes[i];
        int pod_idx = mapping.gltf_to_pod_node[i];
        PODNode n;
        std::string name = tg3_to_string(gn->name);
        for (char& c : name) { if (c == ':' || c == '/' || c == '\\') c = '_'; }
        if (skel.is_joint[i]) {
            if (name.rfind("Bone", 0) != 0 &&
                name.rfind("Control", 0) != 0 &&
                name != "CenterPoint") {
                if (name.empty()) name = "Bone_" + std::to_string(i);
                else name = "Bone_" + name;
            }
        }
        n.name = name;
        n.object_index = -1;
        n.material_index = -1;

        if (skel.is_root_joint[i]) {
            n.parent_index = -1;
            float jw[16];
            gltf_rest_world_matrix(model, skel.parent_of, (int)i, jw, 0);
            n.translation[0] = jw[12] * scale;
            n.translation[1] = jw[13] * scale;
            n.translation[2] = jw[14] * scale;
            float sx = std::sqrt(jw[0]*jw[0] + jw[1]*jw[1] + jw[2]*jw[2]);
            float sy = std::sqrt(jw[4]*jw[4] + jw[5]*jw[5] + jw[6]*jw[6]);
            float sz = std::sqrt(jw[8]*jw[8] + jw[9]*jw[9] + jw[10]*jw[10]);
            float R[9] = {
                jw[0]/sx, jw[1]/sx, jw[2]/sx,
                jw[4]/sy, jw[5]/sy, jw[6]/sy,
                jw[8]/sz, jw[9]/sz, jw[10]/sz
            };
            float Q[4];
            mat3_to_quat(R, Q);
            n.rotation[0] = -Q[0];
            n.rotation[1] = -Q[1];
            n.rotation[2] = -Q[2];
            n.rotation[3] = Q[3];
            n.scale[0] = n.scale[1] = n.scale[2] = 1.0f;
            n.has_translation = n.has_rotation = n.has_scale = true;
            n.has_matrix = false;
        } else if (skel.is_joint[i]) {
            int orig_parent = skel.parent_of[i];
            n.parent_index = (orig_parent >= 0 && orig_parent < (int)mapping.gltf_to_pod_node.size())
                             ? mapping.gltf_to_pod_node[orig_parent] : -1;
            float sk_s = skel.skel_scale_of_joint[i];
            n.translation[0] = (float)gn->translation[0] * sk_s;
            n.translation[1] = (float)gn->translation[1] * sk_s;
            n.translation[2] = (float)gn->translation[2] * sk_s;
            n.rotation[0] = -(float)gn->rotation[0];
            n.rotation[1] = -(float)gn->rotation[1];
            n.rotation[2] = -(float)gn->rotation[2];
            n.rotation[3] = (float)gn->rotation[3];
            n.scale[0] = n.scale[1] = n.scale[2] = 1.0f;
            n.has_translation = n.has_rotation = n.has_scale = true;
            n.has_matrix = false;
        } else {
            int orig_parent = skel.parent_of[i];
            n.parent_index = (orig_parent >= 0 && orig_parent < (int)mapping.gltf_to_pod_node.size())
                             ? mapping.gltf_to_pod_node[orig_parent] : -1;
            if (gn->has_matrix) {
                n.has_matrix = true;
                for (int k = 0; k < 16; ++k) n.matrix[k] = (float)gn->matrix[k];
                if (scale != 1.0f && orig_parent == -1) {
                    n.matrix[12] *= scale;
                    n.matrix[13] *= scale;
                    n.matrix[14] *= scale;
                }
            } else {
                n.has_translation = true;
                n.translation[0] = (float)gn->translation[0];
                n.translation[1] = (float)gn->translation[1];
                n.translation[2] = (float)gn->translation[2];
                n.has_rotation = true;
                n.rotation[0] = -(float)gn->rotation[0];
                n.rotation[1] = -(float)gn->rotation[1];
                n.rotation[2] = -(float)gn->rotation[2];
                n.rotation[3] = (float)gn->rotation[3];
                n.has_scale = true;
                n.scale[0] = (float)gn->scale[0];
                n.scale[1] = (float)gn->scale[1];
                n.scale[2] = (float)gn->scale[2];
                if (scale != 1.0f && orig_parent == -1) {
                    n.translation[0] *= scale;
                    n.translation[1] *= scale;
                    n.translation[2] *= scale;
                }
            }
        }
        out.nodes[pod_idx] = std::move(n);
    }

    std::vector<int> node_mesh_begin(model->nodes_count, -1);
    std::vector<int> node_mesh_end(model->nodes_count, -1);
    for (uint32_t n = 0; n < model->nodes_count; ++n) {
        if (model->nodes[n].mesh >= 0 && model->nodes[n].mesh < (int32_t)model->meshes_count) {
            int m = model->nodes[n].mesh;
            node_mesh_begin[n] = mapping.pod_mesh_of_gltf_mesh[m];
            node_mesh_end[n] = mapping.pod_mesh_of_gltf_mesh[m] + (int)model->meshes[m].primitives_count;
        }
    }

    for (uint32_t i = 0; i < model->nodes_count; ++i) {
        if (model->nodes[i].skin < 0 || model->nodes[i].skin >= (int32_t)model->skins_count) continue;
        const tg3_skin* skin = &model->skins[model->nodes[i].skin];
        if (skin->joints_count == 0) continue;
        int mesh_begin = node_mesh_begin[i];
        int mesh_end = node_mesh_end[i];
        if (mesh_begin < 0) continue;
        for (int mi = mesh_begin; mi < mesh_end; ++mi) {
            PODMesh& m = out.meshes[mi];
            if (m.bones_per_vertex <= 0) continue;
            std::vector<uint32_t> remapped_joints;
            remapped_joints.reserve(skin->joints_count);
            for (uint32_t j = 0; j < skin->joints_count; ++j) {
                int32_t ji = skin->joints[j];
                int rj = (ji >= 0 && ji < (int)mapping.gltf_to_pod_node.size()) ? mapping.gltf_to_pod_node[ji] : ji;
                remapped_joints.push_back(static_cast<uint32_t>(rj));
            }
            m.bone_batches.indices = std::move(remapped_joints);
            m.bone_batches.counts = {static_cast<uint32_t>(m.bone_batches.indices.size())};
            m.bone_batches.offsets = {0};
            m.bone_batches.count = 1;
            m.bone_batches.max_bones = static_cast<int>(m.bone_batches.indices.size());
            m.has_bone_batches = true;
        }
    }

    // ── Capture the bind (rest) pose in POD node-world space ──────────────
    {
        auto local_static = [](const PODNode& n, float m[16]) {
            if (n.has_matrix) { std::memcpy(m, n.matrix, sizeof(float) * 16); return; }
            float S[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            float R[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            float T[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            const float* q = n.rotation;
            const float x = -q[0], y = -q[1], z = -q[2], w = q[3];
            R[0]=1-2*(y*y+z*z); R[4]=2*(x*y-z*w);   R[8]=2*(x*z+y*w);
            R[1]=2*(x*y+z*w);   R[5]=1-2*(x*x+z*z); R[9]=2*(y*z-x*w);
            R[2]=2*(x*z-y*w);   R[6]=2*(y*z+x*w);   R[10]=1-2*(x*x+y*y);
            S[0]=n.scale[0]; S[5]=n.scale[1]; S[10]=n.scale[2];
            T[12]=n.translation[0]; T[13]=n.translation[1]; T[14]=n.translation[2];
            float tr[16]; gltf_mat4_mul(T, R, tr); gltf_mat4_mul(tr, S, m);
        };
        std::function<void(int, float[16], int)> rest_world =
            [&](int idx, float out16[16], int depth) {
                if (idx < 0 || idx >= (int)out.nodes.size() || depth > 128) {
                    std::memset(out16, 0, sizeof(float) * 16);
                    out16[0] = out16[5] = out16[10] = out16[15] = 1.0f;
                    return;
                }
                const PODNode& n = out.nodes[idx];
                float local[16]; local_static(n, local);
                if (n.parent_index >= 0 && n.parent_index != idx) {
                    float parent[16]; rest_world(n.parent_index, parent, depth + 1);
                    gltf_mat4_mul(parent, local, out16);
                } else {
                    std::memcpy(out16, local, sizeof(float) * 16);
                }
            };

        for (uint32_t si = 0; si < model->skins_count; ++si) {
            const tg3_skin* skin = &model->skins[si];
            if (skin->joints_count == 0) continue;

            std::vector<float> ibm;
            bool have_ibm = (skin->inverse_bind_matrices >= 0) &&
                            read_accessor_floats(model, skin->inverse_bind_matrices, ibm) &&
                            ibm.size() >= (size_t)skin->joints_count * 16;

            for (uint32_t j = 0; j < skin->joints_count; ++j) {
                const int32_t ji = skin->joints[j];
                if (ji < 0 || ji >= (int)mapping.gltf_to_pod_node.size()) continue;
                const int rj = mapping.gltf_to_pod_node[ji];
                if (rj < 0 || rj >= (int)out.nodes.size()) continue;
                PODNode& n = out.nodes[rj];
                rest_world(rj, n.bind_matrix, 0);
                n.has_bind_matrix = true;
            }

            // Precompute glTF bind world matrix for each joint in this skin:
            // M_j = (W_gltf(ji) * IBM_j) * scale
            std::vector<std::array<float, 16>> joint_M(skin->joints_count);
            for (uint32_t j = 0; j < skin->joints_count; ++j) {
                const int32_t ji = skin->joints[j];
                float W[16];
                if (ji >= 0 && ji < (int32_t)model->nodes_count) {
                    gltf_rest_world_matrix(model, skel.parent_of, ji, W, 0);
                } else {
                    std::memset(W, 0, sizeof(W));
                    W[0] = W[5] = W[10] = W[15] = 1.0f;
                }
                float M[16];
                if (have_ibm) {
                    gltf_mat4_mul(W, &ibm[(size_t)j * 16], M);
                } else {
                    std::memcpy(M, W, sizeof(M));
                }
                if (scale != 1.0f && scale > 0.0f) {
                    for (int c = 0; c < 15; ++c) M[c] *= scale;
                }
                std::memcpy(joint_M[j].data(), M, sizeof(M));
            }

            for (uint32_t nn = 0; nn < model->nodes_count; ++nn) {
                if (model->nodes[nn].skin != (int32_t)si) continue;
                if (node_mesh_begin[nn] < 0) continue;
                for (int pmi = node_mesh_begin[nn];
                     pmi < node_mesh_end[nn] && pmi < (int)out.meshes.size(); ++pmi) {
                    PODMesh& sm = out.meshes[pmi];
                    const int bpv = sm.bones_per_vertex;
                    if (bpv <= 0) continue;
                    const int nverts = (int)(sm.positions.size() / 3);
                    if (nverts <= 0) continue;
                    if (sm.bone_indices.size() < (size_t)nverts * bpv) continue;

                    for (int v = 0; v < nverts; ++v) {
                        float acc[3] = {0.0f, 0.0f, 0.0f};
                        float nacc[3] = {0.0f, 0.0f, 0.0f};
                        float wsum = 0.0f;
                        const float* p = &sm.positions[(size_t)v * 3];
                        const float* nv = ((size_t)v * 3 + 2 < sm.normals.size())
                                          ? &sm.normals[(size_t)v * 3] : nullptr;

                        for (int k = 0; k < bpv; ++k) {
                            float wk = (sm.bone_weights.size() > (size_t)v * bpv + k)
                                       ? sm.bone_weights[(size_t)v * bpv + k] : (k == 0 ? 1.0f : 0.0f);
                            if (wk <= 0.0f) continue;
                            int j = (int)std::lround(sm.bone_indices[(size_t)v * bpv + k]);
                            if (j < 0 || j >= (int)skin->joints_count) continue;

                            const float* B = joint_M[j].data();
                            acc[0] += wk * (B[0]*p[0] + B[4]*p[1] + B[8]*p[2]  + B[12]);
                            acc[1] += wk * (B[1]*p[0] + B[5]*p[1] + B[9]*p[2]  + B[13]);
                            acc[2] += wk * (B[2]*p[0] + B[6]*p[1] + B[10]*p[2] + B[14]);

                            if (nv) {
                                nacc[0] += wk * (B[0]*nv[0] + B[4]*nv[1] + B[8]*nv[2]);
                                nacc[1] += wk * (B[1]*nv[0] + B[5]*nv[1] + B[9]*nv[2]);
                                nacc[2] += wk * (B[2]*nv[0] + B[6]*nv[1] + B[10]*nv[2]);
                            }
                            wsum += wk;
                        }

                        if (wsum > 0.0f) {
                            float* out_p = &sm.positions[(size_t)v * 3];
                            out_p[0] = acc[0];
                            out_p[1] = acc[1];
                            out_p[2] = acc[2];
                            if (nv) {
                                float* out_n = &sm.normals[(size_t)v * 3];
                                float len = std::sqrt(nacc[0]*nacc[0] + nacc[1]*nacc[1] + nacc[2]*nacc[2]);
                                if (len > 1e-8f) {
                                    out_n[0] = nacc[0] / len;
                                    out_n[1] = nacc[1] / len;
                                    out_n[2] = nacc[2] / len;
                                }
                            }
                        }
                    }

                    sm.min_x = sm.min_y = sm.min_z =  1e30f;
                    sm.max_x = sm.max_y = sm.max_z = -1e30f;
                    for (size_t i = 0; i + 2 < sm.positions.size(); i += 3) {
                        sm.min_x = std::min(sm.min_x, sm.positions[i + 0]);
                        sm.max_x = std::max(sm.max_x, sm.positions[i + 0]);
                        sm.min_y = std::min(sm.min_y, sm.positions[i + 1]);
                        sm.max_y = std::max(sm.max_y, sm.positions[i + 1]);
                        sm.min_z = std::min(sm.min_z, sm.positions[i + 2]);
                        sm.max_z = std::max(sm.max_z, sm.positions[i + 2]);
                    }
                }
            }
        }
    }

    images.clear();
    std::vector<std::string> loaded_img_names(model->images_count);
    for (uint32_t img_idx = 0; img_idx < model->images_count; ++img_idx) {
        const tg3_image* img = &model->images[img_idx];
        std::string name = tg3_to_string(img->name);
        if (name.empty()) {
            std::string uri = tg3_to_string(img->uri);
            if (!uri.empty()) {
                size_t slash = uri.find_last_of("/\\");
                name = (slash != std::string::npos) ? uri.substr(slash + 1) : uri;
            }
        }
        if (name.empty()) name = "texture" + std::to_string(img_idx) + ".png";
        loaded_img_names[img_idx] = name;

        GLTFImageBuffer ib;
        ib.mime = tg3_to_string(img->mime_type);
        if (img->image.data && img->image.count > 0) {
            ib.data.assign(img->image.data, img->image.data + img->image.count);
        } else if (img->buffer_view >= 0 && img->buffer_view < (int32_t)model->buffer_views_count) {
            const tg3_buffer_view* bv = &model->buffer_views[img->buffer_view];
            if (bv->buffer >= 0 && bv->buffer < (int32_t)model->buffers_count) {
                const tg3_buffer* buf = &model->buffers[bv->buffer];
                if (buf->data.data && bv->byte_offset + bv->byte_length <= buf->data.count) {
                    ib.data.assign(buf->data.data + bv->byte_offset,
                                   buf->data.data + bv->byte_offset + bv->byte_length);
                }
            }
        }
        images.push_back(std::move(ib));
    }

    out.materials.clear();
    out.texture_filenames.clear();
    for (uint32_t mi = 0; mi < model->materials_count; ++mi) {
        const tg3_material* mat = &model->materials[mi];
        PODMaterial pm;
        pm.name = tg3_to_string(mat->name);
        pm.diffuse[0] = (float)mat->pbr_metallic_roughness.base_color_factor[0];
        pm.diffuse[1] = (float)mat->pbr_metallic_roughness.base_color_factor[1];
        pm.diffuse[2] = (float)mat->pbr_metallic_roughness.base_color_factor[2];
        pm.opacity    = (float)mat->pbr_metallic_roughness.base_color_factor[3];

        int32_t tex_idx = mat->pbr_metallic_roughness.base_color_texture.index;
        // SpecGloss fallback: albedo lives in the extension's diffuseTexture.
        if (tex_idx < 0) {
            SpecGlossInfo sg = read_spec_gloss(mat);
            if (sg.diffuse_texture >= 0) tex_idx = sg.diffuse_texture;
            if (sg.has_diffuse_factor) {
                pm.diffuse[0] = sg.diffuse_factor[0];
                pm.diffuse[1] = sg.diffuse_factor[1];
                pm.diffuse[2] = sg.diffuse_factor[2];
                pm.opacity    = sg.diffuse_factor[3];
            }
        }
        if (tex_idx >= 0 && tex_idx < (int32_t)model->textures_count) {
            int32_t src_img = texture_source_image(model, tex_idx);
            if (src_img >= 0 && src_img < (int32_t)loaded_img_names.size()) {
                std::string tname = loaded_img_names[src_img];
                auto it = std::find(out.texture_filenames.begin(), out.texture_filenames.end(), tname);
                int ti = (int)(it - out.texture_filenames.begin());
                if (it == out.texture_filenames.end()) {
                    out.texture_filenames.push_back(tname);
                }
                pm.diffuse_texture_index = ti;
            }
        }
        out.materials.push_back(std::move(pm));
    }

    if (pbr) {
        pbr->materials.clear();
        pbr->images.clear();
        pbr->images.reserve(images.size());
        for (const auto& img : images) {
            GLTFPBRInfo::Image pimg;
            pimg.mime = img.mime;
            pimg.data = img.data;
            pbr->images.push_back(std::move(pimg));
        }
        pbr->image_gltf_index.resize(images.size());
        for (size_t k = 0; k < images.size(); ++k) pbr->image_gltf_index[k] = (int)k;

        for (uint32_t mi = 0; mi < model->materials_count; ++mi) {
            const tg3_material* mat = &model->materials[mi];
            GLTFPBRMaterial pm;
            pm.base_color[0] = (float)mat->pbr_metallic_roughness.base_color_factor[0];
            pm.base_color[1] = (float)mat->pbr_metallic_roughness.base_color_factor[1];
            pm.base_color[2] = (float)mat->pbr_metallic_roughness.base_color_factor[2];
            pm.base_color[3] = (float)mat->pbr_metallic_roughness.base_color_factor[3];
            pm.metallic      = (float)mat->pbr_metallic_roughness.metallic_factor;
            pm.roughness     = (float)mat->pbr_metallic_roughness.roughness_factor;
            pm.occlusion     = (float)mat->occlusion_texture.strength;
            pm.emissive[0]   = (float)mat->emissive_factor[0];
            pm.emissive[1]   = (float)mat->emissive_factor[1];
            pm.emissive[2]   = (float)mat->emissive_factor[2];
            pm.normal_scale  = (float)mat->normal_texture.scale;
            pm.alpha_cutoff  = (float)mat->alpha_cutoff;

            std::string am_str = tg3_to_string(mat->alpha_mode);
            if (am_str == "BLEND") pm.alpha_mode = 2;
            else if (am_str == "MASK") pm.alpha_mode = 1;
            else pm.alpha_mode = 0;

            pm.double_sided  = mat->double_sided ? true : false;

            auto tex_to_img = [&](int32_t t_idx) -> int {
                return texture_source_image(model, t_idx);
            };

            pm.base_tex        = tex_to_img(mat->pbr_metallic_roughness.base_color_texture.index);
            pm.metalrough_tex  = tex_to_img(mat->pbr_metallic_roughness.metallic_roughness_texture.index);
            pm.normal_tex      = tex_to_img(mat->normal_texture.index);
            pm.occl_tex        = tex_to_img(mat->occlusion_texture.index);
            pm.emissive_tex    = tex_to_img(mat->emissive_texture.index);

            // SpecGloss fallback (see read_spec_gloss): when the core
            // metallic-roughness base color is absent, take the albedo (base
            // texture + color) from KHR_materials_pbrSpecularGlossiness so the
            // PBR path renders textured instead of flat/untextured.
            int32_t sg_diffuse_tex = -1;
            if (mat->pbr_metallic_roughness.base_color_texture.index < 0) {
                SpecGlossInfo sg = read_spec_gloss(mat);
                if (sg.diffuse_texture >= 0) {
                    sg_diffuse_tex = sg.diffuse_texture;
                    pm.base_tex = tex_to_img(sg.diffuse_texture);
                }
                if (sg.has_diffuse_factor) {
                    pm.base_color[0] = sg.diffuse_factor[0];
                    pm.base_color[1] = sg.diffuse_factor[1];
                    pm.base_color[2] = sg.diffuse_factor[2];
                    pm.base_color[3] = sg.diffuse_factor[3];
                }
            }

            // A3: honour the glTF sampler. Pixel-art assets (Minecraft skins
            // and friends) declare GL_NEAREST — remember which images those
            // are so the viewer can skip the trilinear/aniso blur.
            auto sampler_requests_nearest = [&](int32_t t_idx) -> bool {
                if (t_idx < 0 || t_idx >= (int32_t)model->textures_count) return false;
                int32_t s_idx = model->textures[t_idx].sampler;
                if (s_idx < 0 || s_idx >= (int32_t)model->samplers_count) return false;
                const tg3_sampler& smp = model->samplers[s_idx];
                // glTF filter values are the GL enums (see tiny_gltf_v3.h):
                // NEAREST = 9728, NEAREST_MIPMAP_NEAREST = 9984,
                // NEAREST_MIPMAP_LINEAR = 9986.
                if (smp.mag_filter == TG3_TEXTURE_FILTER_NEAREST) return true;
                if (smp.min_filter == TG3_TEXTURE_FILTER_NEAREST ||
                    smp.min_filter == TG3_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST ||
                    smp.min_filter == TG3_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR) return true;
                return false;
            };
            auto mark_nearest = [&](int32_t t_idx) {
                if (!sampler_requests_nearest(t_idx)) return;
                int img = tex_to_img(t_idx);
                if (img >= 0 && img < (int)pbr->images.size())
                    pbr->images[img].nearest = true;
            };
            mark_nearest(mat->pbr_metallic_roughness.base_color_texture.index);
            mark_nearest(mat->pbr_metallic_roughness.metallic_roughness_texture.index);
            mark_nearest(mat->normal_texture.index);
            mark_nearest(mat->occlusion_texture.index);
            mark_nearest(mat->emissive_texture.index);
            if (sg_diffuse_tex >= 0) mark_nearest(sg_diffuse_tex);  // SpecGloss albedo

            pbr->materials.push_back(std::move(pm));
        }
    }

    out.num_frames = 0;
    out.fps = 0.0f;
    out.total_vertices = 0;
    out.total_faces = 0;

    float minx =  1e30f, miny =  1e30f, minz =  1e30f;
    float maxx = -1e30f, maxy = -1e30f, maxz = -1e30f;

    for (auto& mesh : out.meshes) {
        if (mesh.num_vertices == 0) mesh.num_vertices = (int)(mesh.positions.size() / 3);
        if (mesh.num_faces == 0) mesh.num_faces = (int)(mesh.indices.size() / 3);
        out.total_vertices += mesh.num_vertices;
        out.total_faces    += mesh.num_faces;
        if (mesh.num_vertices > 0) {
            minx = std::min(minx, mesh.min_x); maxx = std::max(maxx, mesh.max_x);
            miny = std::min(miny, mesh.min_y); maxy = std::max(maxy, mesh.max_y);
            minz = std::min(minz, mesh.min_z); maxz = std::max(maxz, mesh.max_z);
        }
    }

    if (!out.meshes.empty() && out.total_vertices > 0) {
        out.min_x = minx; out.max_x = maxx;
        out.min_y = miny; out.max_y = maxy;
        out.min_z = minz; out.max_z = maxz;
        out.center_x = (minx + maxx) * 0.5f;
        out.center_y = (miny + maxy) * 0.5f;
        out.center_z = (minz + maxz) * 0.5f;
        float dx = maxx - minx, dy = maxy - miny, dz = maxz - minz;
        out.radius = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
        if (out.radius < 1.0f) out.radius = 1.0f;
    }

    return true;
}

static void sample_channel(const std::string& interp, int comps,
                           const std::vector<float>& times,
                           const std::vector<float>& vals,
                           int num_frames, float fps,
                           std::vector<float>& out_dense,
                           float scale = 1.0f) {
    if (times.empty() || vals.empty() || num_frames <= 0 || comps <= 0) return;
    out_dense.assign((size_t)num_frames * comps, 0.0f);

    bool is_cubic = (interp == "CUBICSPLINE" || interp == "CUBIC_SPLINE");
    bool is_step = (interp == "STEP");
    size_t stride = is_cubic ? (size_t)comps * 3 : (size_t)comps;
    size_t val_offset = is_cubic ? (size_t)comps : 0;

    auto get_val = [&](size_t k, int c) -> float {
        size_t idx = k * stride + val_offset + c;
        return (idx < vals.size()) ? vals[idx] : 0.0f;
    };
    auto get_in_tan = [&](size_t k, int c) -> float {
        size_t idx = k * stride + c;
        return (idx < vals.size()) ? vals[idx] : 0.0f;
    };
    auto get_out_tan = [&](size_t k, int c) -> float {
        size_t idx = k * stride + 2 * comps + c;
        return (idx < vals.size()) ? vals[idx] : 0.0f;
    };

    for (int f = 0; f < num_frames; ++f) {
        float t = static_cast<float>(f) / fps;
        float* out_val = &out_dense[(size_t)f * comps];

        if (t <= times.front() || times.size() == 1) {
            for (int c = 0; c < comps; ++c) out_val[c] = get_val(0, c);
        } else if (t >= times.back()) {
            size_t last = times.size() - 1;
            for (int c = 0; c < comps; ++c) out_val[c] = get_val(last, c);
        } else {
            size_t k = 0;
            while (k + 1 < times.size() && times[k + 1] < t) ++k;
            float t0 = times[k];
            float t1 = times[k + 1];
            float dt = t1 - t0;
            float alpha = (dt > 1e-6f) ? std::clamp((t - t0) / dt, 0.0f, 1.0f) : 0.0f;

            if (is_step) {
                for (int c = 0; c < comps; ++c) out_val[c] = get_val(k, c);
            } else if (is_cubic) {
                float a2 = alpha * alpha;
                float a3 = a2 * alpha;
                float h00 = 2.0f * a3 - 3.0f * a2 + 1.0f;
                float h10 = a3 - 2.0f * a2 + alpha;
                float h01 = -2.0f * a3 + 3.0f * a2;
                float h11 = a3 - a2;

                for (int c = 0; c < comps; ++c) {
                    float p0 = get_val(k, c);
                    float m0 = get_out_tan(k, c) * dt;
                    float p1 = get_val(k + 1, c);
                    float m1 = get_in_tan(k + 1, c) * dt;
                    out_val[c] = h00 * p0 + h10 * m0 + h01 * p1 + h11 * m1;
                }
                if (comps == 4) {
                    float len = std::sqrt(out_val[0]*out_val[0] + out_val[1]*out_val[1] +
                                          out_val[2]*out_val[2] + out_val[3]*out_val[3]);
                    if (len > 1e-6f) {
                        for (int c = 0; c < 4; ++c) out_val[c] /= len;
                    } else {
                        out_val[0] = 0; out_val[1] = 0; out_val[2] = 0; out_val[3] = 1;
                    }
                }
            } else {
                if (comps == 4) {
                    float q0[4] = {get_val(k, 0), get_val(k, 1), get_val(k, 2), get_val(k, 3)};
                    float q1[4] = {get_val(k+1, 0), get_val(k+1, 1), get_val(k+1, 2), get_val(k+1, 3)};
                    float dot = q0[0]*q1[0] + q0[1]*q1[1] + q0[2]*q1[2] + q0[3]*q1[3];
                    if (dot < 0.0f) {
                        for (int c = 0; c < 4; ++c) q1[c] = -q1[c];
                        dot = -dot;
                    }
                    float s0 = 1.0f - alpha;
                    float s1 = alpha;
                    if (dot < 0.9995f) {
                        float theta = std::acos(std::clamp(dot, -1.0f, 1.0f));
                        float sin_theta = std::sin(theta);
                        if (sin_theta > 1e-5f) {
                            s0 = std::sin((1.0f - alpha) * theta) / sin_theta;
                            s1 = std::sin(alpha * theta) / sin_theta;
                        }
                    }
                    for (int c = 0; c < 4; ++c) out_val[c] = s0 * q0[c] + s1 * q1[c];
                    float len = std::sqrt(out_val[0]*out_val[0] + out_val[1]*out_val[1] +
                                          out_val[2]*out_val[2] + out_val[3]*out_val[3]);
                    if (len > 1e-6f) {
                        for (int c = 0; c < 4; ++c) out_val[c] /= len;
                    } else {
                        out_val[0] = 0; out_val[1] = 0; out_val[2] = 0; out_val[3] = 1;
                    }
                } else {
                    for (int c = 0; c < comps; ++c) {
                        out_val[c] = (1.0f - alpha) * get_val(k, c) + alpha * get_val(k + 1, c);
                    }
                }
            }
        }

        if (scale != 1.0f && comps == 3) {
            for (int c = 0; c < 3; ++c) out_val[c] *= scale;
        }
    }
}

} // namespace
} // namespace av

namespace av {

bool gltf_import_glb(const std::string& path, PODModel& out,
                     std::vector<GLTFImageBuffer>& images, std::string* err,
                     GLTFPBRInfo* pbr, float scale, bool rigid_skin) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) { if (err) *err = "cannot open: " + path; return false; }
    std::streamsize size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> file(static_cast<size_t>(size));
    if (size > 0) f.read(reinterpret_cast<char*>(file.data()), size);
    if (!f) { if (err) *err = "read failed"; return false; }

    tg3_model model;
    tg3_error_stack errors;
    std::memset(&model, 0, sizeof(model));
    tg3_error_stack_init(&errors);

    tg3_parse_options opts;
    tg3_parse_options_init(&opts);
    opts.strictness = TG3_PERMISSIVE;
    opts.images_as_is = 1;

    std::string base_dir;
    size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) base_dir = path.substr(0, slash);

    tg3_error_code code = tg3_parse_auto(&model, &errors, file.data(), file.size(),
                                         base_dir.c_str(), (uint32_t)base_dir.size(), &opts);

    if (code != TG3_OK) {
        if (err) {
            if (tg3_errors_count(&errors) > 0) {
                *err = tg3_errors_get(&errors, 0)->message;
            } else {
                *err = "glTF parse error code " + std::to_string(code);
            }
        }
        tg3_error_stack_free(&errors);
        tg3_model_free(&model);
        return false;
    }

    bool ok = build_pod_from_tg3(&model, out, images, pbr, err, scale, rigid_skin);
    tg3_error_stack_free(&errors);
    tg3_model_free(&model);
    return ok;
}

bool gltf_import_gltf(const std::string& path, PODModel& out,
                      std::vector<GLTFImageBuffer>& images, std::string* err,
                      GLTFPBRInfo* pbr, float scale, bool rigid_skin) {
    return gltf_import_glb(path, out, images, err, pbr, scale, rigid_skin);
}

// ─── Clip timing: ONE rule, shared by the bake and the inspector ────────────
// gltf_import_all_clips writes the POD; gltf_inspect_animations fills the
// clip browser. If they compute fps and frame count separately they drift, and
// the browser advertises a clip the converter never produced. So both call the
// rule below.
//
// The rule: the clip span is the 90th-percentile last-key time of MULTI-key
// samplers only — a lone 1-key "constant" channel, or one stray key parked far
// out on the timeline, must not stretch the whole clip. fps is the engine-parity
// target when one is given (the game hardcodes 24.0 FPS), else derived from the
// source key density snapped to a common authored rate.
struct ClipTiming { float fps = 24.0f; float duration = 0.0f; };

static ClipTiming derive_clip_timing(const tg3_model& model, const tg3_animation* anim,
                                     float target_fps) {
    ClipTiming out;
    auto load_acc = [&](int32_t acc_idx) -> std::vector<float> {
        std::vector<float> buf;
        if (!read_accessor_floats(&model, acc_idx, buf)) buf.clear();
        return buf;
    };

    std::vector<float> sampler_ends;   // last key time of each animated sampler
    std::vector<float> dense_deltas;   // per-sampler median spacing, SAMPLED samplers
    std::vector<float> sparse_deltas;  // same, but including 2-key constants
    for (uint32_t si = 0; si < anim->samplers_count; ++si) {
        int32_t in_acc = anim->samplers[si].input;
        if (in_acc < 0 || in_acc >= (int32_t)model.accessors_count) continue;
        const std::vector<float>& times = load_acc(in_acc);
        if (times.size() < 2) continue;               // skip constant channels
        sampler_ends.push_back(times.back());
        std::vector<float> d;
        d.reserve(times.size() - 1);
        for (size_t k = 1; k < times.size(); ++k) {
            float dt = times[k] - times[k - 1];
            if (dt > 1e-6f) d.push_back(dt);
        }
        if (!d.empty()) {
            std::sort(d.begin(), d.end());
            const float med = d[d.size() / 2];        // median spacing
            sparse_deltas.push_back(med);
            // A "constant" channel can be stored as exactly TWO keys spanning the
            // whole clip, and in a rig export those usually outnumber the moving
            // ones. Their spacing is the clip length, not a key density, so they
            // must not be allowed to drag the estimate down to 1-2 fps (which is
            // what a plain median over all samplers does: soldier's walk clip is
            // 70 constant tracks and 2 dense ones). Prefer genuinely sampled
            // channels whenever any exist.
            if (times.size() >= 4) dense_deltas.push_back(med);
        }
    }
    std::vector<float> key_deltas = dense_deltas.empty() ? sparse_deltas : dense_deltas;

    float max_time = 0.0f;
    if (!sampler_ends.empty()) {
        std::sort(sampler_ends.begin(), sampler_ends.end());
        // 90th-percentile end time as the span, with a gross-outlier guard.
        float p90 = sampler_ends[(size_t)std::floor(0.9 * (sampler_ends.size() - 1))];
        float median_end = sampler_ends[sampler_ends.size() / 2];
        max_time = p90;
        if (max_time > median_end * 4.0f && median_end > 0.0f)
            max_time = median_end;                    // reject gross outlier
    } else {
        // No multi-key samplers at all -> fall back to any last key time.
        for (uint32_t si = 0; si < anim->samplers_count; ++si) {
            int32_t in_acc = anim->samplers[si].input;
            if (in_acc < 0 || in_acc >= (int32_t)model.accessors_count) continue;
            const std::vector<float>& times = load_acc(in_acc);
            if (!times.empty()) max_time = std::max(max_time, times.back());
        }
    }

    // Engine-parity default: 24 fps, not 30 (see target_fps docs above).
    float derived_fps = (target_fps > 0.0f) ? target_fps : 30.0f;
    if (!key_deltas.empty()) {
        std::sort(key_deltas.begin(), key_deltas.end());
        float med_dt = key_deltas[key_deltas.size() / 2];
        if (med_dt > 1e-6f) {
            float raw = 1.0f / med_dt;
            const float candidates[] = {24.0f, 25.0f, 30.0f, 48.0f, 50.0f, 60.0f};
            float best = 30.0f, best_err = 1e30f;
            for (float c : candidates) {
                float e = std::fabs(c - raw);
                if (e < best_err) { best_err = e; best = c; }
            }
            derived_fps = (best_err <= best * 0.15f) ? best
                          : std::clamp(std::round(raw), 1.0f, 120.0f);
        }
    }
    if (target_fps > 0.0f) derived_fps = target_fps;   // engine parity: bypass snapping

    out.fps = derived_fps;
    out.duration = max_time;
    return out;
}

// Companion motions.json clips carry real per-track `times`, so `--anim-fps 0`
// ("keep the authored rate") means here exactly what it means for in-GLB clips.
// The companion path used to hardcode 24 whenever the target was 0, so a JSON
// authored at 30 fps was silently resampled to 24 while the in-GLB path derived
// 30 — one flag, two behaviours. Derive from the same key density, snap to the
// same rate list, and fall back to engine-parity 24 only when there is nothing
// to measure.
static float derive_companion_fps(const tg3json_value* clip, float target_fps) {
    if (target_fps > 0.0f) return target_fps;

    std::vector<float> dense_deltas;   // per-track median spacing, SAMPLED tracks
    std::vector<float> sparse_deltas;  // same, but including 2-key constants
    const tg3json_value* tracks = tg3json_object_get(clip, "tracks");
    if (tracks && tracks->type == TG3JSON_ARRAY) {
        size_t n = tg3json_array_size(tracks);
        for (size_t ti = 0; ti < n; ++ti) {
            const tg3json_value* t = tg3json_array_get(tracks, ti);
            if (!t || t->type != TG3JSON_OBJECT) continue;
            const tg3json_value* tv = tg3json_object_get(t, "times");
            if (!tv || tv->type != TG3JSON_ARRAY) continue;
            size_t tn = tg3json_array_size(tv);
            if (tn < 2) continue;
            std::vector<float> ts;
            ts.reserve(tn);
            for (size_t k = 0; k < tn; ++k) {
                const tg3json_value* v = tg3json_array_get(tv, k);
                if (v->type == TG3JSON_REAL) ts.push_back((float)v->u.real);
                else if (v->type == TG3JSON_INT) ts.push_back((float)v->u.integer);
            }
            std::vector<float> d;
            d.reserve(ts.size() > 0 ? ts.size() - 1 : 0);
            for (size_t k = 1; k < ts.size(); ++k) {
                float dt = ts[k] - ts[k - 1];
                if (dt > 1e-6f) d.push_back(dt);
            }
            if (!d.empty()) {
                std::sort(d.begin(), d.end());
                const float med = d[d.size() / 2];
                sparse_deltas.push_back(med);
                // Most companion tracks are 2-key constants whose only spacing is
                // the clip length; measuring them would report ~1 fps for a clip
                // authored at 30 (soldier's walk: 70 constant tracks vs 2 dense).
                // Measure key density from tracks that are actually sampled.
                if (tn >= 4) dense_deltas.push_back(med);
            }
        }
    }
    const std::vector<float>& deltas = dense_deltas.empty() ? sparse_deltas : dense_deltas;
    if (deltas.empty()) return 24.0f;                  // engine parity fallback

    std::vector<float> sorted(deltas);
    std::sort(sorted.begin(), sorted.end());
    const float med_dt = sorted[sorted.size() / 2];
    if (med_dt <= 1e-6f) return 24.0f;
    const float raw = 1.0f / med_dt;
    const float candidates[] = {24.0f, 25.0f, 30.0f, 48.0f, 50.0f, 60.0f};
    float best = 30.0f, best_err = 1e30f;
    for (float c : candidates) {
        float e = std::fabs(c - raw);
        if (e < best_err) { best_err = e; best = c; }
    }
    return (best_err <= best * 0.15f) ? best : std::clamp(std::round(raw), 1.0f, 120.0f);
}

bool gltf_import_all_clips(const std::string& path,
                           std::vector<std::pair<std::string, PODModel>>& out_clips,
                           std::string* err,
                           float scale,
                           float target_fps,
                           bool rigid_skin) {
    out_clips.clear();
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) { if (err) *err = "cannot open: " + path; return false; }
    std::streamsize size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> file(static_cast<size_t>(size));
    if (size > 0) f.read(reinterpret_cast<char*>(file.data()), size);
    if (!f) { if (err) *err = "read failed"; return false; }

    tg3_model model;
    tg3_error_stack errors;
    std::memset(&model, 0, sizeof(model));
    tg3_error_stack_init(&errors);

    tg3_parse_options opts;
    tg3_parse_options_init(&opts);
    opts.strictness = TG3_PERMISSIVE;
    opts.images_as_is = 1;

    std::string base_dir;
    size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) base_dir = path.substr(0, slash);

    tg3_error_code code = tg3_parse_auto(&model, &errors, file.data(), file.size(),
                                         base_dir.c_str(), (uint32_t)base_dir.size(), &opts);

    if (code != TG3_OK) {
        if (err) {
            if (tg3_errors_count(&errors) > 0) {
                *err = tg3_errors_get(&errors, 0)->message;
            } else {
                *err = "glTF parse error code " + std::to_string(code);
            }
        }
        tg3_error_stack_free(&errors);
        tg3_model_free(&model);
        return false;
    }

    PODModel base_model;
    std::vector<GLTFImageBuffer> imgs;
    if (!build_pod_from_tg3(&model, base_model, imgs, nullptr, err, scale, rigid_skin)) {
        tg3_error_stack_free(&errors);
        tg3_model_free(&model);
        return false;
    }

    if (model.animations_count == 0) {
        tg3_error_stack_free(&errors);
        tg3_model_free(&model);
        std::string comp = gltf_find_companion_motions(path);
        if (!comp.empty()) {
            return gltf_import_companion_motions(path, comp, out_clips, err, scale, target_fps, rigid_skin);
        }
        return true;
    }

    GltfPodMapping mapping = compute_gltf_pod_mapping(&model);
    GltfSkeletonInfo skel = analyze_skeleton(&model, scale);

    for (uint32_t a_idx = 0; a_idx < model.animations_count; ++a_idx) {
        const tg3_animation* anim = &model.animations[a_idx];
        std::string clip_name = tg3_to_string(anim->name);
        if (clip_name.empty()) clip_name = "anim_" + std::to_string(a_idx);
        for (char& c : clip_name) {
            if (c == ':' || c == '/' || c == '\\' || c == ' ') c = '_';
        }

        PODModel clip_model = base_model;
        clip_model.meshes.clear();
        clip_model.materials.clear();
        clip_model.texture_filenames.clear();
        for (auto& n : clip_model.nodes) {
            n.object_index = -1;
            n.material_index = -1;
            n.anim_translation.clear();
            n.anim_rotation.clear();
            n.anim_scale.clear();
            n.anim_flags = 0;
            // BUG D fix (pod_master/05 §4): clip nodes get dense per-frame T/R/S
            // channels below, but get_node_matrix() prefers a static node.matrix
            // (has_matrix branch) OVER the anim channels. A glTF node authored
            // with a matrix (instead of TRS) copies has_matrix=true from the base
            // model, so it would freeze at its rest pose and never animate. Since
            // every clip node is fully animated (dense arrays + anim_flags), drop
            // the static matrix so the animated T·R·S path is taken.
            n.has_matrix = false;
        }

        // By value, for the same reason as build_pod_from_tg3's `load` above:
        // a reference to a shared scratch buffer aliases the moment two
        // results are alive at once.
        auto load_acc = [&](int32_t acc_idx) -> std::vector<float> {
            std::vector<float> buf;
            if (!read_accessor_floats(&model, acc_idx, buf)) buf.clear();
            return buf;
        };

        // Clip span + fps come from the shared rule (derive_clip_timing above),
        // so the clip browser and this bake can never disagree.
        const ClipTiming timing = derive_clip_timing(model, anim, target_fps);
        clip_model.fps = timing.fps;
        clip_model.num_frames = std::max(1, static_cast<int>(std::lround(timing.duration * clip_model.fps)) + 1);

        for (uint32_t ci = 0; ci < anim->channels_count; ++ci) {
            const tg3_animation_channel* ch = &anim->channels[ci];
            if (ch->target.node < 0 || ch->target.node >= (int32_t)model.nodes_count) continue;
            int pod_node_idx = mapping.gltf_to_pod_node[ch->target.node];
            if (pod_node_idx < 0 || pod_node_idx >= (int)clip_model.nodes.size()) continue;
            if (ch->sampler < 0 || ch->sampler >= (int32_t)anim->samplers_count) continue;
            const tg3_animation_sampler* sm = &anim->samplers[ch->sampler];

            const std::vector<float>& times_ref = load_acc(sm->input);
            std::vector<float> times(times_ref.begin(), times_ref.end());
            const std::vector<float>& vals_ref = load_acc(sm->output);
            std::vector<float> vals(vals_ref.begin(), vals_ref.end());
            if (times.empty() || vals.empty()) continue;

            std::string path_str = tg3_to_string(ch->target.path);
            int comps = (path_str == "rotation") ? 4 : 3;
            PODNode& node = clip_model.nodes[pod_node_idx];
            std::string interp_str = tg3_to_string(sm->interpolation);
            int target_gn = ch->target.node;

            if (path_str == "translation") {
                sample_channel(interp_str, comps, times, vals, clip_model.num_frames, clip_model.fps, node.anim_translation, 1.0f);
                if (skel.is_root_joint[target_gn]) {
                    float S_wrap = skel.s_wrapper_of_joint[target_gn];
                    const auto& Q_wrap = skel.q_wrapper_of_joint[target_gn];
                    const auto& P_wrap = skel.p_wrapper_of_joint[target_gn];
                    for (int f = 0; f < clip_model.num_frames; ++f) {
                        float* t = &node.anim_translation[f * 3];
                        float raw_scaled[3] = { t[0] * S_wrap, t[1] * S_wrap, t[2] * S_wrap };
                        float rot_t[3];
                        quat_rotate_vec(Q_wrap.data(), raw_scaled[0], raw_scaled[1], raw_scaled[2], rot_t[0], rot_t[1], rot_t[2]);
                        t[0] = (P_wrap[0] + rot_t[0]) * scale;
                        t[1] = (P_wrap[1] + rot_t[1]) * scale;
                        t[2] = (P_wrap[2] + rot_t[2]) * scale;
                    }
                } else if (skel.is_joint[target_gn]) {
                    float sk_s = skel.skel_scale_of_joint[target_gn];
                    for (int f = 0; f < clip_model.num_frames; ++f) {
                        float* t = &node.anim_translation[f * 3];
                        t[0] *= sk_s;
                        t[1] *= sk_s;
                        t[2] *= sk_s;
                    }
                } else {
                    if (skel.parent_of[target_gn] == -1 && scale != 1.0f) {
                        for (int f = 0; f < clip_model.num_frames; ++f) {
                            float* t = &node.anim_translation[f * 3];
                            t[0] *= scale;
                            t[1] *= scale;
                            t[2] *= scale;
                        }
                    }
                }
                node.anim_flags |= 1;
            } else if (path_str == "rotation") {
                sample_channel(interp_str, comps, times, vals, clip_model.num_frames, clip_model.fps, node.anim_rotation, 1.0f);
                if (skel.is_root_joint[target_gn]) {
                    const auto& Q_wrap = skel.q_wrapper_of_joint[target_gn];
                    for (int f = 0; f < clip_model.num_frames; ++f) {
                        float* q = &node.anim_rotation[f * 4];
                        float q_world[4];
                        quat_mul(Q_wrap.data(), q, q_world);
                        q[0] = -q_world[0];
                        q[1] = -q_world[1];
                        q[2] = -q_world[2];
                        q[3] = q_world[3];
                    }
                } else {
                    for (size_t ki = 0; ki + 3 < node.anim_rotation.size(); ki += 4) {
                        node.anim_rotation[ki + 0] = -node.anim_rotation[ki + 0];
                        node.anim_rotation[ki + 1] = -node.anim_rotation[ki + 1];
                        node.anim_rotation[ki + 2] = -node.anim_rotation[ki + 2];
                    }
                }
                node.anim_flags |= 2;
            } else if (path_str == "scale") {
                std::vector<float> s3;
                sample_channel(interp_str, comps, times, vals,
                               clip_model.num_frames, clip_model.fps, s3, 1.0f);
                node.anim_scale.clear();
                node.anim_scale.reserve((s3.size() / 3) * 7);
                for (size_t ki = 0; ki + 2 < s3.size(); ki += 3) {
                    node.anim_scale.push_back(s3[ki + 0]);
                    node.anim_scale.push_back(s3[ki + 1]);
                    node.anim_scale.push_back(s3[ki + 2]);
                    node.anim_scale.push_back(0.0f); // scale-orientation quat = identity
                    node.anim_scale.push_back(0.0f);
                    node.anim_scale.push_back(0.0f);
                    node.anim_scale.push_back(1.0f);
                }
                node.anim_flags |= 4;
            }
        }

        // ── Fill in constant T/R/S arrays for nodes whose channels were NOT
        // animated by this clip (e.g. a walk cycle animates the legs but not
        // the fingers). pod_loader.cpp lines 1297–1328 reads a single dense
        // array element when the flag bit IS set; for unset bits it uses the
        // static PODNode.translation/rotation/scale field. So we MUST fill in
        // a dense array for every channel, set the flag for it, and write the
        // node's rest-pose value into every frame.
        //
        // SCALE FILL-IN — writes 7-float/key [sx,sy,sz, 0,0,0,1] directly.
        // This is critical: if we wrote 3*NumFrame floats and NumFrame is a
        // multiple of 7 (7,14,21,28,...) then 3*NumFrame % 7 == 0. The writer's
        // expansion guard (size % 7 != 0 && size % 3 == 0) is FALSE, the raw
        // 3-float array passes through, and pod_loader reads it with stride 7
        // → bone-matrix corruption. Always pre-expand to 7/key here.
        for (auto& node : clip_model.nodes) {
            if (node.anim_translation.empty()) {
                // Constant translation channel: repeat the rest-pose value.
                node.anim_translation.assign((size_t)clip_model.num_frames * 3, 0.0f);
                for (int f = 0; f < clip_model.num_frames; ++f) {
                    node.anim_translation[f * 3 + 0] = node.translation[0];
                    node.anim_translation[f * 3 + 1] = node.translation[1];
                    node.anim_translation[f * 3 + 2] = node.translation[2];
                }
                node.anim_flags |= 1;
            }
            if (node.anim_rotation.empty()) {
                // Constant rotation channel: repeat the rest-pose quaternion.
                // Guard against the zero-quat edge case (uninitialized node):
                // force w=1 when x,y,z,w are all zero.
                node.anim_rotation.assign((size_t)clip_model.num_frames * 4, 0.0f);
                const float rw = (node.rotation[3] == 0.0f && node.rotation[0] == 0.0f
                                  && node.rotation[1] == 0.0f && node.rotation[2] == 0.0f)
                                 ? 1.0f : node.rotation[3];
                for (int f = 0; f < clip_model.num_frames; ++f) {
                    node.anim_rotation[f * 4 + 0] = node.rotation[0];
                    node.anim_rotation[f * 4 + 1] = node.rotation[1];
                    node.anim_rotation[f * 4 + 2] = node.rotation[2];
                    node.anim_rotation[f * 4 + 3] = rw;
                }
                node.anim_flags |= 2;
            }
            if (node.anim_scale.empty()) {
                // Constant scale channel: repeat the rest-pose scale as 7-float/key.
                // See the long comment above — NEVER write 3-float/key here.
                const float sx = (node.scale[0] != 0.0f) ? node.scale[0] : 1.0f;
                const float sy = (node.scale[1] != 0.0f) ? node.scale[1] : 1.0f;
                const float sz = (node.scale[2] != 0.0f) ? node.scale[2] : 1.0f;
                node.anim_scale.reserve((size_t)clip_model.num_frames * 7);
                for (int f = 0; f < clip_model.num_frames; ++f) {
                    node.anim_scale.push_back(sx);
                    node.anim_scale.push_back(sy);
                    node.anim_scale.push_back(sz);
                    node.anim_scale.push_back(0.0f); // scale-orientation quaternion = identity
                    node.anim_scale.push_back(0.0f);
                    node.anim_scale.push_back(0.0f);
                    node.anim_scale.push_back(1.0f);
                }
                node.anim_flags |= 4;
            }
            // After fill-in every node has all three dense channels with flags set.
            // Ensure anim_flags is exactly 7 (T|R|S) — it may already be 7 from
            // the channel loop above, but forcing here covers the edge case of a
            // node whose fill-in channels somehow left a bit unset.
            node.anim_flags = 7;
        }

        clip_model.num_mesh_nodes = 0;
        out_clips.emplace_back(clip_name, std::move(clip_model));
    }

    tg3_error_stack_free(&errors);
    tg3_model_free(&model);
    return true;
}

std::string gltf_find_companion_motions(const std::string& glb_path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p(glb_path);
    fs::path dir = p.parent_path();
    if (dir.empty()) dir = ".";
    std::string stem = p.stem().string();

    std::vector<fs::path> candidates = {
        dir / "motions.json",
        dir / (stem + "_motions.json"),
        dir / (stem + ".motions.json"),
        dir / "Motions.json"
    };
    for (const auto& c : candidates) {
        if (fs::exists(c, ec) && fs::is_regular_file(c, ec)) {
            return c.string();
        }
    }
    return "";
}

bool gltf_inspect_animations(const std::string& glb_path,
                             std::vector<AnimationClipSummary>& in_glb_clips,
                             std::vector<AnimationClipSummary>& json_clips,
                             std::string* companion_json_path,
                             float target_fps) {
    in_glb_clips.clear();
    json_clips.clear();
    if (companion_json_path) companion_json_path->clear();

    // 1. Inspect embedded GLB animations
    std::ifstream f(glb_path, std::ios::binary | std::ios::ate);
    if (f.is_open()) {
        std::streamsize size = f.tellg();
        f.seekg(0);
        std::vector<uint8_t> file(static_cast<size_t>(size));
        if (size > 0) f.read(reinterpret_cast<char*>(file.data()), size);
        if (f) {
            tg3_model model;
            tg3_error_stack errors;
            std::memset(&model, 0, sizeof(model));
            tg3_error_stack_init(&errors);

            tg3_parse_options opts;
            tg3_parse_options_init(&opts);
            opts.strictness = TG3_PERMISSIVE;
            opts.images_as_is = 1;

            std::string base_dir;
            size_t slash = glb_path.find_last_of("/\\");
            if (slash != std::string::npos) base_dir = glb_path.substr(0, slash);

            tg3_error_code code = tg3_parse_auto(&model, &errors, file.data(), file.size(),
                                                 base_dir.c_str(), (uint32_t)base_dir.size(), &opts);
            if (code == TG3_OK) {
                for (uint32_t a = 0; a < model.animations_count; ++a) {
                    const tg3_animation* anim = &model.animations[a];
                    AnimationClipSummary s;
                    s.name = anim->name.data ? std::string(anim->name.data, anim->name.len) : ("anim_" + std::to_string(a));
                    s.origin = "In-GLB";
                    // Same rule the bake uses (derive_clip_timing), so the browser
                    // reports the clip the converter will actually produce.
                    const ClipTiming timing = derive_clip_timing(model, anim, target_fps);
                    s.fps = timing.fps;
                    s.duration = timing.duration;
                    s.num_frames = std::max(1, static_cast<int>(std::lround(timing.duration * timing.fps)) + 1);
                    in_glb_clips.push_back(s);
                }
            }
            tg3_error_stack_free(&errors);
            tg3_model_free(&model);
        }
    }

    // 2. Inspect companion motions.json
    std::string json_path = gltf_find_companion_motions(glb_path);
    if (!json_path.empty()) {
        if (companion_json_path) *companion_json_path = json_path;
        std::ifstream jf(json_path, std::ios::binary | std::ios::ate);
        if (jf.is_open()) {
            std::streamsize jsize = jf.tellg();
            jf.seekg(0);
            std::string jdata(static_cast<size_t>(jsize), '\0');
            if (jsize > 0) jf.read(&jdata[0], jsize);
            if (jf) {
                tg3json_value root;
                const char* err_pos = nullptr;
                if (tg3json_parse_n(jdata.data(), jdata.size(), 0, &root, &err_pos) == 1) {
                    const tg3json_value* clips_val = tg3json_object_get(&root, "clips");
                    if (clips_val && clips_val->type == TG3JSON_ARRAY) {
                        size_t ccount = tg3json_array_size(clips_val);
                        for (size_t ci = 0; ci < ccount; ++ci) {
                            const tg3json_value* cval = tg3json_array_get(clips_val, ci);
                            if (!cval || cval->type != TG3JSON_OBJECT) continue;
                            AnimationClipSummary s;
                            const tg3json_value* nval = tg3json_object_get(cval, "name");
                            if (nval && nval->type == TG3JSON_STRING) {
                                s.name = std::string(nval->u.string.ptr, nval->u.string.len);
                            } else {
                                s.name = "clip_" + std::to_string(ci);
                            }
                            const tg3json_value* dval = tg3json_object_get(cval, "duration");
                            if (dval) {
                                if (dval->type == TG3JSON_REAL) s.duration = (float)dval->u.real;
                                else if (dval->type == TG3JSON_INT) s.duration = (float)dval->u.integer;
                            }
                            // `--anim-fps 0` derives the authored rate from the
                            // companion tracks' key density, exactly as it does
                            // for in-GLB clips (see derive_companion_fps).
                            s.fps = derive_companion_fps(cval, target_fps);
                            s.num_frames = std::max(1, static_cast<int>(std::lround(s.duration * s.fps)) + 1);
                            s.origin = "motions.json";
                            json_clips.push_back(s);
                        }
                    }
                    tg3json_value_free(&root);
                }
            }
        }
    }

    return (!in_glb_clips.empty() || !json_clips.empty());
}

bool gltf_import_companion_motions(const std::string& glb_path,
                                   const std::string& motions_json_path,
                                   std::vector<std::pair<std::string, PODModel>>& out_clips,
                                   std::string* err,
                                   float scale,
                                   float target_fps,
                                   bool rigid_skin) {
    out_clips.clear();
    std::ifstream f(glb_path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) { if (err) *err = "cannot open glb: " + glb_path; return false; }
    std::streamsize size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> file(static_cast<size_t>(size));
    if (size > 0) f.read(reinterpret_cast<char*>(file.data()), size);
    if (!f) { if (err) *err = "read glb failed"; return false; }

    tg3_model model;
    tg3_error_stack errors;
    std::memset(&model, 0, sizeof(model));
    tg3_error_stack_init(&errors);

    tg3_parse_options opts;
    tg3_parse_options_init(&opts);
    opts.strictness = TG3_PERMISSIVE;
    opts.images_as_is = 1;

    std::string base_dir;
    size_t slash = glb_path.find_last_of("/\\");
    if (slash != std::string::npos) base_dir = glb_path.substr(0, slash);

    tg3_error_code code = tg3_parse_auto(&model, &errors, file.data(), file.size(),
                                         base_dir.c_str(), (uint32_t)base_dir.size(), &opts);
    if (code != TG3_OK) {
        if (err) *err = "parse glb error";
        tg3_error_stack_free(&errors);
        tg3_model_free(&model);
        return false;
    }

    PODModel base_model;
    std::vector<GLTFImageBuffer> imgs;
    if (!build_pod_from_tg3(&model, base_model, imgs, nullptr, err, scale, rigid_skin)) {
        tg3_error_stack_free(&errors);
        tg3_model_free(&model);
        return false;
    }

    tg3_error_stack_free(&errors);
    tg3_model_free(&model);

    // Read companion motions JSON
    std::ifstream jf(motions_json_path, std::ios::binary | std::ios::ate);
    if (!jf.is_open()) {
        if (err) *err = "cannot open motions json: " + motions_json_path;
        return false;
    }
    std::streamsize jsize = jf.tellg();
    jf.seekg(0);
    std::string jdata(static_cast<size_t>(jsize), '\0');
    if (jsize > 0) jf.read(&jdata[0], jsize);
    if (!jf) {
        if (err) *err = "read motions json failed";
        return false;
    }

    tg3json_value root;
    const char* err_pos = nullptr;
    if (tg3json_parse_n(jdata.data(), jdata.size(), 0, &root, &err_pos) != 1) {
        if (err) *err = "motions json syntax error";
        return false;
    }

    // Identify root joints from rigBinding (where parent == null)
    std::unordered_set<std::string> root_bone_names;
    const tg3json_value* rb_val = tg3json_object_get(&root, "rigBinding");
    if (rb_val && rb_val->type == TG3JSON_ARRAY) {
        size_t rb_count = tg3json_array_size(rb_val);
        for (size_t ri = 0; ri < rb_count; ++ri) {
            const tg3json_value* r_entry = tg3json_array_get(rb_val, ri);
            if (!r_entry || r_entry->type != TG3JSON_OBJECT) continue;
            const tg3json_value* p_val = tg3json_object_get(r_entry, "parent");
            if (!p_val || p_val->type == TG3JSON_NULL) {
                const tg3json_value* n_val = tg3json_object_get(r_entry, "name");
                if (n_val && n_val->type == TG3JSON_STRING) {
                    root_bone_names.insert(std::string(n_val->u.string.ptr, n_val->u.string.len));
                }
            }
        }
    }

    const tg3json_value* clips_val = tg3json_object_get(&root, "clips");
    if (!clips_val || clips_val->type != TG3JSON_ARRAY) {
        tg3json_value_free(&root);
        return true;
    }

    size_t ccount = tg3json_array_size(clips_val);
    for (size_t ci = 0; ci < ccount; ++ci) {
        const tg3json_value* cval = tg3json_array_get(clips_val, ci);
        if (!cval || cval->type != TG3JSON_OBJECT) continue;

        std::string clip_name;
        const tg3json_value* nval = tg3json_object_get(cval, "name");
        if (nval && nval->type == TG3JSON_STRING) {
            clip_name = std::string(nval->u.string.ptr, nval->u.string.len);
        } else {
            clip_name = "clip_" + std::to_string(ci);
        }
        for (char& c : clip_name) {
            if (c == ':' || c == '/' || c == '\\' || c == ' ') c = '_';
        }

        float clip_duration = 0.0f;
        const tg3json_value* dval = tg3json_object_get(cval, "duration");
        if (dval) {
            if (dval->type == TG3JSON_REAL) clip_duration = (float)dval->u.real;
            else if (dval->type == TG3JSON_INT) clip_duration = (float)dval->u.integer;
        }

        PODModel clip_model = base_model;
        clip_model.meshes.clear();
        clip_model.materials.clear();
        clip_model.texture_filenames.clear();
        for (auto& n : clip_model.nodes) {
            n.object_index = -1;
            n.material_index = -1;
            n.anim_translation.clear();
            n.anim_rotation.clear();
            n.anim_scale.clear();
            n.anim_flags = 0;
            n.has_matrix = false;
        }

        // Must match gltf_inspect_animations' companion branch: same fps, same
        // frame count, else the browser lists a clip the bake never writes.
        const float fps = derive_companion_fps(cval, target_fps);
        clip_model.fps = fps;
        clip_model.num_frames = std::max(1, static_cast<int>(std::lround(clip_duration * fps)) + 1);

        auto find_node = [&](const std::string& bname) -> int {
            for (int i = 0; i < (int)clip_model.nodes.size(); ++i) {
                if (clip_model.nodes[i].name == bname) return i;
            }
            std::string pref = "Bone_" + bname;
            for (int i = 0; i < (int)clip_model.nodes.size(); ++i) {
                if (clip_model.nodes[i].name == pref) return i;
            }
            std::string san = bname;
            for (char& c : san) if (c == ':' || c == '/' || c == '\\') c = '_';
            for (int i = 0; i < (int)clip_model.nodes.size(); ++i) {
                if (clip_model.nodes[i].name == san) return i;
            }
            std::string san_pref = "Bone_" + san;
            for (int i = 0; i < (int)clip_model.nodes.size(); ++i) {
                if (clip_model.nodes[i].name == san_pref) return i;
            }
            return -1;
        };

        float detected_unit_factor = 1.0f;
        const tg3json_value* tracks_val = tg3json_object_get(cval, "tracks");
        if (tracks_val && tracks_val->type == TG3JSON_ARRAY) {
            size_t tcount = tg3json_array_size(tracks_val);
            std::map<float, int> unit_votes;
            for (size_t ti = 0; ti < tcount; ++ti) {
                const tg3json_value* tval = tg3json_array_get(tracks_val, ti);
                if (!tval || tval->type != TG3JSON_OBJECT) continue;
                const tg3json_value* fn_val = tg3json_object_get(tval, "name");
                if (!fn_val || fn_val->type != TG3JSON_STRING) continue;
                std::string full_name(fn_val->u.string.ptr, fn_val->u.string.len);
                size_t dot = full_name.find_last_of('.');
                if (dot == std::string::npos) continue;
                std::string bname = full_name.substr(0, dot);
                std::string prop = full_name.substr(dot + 1);
                if (prop != "position") continue;
                int pni = find_node(bname);
                if (pni < 0 || pni >= (int)clip_model.nodes.size()) continue;
                const PODNode& pnode = clip_model.nodes[pni];
                if (root_bone_names.count(bname) > 0 || pnode.parent_index == -1) continue;

                const tg3json_value* vv = tg3json_object_get(tval, "values");
                if (!vv || vv->type != TG3JSON_ARRAY) continue;
                size_t vn = tg3json_array_size(vv);
                if (vn < 3) continue;

                const float base_len = std::sqrt(pnode.translation[0]*pnode.translation[0] +
                                                 pnode.translation[1]*pnode.translation[1] +
                                                 pnode.translation[2]*pnode.translation[2]);
                if (base_len <= 0.001f) continue;

                double sum_sq = 0.0;
                size_t samples = 0;
                for (size_t k = 0; k + 2 < vn; k += 3) {
                    const tg3json_value* v0 = tg3json_array_get(vv, k);
                    const tg3json_value* v1 = tg3json_array_get(vv, k+1);
                    const tg3json_value* v2 = tg3json_array_get(vv, k+2);
                    double x = (v0->type == TG3JSON_REAL) ? v0->u.real : v0->u.integer;
                    double y = (v1->type == TG3JSON_REAL) ? v1->u.real : v1->u.integer;
                    double z = (v2->type == TG3JSON_REAL) ? v2->u.real : v2->u.integer;
                    sum_sq += x*x + y*y + z*z;
                    ++samples;
                }
                if (samples == 0) continue;
                const double track_rms = std::sqrt(sum_sq / (double)samples);
                const double ratio = (track_rms > 1e-9) ? track_rms / (double)base_len : 1.0;
                static const double kKnown[] = {10.0, 100.0, 1000.0, 39.3701, 3.28084};
                for (double known : kKnown) {
                    if (std::fabs(ratio - known) <= 0.05 * known) {
                        unit_votes[(float)(1.0 / known)]++;
                        break;
                    } else if (std::fabs(ratio - 1.0 / known) <= 0.05 / known) {
                        unit_votes[(float)known]++;
                        break;
                    }
                }
            }
            int max_votes = 0;
            for (const auto& kv : unit_votes) {
                if (kv.second > max_votes) {
                    max_votes = kv.second;
                    detected_unit_factor = kv.first;
                }
            }

            for (size_t ti = 0; ti < tcount; ++ti) {
                const tg3json_value* tval = tg3json_array_get(tracks_val, ti);
                if (!tval || tval->type != TG3JSON_OBJECT) continue;

                const tg3json_value* full_name_val = tg3json_object_get(tval, "name");
                if (!full_name_val || full_name_val->type != TG3JSON_STRING) continue;

                std::string full_name(full_name_val->u.string.ptr, full_name_val->u.string.len);
                size_t dot = full_name.find_last_of('.');
                if (dot == std::string::npos) continue;

                std::string bone_name = full_name.substr(0, dot);
                std::string prop = full_name.substr(dot + 1);

                int pod_node_idx = find_node(bone_name);
                if (pod_node_idx < 0 || pod_node_idx >= (int)clip_model.nodes.size()) continue;

                PODNode& node = clip_model.nodes[pod_node_idx];

                std::vector<float> times;
                const tg3json_value* times_val = tg3json_object_get(tval, "times");
                if (times_val && times_val->type == TG3JSON_ARRAY) {
                    size_t tn = tg3json_array_size(times_val);
                    times.reserve(tn);
                    for (size_t k = 0; k < tn; ++k) {
                        const tg3json_value* v = tg3json_array_get(times_val, k);
                        if (v->type == TG3JSON_REAL) times.push_back((float)v->u.real);
                        else if (v->type == TG3JSON_INT) times.push_back((float)v->u.integer);
                    }
                }

                std::vector<float> vals;
                const tg3json_value* vals_val = tg3json_object_get(tval, "values");
                if (vals_val && vals_val->type == TG3JSON_ARRAY) {
                    size_t vn = tg3json_array_size(vals_val);
                    vals.reserve(vn);
                    for (size_t k = 0; k < vn; ++k) {
                        const tg3json_value* v = tg3json_array_get(vals_val, k);
                        if (v->type == TG3JSON_REAL) vals.push_back((float)v->u.real);
                        else if (v->type == TG3JSON_INT) vals.push_back((float)v->u.integer);
                    }
                }

                if (times.empty() || vals.empty()) continue;

                bool is_root = root_bone_names.count(bone_name) > 0 || node.parent_index == -1;
                float unit_factor = detected_unit_factor;

                float chan_scale = (prop == "position") ? (is_root ? (scale * unit_factor) : unit_factor) : 1.0f;

                if (prop == "position") {
                    sample_channel("LINEAR", 3, times, vals, clip_model.num_frames, clip_model.fps, node.anim_translation, chan_scale);
                    node.anim_flags |= 1;
                } else if (prop == "quaternion") {
                    sample_channel("LINEAR", 4, times, vals, clip_model.num_frames, clip_model.fps, node.anim_rotation, 1.0f);
                    // Invert xyz for all sampled frames to match PowerVR / TouchFoo convention
                    for (size_t ki = 0; ki + 3 < node.anim_rotation.size(); ki += 4) {
                        node.anim_rotation[ki + 0] = -node.anim_rotation[ki + 0];
                        node.anim_rotation[ki + 1] = -node.anim_rotation[ki + 1];
                        node.anim_rotation[ki + 2] = -node.anim_rotation[ki + 2];
                    }
                    node.anim_flags |= 2;
                } else if (prop == "scale") {
                    std::vector<float> s3;
                    sample_channel("LINEAR", 3, times, vals, clip_model.num_frames, clip_model.fps, s3, 1.0f);
                    node.anim_scale.clear();
                    node.anim_scale.reserve((s3.size() / 3) * 7);
                    for (size_t ki = 0; ki + 2 < s3.size(); ki += 3) {
                        node.anim_scale.push_back(s3[ki + 0]);
                        node.anim_scale.push_back(s3[ki + 1]);
                        node.anim_scale.push_back(s3[ki + 2]);
                        node.anim_scale.push_back(0.0f);
                        node.anim_scale.push_back(0.0f);
                        node.anim_scale.push_back(0.0f);
                        node.anim_scale.push_back(1.0f);
                    }
                    node.anim_flags |= 4;
                }
            }
        }

        // Fill in unkeyed channels
        for (auto& node : clip_model.nodes) {
            if (node.anim_translation.empty()) {
                node.anim_translation.assign((size_t)clip_model.num_frames * 3, 0.0f);
                for (int f = 0; f < clip_model.num_frames; ++f) {
                    node.anim_translation[f * 3 + 0] = node.translation[0];
                    node.anim_translation[f * 3 + 1] = node.translation[1];
                    node.anim_translation[f * 3 + 2] = node.translation[2];
                }
                node.anim_flags |= 1;
            }
            if (node.anim_rotation.empty()) {
                node.anim_rotation.assign((size_t)clip_model.num_frames * 4, 0.0f);
                const float rw = (node.rotation[3] == 0.0f && node.rotation[0] == 0.0f
                                  && node.rotation[1] == 0.0f && node.rotation[2] == 0.0f)
                                 ? 1.0f : node.rotation[3];
                for (int f = 0; f < clip_model.num_frames; ++f) {
                    node.anim_rotation[f * 4 + 0] = node.rotation[0];
                    node.anim_rotation[f * 4 + 1] = node.rotation[1];
                    node.anim_rotation[f * 4 + 2] = node.rotation[2];
                    node.anim_rotation[f * 4 + 3] = rw;
                }
                node.anim_flags |= 2;
            }
            if (node.anim_scale.empty()) {
                const float sx = (node.scale[0] != 0.0f) ? node.scale[0] : 1.0f;
                const float sy = (node.scale[1] != 0.0f) ? node.scale[1] : 1.0f;
                const float sz = (node.scale[2] != 0.0f) ? node.scale[2] : 1.0f;
                node.anim_scale.reserve((size_t)clip_model.num_frames * 7);
                for (int f = 0; f < clip_model.num_frames; ++f) {
                    node.anim_scale.push_back(sx);
                    node.anim_scale.push_back(sy);
                    node.anim_scale.push_back(sz);
                    node.anim_scale.push_back(0.0f);
                    node.anim_scale.push_back(0.0f);
                    node.anim_scale.push_back(0.0f);
                    node.anim_scale.push_back(1.0f);
                }
                node.anim_flags |= 4;
            }
            node.anim_flags = 7;
        }

        clip_model.num_mesh_nodes = 0;
        out_clips.emplace_back(clip_name, std::move(clip_model));
    }

    tg3json_value_free(&root);
    return !out_clips.empty();
}

// ─── S2: rigid-bone selection scored against the clips' motion ───────────────
//
// See gltf_glb.h for the contract. The short version: the engine reads one bone
// per vertex, so a smooth rig has to collapse onto a single influence, and
// choosing it at the bind pose is a guess made while the model is standing
// still. This scores each of a vertex's own influences against the smooth result
// it is collapsing away, over frames drawn from every clip, and keeps the best.
//
// The scoring reuses skin_mesh's exact matrix —
//     skin[j] = inverse(world(mesh_node)) · world(j,f) · inverse(bind[j]) · bind(mesh_node)
// — because a metric that disagrees with the runtime's contract measures the
// wrong thing. Both sides of the comparison come from the same per-frame skin
// table, so the mesh-node and bind factors cancel and the comparison is purely
// about which bone tracks the vertex.

namespace {

// Column-major 4x4 multiply, matching pod_loader's local_mat4_mul.
void rs_mat4_mul(const float a[16], const float b[16], float out[16]) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            out[c * 4 + r] = a[r] * b[c * 4 + 0] + a[4 + r] * b[c * 4 + 1]
                           + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
}

void rs_mat4_identity(float m[16]) {
    std::memset(m, 0, sizeof(float) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void rs_mat4_point(const float m[16], const float p[3], float out[3]) {
    out[0] = m[0] * p[0] + m[4] * p[1] + m[8]  * p[2] + m[12];
    out[1] = m[1] * p[0] + m[5] * p[1] + m[9]  * p[2] + m[13];
    out[2] = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
}

// Frames scored per clip. 8 spread evenly across the clip is enough to expose
// which bone carries a vertex through the motion, and keeps the pass cheap on a
// 100k-vertex model with a dozen clips.
constexpr int kRigidScoreFramesPerClip = 8;

struct RsPoseSample {
    // skin[(instance * n_nodes + node) * 16 ...] — the matrix skin_mesh would
    // apply to a vertex of that instance driven by that node, at this frame.
    std::vector<float> skin;
};

} // namespace

bool refine_rigid_skin(PODModel& model,
                       const std::vector<std::pair<std::string, PODModel>>& clips,
                       RigidSkinRefineStats* stats,
                       std::string* err) {
    RigidSkinRefineStats out;
    (void)err;

    struct RsInstance { int mesh_node; int mesh; };
    std::vector<RsInstance> instances;
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        const int oi = model.nodes[i].object_index;
        if (oi < 0 || oi >= (int)model.meshes.size()) continue;
        const PODMesh& m = model.meshes[oi];
        if (m.bones_per_vertex <= 0 || m.positions.empty()) continue;
        if (m.bones_per_vertex == 1) continue;         // already rigid: nothing to choose
        instances.push_back({(int)i, oi});
    }
    if (instances.empty()) {
        if (stats) *stats = out;
        return true;
    }

    const int n_nodes = (int)model.nodes.size();
    std::vector<RsPoseSample> poses;

    for (const auto& kv : clips) {
        const PODModel& clip = kv.second;
        // The clip PODs the importer builds are copies of the base model with
        // their animation streams filled in, so nodes line up 1:1 and by name.
        // Anything else is not a sibling of this model and cannot be scored.
        if (clip.nodes.size() != model.nodes.size()) continue;
        if (clip.num_frames <= 0) continue;

        PODModel pose;
        pose.nodes = model.nodes;                 // carries has_bind_matrix
        pose.num_frames = clip.num_frames;
        pose.fps = clip.fps;
        for (size_t i = 0; i < pose.nodes.size(); ++i) {
            const PODNode& src = clip.nodes[i];
            if (src.name != pose.nodes[i].name) continue;
            if (!src.anim_translation.empty()) pose.nodes[i].anim_translation = src.anim_translation;
            if (!src.anim_rotation.empty())    pose.nodes[i].anim_rotation    = src.anim_rotation;
            if (!src.anim_scale.empty())       pose.nodes[i].anim_scale       = src.anim_scale;
            if (!src.anim_matrix.empty())      pose.nodes[i].anim_matrix      = src.anim_matrix;
            pose.nodes[i].anim_flags = src.anim_flags;
            // A stream, when present, wins over the static matrix — the same
            // precedence pod_loader applies after a clip merge.
            if (!src.anim_translation.empty() || !src.anim_rotation.empty() ||
                !src.anim_scale.empty() || !src.anim_matrix.empty())
                pose.nodes[i].has_matrix = false;
        }

        const int nf = pose.num_frames;
        std::vector<int> frames;
        for (int k = 0; k < kRigidScoreFramesPerClip; ++k) {
            const int f = (int)std::llround((double)k * (nf - 1) /
                                            std::max(1, kRigidScoreFramesPerClip - 1));
            if (std::find(frames.begin(), frames.end(), f) == frames.end()) frames.push_back(f);
        }

        std::vector<float> cur((size_t)n_nodes * 16), bind((size_t)n_nodes * 16);
        for (int f : frames) {
            for (int j = 0; j < n_nodes; ++j) {
                get_node_matrix(pose, j, (float)f, &cur[(size_t)j * 16]);
                if (pose.nodes[j].has_bind_matrix)
                    std::memcpy(&bind[(size_t)j * 16], pose.nodes[j].bind_matrix, 16 * sizeof(float));
                else
                    get_node_matrix(pose, j, 0.0f, &bind[(size_t)j * 16]);
            }
            RsPoseSample ps;
            ps.skin.assign((size_t)instances.size() * (size_t)n_nodes * 16, 0.0f);
            for (size_t mi = 0; mi < instances.size(); ++mi) {
                const int mesh_node = instances[mi].mesh_node;
                float mesh_world[16], mesh_inv[16];
                get_node_matrix(pose, mesh_node, (float)f, mesh_world);
                if (!gltf_mat4_inverse(mesh_world, mesh_inv)) rs_mat4_identity(mesh_inv);
                for (int j = 0; j < n_nodes; ++j) {
                    float inv_bind[16];
                    if (!gltf_mat4_inverse(&bind[(size_t)j * 16], inv_bind)) rs_mat4_identity(inv_bind);
                    float t[16], s[16];
                    rs_mat4_mul(&cur[(size_t)j * 16], inv_bind, t);
                    rs_mat4_mul(t, &bind[(size_t)mesh_node * 16], s);
                    rs_mat4_mul(mesh_inv, s, &ps.skin[((size_t)mi * n_nodes + j) * 16]);
                }
            }
            poses.push_back(std::move(ps));
        }
    }
    out.pose_samples = (int)poses.size();

    double sum_before = 0.0, sum_after = 0.0;
    long   scored = 0;

    for (size_t mi = 0; mi < instances.size(); ++mi) {
        PODMesh& mesh = model.meshes[instances[mi].mesh];
        const int bpv = mesh.bones_per_vertex;
        const int nverts = mesh.num_vertices;
        if (bpv <= 1 || bpv > 8 || nverts <= 0) continue;
        if (mesh.bone_indices.size() < (size_t)nverts * bpv) continue;

        const std::vector<uint32_t>& table = mesh.bone_batches.indices;
        auto pod_node_of_slot = [&](int slot) -> int {
            if (mesh.has_bone_batches && !table.empty() && slot >= 0 && (size_t)slot < table.size())
                return (int)table[slot];
            return slot;
        };

        std::vector<float> chosen((size_t)nverts, 0.0f);
        out.vertices += nverts;

        for (int v = 0; v < nverts; ++v) {
            const float* p = &mesh.positions[(size_t)v * 3];

            // The vertex's own influences, deduped, in raw slot space — slot
            // space is what bone_indices stores; bone_batches maps it to a POD
            // node, and keeping that indirection intact is what lets the
            // engine's two-level lookup keep working unchanged.
            int   cand[8];
            double cost[8] = {0, 0, 0, 0, 0, 0, 0, 0};
            int   n_cand = 0;
            float wsum = 0.0f;
            int   dominant = 0;
            float dominant_w = -1.0f;
            for (int k = 0; k < bpv; ++k) {
                const size_t i = (size_t)v * bpv + k;
                if (i >= mesh.bone_indices.size()) break;
                const float wk = mesh.bone_weights.size() > i ? mesh.bone_weights[i] : 0.0f;
                const int slot = (int)std::lround(mesh.bone_indices[i]);
                if (wk > dominant_w) { dominant_w = wk; dominant = std::max(slot, 0); }
                if (wk <= 0.0f || slot < 0) continue;
                bool seen = false;
                for (int c = 0; c < n_cand; ++c) if (cand[c] == slot) { seen = true; break; }
                if (!seen && n_cand < 8) cand[n_cand++] = slot;
                wsum += wk;
            }
            if (n_cand == 0 || wsum <= 0.0f) { chosen[(size_t)v] = (float)std::max(dominant, 0); continue; }

            // With no clips to score against, keep exactly the old behaviour
            // rather than collapsing onto an arbitrary first influence.
            if (poses.empty()) { chosen[(size_t)v] = (float)dominant; continue; }

            int dominant_c = 0;
            for (int c = 0; c < n_cand; ++c) if (cand[c] == dominant) { dominant_c = c; break; }

            const float inv_wsum = 1.0f / wsum;
            for (const RsPoseSample& ps : poses) {
                // The smooth result this vertex is being collapsed away from,
                // normalised the way skin_mesh normalises it.
                float smooth[3] = {0.0f, 0.0f, 0.0f};
                for (int k = 0; k < bpv; ++k) {
                    const size_t i = (size_t)v * bpv + k;
                    if (i >= mesh.bone_indices.size()) break;
                    const float wk = mesh.bone_weights.size() > i ? mesh.bone_weights[i] : 0.0f;
                    if (wk <= 0.0f) continue;
                    const int node = pod_node_of_slot((int)std::lround(mesh.bone_indices[i]));
                    if (node < 0 || node >= n_nodes) continue;
                    float q[3];
                    rs_mat4_point(&ps.skin[((size_t)mi * n_nodes + node) * 16], p, q);
                    smooth[0] += wk * q[0]; smooth[1] += wk * q[1]; smooth[2] += wk * q[2];
                }
                smooth[0] *= inv_wsum; smooth[1] *= inv_wsum; smooth[2] *= inv_wsum;

                for (int c = 0; c < n_cand; ++c) {
                    const int node = pod_node_of_slot(cand[c]);
                    if (node < 0 || node >= n_nodes) { cost[c] += 1e30; continue; }
                    float q[3];
                    rs_mat4_point(&ps.skin[((size_t)mi * n_nodes + node) * 16], p, q);
                    const double dx = (double)q[0] - smooth[0];
                    const double dy = (double)q[1] - smooth[1];
                    const double dz = (double)q[2] - smooth[2];
                    cost[c] += dx * dx + dy * dy + dz * dz;
                }
            }

            int best = 0;
            for (int c = 1; c < n_cand; ++c) if (cost[c] < cost[best]) best = c;
            chosen[(size_t)v] = (float)cand[best];
            if (cand[best] != dominant) ++out.moved;

            const double n = (double)poses.size();
            const double e_before = std::sqrt(cost[dominant_c] / n);
            const double e_after  = std::sqrt(cost[best] / n);
            sum_before += e_before;
            sum_after  += e_after;
            out.worst_before = std::max(out.worst_before, e_before);
            out.worst_after  = std::max(out.worst_after,  e_after);
            ++scored;
        }

        mesh.bones_per_vertex = 1;
        mesh.bone_indices = std::move(chosen);
        mesh.bone_weights.assign((size_t)nverts, 1.0f);
    }

    if (scored > 0) {
        out.mean_before = sum_before / (double)scored;
        out.mean_after  = sum_after / (double)scored;
    }
    if (stats) *stats = out;
    return true;
}

} // namespace av
