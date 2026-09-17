#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <QOpenGLFunctions>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector3D>
#include <QVector4D>

#include <QOpenGLExtraFunctions>

namespace ruby::render {

class ViewportShader;

struct GLBMeshGpu {
    GLuint vao = 0;
    GLuint pos_vbo = 0;
    GLuint norm_vbo = 0;
    GLuint uv_vbo = 0;
    GLuint ebo = 0;
    int index_count = 0;
    int vertex_count = 0;

    GLuint texture_id = 0;
    QVector4D base_color{1.0f, 1.0f, 1.0f, 1.0f};

    bool has_skin = false;
    int skin_index = -1;

    // CPU copies for CPU-skinning or bounding box
    std::vector<float> orig_positions;
    std::vector<float> orig_normals;
    std::vector<float> skinned_positions;
    std::vector<float> skinned_normals;
    std::vector<float> uvs;
    std::vector<uint16_t> joints;   // 4 joints per vertex
    std::vector<float> weights;      // 4 weights per vertex
    std::vector<uint32_t> indices;

    void release(QOpenGLExtraFunctions* gl);
};

struct GLBNode {
    std::string name;
    int parent = -1;
    std::vector<int> children;

    QVector3D translation{0.0f, 0.0f, 0.0f};
    QQuaternion rotation{1.0f, 0.0f, 0.0f, 0.0f}; // scalar (w), x, y, z
    QVector3D scale{1.0f, 1.0f, 1.0f};

    bool has_matrix = false;
    QMatrix4x4 local_matrix;
    QMatrix4x4 world_matrix;

    int mesh_index = -1;
    int skin_index = -1;
};

struct GLBSkin {
    std::string name;
    int skeleton_root = -1;
    std::vector<int> joints; // Node indices
    std::vector<QMatrix4x4> inverse_bind_matrices;
    std::vector<QMatrix4x4> palette;
};

struct GLBAnimationChannel {
    int node_index = -1;
    enum Path { Translation, Rotation, Scale } path = Translation;
    std::vector<float> times;
    std::vector<float> values; // 3 floats per key for trans/scale, 4 for quat
};

struct GLBAnimation {
    std::string name;
    float duration = 0.0f;
    std::vector<GLBAnimationChannel> channels;
};

class GLBModel : protected QOpenGLExtraFunctions {
public:
    GLBModel() = default;
    ~GLBModel();

    void clear();

    bool load_from_file(const std::string& filepath, std::string* err = nullptr);

    void update_animation(float time_seconds);
    void set_active_animation(int index);
    int active_animation() const { return m_active_animation; }
    int animation_count() const { return static_cast<int>(m_animations.size()); }
    const std::string& animation_name(int index) const;
    float current_animation_duration() const;

    void draw(bool wireframe, bool show_textures, ViewportShader* shader = nullptr);

    bool is_loaded() const { return !m_meshes.empty(); }
    float radius() const { return m_radius; }
    QVector3D center() const { return m_center; }
    QVector3D min_bounds() const { return m_min_bounds; }
    QVector3D max_bounds() const { return m_max_bounds; }
    int mesh_count() const { return static_cast<int>(m_meshes.size()); }
    int total_vertices() const;

    const std::vector<GLBAnimation>& animations() const { return m_animations; }

private:
    bool load_companion_motions(const std::string& glb_path);
    void update_node_world_transforms();
    void perform_skinning();

    std::vector<GLBMeshGpu> m_meshes;
    std::vector<GLBNode> m_nodes;
    std::vector<GLBSkin> m_skins;
    std::vector<GLBAnimation> m_animations;
    std::vector<GLuint> m_textures;

    int m_active_animation = 0;
    float m_current_time = 0.0f;

    QVector3D m_center{0.0f, 0.0f, 0.0f};
    QVector3D m_min_bounds{0.0f, 0.0f, 0.0f};
    QVector3D m_max_bounds{0.0f, 0.0f, 0.0f};
    float m_radius = 50.0f;
};

} // namespace ruby::render
