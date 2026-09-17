#include "glb_model.h"
#include "viewport_shader.h"
#include "tiny_gltf_v3.h"
#include "tools/image_decode.h"   // WebP fallback: Qt may lack its webp plugin

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QOpenGLContext>

#include <cmath>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <iostream>

namespace ruby::render {

namespace {

static bool read_floats(const tg3_model* model, int32_t acc_idx, std::vector<float>& out, int expected_comps) {
    (void)expected_comps;
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

    if (start_offset + (acc->count - 1) * stride + elem_size > buf->data.count) return false;

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
                case TG3_COMPONENT_TYPE_FLOAT:
                    val = *(const float*)cp;
                    break;
                case TG3_COMPONENT_TYPE_DOUBLE:
                    val = (float)*(const double*)cp;
                    break;
                default:
                    val = (float)*(const uint32_t*)cp;
                    break;
            }
            dst[c] = val;
        }
    }
    return true;
}

static bool read_indices(const tg3_model* model, int32_t acc_idx, std::vector<uint32_t>& out) {
    out.clear();
    if (!model || acc_idx < 0 || acc_idx >= (int32_t)model->accessors_count) return false;
    const tg3_accessor* acc = &model->accessors[acc_idx];
    if (acc->count == 0) return true;
    if (acc->buffer_view < 0 || acc->buffer_view >= (int32_t)model->buffer_views_count) return false;
    const tg3_buffer_view* bv = &model->buffer_views[acc->buffer_view];
    if (bv->buffer < 0 || bv->buffer >= (int32_t)model->buffers_count) return false;
    const tg3_buffer* buf = &model->buffers[bv->buffer];
    if (!buf->data.data) return false;

    size_t stride = (bv->byte_stride > 0) ? bv->byte_stride : 0;
    uint64_t start_offset = bv->byte_offset + acc->byte_offset;
    const uint8_t* base_ptr = buf->data.data + start_offset;

    out.resize(acc->count);
    for (uint64_t i = 0; i < acc->count; ++i) {
        if (acc->component_type == TG3_COMPONENT_TYPE_UNSIGNED_BYTE) {
            const uint8_t* ptr = base_ptr + (stride ? i * stride : i * 1);
            out[i] = *ptr;
        } else if (acc->component_type == TG3_COMPONENT_TYPE_UNSIGNED_SHORT) {
            const uint16_t* ptr = (const uint16_t*)(base_ptr + (stride ? i * stride : i * 2));
            out[i] = *ptr;
        } else {
            const uint32_t* ptr = (const uint32_t*)(base_ptr + (stride ? i * stride : i * 4));
            out[i] = *ptr;
        }
    }
    return true;
}

static int find_attribute(const tg3_primitive* prim, const char* name) {
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

static GLuint upload_texture_rgba(QOpenGLExtraFunctions* gl, const uint8_t* data, int w, int h) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    if (gl) gl->glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

} // namespace

void GLBMeshGpu::release(QOpenGLExtraFunctions* gl) {
    if (!gl) return;
    if (vao) { gl->glDeleteVertexArrays(1, &vao); vao = 0; }
    if (pos_vbo) { gl->glDeleteBuffers(1, &pos_vbo); pos_vbo = 0; }
    if (norm_vbo) { gl->glDeleteBuffers(1, &norm_vbo); norm_vbo = 0; }
    if (uv_vbo) { gl->glDeleteBuffers(1, &uv_vbo); uv_vbo = 0; }
    if (ebo) { gl->glDeleteBuffers(1, &ebo); ebo = 0; }
}

GLBModel::~GLBModel() {
    clear();
}

void GLBModel::clear() {
    initializeOpenGLFunctions();
    for (auto& mesh : m_meshes) {
        mesh.release(this);
    }
    m_meshes.clear();
    m_nodes.clear();
    m_skins.clear();
    m_animations.clear();
    for (GLuint tex : m_textures) {
        if (tex) glDeleteTextures(1, &tex);
    }
    m_textures.clear();
    m_active_animation = 0;
    m_current_time = 0.0f;
    m_center = QVector3D(0.0f, 0.0f, 0.0f);
    m_min_bounds = QVector3D(0.0f, 0.0f, 0.0f);
    m_max_bounds = QVector3D(0.0f, 0.0f, 0.0f);
    m_radius = 50.0f;
}

int GLBModel::total_vertices() const {
    int total = 0;
    for (const auto& mesh : m_meshes) {
        total += mesh.vertex_count;
    }
    return total;
}

const std::string& GLBModel::animation_name(int index) const {
    static const std::string empty;
    if (index >= 0 && index < (int)m_animations.size()) {
        return m_animations[index].name;
    }
    return empty;
}

float GLBModel::current_animation_duration() const {
    if (m_active_animation >= 0 && m_active_animation < (int)m_animations.size()) {
        return m_animations[m_active_animation].duration;
    }
    return 0.0f;
}

void GLBModel::set_active_animation(int index) {
    if (index >= 0 && index < (int)m_animations.size()) {
        m_active_animation = index;
    }
}

bool GLBModel::load_from_file(const std::string& filepath, std::string* err) {
    initializeOpenGLFunctions();
    clear();

    std::ifstream f(filepath, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        if (err) *err = "Could not open file: " + filepath;
        return false;
    }
    std::streamsize size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> file_data(static_cast<size_t>(size));
    if (size > 0) f.read(reinterpret_cast<char*>(file_data.data()), size);

    tg3_model model;
    tg3_error_stack errors;
    std::memset(&model, 0, sizeof(model));
    tg3_error_stack_init(&errors);

    tg3_parse_options opts;
    tg3_parse_options_init(&opts);
    opts.strictness = TG3_PERMISSIVE;
    opts.images_as_is = 1;

    std::string base_dir;
    size_t slash = filepath.find_last_of("/\\");
    if (slash != std::string::npos) base_dir = filepath.substr(0, slash);

    tg3_error_code code = tg3_parse_auto(&model, &errors, file_data.data(), file_data.size(),
                                         base_dir.c_str(), (uint32_t)base_dir.size(), &opts);
    if (code != TG3_OK) {
        if (err) *err = "tg3_parse_auto failed with code " + std::to_string(code);
        tg3_error_stack_free(&errors);
        tg3_model_free(&model);
        return false;
    }

    // 1. Textures & Images
    std::vector<GLuint> loaded_textures(model.textures_count, 0);
    for (uint32_t t = 0; t < model.textures_count; ++t) {
        int32_t img_idx = model.textures[t].source;
        if (img_idx >= 0 && img_idx < (int32_t)model.images_count) {
            const tg3_image* img = &model.images[img_idx];
            const uint8_t* img_bytes = nullptr;
            size_t img_size = 0;

            if (img->buffer_view >= 0 && img->buffer_view < (int32_t)model.buffer_views_count) {
                const tg3_buffer_view* bv = &model.buffer_views[img->buffer_view];
                if (bv->buffer >= 0 && bv->buffer < (int32_t)model.buffers_count) {
                    const tg3_buffer* buf = &model.buffers[bv->buffer];
                    if (buf->data.data && bv->byte_offset + bv->byte_length <= buf->data.count) {
                        img_bytes = buf->data.data + bv->byte_offset;
                        img_size = bv->byte_length;
                    }
                }
            } else if (img->image.data && img->image.count > 0) {
                img_bytes = img->image.data;
                img_size = img->image.count;
            }

            if (img_bytes && img_size > 0) {
                QImage qimg = QImage::fromData(QByteArray((const char*)img_bytes, (int)img_size));
                if (qimg.isNull() && av::bytes_are_webp(img_bytes, img_size)) {
                    // Same fallback as the viewport: EXT_texture_webp payloads must
                    // not depend on Qt's optional webp image-format plugin.
                    std::vector<uint8_t> rgba;
                    int ww = 0, hh = 0;
                    if (av::webp_decode_rgba(img_bytes, img_size, rgba, ww, hh) && !rgba.empty())
                        qimg = QImage(rgba.data(), ww, hh, ww * 4, QImage::Format_RGBA8888).copy();
                }
                if (!qimg.isNull()) {
                    QImage rgba = qimg.convertToFormat(QImage::Format_RGBA8888);
                    GLuint tex = upload_texture_rgba(this, rgba.bits(), rgba.width(), rgba.height());
                    loaded_textures[t] = tex;
                    m_textures.push_back(tex);
                }
            }
        }
    }

    // 2. Nodes
    m_nodes.resize(model.nodes_count);
    for (uint32_t i = 0; i < model.nodes_count; ++i) {
        const tg3_node* gn = &model.nodes[i];
        GLBNode& n = m_nodes[i];
        n.name = gn->name.data ? std::string(gn->name.data, gn->name.len) : ("node_" + std::to_string(i));
        n.mesh_index = gn->mesh;
        n.skin_index = gn->skin;

        for (uint32_t c = 0; c < gn->children_count; ++c) {
            n.children.push_back(gn->children[c]);
        }

        if (gn->has_matrix) {
            n.has_matrix = true;
            n.local_matrix = QMatrix4x4(
                (float)gn->matrix[0], (float)gn->matrix[4], (float)gn->matrix[8], (float)gn->matrix[12],
                (float)gn->matrix[1], (float)gn->matrix[5], (float)gn->matrix[9], (float)gn->matrix[13],
                (float)gn->matrix[2], (float)gn->matrix[6], (float)gn->matrix[10], (float)gn->matrix[14],
                (float)gn->matrix[3], (float)gn->matrix[7], (float)gn->matrix[11], (float)gn->matrix[15]
            );
        } else {
            n.has_matrix = false;
            n.translation = QVector3D((float)gn->translation[0], (float)gn->translation[1], (float)gn->translation[2]);
            n.rotation = QQuaternion((float)gn->rotation[3], (float)gn->rotation[0], (float)gn->rotation[1], (float)gn->rotation[2]);
            n.scale = QVector3D((float)gn->scale[0], (float)gn->scale[1], (float)gn->scale[2]);
        }
    }

    // Build parent hierarchy
    for (uint32_t i = 0; i < model.nodes_count; ++i) {
        for (int child : m_nodes[i].children) {
            if (child >= 0 && child < (int)m_nodes.size()) {
                m_nodes[child].parent = (int)i;
            }
        }
    }

    // 3. Skins
    m_skins.resize(model.skins_count);
    for (uint32_t s = 0; s < model.skins_count; ++s) {
        const tg3_skin* gskin = &model.skins[s];
        GLBSkin& skin = m_skins[s];
        skin.name = gskin->name.data ? std::string(gskin->name.data, gskin->name.len) : ("skin_" + std::to_string(s));
        skin.skeleton_root = gskin->skeleton;

        for (uint32_t j = 0; j < gskin->joints_count; ++j) {
            skin.joints.push_back(gskin->joints[j]);
        }

        std::vector<float> ibm_floats;
        if (gskin->inverse_bind_matrices >= 0 && read_floats(&model, gskin->inverse_bind_matrices, ibm_floats, 16)) {
            size_t count = ibm_floats.size() / 16;
            skin.inverse_bind_matrices.resize(count);
            for (size_t j = 0; j < count; ++j) {
                const float* m = &ibm_floats[j * 16];
                skin.inverse_bind_matrices[j] = QMatrix4x4(
                    m[0], m[4], m[8], m[12],
                    m[1], m[5], m[9], m[13],
                    m[2], m[6], m[10], m[14],
                    m[3], m[7], m[11], m[15]
                );
            }
        } else {
            skin.inverse_bind_matrices.assign(skin.joints.size(), QMatrix4x4());
        }
        skin.palette.resize(skin.joints.size());
    }

    // 4. Meshes & Primitives
    float min_b[3] = {1e9f, 1e9f, 1e9f};
    float max_b[3] = {-1e9f, -1e9f, -1e9f};

    for (uint32_t n_idx = 0; n_idx < model.nodes_count; ++n_idx) {
        int m_idx = m_nodes[n_idx].mesh_index;
        if (m_idx < 0 || m_idx >= (int32_t)model.meshes_count) continue;
        const tg3_mesh* gmesh = &model.meshes[m_idx];

        for (uint32_t p = 0; p < gmesh->primitives_count; ++p) {
            const tg3_primitive* prim = &gmesh->primitives[p];
            int pos_acc = find_attribute(prim, "POSITION");
            if (pos_acc < 0) continue;

            GLBMeshGpu gpu;
            read_floats(&model, pos_acc, gpu.orig_positions, 3);
            gpu.vertex_count = (int)(gpu.orig_positions.size() / 3);
            gpu.skinned_positions = gpu.orig_positions;

            for (size_t i = 0; i < gpu.orig_positions.size(); i += 3) {
                for (int c = 0; c < 3; ++c) {
                    min_b[c] = std::min(min_b[c], gpu.orig_positions[i + c]);
                    max_b[c] = std::max(max_b[c], gpu.orig_positions[i + c]);
                }
            }

            int norm_acc = find_attribute(prim, "NORMAL");
            if (norm_acc >= 0) {
                read_floats(&model, norm_acc, gpu.orig_normals, 3);
                gpu.skinned_normals = gpu.orig_normals;
            }

            int uv_acc = find_attribute(prim, "TEXCOORD_0");
            if (uv_acc >= 0) {
                read_floats(&model, uv_acc, gpu.uvs, 2);
            }

            int joints_acc = find_attribute(prim, "JOINTS_0");
            int weights_acc = find_attribute(prim, "WEIGHTS_0");
            if (joints_acc >= 0 && weights_acc >= 0 && m_nodes[n_idx].skin_index >= 0) {
                std::vector<float> raw_joints, raw_weights;
                read_floats(&model, joints_acc, raw_joints, 4);
                read_floats(&model, weights_acc, raw_weights, 4);
                if (raw_joints.size() == (size_t)gpu.vertex_count * 4) {
                    gpu.joints.resize(raw_joints.size());
                    for (size_t k = 0; k < raw_joints.size(); ++k) {
                        gpu.joints[k] = (uint16_t)raw_joints[k];
                    }
                    gpu.weights = std::move(raw_weights);
                    gpu.has_skin = true;
                    gpu.skin_index = m_nodes[n_idx].skin_index;
                }
            }

            if (prim->indices >= 0) {
                read_indices(&model, prim->indices, gpu.indices);
                gpu.index_count = (int)gpu.indices.size();
            }

            // Material & Texture
            if (prim->material >= 0 && prim->material < (int32_t)model.materials_count) {
                const tg3_material* mat = &model.materials[prim->material];
                int tex_idx = mat->pbr_metallic_roughness.base_color_texture.index;
                if (tex_idx >= 0 && tex_idx < (int)loaded_textures.size()) {
                    gpu.texture_id = loaded_textures[tex_idx];
                }
                gpu.base_color = QVector4D(
                    (float)mat->pbr_metallic_roughness.base_color_factor[0],
                    (float)mat->pbr_metallic_roughness.base_color_factor[1],
                    (float)mat->pbr_metallic_roughness.base_color_factor[2],
                    (float)mat->pbr_metallic_roughness.base_color_factor[3]
                );
            }

            // OpenGL Buffer Upload
            glGenVertexArrays(1, &gpu.vao);
            glBindVertexArray(gpu.vao);

            glGenBuffers(1, &gpu.pos_vbo);
            glBindBuffer(GL_ARRAY_BUFFER, gpu.pos_vbo);
            glBufferData(GL_ARRAY_BUFFER, gpu.orig_positions.size() * sizeof(float), gpu.orig_positions.data(), GL_DYNAMIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);

            if (!gpu.orig_normals.empty()) {
                glGenBuffers(1, &gpu.norm_vbo);
                glBindBuffer(GL_ARRAY_BUFFER, gpu.norm_vbo);
                glBufferData(GL_ARRAY_BUFFER, gpu.orig_normals.size() * sizeof(float), gpu.orig_normals.data(), GL_DYNAMIC_DRAW);
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
            }

            if (!gpu.uvs.empty()) {
                glGenBuffers(1, &gpu.uv_vbo);
                glBindBuffer(GL_ARRAY_BUFFER, gpu.uv_vbo);
                glBufferData(GL_ARRAY_BUFFER, gpu.uvs.size() * sizeof(float), gpu.uvs.data(), GL_STATIC_DRAW);
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
            }

            if (!gpu.indices.empty()) {
                glGenBuffers(1, &gpu.ebo);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ebo);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, gpu.indices.size() * sizeof(uint32_t), gpu.indices.data(), GL_STATIC_DRAW);
            }

            glBindVertexArray(0);
            m_meshes.push_back(std::move(gpu));
        }
    }

    if (min_b[0] < max_b[0]) {
        m_min_bounds = QVector3D(min_b[0], min_b[1], min_b[2]);
        m_max_bounds = QVector3D(max_b[0], max_b[1], max_b[2]);
        m_center = QVector3D((min_b[0] + max_b[0]) * 0.5f, (min_b[1] + max_b[1]) * 0.5f, (min_b[2] + max_b[2]) * 0.5f);
        float dx = max_b[0] - min_b[0];
        float dy = max_b[1] - min_b[1];
        float dz = max_b[2] - min_b[2];
        m_radius = std::max(10.0f, 0.5f * std::sqrt(dx*dx + dy*dy + dz*dz));
    }

    // 5. Embedded glTF Animations
    for (uint32_t a = 0; a < model.animations_count; ++a) {
        const tg3_animation* ganim = &model.animations[a];
        GLBAnimation anim;
        anim.name = ganim->name.data ? std::string(ganim->name.data, ganim->name.len) : ("anim_" + std::to_string(a));

        for (uint32_t c = 0; c < ganim->channels_count; ++c) {
            const tg3_animation_channel* gch = &ganim->channels[c];
            if (gch->sampler < 0 || gch->sampler >= (int32_t)ganim->samplers_count) continue;
            const tg3_animation_sampler* gsm = &ganim->samplers[gch->sampler];

            GLBAnimationChannel ch;
            ch.node_index = gch->target.node;
            std::string path_str = gch->target.path.data ? std::string(gch->target.path.data, gch->target.path.len) : "";
            if (path_str == "translation") ch.path = GLBAnimationChannel::Translation;
            else if (path_str == "rotation") ch.path = GLBAnimationChannel::Rotation;
            else if (path_str == "scale") ch.path = GLBAnimationChannel::Scale;
            else continue;

            read_floats(&model, gsm->input, ch.times, 1);
            read_floats(&model, gsm->output, ch.values, (ch.path == GLBAnimationChannel::Rotation ? 4 : 3));

            if (!ch.times.empty()) {
                anim.duration = std::max(anim.duration, ch.times.back());
                anim.channels.push_back(std::move(ch));
            }
        }
        if (!anim.channels.empty()) {
            m_animations.push_back(std::move(anim));
        }
    }

    // 6. Auto-detect companion motions.json (Smash Royale & web-extracted animations)
    load_companion_motions(filepath);

    tg3_error_stack_free(&errors);
    tg3_model_free(&model);

    update_node_world_transforms();
    return true;
}

bool GLBModel::load_companion_motions(const std::string& glb_path) {
    QFileInfo fi(QString::fromStdString(glb_path));
    QDir dir = fi.dir();

    QStringList candidates = {
        dir.filePath("motions.json"),
        dir.filePath(fi.baseName() + "_motions.json"),
        dir.filePath(fi.baseName() + ".motions.json")
    };

    QString found_path;
    for (const QString& candidate : candidates) {
        if (QFile::exists(candidate)) {
            found_path = candidate;
            break;
        }
    }

    if (found_path.isEmpty()) return false;

    QFile file(found_path);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return false;

    QJsonObject root = doc.object();
    QJsonArray clips = root.value("clips").toArray();
    if (clips.isEmpty()) return false;

    std::unordered_map<std::string, int> node_name_to_idx;
    for (int i = 0; i < (int)m_nodes.size(); ++i) {
        node_name_to_idx[m_nodes[i].name] = i;
    }

    int loaded_clips = 0;
    for (const QJsonValue& cval : clips) {
        QJsonObject cobj = cval.toObject();
        GLBAnimation anim;
        anim.name = cobj.value("name").toString("clip").toStdString();
        anim.duration = (float)cobj.value("duration").toDouble(0.0);

        QJsonArray tracks = cobj.value("tracks").toArray();
        for (const QJsonValue& tval : tracks) {
            QJsonObject tobj = tval.toObject();
            QString full_name = tobj.value("name").toString();
            int dot = full_name.lastIndexOf('.');
            if (dot < 0) continue;

            std::string bone_name = full_name.left(dot).toStdString();
            std::string prop = full_name.mid(dot + 1).toStdString();

            auto it = node_name_to_idx.find(bone_name);
            if (it == node_name_to_idx.end()) continue;

            GLBAnimationChannel ch;
            ch.node_index = it->second;
            int comps = 3;
            if (prop == "position") { ch.path = GLBAnimationChannel::Translation; comps = 3; }
            else if (prop == "quaternion") { ch.path = GLBAnimationChannel::Rotation; comps = 4; }
            else if (prop == "scale") { ch.path = GLBAnimationChannel::Scale; comps = 3; }
            else continue;

            QJsonArray times_arr = tobj.value("times").toArray();
            QJsonArray vals_arr = tobj.value("values").toArray();
            if (times_arr.isEmpty() || vals_arr.isEmpty()) continue;

            ch.times.reserve(times_arr.size());
            for (const QJsonValue& v : times_arr) ch.times.push_back((float)v.toDouble());

            ch.values.reserve(vals_arr.size());
            for (const QJsonValue& v : vals_arr) ch.values.push_back((float)v.toDouble());

            if (ch.values.size() == ch.times.size() * comps) {
                anim.duration = std::max(anim.duration, ch.times.back());
                anim.channels.push_back(std::move(ch));
            }
        }

        if (!anim.channels.empty()) {
            m_animations.push_back(std::move(anim));
            loaded_clips++;
        }
    }

    return (loaded_clips > 0);
}

void GLBModel::update_animation(float time_seconds) {
    if (m_animations.empty() || m_active_animation < 0 || m_active_animation >= (int)m_animations.size()) {
        update_node_world_transforms();
        return;
    }

    const GLBAnimation& anim = m_animations[m_active_animation];
    if (anim.duration > 0.0f) {
        m_current_time = std::fmod(time_seconds, anim.duration);
    } else {
        m_current_time = 0.0f;
    }

    for (const auto& ch : anim.channels) {
        if (ch.node_index < 0 || ch.node_index >= (int)m_nodes.size()) continue;
        if (ch.times.empty() || ch.values.empty()) continue;

        GLBNode& node = m_nodes[ch.node_index];
        float t = m_current_time;

        if (t <= ch.times.front() || ch.times.size() == 1) {
            if (ch.path == GLBAnimationChannel::Translation) {
                node.translation = QVector3D(ch.values[0], ch.values[1], ch.values[2]);
            } else if (ch.path == GLBAnimationChannel::Rotation) {
                node.rotation = QQuaternion(ch.values[3], ch.values[0], ch.values[1], ch.values[2]);
            } else if (ch.path == GLBAnimationChannel::Scale) {
                node.scale = QVector3D(ch.values[0], ch.values[1], ch.values[2]);
            }
            continue;
        }

        if (t >= ch.times.back()) {
            size_t last = ch.times.size() - 1;
            if (ch.path == GLBAnimationChannel::Translation) {
                node.translation = QVector3D(ch.values[last*3], ch.values[last*3+1], ch.values[last*3+2]);
            } else if (ch.path == GLBAnimationChannel::Rotation) {
                node.rotation = QQuaternion(ch.values[last*4+3], ch.values[last*4], ch.values[last*4+1], ch.values[last*4+2]);
            } else if (ch.path == GLBAnimationChannel::Scale) {
                node.scale = QVector3D(ch.values[last*3], ch.values[last*3+1], ch.values[last*3+2]);
            }
            continue;
        }

        size_t k = 0;
        while (k + 1 < ch.times.size() && ch.times[k + 1] < t) ++k;

        float t0 = ch.times[k];
        float t1 = ch.times[k + 1];
        float alpha = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;

        if (ch.path == GLBAnimationChannel::Translation) {
            QVector3D p0(ch.values[k*3], ch.values[k*3+1], ch.values[k*3+2]);
            QVector3D p1(ch.values[(k+1)*3], ch.values[(k+1)*3+1], ch.values[(k+1)*3+2]);
            node.translation = p0 * (1.0f - alpha) + p1 * alpha;
        } else if (ch.path == GLBAnimationChannel::Rotation) {
            QQuaternion q0(ch.values[k*4+3], ch.values[k*4], ch.values[k*4+1], ch.values[k*4+2]);
            QQuaternion q1(ch.values[(k+1)*4+3], ch.values[(k+1)*4], ch.values[(k+1)*4+1], ch.values[(k+1)*4+2]);
            node.rotation = QQuaternion::slerp(q0, q1, alpha);
        } else if (ch.path == GLBAnimationChannel::Scale) {
            QVector3D s0(ch.values[k*3], ch.values[k*3+1], ch.values[k*3+2]);
            QVector3D s1(ch.values[(k+1)*3], ch.values[(k+1)*3+1], ch.values[(k+1)*3+2]);
            node.scale = s0 * (1.0f - alpha) + s1 * alpha;
        }
    }

    update_node_world_transforms();
    perform_skinning();
}

void GLBModel::update_node_world_transforms() {
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        GLBNode& n = m_nodes[i];
        if (!n.has_matrix) {
            n.local_matrix.setToIdentity();
            n.local_matrix.translate(n.translation);
            n.local_matrix.rotate(n.rotation);
            n.local_matrix.scale(n.scale);
        }
    }

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i].parent == -1) {
            m_nodes[i].world_matrix = m_nodes[i].local_matrix;
        }
    }

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i].parent >= 0) {
            m_nodes[i].world_matrix = m_nodes[m_nodes[i].parent].world_matrix * m_nodes[i].local_matrix;
        }
    }

    // Compute skin palettes
    for (auto& skin : m_skins) {
        for (size_t j = 0; j < skin.joints.size() && j < skin.inverse_bind_matrices.size(); ++j) {
            int joint_node = skin.joints[j];
            if (joint_node >= 0 && joint_node < (int)m_nodes.size()) {
                skin.palette[j] = m_nodes[joint_node].world_matrix * skin.inverse_bind_matrices[j];
            } else {
                skin.palette[j].setToIdentity();
            }
        }
    }
}

void GLBModel::perform_skinning() {
    for (auto& mesh : m_meshes) {
        if (!mesh.has_skin || mesh.skin_index < 0 || mesh.skin_index >= (int)m_skins.size()) continue;
        const GLBSkin& skin = m_skins[mesh.skin_index];
        if (skin.palette.empty()) continue;

        const size_t nv = mesh.orig_positions.size() / 3;
        mesh.skinned_positions.resize(mesh.orig_positions.size());
        mesh.skinned_normals.resize(mesh.orig_normals.size());

        for (size_t v = 0; v < nv; ++v) {
            float px = mesh.orig_positions[v * 3 + 0];
            float py = mesh.orig_positions[v * 3 + 1];
            float pz = mesh.orig_positions[v * 3 + 2];
            QVector3D p(px, py, pz);

            QVector3D skinned_p(0.0f, 0.0f, 0.0f);
            float total_weight = 0.0f;

            for (int k = 0; k < 4; ++k) {
                float w = mesh.weights[v * 4 + k];
                if (w <= 1e-5f) continue;
                uint16_t j = mesh.joints[v * 4 + k];
                if (j < skin.palette.size()) {
                    skinned_p += skin.palette[j].map(p) * w;
                    total_weight += w;
                }
            }

            if (total_weight > 1e-4f) {
                mesh.skinned_positions[v * 3 + 0] = skinned_p.x();
                mesh.skinned_positions[v * 3 + 1] = skinned_p.y();
                mesh.skinned_positions[v * 3 + 2] = skinned_p.z();
            } else {
                mesh.skinned_positions[v * 3 + 0] = px;
                mesh.skinned_positions[v * 3 + 1] = py;
                mesh.skinned_positions[v * 3 + 2] = pz;
            }
        }

        // Upload updated positions to GPU VBO
        if (mesh.pos_vbo) {
            glBindBuffer(GL_ARRAY_BUFFER, mesh.pos_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, mesh.skinned_positions.size() * sizeof(float), mesh.skinned_positions.data());
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
    }
}

void GLBModel::draw(bool wireframe, bool show_textures, ViewportShader* shader) {
    glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
    glEnable(GL_LIGHTING);

    for (const auto& mesh : m_meshes) {
        if (!mesh.vao || mesh.index_count <= 0) continue;

        GLuint texture = (show_textures && mesh.texture_id) ? mesh.texture_id : 0;
        if (texture) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texture);
        } else {
            glDisable(GL_TEXTURE_2D);
        }

        glColor4f(mesh.base_color.x(), mesh.base_color.y(), mesh.base_color.z(), mesh.base_color.w());

        if (shader && shader->ready()) {
            shader->setHasTexture(texture != 0);
            if (texture) shader->setTexture(0);
            shader->setMaterialColor(mesh.base_color.x(), mesh.base_color.y(), mesh.base_color.z(), mesh.base_color.w());
        }

        glBindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);

        if (texture) glBindTexture(GL_TEXTURE_2D, 0);
    }
}

} // namespace ruby::render
