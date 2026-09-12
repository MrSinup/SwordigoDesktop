#pragma once
// rubymesh_editor.h — RubyMesh Workspace: a full-screen 3D editor for manually
// authoring collision zones (.rbm) on a POD model.
//
// All ImGui + OpenGL drawing lives here (and in rubymesh_editor.cpp); all
// pure data / file logic lives in rubymesh.h/.cpp. draw_workspace() is called
// unconditionally every frame while the workspace is open; it returns true on
// the frame the user clicks "Apply to Scene" so the host can run the apply
// pipeline afterwards.

#include <cstdint>
#include <string>
#include <vector>

#include "tools/av_renderer.h"
#include "tools/pod_loader.h"
#include "tools/rubymesh.h"

namespace rbmed {

// Everything the workspace needs from the host (bound to the scene's model
// caches, see asset_viewer.cpp).
struct WorkspaceContext {
    av::PODModel&             model;      // loaded POD (authored pose)
    std::vector<av::GPUMesh>& gpu_meshes; // uploaded meshes, parallel to model.meshes
    std::vector<unsigned int>& textures;  // GL textures, parallel to model.texture_filenames
    rbm::RubyMesh&            rubymesh;
    std::string               scene_dir;
    std::string               rbm_path;
    av::Camera&               camera;

    // Extensions over the base spec (documented in implementation_plan.md):
    float seed_world_z = 40.0f;   // source object's Depth — seeds new zones (D3)
    float source_z_shift = 0.0f;  // ghost render Z offset = source object depth (D4)
};

// Per-session UI state (owned by the host ViewerState, one per app).
struct WorkspaceState {
    bool  open           = false;
    int   active_zone    = -1;
    int   hovered_vertex = -1;
    int   dragging_vertex = -1;
    bool  insert_mode    = false;   // [A] key / "+ Add Vertex" button
    bool  show_pod       = true;
    bool  show_all_zones = true;
    bool  dirty          = false;
    float autosave_timer = 0.0f;    // countdown; saves at 0 when dirty
    float save_badge_timer = 0.0f;  // "Saved" badge visible while > 0
    std::string notice;             // transient status line (bottom bar)
    float       notice_timer = 0.0f;

    // Undo/redo: full snapshots of rbm::RubyMesh::zones.
    std::vector<std::vector<rbm::Zone>> undo_stack;
    std::vector<std::vector<rbm::Zone>> redo_stack;

    // Internal (FBO + close-confirm) — not part of the public API contract.
    unsigned int fbo = 0;
    unsigned int fbo_tex = 0;
    int          fbo_w = 0, fbo_h = 0;
    bool         request_close = false;  // [← Back] clicked: show the Save? modal
};

// Draw the full-screen workspace. Call unconditionally every frame while
// ws.open is true. Returns true on the frame the user clicks "Apply to Scene".
bool draw_workspace(WorkspaceContext& ctx, WorkspaceState& ws,
                    int screen_w, int screen_h);

} // namespace rbmed
