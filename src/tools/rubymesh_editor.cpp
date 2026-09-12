// rubymesh_editor.cpp — RubyMesh Workspace: full-screen 3D editor for manually
// authoring .rbm collision zones on a POD model.
//
// Pure-UI module: every piece of editable state lives in rbmed::WorkspaceState
// (owned by the host ViewerState); all persistent data lives in the
// rbm::RubyMesh handed in via WorkspaceContext; geometry/GL primitives come
// from av:: (renderer / FBO / camera) and swk:: (screen projection). No
// globals: draw_workspace() is fully re-entrant per (ctx, ws).

#include "tools/rubymesh_editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "imgui.h"
#include "platform/IconsFontAwesome6.h"
#include "tools/rubymesh.h"
#include "tools/scene_workspace.h"

namespace rbmed {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// ── Tiny vector math (local, no renderer dependency) ───────────────────────
void vec_cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
float vec_dot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
void vec_sub(const float a[3], const float b[3], float out[3]) {
    out[0] = a[0] - b[0]; out[1] = a[1] - b[1]; out[2] = a[2] - b[2];
}
void vec_transform(const float m[16], const float v[3], float out[3]) {
    out[0] = m[0] * v[0] + m[4] * v[1] + m[8]  * v[2] + m[12];
    out[1] = m[1] * v[0] + m[5] * v[1] + m[9]  * v[2] + m[13];
    out[2] = m[2] * v[0] + m[6] * v[1] + m[10] * v[2] + m[14];
}

// ── Möller–Trumbore ray/triangle intersection ──────────────────────────────
// Returns false for parallel/degenerate triangles. With @p reject_backfaces
// only hits whose normal faces the ray direction (det > eps) are accepted.
bool ray_triangle_mt(const float ro[3], const float rd[3],
                     const float v0[3], const float v1[3], const float v2[3],
                     bool reject_backfaces, float* t_out) {
    constexpr float kEps = 1e-7f;
    float e1[3], e2[3], pv[3], tv[3], qv[3];
    vec_sub(v1, v0, e1);
    vec_sub(v2, v0, e2);
    vec_cross(rd, e2, pv);
    const float det = vec_dot(e1, pv);
    if (reject_backfaces) {
        if (det < kEps) return false;
    } else if (det > -kEps && det < kEps) {
        return false;
    }
    const float inv_det = 1.0f / det;
    vec_sub(ro, v0, tv);
    const float u = vec_dot(tv, pv) * inv_det;
    if (u < 0.0f || u > 1.0f) return false;
    vec_cross(tv, e1, qv);
    const float v = vec_dot(rd, qv) * inv_det;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = vec_dot(e2, qv) * inv_det;
    if (t < 0.0f) return false;
    if (t_out) *t_out = t;
    return true;
}

// Ray / horizontal plane (constant Z) intersection. False when parallel or
// the plane lies behind the ray origin.
bool ray_plane_z(const float ro[3], const float rd[3], float plane_z, float out[3]) {
    if (std::fabs(rd[2]) < 1e-6f) return false;
    const float t = (plane_z - ro[2]) / rd[2];
    if (t < 0.0f) return false;
    out[0] = ro[0] + rd[0] * t;
    out[1] = ro[1] + rd[1] * t;
    out[2] = plane_z;
    return true;
}

// ── Model transform instances ──────────────────────────────────────────────
// Bind pose (frame 0) node matrices + optional center-point removal + the
// workspace depth shift — IDENTICAL for the ghost render and the ray picking,
// so what you see is exactly what you pick.
struct MeshInstance {
    int   mesh_index = -1;
    float mat[16];
};

void build_instances(const WorkspaceContext& ctx, std::vector<MeshInstance>& out) {
    out.clear();
    auto base_matrix = [&](float m[16]) {
        av::mat4_identity(m);
        if (ctx.model.has_center_point) {
            float c[16], tmp[16];
            av::mat4_translate(c, -ctx.model.center_point[0],
                               -ctx.model.center_point[1],
                               -ctx.model.center_point[2]);
            av::mat4_multiply(tmp, m, c);
            std::memcpy(m, tmp, sizeof(float) * 16);
        }
        if (ctx.source_z_shift != 0.0f) {
            float s[16], tmp[16];
            av::mat4_translate(s, 0.0f, 0.0f, ctx.source_z_shift);
            av::mat4_multiply(tmp, s, m);
            std::memcpy(m, tmp, sizeof(float) * 16);
        }
    };

    if (!ctx.model.nodes.empty()) {
        for (int i = 0; i < (int)ctx.model.nodes.size(); ++i) {
            const auto& node = ctx.model.nodes[i];
            if (node.object_index < 0 ||
                node.object_index >= (int)ctx.model.meshes.size())
                continue;
            MeshInstance inst;
            inst.mesh_index = node.object_index;
            float base[16], node_mat[16];
            base_matrix(base);
            av::get_node_matrix(ctx.model, i, 0.0f, node_mat);
            av::mat4_multiply(inst.mat, base, node_mat);
            out.push_back(inst);
        }
    } else {
        for (int i = 0; i < (int)ctx.model.meshes.size(); ++i) {
            MeshInstance inst;
            inst.mesh_index = i;
            base_matrix(inst.mat);
            out.push_back(inst);
        }
    }
}

// ── Zone / state helpers ───────────────────────────────────────────────────

void mark_dirty(WorkspaceState& ws) {
    ws.dirty = true;
    ws.autosave_timer = 0.5f;          // autosave ≤500 ms after any edit
}

void push_undo(WorkspaceContext& ctx, WorkspaceState& ws) {
    ws.undo_stack.push_back(ctx.rubymesh.zones);
    if (ws.undo_stack.size() > 64)
        ws.undo_stack.erase(ws.undo_stack.begin());
    ws.redo_stack.clear();
}

void notify(WorkspaceState& ws, const std::string& msg) {
    ws.notice = msg;
    ws.notice_timer = 5.0f;
}

rbm::Zone* active_zone(rbm::RubyMesh& mesh, const WorkspaceState& ws) {
    if (ws.active_zone < 0 || ws.active_zone >= (int)mesh.zones.size()) return nullptr;
    return &mesh.zones[ws.active_zone];
}

void select_zone(WorkspaceState& ws, int index) {
    ws.active_zone = index;
    ws.hovered_vertex = -1;
    ws.dragging_vertex = -1;
}

std::string fresh_zone_name(const rbm::RubyMesh& mesh) {
    int highest = -1;
    for (const auto& z : mesh.zones) {
        if (z.name.rfind("zone_", 0) != 0) continue;
        bool numeric = true;
        int value = 0;
        for (size_t i = 5; i < z.name.size(); ++i) {
            if (z.name[i] < '0' || z.name[i] > '9') { numeric = false; break; }
            value = value * 10 + (z.name[i] - '0');
            if (value > 1000000) { numeric = false; break; }
        }
        if (numeric) highest = std::max(highest, value);
    }
    return "zone_" + std::to_string(highest + 1);
}

void frame_camera_on_model(WorkspaceContext& ctx) {
    av::Camera& cam = ctx.camera;
    const av::PODModel& m = ctx.model;
    cam.target[0] = (m.min_x + m.max_x) * 0.5f;
    cam.target[1] = (m.min_y + m.max_y) * 0.5f;
    cam.target[2] = (m.min_z + m.max_z) * 0.5f + ctx.source_z_shift;
    const float extent = std::max(m.max_x - m.min_x,
                          std::max(m.max_y - m.min_y, m.max_z - m.min_z));
    cam.distance = std::max(2.0f, std::max(1.0f, extent) * 1.6f);
    cam.near_plane = std::max(0.01f, cam.distance / 10000.0f);
    cam.far_plane = std::max(1000.0f, cam.distance + 4000.0f);
}

bool save_now(WorkspaceContext& ctx, WorkspaceState& ws) {
    if (ctx.rbm_path.empty()) {
        notify(ws, "No .rbm path — cannot save.");
        return false;
    }
    std::string err;
    if (!rbm::rbm_save(ctx.rubymesh, ctx.rbm_path, err)) {
        notify(ws, "Save failed: " + err);
        std::fprintf(stderr, "[RubyMesh] save failed: %s\n", err.c_str());
        return false;
    }
    ws.dirty = false;
    ws.autosave_timer = 0.0f;
    ws.save_badge_timer = 2.5f;
    return true;
}

// ── Picking ────────────────────────────────────────────────────────────────

bool pick_surface_point(WorkspaceContext& ctx,
                        const std::vector<MeshInstance>& instances,
                        const float ro[3], const float rd[3], float out_xyz[3]) {
    float best_t = 1e30f;
    bool found = false;
    // Pass 1 rejects back-faces (spec). Pass 2 — only used when pass 1 missed —
    // accepts either winding so inward-wound assets can still be traced.
    for (int pass = 0; pass < 2 && !(pass == 1 && found); ++pass) {
        const bool reject_back = pass == 0;
        for (const auto& inst : instances) {
            const av::PODMesh& mesh = ctx.model.meshes[inst.mesh_index];
            const float* pos = mesh.positions.data();
            const size_t vcount = mesh.positions.size() / 3;
            if (!pos || vcount < 3) continue;

            auto check_tri = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
                if (i0 >= vcount || i1 >= vcount || i2 >= vcount) return;
                float lv[3], wv[3][3], t_hit;
                const uint32_t idx[3] = {i0, i1, i2};
                for (int k = 0; k < 3; ++k) {
                    const size_t base = (size_t)idx[k] * 3;
                    lv[0] = pos[base]; lv[1] = pos[base + 1]; lv[2] = pos[base + 2];
                    vec_transform(inst.mat, lv, wv[k]);
                }
                if (ray_triangle_mt(ro, rd, wv[0], wv[1], wv[2], reject_back, &t_hit) &&
                    t_hit < best_t) {
                    best_t = t_hit;
                    out_xyz[0] = ro[0] + rd[0] * t_hit;
                    out_xyz[1] = ro[1] + rd[1] * t_hit;
                    out_xyz[2] = ro[2] + rd[2] * t_hit;
                    found = true;
                }
            };

            if (!mesh.indices.empty()) {
                const size_t n_tri = mesh.indices.size() / 3;
                for (size_t t = 0; t < n_tri; ++t)
                    check_tri(mesh.indices[t * 3], mesh.indices[t * 3 + 1],
                              mesh.indices[t * 3 + 2]);
            } else {
                for (size_t t = 0; t + 2 < vcount; t += 3)
                    check_tri((uint32_t)t, (uint32_t)(t + 1), (uint32_t)(t + 2));
            }
        }
    }
    return found;
}

// Insertion point for a new vertex at a mouse ray: POD surface hit XY when
// available, else the zone's world_z plane. False only when the ray never
// reaches either (parallel + no surface under the cursor).
bool compute_insert_point(WorkspaceContext& ctx,
                          const std::vector<MeshInstance>& instances,
                          float world_z, const float ro[3], const float rd[3],
                          float out_xy[2]) {
    float hit[3];
    if (pick_surface_point(ctx, instances, ro, rd, hit)) {
        out_xy[0] = hit[0];
        out_xy[1] = hit[1];
        return true;
    }
    float plane[3];
    if (ray_plane_z(ro, rd, world_z, plane)) {
        out_xy[0] = plane[0];
        out_xy[1] = plane[1];
        return true;
    }
    return false;
}

// Project one zone vertex (world XY + zone depth) to viewport pixels.
bool zone_vertex_to_screen(const av::Camera& cam, const rbm::Zone& zone,
                           const std::pair<float, float>& v, int w, int h,
                           const ImVec2& vp_pos, ImVec2& out) {
    const float wp[3] = {v.first, v.second, zone.world_z};
    return swk::world_to_screen(cam, w, h, vp_pos, wp, out);
}

// 2D polygon helpers ---------------------------------------------------------

double poly_area2(const std::vector<std::pair<float, float>>& pts) {
    const size_t n = pts.size();
    if (n < 3) return 0.0;
    double a = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const size_t j = (i + 1) % n;
        a += (double)pts[i].first * pts[j].second -
             (double)pts[j].first * pts[i].second;
    }
    return a * 0.5;
}

// Andrew monotone chain; output CCW without collinear points.
std::vector<std::pair<float, float>> convex_hull2(
    std::vector<std::pair<float, float>> pts) {
    if (pts.size() < 3) return pts;
    std::sort(pts.begin(), pts.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first < b.first : a.second < b.second;
    });
    const auto cross = [](const std::pair<float, float>& o,
                          const std::pair<float, float>& a,
                          const std::pair<float, float>& b) {
        return (a.first - o.first) * (b.second - o.second) -
               (a.second - o.second) * (b.first - o.first);
    };
    std::vector<std::pair<float, float>> lower, upper;
    for (const auto& p : pts) {
        while (lower.size() >= 2 &&
               cross(lower[lower.size() - 2], lower.back(), p) <= 0.0f)
            lower.pop_back();
        lower.push_back(p);
    }
    for (auto it = pts.rbegin(); it != pts.rend(); ++it) {
        while (upper.size() >= 2 &&
               cross(upper[upper.size() - 2], upper.back(), *it) <= 0.0f)
            upper.pop_back();
        upper.push_back(*it);
    }
    lower.pop_back();
    upper.pop_back();
    lower.insert(lower.end(), upper.begin(), upper.end());
    return lower;
}

bool point_in_tri(const ImVec2& a, const ImVec2& b, const ImVec2& c, const ImVec2& p) {
    const float d1 = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
    const float d2 = (c.x - b.x) * (p.y - b.y) - (c.y - b.y) * (p.x - b.x);
    const float d3 = (a.x - c.x) * (p.y - c.y) - (a.y - c.y) * (p.x - c.x);
    const bool has_neg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
    const bool has_pos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
    return !(has_neg && has_pos);
}

void triangulate_polygon(const std::vector<ImVec2>& pts, std::vector<int>& out) {
    out.clear();
    const int n = (int)pts.size();
    if (n < 3) return;
    std::vector<int> idx(n);
    for (int i = 0; i < n; ++i) idx[i] = i;
    int guard = n * n * 2;
    while ((int)idx.size() > 3 && guard-- > 0) {
        bool clipped = false;
        const int m = (int)idx.size();
        for (int i = 0; i < m && !clipped; ++i) {
            const int i0 = (i + m - 1) % m;
            const int i1 = i;
            const int i2 = (i + 1) % m;
            const ImVec2& a = pts[idx[i0]];
            const ImVec2& b = pts[idx[i1]];
            const ImVec2& c = pts[idx[i2]];
            const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (std::fabs(area) < 1e-9f) continue;   // collinear ear
            bool any_inside = false;
            for (int k = 0; k < m && !any_inside; ++k) {
                if (k == i0 || k == i1 || k == i2) continue;
                if (point_in_tri(a, b, c, pts[idx[k]])) any_inside = true;
            }
            if (any_inside) continue;
            out.push_back(idx[i0]);
            out.push_back(idx[i1]);
            out.push_back(idx[i2]);
            idx.erase(idx.begin() + i1);
            clipped = true;
        }
        if (!clipped) break;   // self-intersecting polygon — skip fill
    }
    if (idx.size() == 3) {
        out.push_back(idx[0]);
        out.push_back(idx[1]);
        out.push_back(idx[2]);
    }
}

const char* kMaterialNames[] = {"Stone", "Grass", "Dirt", "Wood",
                                "Metal", "Sand", "Ice", "Custom"};
constexpr int kMaterialCount = 8;

} // namespace

// ============================================================================
// draw_workspace
// ============================================================================
bool draw_workspace(WorkspaceContext& ctx, WorkspaceState& ws,
                    int screen_w, int screen_h) {
    if (!ws.open) {
        if (ws.fbo) { av::delete_fbo(ws.fbo, ws.fbo_tex); ws.fbo = 0; ws.fbo_tex = 0; }
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    const float dt = std::max(1e-4f, io.DeltaTime);
    if (ws.save_badge_timer > 0.0f) ws.save_badge_timer -= dt;
    if (ws.notice_timer > 0.0f) {
        ws.notice_timer -= dt;
        if (ws.notice_timer <= 0.0f) ws.notice.clear();
    }

    // Keep selection valid.
    if (ctx.rubymesh.zones.empty()) {
        ws.active_zone = -1;
    } else if (ws.active_zone >= (int)ctx.rubymesh.zones.size()) {
        ws.active_zone = (int)ctx.rubymesh.zones.size() - 1;
    }
    if (ws.dragging_vertex >= 0) {
        const rbm::Zone* z = active_zone(ctx.rubymesh, ws);
        if (!z || ws.dragging_vertex >= (int)z->vertices.size())
            ws.dragging_vertex = -1;
    }

    // ── Autosave ────────────────────────────────────────────────────────────
    if (ws.dirty) {
        ws.autosave_timer -= dt;
        if (ws.autosave_timer <= 0.0f) save_now(ctx, ws);
    }

    // ── Full-screen window (host calls us after its own main window, so this
    //    window renders on top and occludes the scene editor beneath) ───────
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp ? vp->WorkPos : ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)screen_w, (float)screen_h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.11f, 0.135f, 1.0f));
    ImGui::Begin("##RubyMeshWorkspace", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        ImGui::SetWindowFocus();

    const float window_w = ImGui::GetWindowWidth();
    constexpr float kBottomH = 42.0f;
    constexpr float kLeftW = 224.0f;
    const bool has_zone = ws.active_zone >= 0 &&
                          ws.active_zone < (int)ctx.rubymesh.zones.size();

    bool close_requested = false;
    bool apply_requested = false;
    bool save_clicked = false;

    // ── Title bar ───────────────────────────────────────────────────────────
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.17f, 0.19f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.27f, 0.30f, 0.38f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.21f, 0.24f, 0.32f, 1.0f));
        if (ImGui::Button(ICON_FA_ARROW_LEFT "  Back"))
            close_requested = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Leave the RubyMesh workspace.\nUnsaved edits prompt before closing.");
        ImGui::SameLine();
        ImGui::TextUnformatted(" RubyMesh Workspace");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.85f, 0.60f, 1.0f));
        ImGui::TextUnformatted(ICON_FA_LAYER_GROUP);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextUnformatted(ctx.rubymesh.model_name.c_str());
        if (ImGui::IsItemHovered() && !ctx.rbm_path.empty())
            ImGui::SetTooltip("%s", ctx.rbm_path.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled(ws.dirty ? " *" : "");

        ImGui::SameLine(window_w - 252.0f);
        if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Save"))
            save_clicked = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save the .rbm file now");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.48f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.62f, 0.36f, 1.0f));
        if (ImGui::Button("Apply to Scene " ICON_FA_ARROW_RIGHT))
            apply_requested = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Persist the .rbm, then generate a Swordigo GroundMesh\n"
                              "object for every enabled zone with ≥3 vertices.");
        ImGui::PopStyleColor(2);
        if (ws.save_badge_timer > 0.0f) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.95f, 0.55f, 1.0f));
            ImGui::TextUnformatted(ICON_FA_CHECK " Saved");
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleColor(3);
        ImGui::Separator();
    }
    if (save_clicked) save_now(ctx, ws);

    // ── Layout: body (left panel + viewport) over a bottom bar ──────────────
    const float avail_x = ImGui::GetContentRegionAvail().x;
    const float body_h = ImGui::GetContentRegionAvail().y - kBottomH - 4.0f;
    ImGui::BeginChild("##rbm_body", ImVec2(avail_x, body_h), false,
                      ImGuiWindowFlags_NoScrollbar);
    {
        // Left panel --------------------------------------------------------
        ImGui::BeginChild("##rbm_left", ImVec2(kLeftW, 0), ImGuiChildFlags_Borders);
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 3.0f));
            // View toggles
            ImGui::PushStyleColor(ImGuiCol_Button, ws.show_pod
                ? ImVec4(0.16f, 0.40f, 0.60f, 1.0f) : ImVec4(0.16f, 0.18f, 0.23f, 1.0f));
            if (ImGui::Button(ICON_FA_EYE " Show POD", ImVec2((kLeftW - 12.0f) * 0.5f, 0)))
                ws.show_pod = !ws.show_pod;
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ws.show_all_zones
                ? ImVec4(0.16f, 0.40f, 0.60f, 1.0f) : ImVec4(0.16f, 0.18f, 0.23f, 1.0f));
            if (ImGui::Button("Zones", ImVec2((kLeftW - 12.0f) * 0.5f, 0)))
                ws.show_all_zones = !ws.show_all_zones;
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Show the other zones faintly (the active zone is always shown)");

            if (ImGui::Button(ICON_FA_PLUS " New Zone", ImVec2(-1.0f, 0.0f))) {
                push_undo(ctx, ws);
                rbm::Zone z;
                z.name = fresh_zone_name(ctx.rubymesh);
                z.world_z = ctx.seed_world_z;    // glue to the source object depth
                ctx.rubymesh.zones.push_back(std::move(z));
                select_zone(ws, (int)ctx.rubymesh.zones.size() - 1);
                mark_dirty(ws);
                notify(ws, "Created zone '" + ctx.rubymesh.zones.back().name + "'.");
            }
            ImGui::Separator();

            // Zone rows: [enabled]  name (n)   ^ v x
            const float rows_avail = std::max(0.0f, ImGui::GetContentRegionAvail().y);
            const float list_h = std::clamp(rows_avail * 0.46f, 40.0f, 420.0f);
            ImGui::BeginChild("##rbm_zone_rows", ImVec2(0, list_h),
                              ImGuiChildFlags_None);
            for (int i = 0; i < (int)ctx.rubymesh.zones.size(); ++i) {
                ImGui::PushID(i);
                rbm::Zone& z = ctx.rubymesh.zones[i];
                const bool selected = ws.active_zone == i;

                const bool was_enabled = z.enabled;
                if (ImGui::Checkbox("##on", &z.enabled) && z.enabled != was_enabled)
                    mark_dirty(ws);
                ImGui::SameLine();

                char label[160];
                std::snprintf(label, sizeof(label), "%s (%zu)",
                              z.name.c_str(), z.vertices.size());
                const float row_w = ImGui::GetContentRegionAvail().x;
                const float btns_w = 58.0f;   // ^ v x + spacing
                const float name_w = std::max(40.0f, row_w - btns_w - 4.0f);
                if (selected) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.95f, 0.60f, 1.0f));
                if (ImGui::Selectable(label, selected, 0, ImVec2(name_w, 0)) && !selected)
                    select_zone(ws, i);
                if (selected) ImGui::PopStyleColor();
                ImGui::SameLine();
                if (ImGui::SmallButton("^") && i > 0) {
                    push_undo(ctx, ws);
                    std::swap(ctx.rubymesh.zones[i - 1], ctx.rubymesh.zones[i]);
                    select_zone(ws, i - 1);
                    mark_dirty(ws);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("v") && i + 1 < (int)ctx.rubymesh.zones.size()) {
                    push_undo(ctx, ws);
                    std::swap(ctx.rubymesh.zones[i], ctx.rubymesh.zones[i + 1]);
                    select_zone(ws, i + 1);
                    mark_dirty(ws);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) {
                    push_undo(ctx, ws);
                    ctx.rubymesh.zones.erase(ctx.rubymesh.zones.begin() + i);
                    if (ws.active_zone == i) select_zone(ws, -1);
                    else if (ws.active_zone > i) ws.active_zone--;
                    mark_dirty(ws);
                    notify(ws, "Deleted zone.");
                    --i;   // re-examine the element that shifted into this slot
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Delete this zone");
                ImGui::PopID();
            }
            ImGui::EndChild();

            ImGui::Separator();
            ImGui::TextDisabled("Properties");
            const float props_h = ImGui::GetContentRegionAvail().y;
            ImGui::BeginChild("##rbm_props", ImVec2(0, props_h));
            {
                rbm::Zone* z = active_zone(ctx.rubymesh, ws);
                if (!z) {
                    ImGui::TextDisabled("Select or create a zone.");
                } else {
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
                    // Name
                    {
                        char buf[160];
                        std::snprintf(buf, sizeof(buf), "%s", z->name.c_str());
                        ImGui::SetNextItemWidth(-1.0f);
                        const bool edited = ImGui::InputText("Name", buf, sizeof(buf));
                        if (edited) {
                            if (ImGui::IsItemActivated()) push_undo(ctx, ws);
                            z->name = buf;
                            mark_dirty(ws);
                        }
                    }
                    auto drag_field = [&](const char* label, float& value, float speed,
                                          const char* fmt, float lo, float hi) {
                        const bool changed = ImGui::DragFloat(label, &value, speed, lo, hi, fmt);
                        if (changed) {
                            if (ImGui::IsItemActivated()) push_undo(ctx, ws);
                            mark_dirty(ws);
                        }
                        return changed;
                    };
                    ImGui::SetNextItemWidth(-1.0f);
                    drag_field("WorldZ", z->world_z, 0.5f, "%.2f", -100000.0f, 100000.0f);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Scene-depth layer for this zone's GroundMesh object.\n"
                                          "DepthMin/Max below are offsets around it.");
                    ImGui::SetNextItemWidth(-1.0f);
                    drag_field("DepthMin", z->depth_min, 0.5f, "%.2f", -100000.0f, 100000.0f);
                    ImGui::SetNextItemWidth(-1.0f);
                    drag_field("DepthMax", z->depth_max, 0.5f, "%.2f", -100000.0f, 100000.0f);

                    int mat = (int)z->material;
                    if (ImGui::Combo("Material", &mat, kMaterialNames, kMaterialCount)) {
                        if ((rbm::SurfaceMaterial)mat != z->material) {
                            push_undo(ctx, ws);
                            z->material = (rbm::SurfaceMaterial)mat;
                            mark_dirty(ws);
                        }
                    }

                    const bool was_inv = z->invisible;
                    if (ImGui::Checkbox("Invisible (ruby_transparent)", &z->invisible) &&
                        z->invisible != was_inv)
                        mark_dirty(ws);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Invisible zones render with ruby_transparent.pvr in-game.\n"
                                          "Uncheck to show textures below instead.");
                    if (!z->invisible) {
                        {
                            char buf[96];
                            std::snprintf(buf, sizeof(buf), "%s", z->top_texture.c_str());
                            ImGui::SetNextItemWidth(-1.0f);
                            if (ImGui::InputText("Top texture", buf, sizeof(buf))) {
                                if (ImGui::IsItemActivated()) push_undo(ctx, ws);
                                z->top_texture = buf;
                                mark_dirty(ws);
                            }
                        }
                        {
                            char buf[96];
                            std::snprintf(buf, sizeof(buf), "%s", z->front_texture.c_str());
                            ImGui::SetNextItemWidth(-1.0f);
                            if (ImGui::InputText("Front texture", buf, sizeof(buf))) {
                                if (ImGui::IsItemActivated()) push_undo(ctx, ws);
                                z->front_texture = buf;
                                mark_dirty(ws);
                            }
                        }
                    }
                    ImGui::TextDisabled("Vertices: %zu", z->vertices.size());

                    ImGui::Separator();
                    if (ImGui::Button("Clear Vertices", ImVec2(-1.0f, 0.0f)) &&
                        !z->vertices.empty()) {
                        push_undo(ctx, ws);
                        z->vertices.clear();
                        mark_dirty(ws);
                    }
                    if (ImGui::Button("Reverse Winding", ImVec2(-1.0f, 0.0f)) &&
                        z->vertices.size() >= 3) {
                        push_undo(ctx, ws);
                        std::reverse(z->vertices.begin(), z->vertices.end());
                        mark_dirty(ws);
                    }
                    ImGui::BeginDisabled(z->vertices.size() < 3);
                    if (ImGui::Button("Convex Hull", ImVec2(-1.0f, 0.0f))) {
                        push_undo(ctx, ws);
                        z->vertices = convex_hull2(z->vertices);
                        rbm::ensure_ccw(z->vertices);
                        mark_dirty(ws);
                    }
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Replace the polygon with its convex hull (CCW)");
                    ImGui::PopStyleVar();
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();

        // Viewport ----------------------------------------------------------
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::BeginChild("##rbm_view", ImVec2(0, 0), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            const ImVec2 vp0 = ImGui::GetCursorScreenPos();
            const float vp_avail_x = ImGui::GetContentRegionAvail().x;
            const float vp_avail_y = ImGui::GetContentRegionAvail().y;
            const int vw = std::max(1, (int)vp_avail_x);
            const int vh = std::max(1, (int)vp_avail_y);

            // ── Own FBO ─────────────────────────────────────────────────────
            if (!ws.fbo) {
                ws.fbo = av::create_fbo_hdr(vw, vh, &ws.fbo_tex);
            } else if (ws.fbo_w != vw || ws.fbo_h != vh) {
                av::resize_fbo_hdr(ws.fbo, vw, vh, &ws.fbo_tex);
            }
            ws.fbo_w = vw;
            ws.fbo_h = vh;

            // ── 3D pass: guide plane + POD ghost ────────────────────────────
            std::vector<MeshInstance> instances;
            if (ws.fbo) {
                build_instances(ctx, instances);
                av::begin_3d(ws.fbo, vw, vh, ctx.camera);
                av::set_inline_tonemap(true);
                av::clear_point_lights();
                av::clear_directional_lights();
                av::set_depth_fog(false, nullptr, 0.0f, 0.0f);

                // Guide plane at the active zone's depth (or the seed depth).
                const rbm::Zone* zact = active_zone(ctx.rubymesh, ws);
                const float plane_z = zact ? zact->world_z : ctx.seed_world_z;
                const av::PODModel& m = ctx.model;
                const float extent = std::max(m.max_x - m.min_x,
                                      std::max(m.max_y - m.min_y, m.max_z - m.min_z));
                av::render_grid_xy(std::max(120.0f, extent * 1.2f), plane_z);

                if (ws.show_pod) {
                    float ghost[4] = {0.42f, 0.52f, 0.75f, 0.40f};
                    for (const auto& inst : instances) {
                        if (inst.mesh_index < 0 ||
                            inst.mesh_index >= (int)ctx.gpu_meshes.size())
                            continue;
                        av::GPUMesh& gm = ctx.gpu_meshes[inst.mesh_index];
                        if (!gm.vao) continue;
                        gm.texture_id = 0;   // flat blue-grey ghost, no texture
                        av::render_mesh(gm, inst.mat, ghost, false);
                    }
                }
                av::end_3d();
            }

            // ── Blit + overlay ──────────────────────────────────────────────
            if (ws.fbo) {
                ImGui::Image((ImTextureID)(intptr_t)ws.fbo_tex,
                             ImVec2((float)vw, (float)vh),
                             ImVec2(0, 1), ImVec2(1, 0));
            }
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 vp1 = ImVec2(vp0.x + vw, vp0.y + vh);
            dl->PushClipRect(vp0, vp1, true);

            const av::Camera& cam = ctx.camera;
            const ImVec2 mouse = io.MousePos;
            const bool mouse_in_vp = mouse.x >= vp0.x && mouse.x <= vp1.x &&
                                     mouse.y >= vp0.y && mouse.y <= vp1.y;
            const bool text_typing = io.WantTextInput;

            // ── Project the zones onto the overlay ──────────────────────────
            std::vector<ImVec2> proj;
            std::vector<int> tri;
            for (int zi = 0; zi < (int)ctx.rubymesh.zones.size(); ++zi) {
                const rbm::Zone& z = ctx.rubymesh.zones[zi];
                const bool is_active = zi == ws.active_zone;
                if (!is_active && !ws.show_all_zones) continue;
                const size_t n = z.vertices.size();
                if (n == 0) continue;

                proj.clear();
                proj.reserve(n);
                bool all_ok = true;
                for (const auto& v : z.vertices) {
                    ImVec2 sp;
                    if (!zone_vertex_to_screen(cam, z, v, vw, vh, vp0, sp)) {
                        all_ok = false;
                        break;
                    }
                    proj.push_back(sp);
                }
                if (!all_ok) continue;

                const ImU32 fill_col = is_active
                    ? IM_COL32(20, 220, 120, 72)
                    : IM_COL32(60, 190, 210, 34);
                const ImU32 wire_col = is_active
                    ? IM_COL32(210, 255, 230, 255)
                    : IM_COL32(130, 220, 235, 150);

                if (proj.size() >= 3) {
                    triangulate_polygon(proj, tri);
                    for (size_t t = 0; t + 2 < tri.size(); t += 3)
                        dl->AddTriangleFilled(proj[tri[t]], proj[tri[t + 1]],
                                              proj[tri[t + 2]], fill_col);
                }
                for (size_t i = 0; i < proj.size(); ++i) {
                    const ImVec2& a = proj[i];
                    const ImVec2& b = proj[(i + 1) % proj.size()];
                    dl->AddLine(a, b, wire_col, is_active ? 2.0f : 1.2f);
                }

                // Vertex handles for the active zone only.
                if (is_active) {
                    for (size_t i = 0; i < proj.size(); ++i) {
                        const bool hover = ws.hovered_vertex == (int)i;
                        const bool drag = ws.dragging_vertex == (int)i;
                        const ImU32 c = drag ? IM_COL32(255, 255, 255, 255)
                                          : hover ? IM_COL32(255, 220, 70, 255)
                                                  : IM_COL32(235, 235, 235, 235);
                        const float r = drag ? 7.0f : hover ? 6.0f : 4.5f;
                        dl->AddCircleFilled(proj[i], r, c);
                        if (drag)
                            dl->AddCircle(proj[i], 10.0f, IM_COL32(255, 255, 255, 160), 0, 1.5f);
                    }
                }
            }

            // Hovered-vertex coordinate readout.
            if (!ws.insert_mode && ws.hovered_vertex >= 0 && ws.dragging_vertex < 0) {
                const rbm::Zone* z = active_zone(ctx.rubymesh, ws);
                if (z && ws.hovered_vertex < (int)z->vertices.size()) {
                    const auto& v = z->vertices[ws.hovered_vertex];
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "v%d  (%.1f, %.1f)",
                                  ws.hovered_vertex, v.first, v.second);
                    dl->AddText(ImVec2(vp0.x + 8.0f, vp1.y - 22.0f),
                                IM_COL32(255, 255, 255, 220), buf);
                }
            }
            // Mode hint.
            {
                const char* hint = ws.insert_mode
                    ? "INSERT: click the POD surface or guide plane to add a vertex  [V]/Esc = select"
                    : "SELECT: drag vertex handles  [A] = add-vertex mode  (MMB orbit / wheel zoom / RMB pan)";
                dl->AddText(ImVec2(vp0.x + 8.0f, vp0.y + 6.0f),
                            IM_COL32(150, 170, 200, 190), hint);
            }

            // ── Interaction ─────────────────────────────────────────────────
            const bool vp_active = mouse_in_vp && !text_typing;
            if (ws.insert_mode && mouse_in_vp)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

            // Hover computation (select mode only).
            if (!ws.insert_mode && vp_active && ws.dragging_vertex < 0) {
                int best = -1;
                float best_d2 = 12.0f * 12.0f;
                const rbm::Zone* z = active_zone(ctx.rubymesh, ws);
                if (z) {
                    for (size_t i = 0; i < z->vertices.size(); ++i) {
                        ImVec2 sp;
                        if (!zone_vertex_to_screen(cam, *z, z->vertices[i], vw, vh, vp0, sp))
                            continue;
                        const float dx = sp.x - mouse.x;
                        const float dy = sp.y - mouse.y;
                        const float d2 = dx * dx + dy * dy;
                        if (d2 < best_d2) { best_d2 = d2; best = (int)i; }
                    }
                }
                ws.hovered_vertex = best;
            } else if (ws.dragging_vertex < 0) {
                ws.hovered_vertex = -1;
            }

            // Camera: MMB orbit, RMB pan, wheel zoom.
            if (vp_active || ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                    const ImVec2 delta = io.MouseDelta;
                    ctx.camera.yaw += delta.x * 0.5f;
                    ctx.camera.pitch = std::clamp(ctx.camera.pitch + delta.y * 0.5f,
                                                  -89.0f, 89.0f);
                } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                    const ImVec2 delta = io.MouseDelta;
                    const float scale = ctx.camera.distance * 0.003f;
                    const float yaw = ctx.camera.yaw * kPi / 180.0f;
                    const float pitch = ctx.camera.pitch * kPi / 180.0f;
                    const float rx = std::cos(yaw), rz = -std::sin(yaw);
                    const float ux = -std::sin(yaw) * std::sin(pitch);
                    const float uy = std::cos(pitch);
                    const float uz = -std::cos(yaw) * std::sin(pitch);
                    ctx.camera.target[0] -= rx * delta.x * scale;
                    ctx.camera.target[2] -= rz * delta.x * scale;
                    ctx.camera.target[0] += ux * delta.y * scale;
                    ctx.camera.target[1] += uy * delta.y * scale;
                    ctx.camera.target[2] += uz * delta.y * scale;
                }
                if (mouse_in_vp && io.MouseWheel != 0.0f) {
                    const float factor = std::pow(0.94f, io.MouseWheel);
                    const float min_dist = std::max(1.0f, ctx.camera.near_plane * 4.0f);
                    ctx.camera.distance = std::clamp(ctx.camera.distance * factor,
                                                     min_dist, 5000.0f);
                    ctx.camera.near_plane = std::max(0.01f, ctx.camera.distance / 10000.0f);
                    ctx.camera.far_plane = std::max(1000.0f, ctx.camera.distance + 4000.0f);
                }
            }

            // Keyboard shortcuts (only when this window has keyboard focus).
            const bool keys_ok = !text_typing &&
                ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            if (keys_ok) {
                if (ImGui::IsKeyPressed(ImGuiKey_F)) frame_camera_on_model(ctx);
                if (ImGui::IsKeyPressed(ImGuiKey_A)) {
                    if (ctx.rubymesh.zones.empty()) {
                        // First use: create zone_0 automatically.
                        push_undo(ctx, ws);
                        rbm::Zone z;
                        z.name = fresh_zone_name(ctx.rubymesh);
                        z.world_z = ctx.seed_world_z;
                        ctx.rubymesh.zones.push_back(std::move(z));
                        select_zone(ws, 0);
                        mark_dirty(ws);
                    }
                    ws.insert_mode = true;
                    ws.dragging_vertex = -1;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_V) ||
                    ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    ws.insert_mode = false;
                    ws.dragging_vertex = -1;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Delete) &&
                    !ws.insert_mode && ws.hovered_vertex >= 0) {
                    rbm::Zone* z = active_zone(ctx.rubymesh, ws);
                    if (z && ws.hovered_vertex < (int)z->vertices.size()) {
                        push_undo(ctx, ws);
                        z->vertices.erase(z->vertices.begin() + ws.hovered_vertex);
                        ws.hovered_vertex = -1;
                        ws.dragging_vertex = -1;
                        mark_dirty(ws);
                    }
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Z) && io.KeyCtrl) {
                    if (!io.KeyShift && !ws.undo_stack.empty()) {
                        ws.redo_stack.push_back(ctx.rubymesh.zones);
                        ctx.rubymesh.zones = std::move(ws.undo_stack.back());
                        ws.undo_stack.pop_back();
                        mark_dirty(ws);
                    } else if (io.KeyShift && !ws.redo_stack.empty()) {
                        ws.undo_stack.push_back(ctx.rubymesh.zones);
                        ctx.rubymesh.zones = std::move(ws.redo_stack.back());
                        ws.redo_stack.pop_back();
                        mark_dirty(ws);
                    }
                } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y) &&
                           !ws.redo_stack.empty()) {
                    ws.undo_stack.push_back(ctx.rubymesh.zones);
                    ctx.rubymesh.zones = std::move(ws.redo_stack.back());
                    ws.redo_stack.pop_back();
                    mark_dirty(ws);
                }
            }

            // Mouse editing --------------------------------------------------
            if (!text_typing) {
                rbm::Zone* z = active_zone(ctx.rubymesh, ws);

                // Vertex drag (select mode).
                if (!ws.insert_mode) {
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse_in_vp) {
                        if (ws.hovered_vertex >= 0 && z &&
                            ws.hovered_vertex < (int)z->vertices.size()) {
                            push_undo(ctx, ws);   // snapshot at drag start
                            ws.dragging_vertex = ws.hovered_vertex;
                        }
                    }
                    if (ws.dragging_vertex >= 0 && z &&
                        ws.dragging_vertex < (int)z->vertices.size()) {
                        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                            float ro[3], rd[3];
                            av::unproject_ray(ctx.camera, vw, vh, vp0.x, vp0.y,
                                              mouse.x, mouse.y, ro, rd);
                            float plane[3];
                            if (ray_plane_z(ro, rd, z->world_z, plane)) {
                                z->vertices[ws.dragging_vertex].first = plane[0];
                                z->vertices[ws.dragging_vertex].second = plane[1];
                                mark_dirty(ws);
                            }
                        } else {
                            ws.dragging_vertex = -1;
                        }
                    }
                } else {
                    // Insert mode: every left click appends a vertex.
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse_in_vp) {
                        if (!z) {
                            notify(ws, "Create a zone first ([+ New Zone] or press A again).");
                        } else {
                            float ro[3], rd[3];
                            av::unproject_ray(ctx.camera, vw, vh, vp0.x, vp0.y,
                                              mouse.x, mouse.y, ro, rd);
                            float xy[2];
                            if (compute_insert_point(ctx, instances, z->world_z,
                                                     ro, rd, xy)) {
                                push_undo(ctx, ws);
                                z->vertices.emplace_back(xy[0], xy[1]);
                                ws.hovered_vertex = (int)z->vertices.size() - 1;
                                mark_dirty(ws);
                            } else {
                                notify(ws, "Click did not reach the surface or guide plane.");
                            }
                        }
                    }
                }
            }

            dl->PopClipRect();
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();

    // ── Bottom bar ──────────────────────────────────────────────────────────
    ImGui::BeginChild("##rbm_bottom", ImVec2(avail_x, kBottomH), false,
                      ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ws.insert_mode
            ? ImVec4(0.35f, 0.24f, 0.15f, 1.0f) : ImVec4(0.16f, 0.18f, 0.24f, 1.0f));
        if (ImGui::Button(ICON_FA_PLUS " Add Vertex")) {
            if (ctx.rubymesh.zones.empty()) {
                push_undo(ctx, ws);
                rbm::Zone z;
                z.name = fresh_zone_name(ctx.rubymesh);
                z.world_z = ctx.seed_world_z;
                ctx.rubymesh.zones.push_back(std::move(z));
                select_zone(ws, 0);
                mark_dirty(ws);
            }
            ws.insert_mode = true;
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Click the POD surface / guide plane to add a vertex [A]");

        ImGui::SameLine();
        ImGui::BeginDisabled(!has_zone);
        if (ImGui::Button("Delete Vertex")) {
            rbm::Zone* z = active_zone(ctx.rubymesh, ws);
            if (z && ws.hovered_vertex >= 0 && ws.hovered_vertex < (int)z->vertices.size()) {
                push_undo(ctx, ws);
                z->vertices.erase(z->vertices.begin() + ws.hovered_vertex);
                ws.hovered_vertex = -1;
                ws.dragging_vertex = -1;
                mark_dirty(ws);
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Delete the highlighted vertex (Del key)");

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::BeginDisabled(!has_zone);
        {
            const float z_drag_w = 130.0f;
            ImGui::SetNextItemWidth(z_drag_w);
            rbm::Zone* z = active_zone(ctx.rubymesh, ws);
            const bool changed = z && ImGui::DragFloat("Z", &z->world_z, 0.5f,
                                                       -100000.0f, 100000.0f, "%.1f");
            if (changed) {
                if (ImGui::IsItemActivated()) push_undo(ctx, ws);
                mark_dirty(ws);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Active zone's WorldZ (depth layer)");
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!has_zone);
        if (ImGui::Button("Convex Hull")) {
            rbm::Zone* z = active_zone(ctx.rubymesh, ws);
            if (z && z->vertices.size() >= 3) {
                push_undo(ctx, ws);
                z->vertices = convex_hull2(z->vertices);
                rbm::ensure_ccw(z->vertices);
                mark_dirty(ws);
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(ws.undo_stack.empty());
        if (ImGui::Button(ICON_FA_ARROW_ROTATE_LEFT " Undo")) {
            ws.redo_stack.push_back(ctx.rubymesh.zones);
            ctx.rubymesh.zones = std::move(ws.undo_stack.back());
            ws.undo_stack.pop_back();
            mark_dirty(ws);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(ws.redo_stack.empty());
        if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT " Redo")) {
            ws.undo_stack.push_back(ctx.rubymesh.zones);
            ctx.rubymesh.zones = std::move(ws.redo_stack.back());
            ws.redo_stack.pop_back();
            mark_dirty(ws);
        }
        ImGui::EndDisabled();

        if (!ws.notice.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.45f, 1.0f), "%s", ws.notice.c_str());
        }
    }
    ImGui::EndChild();

    // ── Close confirm modal ─────────────────────────────────────────────────
    bool close_now = false;
    if (close_requested) {
        if (ws.dirty) {
            ws.request_close = true;
        } else {
            close_now = true;
        }
    }
    if (ws.request_close) {
        ws.request_close = false;   // one-shot open: Esc or an outside click cancels
        ImGui::OpenPopup("Save changes?");
    }
    if (ImGui::BeginPopupModal("Save changes?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The .rbm zone file has unsaved changes.");
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.48f, 0.28f, 1.0f));
        if (ImGui::Button("Save & Close")) {
            save_now(ctx, ws);
            close_now = true;
            ws.request_close = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Discard & Close")) {
            ws.dirty = false;
            ws.request_close = false;
            close_now = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ws.request_close = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (close_now) {
        if (ws.fbo) { av::delete_fbo(ws.fbo, ws.fbo_tex); ws.fbo = 0; ws.fbo_tex = 0; }
        ws.open = false;
        ws.dirty = false;
        ws.insert_mode = false;
        ws.hovered_vertex = -1;
        ws.dragging_vertex = -1;
    }

    ImGui::End();
    return apply_requested;
}

} // namespace rbmed
