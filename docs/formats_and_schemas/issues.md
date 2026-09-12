Ran command: `python3 /home/quantumcreeper/.gemini/antigravity/brain/82598a4f-f6c1-4b5f-a4b8-60bb5739b977/scratch/check_bee_nodes.py`
Created test_demon.py
Ran command: `python3 /home/quantumcreeper/.gemini/antigravity/brain/82598a4f-f6c1-4b5f-a4b8-60bb5739b977/scratch/test_demon.py`
Searched for "glTexParameteri"
Viewed asset_viewer.cpp:1810-1860
Viewed asset_viewer.cpp:1910-1950
Searched for "GPUMesh"
Searched for "set_mesh_flip_uv"
Viewed av_renderer.cpp:1930-1960
Searched for "flip_uv_v"
Searched for "upload_mesh"
Viewed asset_viewer.cpp:2340-2365
Viewed asset_viewer.cpp:2320-2345
Viewed pod_convert.cpp:640-680
Viewed pod_convert.cpp:680-730
Viewed pod_convert.cpp:320-370
Created extract_tex.py
Ran command: `python3 /home/quantumcreeper/.gemini/antigravity/brain/82598a4f-f6c1-4b5f-a4b8-60bb5739b977/scratch/extract_tex.py`
Created inspect_demon_mesh.py
Ran command: `python3 /home/quantumcreeper/.gemini/antigravity/brain/82598a4f-f6c1-4b5f-a4b8-60bb5739b977/scratch/inspect_demon_mesh.py`
Created inspect_matrices.py
Ran command: `python3 /home/quantumcreeper/.gemini/antigravity/brain/82598a4f-f6c1-4b5f-a4b8-60bb5739b977/scratch/inspect_matrices.py`
Searched for "alpha_mode"
Viewed av_renderer.cpp:2435-2500
Searched for "camera.far_plane"

# Forensic Binary & Architectural Report: GLB Rendering, UV Corruption, and Camera Glitches

> **RESEARCH STAGE ONLY.**  
> Grounded in primary source binary inspection of the actual `.glb` assets (`minecraft_bee.glb`, `mc_demon.glb`, `skeleton_mob.glb`), runtime shader analysis in `av_renderer.cpp`, import pipeline in `gltf_import.cpp`, conversion pipeline in `pod_convert.cpp`, and viewport camera mechanics in `asset_viewer.cpp`.

---

## Executive Summary & Root Cause Matrix

| Observed Glitch | Affected Models | Primary Culprit | Location in Code |
|---|---|---|---|
| **Mesh Exploded / Fully Glitched** | `minecraft_bee.glb` | **Missing Inverse Bind Matrices (IBM)** & glTF skin evaluation mismatch. Node 13 scale ($100\times$) + unbaked rest-space vertex offsets explode in `skin_mesh`. | `gltf_import.cpp:394–417`, `pod_loader.cpp:1240–1270` |
| **Camera Jump & Stuck Inside Model** | Big models (and `minecraft_bee.glb`) | **Hardcoded 500-unit Zoom Ceiling** (`distance > 500.0f`) + Fixed `far_plane = 1000.0f` clipping planes. | `asset_viewer.cpp:3396–3397`, `av_renderer.h:31–32` |
| **Textures "Blurry as Hell"** | `mc_demon.glb`, pixel-art GLBs | **Ignored glTF Texture Sampler**: Trilinear mipmapping (`GL_LINEAR_MIPMAP_LINEAR`) + 4x anisotropy forced onto $128\times 128$ pixel art. | `asset_viewer.cpp:1823–1827` |
| **Texture UV "Corrupted" / Inverted** | `mc_demon.glb`, POD converted GLBs | **Double V-Inversion Hazard** + **Alpha-Blend Depth Sorting Collision** (`alphaMode = BLEND` with depth write on). | `asset_viewer.cpp:1816`, `av_renderer.cpp:2440–2473`, `pod_convert.cpp:320, 657` |
| **Why `skeleton_mob.glb` Works** | `skeleton_mob.glb` | **Zero Textures + Zero Skinning**: Pure static geometry. Has no texture sampler or skinning tracks to trigger the bugs. | Direct Binary Inspection |

---

## 1. Deep Binary Analysis of the Assets

### A. `minecraft_bee.glb` (Exploded / Glitched)
- **Binary Chunks**: Total size `1,048,576` bytes. JSON chunk `42,912` bytes, BIN chunk `1,005,640` bytes.
- **Node Hierarchy (50 Nodes)**:
  - Node 13 (`Armature`): `translation = [0.0, -110.0, 0.0]`, `scale = [100.0, 100.0, 100.0]`.
  - Node 14 (`Object_14`): Child of Node 13; holds all 9 meshes and bone roots.
  - Nodes 16, 18, 20, 22, 24, 26, 28, 30, 32: Contain $4\times 4$ transformation matrices carrying local offsets.
  - Nodes 17, 19, 21, 23, 25, 27, 29, 31, 33: The 9 skinned mesh instances, each bound to `skin: 0`.
- **The Disconnect in Raw Vertex Positions**:
  - Mesh 0 (`Body_Bee_0`): 24 vertices, positioned around origin: $\approx [-1.0, 1.0]$.
  - Mesh 1 (`WingR_Bee_0`): 4 vertices, span $X \approx 39.0, Z \approx 62.0$.
  - Mesh 3 (`AntennaR_Bee_0`): 4 vertices, positioned at $X \approx -230.0, Y \approx -73.5$.
  - Mesh 8 (`Stinger_Stinger_0`): 4 vertices, positioned at $X \approx 553.0, Y \approx 59.0$.
- **Skin & IBM Data**:
  - `Skin 0` contains 17 joints (`[15, 34, 35, 36, 37, ... 49]`).
  - Accessor 36 contains **17 Inverse Bind Matrices (IBMs)**.
  - The authored IBMs contain scale factors of $0.01$ ($1/100$) and explicit inverse offsets designed to bring the vertices from their arbitrary modeled bind positions into joint space.

### B. `mc_demon.glb` (UV Corrupted & Blurry)
- **Binary Chunks**: Total size `45,716` bytes. JSON chunk `2,120` bytes, BIN chunk `43,568` bytes.
- **Meshes & Nodes**: 1 Mesh (`Object_0`), 1016 vertices, 516 triangles. 0 skins, 0 animations.
- **Texture & Sampler**:
  - Embedded image: $128 \times 128$ RGBA PNG (Minecraft pixel-art skin).
  - Texture 0 Sampler explicitly specifies:
    - `magFilter: 9728 (GL_NEAREST)` $\implies$ **Crisp pixel-art rendering required**.
    - `minFilter: 9986 (GL_NEAREST_MIPMAP_LINEAR)`.
- **Material**:
  - `alphaMode: "BLEND"`, `doubleSided: true`.
- **Root Transforms**:
  - Node 0 (`Sketchfab_model`): Rotation matrix: $[1, 0, 0, 0, \ 0, 0, -1, 0, \ 0, 1, 0, 0, \ 0, 0, 0, 1]$ ($-90^\circ$ rotation around $X$, converting $Z$-up OBJ to $Y$-up glTF).

### C. `skeleton_mob.glb` (Loads & Converts Cleanly)
- **Binary Chunks**: Total size `114,040` bytes.
- **Meshes & Nodes**: 1 Mesh, 2658 vertices.
- **Texture**: **None** (`baseColorTexture` is null; material is plain Lambert grey/white).
- **Skinning**: **None** (`skins` array is empty).
- **Nodes**: Node 0 has $R_x(-90^\circ)$, Node 1 has $R_x(+90^\circ)$. They cancel out ($R_x(-90) \cdot R_x(+90) = I$).

---

## 2. Why `minecraft_bee.glb` is Fully Glitched in the GLB Renderer

### Root Cause 1: `gltf_import.cpp` Discards `inverseBindMatrices`
In the glTF 2.0 specification (§3.7.2.1), the world-space position of a skinned vertex is computed as:
$$v_{\text{world}} = \sum_{j} w_j \cdot M_{\text{joint}_j} \cdot \text{IBM}_j \cdot v_{\text{bind}}$$
Where $\text{IBM}_j$ is the $j$-th matrix in `skin.inverseBindMatrices`.

**What Ruby actually does**:
1. `gltf_import.cpp` (lines 393–417) parses `skin->joints`, but **completely ignores `skin->inverse_bind_matrices`**. It never reads accessor 36, and leaves `node.has_bind_matrix = false`.
2. When the renderer runs (`asset_viewer.cpp:3151`), it sees `bones_per_vertex > 0` and calls `av::skin_mesh`.
3. In `pod_loader.cpp` (lines 1240–1248):
   ```cpp
   for (size_t i = 0; i < model.nodes.size(); ++i) {
       if (model.nodes[i].has_bind_matrix) {
           std::memcpy(&bind_world[i * 16], model.nodes[i].bind_matrix, 16 * sizeof(float));
       } else {
           get_node_matrix(model, static_cast<int>(i), 0.0f, &bind_world[i * 16]);
       }
   }
   ```
   Because `has_bind_matrix` is false, it computes `bind_world` by evaluating the node transforms at frame 0.
4. But in `minecraft_bee.glb`, Node 13 (`Armature`) has `scale = [100.0, 100.0, 100.0]` and `translation = [0, -110, 0]`.
5. The raw vertices in the file are already at large arbitrary offsets (Stinger is at $X = 553$, Antennae at $X = -230$). In standard glTF, the authored IBMs contain $1/100$ scaling and translation offsets to cancel this out.
6. Because the real IBMs were discarded, `skin_mesh` applies an uncompensated, inverted transform. The antennae, legs, stinger, and wings explode across the screen into giant stretched polys.

### Root Cause 2: glTF Animation Tracks are Not Bound to PODModel in `gltf_import_glb`
In `gltf_import.cpp` (lines 529–530):
```cpp
out.num_frames = 0;
out.fps = 0.0f;
```
The animation importer (`gltf_import_all_clips`) exists as a separate utility function, but `gltf_import_glb` hardcodes `num_frames = 0`. As a result, the model is evaluated at frame 0 with static invalid matrices, while the CPU skinning loop continues to run every frame.

---

## 3. Why `mc_demon.glb` UV is Corrupted and "Blurry as Hell"

### Root Cause 1: Ignored Texture Sampler ("Blurry as Hell")
- `mc_demon.glb`'s texture is a **$128 \times 128$ pixel art Minecraft skin**.
- The glTF specifies `GL_NEAREST` filtering.
- In `asset_viewer.cpp` (lines 1823–1827), the texture loading function ignores the glTF sampler entirely:
  ```cpp
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 4.0f);
  ```
  Trilinear filtering (`GL_LINEAR_MIPMAP_LINEAR`) with 4x anisotropy blurs $128 \times 128$ crisp pixel art into muddy sludge.

### Root Cause 2: Double-Flipping & Inconsistent Coordinate Conventions ("Corrupted UV")
1. In `asset_viewer.cpp` line 1816:
   ```cpp
   flip_surface_vertical(conv); // FLIPS IMAGE PIXELS VERTICALLY IN MEMORY
   ```
2. In `asset_viewer.cpp` line 2333:
   ```cpp
   const bool dcc_uv = (ext == ".glb") || (ext == ".gltf") || ...;
   ```
   This passes `flip_uv_v = true` to the shader.
3. In `av_renderer.cpp` (lines 2377, 2456), the shader does:
   ```glsl
   v_texcoord = vec2(a_texcoord.x, 1.0 - a_texcoord.y);
   ```
4. **The conflict**: The image pixel rows were *already inverted* during upload (`flip_surface_vertical`), and the vertex shader *inverts $V$ again* ($1.0 - V$).
5. Furthermore, in `pod_convert.cpp`:
   - `encode_texture` (lines 320–321) flips the pixel buffer vertically when creating `.pvr` / `.tex.png`.
   - If `--flip-v` is passed, lines 657–662 flip the UV array ($v' = 1.0 - v$) as well.
   - Flipping both the image buffer and the UV array cancels the flip, leaving the texture upside-down relative to the mesh.

### Root Cause 3: Alpha-Blend Depth Sorting Collision
- `mc_demon.glb` specifies `alphaMode: "BLEND"` and `doubleSided: true`.
- In `av_renderer.cpp` (lines 2440–2474):
  ```cpp
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  ...
  glUniform1f(s_pbr.alpha_cutoff, 0.0f); // alpha test disabled!
  ```
- Because standard depth writing (`glDepthMask(GL_TRUE)`) remains active, transparent geometry (horns, outer layer clothing, wings) writes to the depth buffer before the geometry behind it is drawn. The back faces and interior surfaces are depth-culled. This causes noisy, missing polygons and black geometric silhouettes that look like corrupted UV mapping.

---

## 4. Why Big Models Break the Camera (Zooming Glitchy & Stuck)

### Root Cause 1: Hardcoded 500-Unit Zoom Clamp in 3D Viewport
In `asset_viewer.cpp` (lines 3393–3398):
```cpp
if (io.MouseWheel != 0.0f) {
    const float factor = std::pow(0.94f, io.MouseWheel * st.cam_zoom_speed);
    st.camera.distance *= factor;
    if (st.camera.distance < 0.1f) st.camera.distance = 0.1f;
    if (st.camera.distance > 500.0f) st.camera.distance = 500.0f; // <--- FATAL FLAW
}
```

### The Exact Failure Sequence on Large Assets:
1. **Model Loading**:
   When a large model is opened (e.g. `minecraft_bee.glb` with radius $514.32$, or a large level terrain with radius $800$–$2000$), `asset_viewer.cpp` (line 2363) correctly computes:
   $$\text{distance} = \text{radius} \times 2.5 = 514.32 \times 2.5 = 1285.80$$
2. **The Mouse Wheel Snapping Bug**:
   The moment the user scrolls the mouse wheel by even a single notch, line 3397 executes:
   ```cpp
   if (st.camera.distance > 500.0f) st.camera.distance = 500.0f;
   ```
   The camera distance **instantly collapses from $1285.8$ down to $500.0$**!
3. **Stuck Inside the Mesh**:
   Because the model radius is $514$, a camera distance of $500$ places the camera *inside the geometry*.
4. **Impossible to Zoom Out**:
   Any backward scrolling is immediately capped at $500.0f$. The user can zoom *in*, but can **never zoom out again**.

### Root Cause 2: Fixed Far Clipping Plane (`far_plane = 1000.0f`)
In `av_renderer.h` (line 32):
```cpp
float far_plane = 1000.0f;
```
In the 3D model viewport, `st.camera.far_plane` is **never updated based on model bounds**.
- On `minecraft_bee.glb`, the camera starts at distance $1285.80$. Because the far clipping plane is $1000.0$, the entire model lies beyond the far plane and is completely clipped or sliced in half.
- In contrast, the Scene Editor (`asset_viewer.cpp:5303`) dynamically updates:
  ```cpp
  st.camera.far_plane = std::max(1000.0f, st.camera.distance + radius * 8.0f);
  ```
  This dynamic adjustment was omitted in the 3D model previewer.

---

## 5. Architectural Verification: Why `skeleton_mob.glb` Succeeds

`skeleton_mob.glb` loads cleanly and converts properly because its binary features bypass every buggy code path:
1. **No Skinning**: `skins` count is $0$. `skin_mesh` is never called, so the missing `inverseBindMatrices` bug is never triggered.
2. **No Textures**: `baseColorTexture` is null. `load_texture_file` is never invoked, so the trilinear blurring bug and the vertical UV double-flip bug never occur.
3. **Small Dimensions**: Its radius is $20.32$ units ($\text{span } X = 29.84, Y = 26.95$). The default framing distance is:
   $$\text{distance} = 20.32 \times 2.5 = 50.79 \text{ units}$$
   $50.79$ is well below the $500.0$ zoom clamp and far below the $1000.0$ far plane. The camera orbits and zooms smoothly without clipping.

---

## 6. Required Architectural Fixes (Reference for Future Implementation)

### 1. Fix Camera Zoom & Dynamic Clipping (`asset_viewer.cpp`)
- Replace the hardcoded `500.0f` clamp with a dynamic ceiling based on model radius:
  ```cpp
  const float max_dist = std::max(2000.0f, st.model.radius * 20.0f);
  const float min_dist = std::max(0.01f, st.model.radius * 0.01f);
  st.camera.distance = std::clamp(st.camera.distance * factor, min_dist, max_dist);
  st.camera.near_plane = std::max(0.01f, st.camera.distance / 10000.0f);
  st.camera.far_plane  = std::max(2000.0f, st.camera.distance + st.model.radius * 10.0f);
  ```

### 2. Preserve `inverseBindMatrices` in `gltf_import.cpp`
- Read `skin->inverse_bind_matrices` accessor into `PODNode.bind_matrix` (or an explicit `IBM` array on `PODMesh`).
- Use the actual glTF IBMs in `skin_mesh` instead of attempting to compute bind pose from unbaked node hierarchies.

### 3. Respect glTF Texture Sampler Filtering (`asset_viewer.cpp`)
- Query the glTF sampler `magFilter` / `minFilter`:
  - If `magFilter == GL_NEAREST`, set `GL_NEAREST` on the texture instead of forcing `GL_LINEAR_MIPMAP_LINEAR`.
  - For pixel-art models ($128 \times 128$ or smaller), disable trilinear mipmap filtering to keep edges sharp.

### 4. Resolve UV Flipping and Alpha Discard
- Enforce single-point UV handling: if the texture pixel buffer is flipped during upload/export, do not invert UV coordinates in the shader or CLI.
- For materials with `alphaMode == "BLEND"` or `"MASK"`, enable `alpha_cutoff` (e.g. `0.5f`) in the shader to discard transparent fragments before depth-buffer writes.