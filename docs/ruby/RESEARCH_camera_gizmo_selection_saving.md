# Ruby GG Research Report — Camera, Gizmo, Selection Highlight & Scene Save Integrity

**Scope:** `src/ruby` (Qt "Ruby GG" studio) — compared against `src/tools/asset_viewer.cpp`
("Ruby Classics ImGui edition", the camera the user loves) and the real
[ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) v1.92.5 sources
(`src/ImGuizmo.cpp`, fetched and studied). No ImGui is introduced anywhere;
we only borrow *behavior*.

---

## 1. Root causes found (with file:line evidence)

### 1.1 Save → viewport reload → "my edits disappear"

The save path (`RubyMainWindow::save_scene_doc_structured`,
`src/ruby/editor/ruby_main_window.cpp:1362`) **always** re-parses the file it
just wrote and feeds the parsed copy back into the viewport via
`apply_scene_data()`. Three ways to lose edits:

| # | Mechanism | Where |
|---|-----------|-------|
| 1 | `sync_scene_text_buffer(path, from_disk=false)` serializes the **live RAM scene** into `m_script_buffers` **before** the disk bytes exist; if the save then throws, the "post-save sync" state markers are still cleared by later paths → the buffer claims to be clean while the file holds stale bytes. The next save is then a no-op ("disk already matches") and the user's last edits never land. | `sync_scene_text_buffer`, `onSaveFile` |
| 2 | `scene_structure_matches` bails on *any* structural difference → fallback `evict_scene_cache` + `load_scene` + `set_camera_state`. The evict clears the RAM scene, so any gizmo edit that raced the save (e.g. inspector field committed between serialize and reload) is wiped by the fresh disk parse. | `viewport_3d_widget.cpp:2068-2160`, `save_scene_doc_structured` |
| 3 | `apply_scene_data` re-reads bounds from the parsed scene but **restores the camera and keeps `m_render_objects` patching by index** — correct — *but* it does not re-verify that the file bytes are parseable. A truncated write (power loss mid-save) leaves disk stale; viewport still shows the edited RAM scene, user believes the save worked. | `scene_save` has no post-write verification |

**Fix (implemented):** the RAM scene *is* the state that was written —
`av::scene_save` is a verbatim serializer of it (proven by
`tests/scene_smart_save_test.cpp`). So:

1. `apply_scene_data` in place, **no disk re-parse, no evict** on structured save.
2. Post-save **validation**: re-parse to a throwaway `SceneData`, verify
   `scene_structure_matches(ram, parsed)` and transform equality; on mismatch
   → loud console/status error, keep RAM authoritative, refuse to evict.
3. Structured save only marks text "synced" **after** the validation passed.
4. `save_scene_doc_structured` refuses to save when the viewport is not
   showing that scene *and* there are unsaved structured edits — it used to
   silently load the target from disk, blowing away edits made in another tab.

### 1.2 Two open scenes share state → "maximum corruption" on tab switch

Three shared/borrowed layers (`viewport_3d_widget.cpp:24-30`):

```cpp
static std::unordered_map<std::string, QImage>     s_ram_image_cache;
static std::unordered_map<std::string, av::PODModel> s_ram_pod_cache;
static std::unordered_map<std::string, av::SceneData> s_ram_scene_cache;  // ← the "game state" collision
```

* `s_ram_scene_cache` is **process-wide and keyed only by path**. `sync_edited_object_caches()` writes gizmo edits into it for the *non-active* scene too; two tabs → each write lands in the shared entry → edits bleed across scenes and get resurrected after evict.
* Global `ProjectContext::selectionChanged` carries a **bare object index** with no scene identity — switching tabs applies scene A's selected index to scene B (`ruby_main_window.cpp:504-516`).
* `activate_document` → `load_scene_async` fires a detached parse thread; if the user switches back to scene A **before** B's thread lands, B's completion lambda is dropped by the seq guard — good — but the *async path also leaves `m_scene_load_seq` shared*, so a scene-text preview (`load_scene_async(path, &binary)`) can cancel a legit load and the overlay stays stuck.
* `sync_scene_text_buffer(from_disk=true)` reads the file with `QFile` and trusts the read length; `load_scene`'s banner sniff reads only 64 bytes and assumes the rest is binary.

**Fix (implemented):**
- All three global caches moved **into the widget as per-document session state**
  (`SceneSession` keyed by canonical path, holding scene, models, textures, GPU buffers,
  camera, selection, gizmo mode, dirty flag). One session per document; sessions never merge.
- New `SceneKey` (canonical path) used everywhere; `ProjectContext::set_selected_object`
  got a scene-path overload, and the window routes selection only when the
  selected scene is the active document.
- Async loads carry their own token; a load for scene A can never be cancelled
  by activating scene B and vice versa (token = path + seq, checked on landing).
- Every file read goes through `read_file_guarded()` (exact-size, open-error reported,
  FileRift-text sniff on the *whole* head, not just 64 bytes) and every structured
  write through `scene_save` (already atomic: temp + rename, verified by test).

### 1.3 The selection highlight is a 2D rectangle over the object

`draw_scene()` (`viewport_3d_widget.cpp:1188-1250`) draws an axis-aligned
**bounding box** (`parse_local_aabb` → 3D box wireframe + violet glow + halo ring
around the *origin*). The user wants the **mesh itself** highlighted — edges and
silhouette — like a proper editor.

**Fix (implemented):** a dedicated selection pass
`draw_selection_highlight()`:

1. **Mesh silhouette pass** — for the selected object's POD meshes, draw every
   triangle edge with `glPolygonOffset`-free `GL_LINES` overlay in electric blue,
   reusing the exact node matrices used by `draw_pod_instance` (so the highlight
   matches the render transform 1:1, including baked rotation/scaling and skin).
2. **Rim/stencil outline** — second pass with front-face culling inverted and a
   slightly scaled model matrix (the classic inverted-hull outline) in violet,
   additive blend, so the outline is visible *around* the mesh, not over it.
3. Non-visual objects (spawn/portal/trigger) keep their existing octahedron/zone
   visuals, but the old violet box wireframe is replaced by the object's actual
   zone-volume edges (already parsed from `local_aabb`) — no more rectangle around
   a mesh object.
4. Ground-mesh objects get their real triangle edges highlighted.
5. Depth-tested so occluded parts dim naturally; the outline pass writes no depth
   (`glDepthMask(GL_FALSE)`), so it never z-fights the fill.

### 1.4 Camera feels worse than asset_viewer.cpp

asset_viewer's feel (measured at `asset_viewer.cpp:3490-3510, 12230-12360`):
- orbit sensitivity scaled by **`cam_orbit_speed` (user-adjustable, default 1.0) × 0.5°/px**, pitch clamped ±89°,
- wheel zoom is **exponential** `pow(0.94, wheel × cam_zoom_speed)` with min-distance clamp tied to the near plane,
- zoom **converges on the selected object** (0.12·|wheel| lerp of target toward selection) or spawn point — "zoom heads toward the playable area",
- RMB pan uses **camera-local Right/Up** vectors (yaw + pitch aware) scaled by `distance × 0.003 × cam_pan_speed`,
- optional turntable (`R`) and reframe (`F`).

Ruby GG's Qt viewport today: linear 0.4°/px orbit, pan missing the Up-vector's pitch term is present but the **pan speed is not clamped** and zoom does not converge on selection; wheel zoom math re-derives basis vectors inline (duplicate of paintGL, divergence risk); no inertia; no focus-converge.

**Fix (implemented)** in `Viewport3DWidget`:
- `update_camera_dynamics()` per-frame: velocity-damped orbit/pan inertia (exp decay 8/s), exponential zoom, pitch clamp ±89°.
- Wheel: `pow(0.94, wheel)` zoom with near-plane-tied min distance, target converges on the **selected object** (0.12·|wheel| lerp), else spawn point, else cursor focal point — exactly asset_viewer's policy.
- Pan: camera-local Right/Up (incl. pitch term), speed `distance × 0.003 × pan_speed`, clamped against drifting to infinity.
- `focus_object()` keeps the *current distance* (old code forced 50–300), with the animation bar unchanged.
- Continuous `update()` while inertia is active so the orbit glides like the ImGui one.

### 1.5 Gizmo is weaker than ImGuizmo — what ImGuizmo actually does

From the real `ImGuizmo.cpp` (v1.92.5):

1. **Axis flip** (`ComputeTripodAxisAndVisibility`): each axis is drawn toward the
   camera side by comparing clip-space lengths `lenDir` vs `lenDirMinus`
   (`dirAxis *= (lenDir < lenDirMinus) ? -1 : 1`), recomputed **per frame** but
   **frozen during a drag** (`TripodState` / `mActiveTripodState`) so the gizmo
   never flips mid-drag.
2. **Axis-plane drag planes** (`TRANSLATE_PLANS`): dragging axis X uses the plane
   spanned by the *other two* axes (`Y|Z`), not a camera-facing plane — this is
   the main source of the "drifting sideways" feel in our current gizmo, which
   builds a camera-orthogonal plane instead.
3. **Rotation angle** (`ComputeAngleOnPlan`): angle = acos(dot(localPos, source))
   signed by the perpendicular cross — same as ours, but computed against the
   **frozen** drag plane; ours recomputes the plane from live rotation, so a
   Y-rotation changes the ring plane mid-drag and the angle jumps.
4. **Rotation rings are drawn as two half-circles** with the *back half dimmed*
   (front-facing only), so you always see which half is hot.
5. **Scale**: ImGuizmo scales along the axis in model space using the drag-plane
   intersection ratio; uniform scale via center (`MT_SCALE_XYZ`).
6. **`screenRotateSize = 0.06`** — the screen rotate ring radius is tied to
   viewport height, not world units.

**Fix (implemented)** in `ruby_gizmo.cpp`:
- Axis translate drag now uses the **world-axis plane spanned by the other two
  axes** (ImGuizmo `TRANSLATE_PLANS`), falling back to the camera plane when
  edge-on. No more lateral drift.
- Rotation drag **freezes the plane and the reference vector at grab time**
  (they already were; the bug was `pos` drift — the plane is now built at
  drag start and reused, matching `mTranslationPlan`).
- Per-axis world directions for translate/scale (engine rotation applied only
  for rotate, like ImGuizmo WORLD mode).
- Back-half ring dimming for rotation rings.
- Clamp: scale ratio floor 0.01, translate never NaNs on degenerate planes.

### 1.6 Scene loading must validate before the engine gets it

The user asks: "re-encode the selected scene from the last save, check it's
encoded and valid via roundtrip, and only if roundtrip succeeds point the scene
load command to Swordfare engine preview — don't give the game a corrupted
scene; show error."

`save_scene_doc_for_run()` (`ruby_main_window.cpp:922`) already re-encodes dirty
FileRift text, but it **does not validate** the result before `send_scene_shift`.

**Fix (implemented):** `validate_scene_on_disk()`:
1. Read the file fully (guarded).
2. If it starts with a FileRift banner → reject (binary expected).
3. `av::scene_load_bytes` parse → must be non-empty.
4. `filerift::decode_protobuf(serialize(parse))` round trip → must be non-empty.
5. Only then `pod->send_scene_shift(...)`. On failure: status + console error,
   no shift, game never sees bad bytes.

---

## 2. Implementation map

| Change | File |
|---|---|
| Per-document `SceneSession` (no shared scene state) | `src/ruby/viewport/viewport_3d_widget.{h,cpp}` |
| Guarded file read/encode helpers | `src/ruby/viewport/viewport_3d_widget.cpp`, `src/ruby/editor/ruby_main_window.cpp` |
| Mesh-accurate selection highlight | `draw_selection_highlight()` in `viewport_3d_widget.cpp` |
| Inertial camera + selection-converge zoom | `viewport_3d_widget.cpp` mouse/paint paths |
| ImGuizmo-grade gizmo drag math | `src/ruby/viewport/ruby_gizmo.cpp` |
| No-reload smart save + post-save validation | `ruby_main_window.cpp::save_scene_doc_structured` |
| Play-guard (validate → then shift) | `ruby_main_window.cpp::on_engine_run_scene` |
| Selection-scoped context | `src/ruby/core/project_context.{h}` |

Behavior invariants kept: camera preserved across tab switches and saves
(`apply_scene_data` in-place path), zero-flash tab restore, atomic writes,
`scene_save` byte-exactness (test-guarded).

## 3. Test plan

- `scene_smart_save_test` (existing) still passes — byte-exact round trip.
- New `scene_play_guard_test`: corrupt/valid .scene → `validate_scene_on_disk`
  accepts/rejects (wired as a small static-lib-able helper so it is testable
  headless).
- Manual: two scenes in tabs — gizmo-drag in A, switch to B, back to A: edit
  intact, camera intact, no cross bleed. Save in A: no reload flash, camera kept.
