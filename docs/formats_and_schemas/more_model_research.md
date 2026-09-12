Searched for files: *pod*
Searched for files: *pod*
Viewed pod_3d_model_format_spec.md:1-58
Listed directory tools
Listed directory OpenSwordigo
Searched for files: *pod*
Viewed pod_loader.h:1-74
Searched for "POD"
Searched for "ReadPOD"
Viewed swordigo_symbols_demangled.txt:4400-4450
Searched for files: *
Listed directory functions
Searched for files: *
Viewed 00000000004E3CC8__ReadPODModelFromFile.c:1-49
Viewed 00000000004E5834__MeshValueTypeFromPODType.c:1-28
Searched for "MeshValueTypeFromPODType"
Searched for "ReadPODModelFromFile"
Searched for "4E3CC8"
Searched for "CPVRTModelPOD"
Viewed libswordigo_ida.c:469530-470000
Viewed libswordigo_ida.c:470001-470600
Viewed libswordigo_ida.c:470601-471020
Searched for "CPVRTModelPOD::GetWorldMatrix("
Viewed libswordigo_ida.c:631270-631370
Viewed libswordigo_ida.c:631180-631270
Searched for "CPVRTModelPOD::GetWorldMatrixNoCache(PVRTMATRIXf"
Viewed libswordigo_ida.c:630785-630950
Searched for "PVRTMatrixMultiplyF("
Viewed libswordigo_ida.c:637730-637780
Searched for files: *PVRT*
Searched for files: *PVRT*
Listed directory PowerVR_Tools
Searched for files: *PVRTModelPOD*
Searched for files: *
Searched for files: *
Viewed caver_mesh.h:1-33
Viewed caver_renderer.h:1-52
Viewed caver_object.h:1-49
Searched for "SceneObject"
Searched for "ModelComponent"
Searched for "0000000000224688"
Searched for "ModelComponent::LoadFromProtobufMessage"
Viewed libswordigo_ida.c:18195-18350
Searched for "ModelComponent::Render"
Searched for "Caver::ModelComponent::"
Viewed libswordigo_ida.c:18920-19200
Searched for "Caver::SceneObject::WorldMatrix"
Viewed libswordigo_ida.c:500983-501060
Searched for "Caver::Camera::"
Viewed libswordigo_ida.c:477100-477125
Searched for "259DC0"
Searched for files: *data*
Searched for files: *ForwardDirection*
Viewed 000000000036E210__ForwardDirection.c:1-58
Viewed 00000000001A85D8__ForwardDirection.c:1-28
Viewed 00000000002E3128__ForwardDirection.c:1-58
Viewed pod_loader.h:1-181
Viewed pod_convert.cpp:1-150
Viewed pod_convert.cpp:300-450
Viewed pod_convert.cpp:451-600
Viewed gltf_import.cpp:1-160
Viewed gltf_import.cpp:161-350
Viewed gltf_import.cpp:351-460
Viewed pod_loader.cpp:1-150
Searched for "get_node_matrix"
Viewed pod_loader.cpp:1050-1160
Viewed pod_loader.cpp:990-1050
Viewed pod_loader.cpp:950-995
Searched for "Caver::Matrix4::FromRotationQuaternion"
Viewed libswordigo_ida.c:552760-552830
Searched for "Caver::ModelInstance::"
Viewed libswordigo_ida.c:467745-467840
Searched for "Caver::MeshInstance::SkinWithBoneMatrices"
Viewed libswordigo_ida.c:465940-466080
Searched for "C_Matrix4Vector3ArraySkin"
Viewed libswordigo_ida.c:552988-553100
Searched for "ArraySkin"
Searched for "SkinWithBoneMatrices"
Searched for "Caver::Mesh::Mesh("
Viewed libswordigo_ida.c:139031-139080
Searched for "struct SPODMesh"
Searched for "CPODData"
Listed directory PVRGeoPOD
Listed directory Documentation
Listed directory topics
Viewed pvrgeopod_using_POD_files.html:1-157
Viewed pvrgeopod_export_conversion.html:1-116
Viewed pvrgeopod_command_line.html:1-108
Viewed pvrgeopod_command_line_flags.html:1-200
Viewed pvrgeopod_command_line_flags.html:201-350
Viewed pvrgeopod_command_line_flags.html:351-450
Searched for files: *.pod
Searched for files: *.POD
Searched for files: *Models*
Searched for files: *Models*
Searched for files: *assets*
Listed directory assets
Listed directory models
Searched for files: *.pod
Searched for files: *.pod
Viewed pod_pipeline.md:1-42
Viewed pod-format.md:1-200
Searched for files: *hiro*
Searched for files: *.POD
Searched for files: *.scl
Viewed hiro.scl:2-101
Searched for files: *.proto
Viewed scene_schemas.h:1-23
Searched for files: *
Viewed CAVER_RE_REPORT.md:1-111
Searched for "2835"
Viewed scene_player.cpp:435-470
Viewed v3_research_notes.md:1-81
Searched for "hiro.POD"
Viewed BLENDERIFT_INTEGRATION.md:1-62
Listed directory blenderift
Viewed fullscriptmaybe.py:1-200
Viewed jPOD.py:1-120
Viewed temp.jpod:1-150
Viewed temp.jpod:151-300
Viewed temp.jpod:301-443
Viewed pod_writer.cpp:1-150
Viewed pod_writer.cpp:151-297
Searched for "eMeshBoneBatchIndexList"
Viewed pod_loader.cpp:420-465
Viewed pod_loader.cpp:1230-1320
Viewed fbx_import.cpp:1-160
Viewed gltf_export.cpp:1-160
Viewed gltf_export.cpp:230-300
Viewed gltf_import.cpp:230-290
Searched for "sBoneBatches"
Searched for "6015"
Searched for "CPVRTModelPOD::ReadMesh"
Searched for "ReadMesh"
Viewed libswordigo_ida.c:470430-470550
Viewed libswordigo_ida.c:470551-470700
Viewed libswordigo_ida.c:470701-470850
Viewed libswordigo_ida.c:470870-470920
Viewed libswordigo_ida.c:470921-470960
Viewed libswordigo_ida.c:470980-471015
Searched for "BlendAnimationComponent"
Searched for "Caver::AnimKeysNode"
Searched for "PODLoader::CreateAnimation"
Viewed libswordigo_ida.c:471170-471270
Viewed libswordigo_ida.c:471280-471350

# Comprehensive Forensic Research Report: GLTF/GLB/FBX ↔ Swordigo POD Compatibility & In-Game Correctness

> **RESEARCH ONLY — NO CODE OR ASSETS MODIFIED.**  
> Grounded in decompiled ARM64/ARM32 disassembly (`libswordigo_ida.c`), Caver engine source archaeology (`Caver::*`), Imagination Technologies PowerVR SDK / PVRGeoPOD v1.15 specifications, and Ruby's converter toolchain (`pod_loader`, `pod_writer`, `gltf_import`, `gltf_export`, `fbx_import`).

---

## 1. Coordinate-System Archaeology

### Handedness & Axis Conventions
* **OpenGL Right-Handed System**:
  * **$+X$**: Screen Right (horizontal lateral movement).
  * **$+Y$**: Up (jumping / gravity opposite).
  * **$+Z$**: Out of screen / Toward Camera (viewer depth).
  * **$-Z$**: Into screen / Forward (camera look direction).
* **Decompiled Proof**:
  * `Caver::Camera::ForwardDirection` (`OpenSwordigo/SwResurected/ida/functions/Caver/Camera/00000000002E3128__ForwardDirection.c`, `libswordigo_ida.c:477100`):
    The forward vector is explicitly initialized to:
    $$\vec{f} = (0.0,\; 0.0,\; -1.0,\; 0.0)$$
  * The game camera sits at fixed view offset $\mathbf{T}_{\text{cam}} = (0,\; 0,\; 2835.6)$ looking toward the origin along $-Z$.
* **glTF Compatibility**:
  * The glTF 2.0 specification natively dictates **Right-Handed, $+Y$ Up, $-Z$ Forward**.
  * **Crucial Finding**: **glTF and Swordigo POD share identical coordinate axes and handedness**. No coordinate basis swap ($X \leftrightarrow Y$ or winding flip) is required between glTF and Caver model space.
* **FBX Import Trap**:
  * FBX files can be exported from 3ds Max ($Z$-up, right-handed), Maya ($Y$-up, right-handed), or Blender ($Z$-up, right-handed). `ufbx` must be configured with `opts.target_axes = ufbx_axes_right_handed_y_up;` to automatically bake axis conversion into node matrices and geometry.

### Matrix Mathematics & Multiplication Order
* **Storage**: Column-major format ($4 \times 4$ floats, 16 contiguous elements).
* **Multiplication Logic** (`C_Matrix4Mul` at `libswordigo_ida.c:552814`, `PVRTMatrixMultiplyF` at `libswordigo_ida.c:637734`):
  $$C_{r, c} = \sum_{k=0}^{3} A_{r, k} \cdot B_{k, c} \quad \Longleftrightarrow \quad C = A \times B$$
  Column vectors are multiplied as $\mathbf{v}' = \mathbf{M} \cdot \mathbf{v}$.
* **Local Transform Composition** (`CPVRTModelPOD::GetWorldMatrixNoCache`, `libswordigo_ida.c:630785`):
  $$\mathbf{M}_{\text{local}} = \mathbf{T} \times \mathbf{R} \times \mathbf{S}$$
  $$\mathbf{M}_{\text{world}} = \mathbf{M}_{\text{parent}} \times \mathbf{M}_{\text{local}}$$
* **Quaternion Representation**:
  * Imagination Technologies PowerVR POD and `Caver::Matrix4::FromRotationQuaternion` (`libswordigo_ida.c:552764`):
    Stored as 4-float vectors $(x, y, z, w)$ with scalar $w$ as the 4th element (`q[3]`), identical to glTF quaternion convention.

---

## 2. Pivot, Origin, and Node Hierarchy Archaeology

### The `CenterPoint` Node
* In `Caver::PODLoader::CreateSkeleton` (`libswordigo_ida.c:469967-469972`):
  ```c
  if (!strcmp(node_name, "CenterPoint")) {
      v21 = *(float**)(node_ptr + 40); // Node translation vector
      this->origin.x = v21[0];
      this->origin.y = v21[1];
      this->origin.z = v21[2];
  }
  ```
* **Semantics**:
  * In original 3ds Max scenes, artists placed a dummy helper node named `"CenterPoint"`.
  * The engine extracts the local translation of `"CenterPoint"` as a model-wide offset.
  * In `Caver::ModelComponent::WorldMatrix` (`libswordigo_ida.c:19115-19166`), when computing the entity's render matrix:
    $$\mathbf{M}_{\text{render}} = \mathbf{M}_{\text{object}} \times \text{Translate}(-\mathbf{origin})$$
  * This shifts the model's pivot point so that `"CenterPoint"` becomes $(0, 0, 0)$. Without this, objects rotate and scale around arbitrary mesh origins (e.g. feet vs center-of-mass) rather than their intended in-game anchor.

### Node Ordering In POD
* `temp.jpod` and `CPVRTModelPOD` establish a strict rule:
  * Total nodes = `NumNode`.
  * The first `NumMeshNode` nodes ($0 \le i < \text{NumMeshNode}$) **must** correspond to drawable meshes (`node.nIdx >= 0`).
  * Subsequent nodes ($i \ge \text{NumMeshNode}$) are bones and transform helpers (`node.nIdx == -1`).
  * Node parent indices (`nIdxParent`) reference other nodes in the same array ($-1$ indicates a root node).

---

## 3. Mesh Representation Archaeology

### Vertex & Index Buffer Architecture
* **Index Data**:
  * Format: `eMeshVertexIndexList` (tag `6003`).
  * Primitive Types: Triangle Lists (`GL_TRIANGLES`, primitive type `0`) and Triangle Strips (`GL_TRIANGLE_STRIP`, primitive type `2`).
  * **In-Game Reality**: All stock Swordigo models use **triangle lists** (`nNumStrips == 0`, `ePrimType == 0`).
  * Index Type: `GL_UNSIGNED_SHORT` (16-bit). Every mesh is limited to **65,535 vertices**.
* **Vertex Layout Modes**:
  1. **Interleaved Mode (`eMeshInterleaved` tag 6006)**:
     * Used by complex models like `hiro.POD`.
     * 32-byte stride: Position (12B float3), Normal (12B float3), UV0 (8B float2).
     * Or 40-byte stride with skinning: Position (12B), Normal (12B), UV0 (8B), Bone Index (4B int), Bone Weight (4B float).
  2. **Non-Interleaved Mode (`eMeshVertexList` 6008, `eMeshNormalList` 6009, etc.)**:
     * Used by models like `bat.POD`, props, and environmental geometry.
     * When Caver loads a non-interleaved mesh (`Caver::PODLoader::CreateMesh`, `libswordigo_ida.c:470828-470850`), it dynamically repacks all separate streams into a single interleaved VBO padded to 4-byte boundaries.

---

## 4. Normal & Tangent Correctness

### Fixed-Function / GLES 1.1 Constraints
* Inspected `Caver::MeshInstance::Draw` (`libswordigo_ida.c:465987-466066`):
  * Active client state / VAO pointers:
    * `32884` (`GL_VERTEX_ARRAY`)
    * `32885` (`GL_NORMAL_ARRAY`)
    * `32888` (`GL_TEXTURE_COORD_ARRAY`)
    * `32886` (`GL_COLOR_ARRAY`)
* **Tangents & Binormals**:
  * `GL_TANGENT_ARRAY` does not exist in standard GLES 1.1 / Caver.
  * In `temp.jpod`, `nbTangentSpace = 0`, `nexportTangents = 0`.
  * **Caver ignores tangent and binormal streams completely**. Any tangents exported into POD consume memory but are never bound or shaded.
* **Normal Normalization**:
  * When skinning is evaluated (`C_Matrix4Vector3ArraySkin`, `libswordigo_ida.c:552991`), normals are transformed using the upper $3 \times 3$ rotation matrix with translation zeroed out.
  * The renderer relies on `GL_NORMALIZE` / `GL_RESCALE_NORMAL` or expects unit-length input normals. Exported normals must be strictly pre-normalized to length $1.0$.

---

## 5. UV Archaeology & Texture Inversion

### The Dual V-Flip Confusion
* **OpenGL vs Image File Origin**:
  * OpenGL texture space places $(0, 0)$ at the **bottom-left** ($V=0$ at bottom, $V=1$ at top).
  * Standard PNG/JPG/FBX image buffers store row 0 at the **top-left** ($V=0$ at top).
* **The Stock Pipeline**:
  * In `temp.jpod` (`bat.POD` metadata): `"nbFlipTextureV": 0`.
  * 3ds Max textures were flipped vertically during image compression into `.pvr` / `.tex.png` (using PVRTexTool), leaving UV coordinates unflipped.
* **The Ruby Bug**:
  * In `src/tools/pod_convert.cpp:328`, `encode_texture` vertically flips the PNG image (`flipped[y] = rgba[h - 1 - y]`).
  * Meanwhile, `pod_convert.cpp:419` has an optional `opts.flip_v` flag that inverts UV coordinates ($v' = 1.0 - v$).
  * If both occur simultaneously, textures appear inverted/mirrored in the game!
* **Subtexture Atlasing**:
  * In `Caver::PODLoader::CreateMesh` (`libswordigo_ida.c:470870-470892`):
    If a texture name matches an atlas subtexture, Caver calls:
    `Caver::Texture::ConvertSubtextureCoordinatesToParentTextureCoordinates(&v127, texture, uv)`
    which scales and offsets the mesh UVs to map into the atlas sheet at load time.

---

## 6. Skeletal System & Skinning Archaeology (Major Discovery)

### 1-Bone Rigid Influence Per Vertex
* **The Decompiled Truth**:
  * Inspecting `Caver::MeshInstance::SkinWithBoneMatrices` (`libswordigo_ida.c:465943`) and `C_Matrix4Vector3ArraySkin` (`libswordigo_ida.c:552991`):
  ```c
  void C_Matrix4Vector3ArraySkin(
      float *out_verts,        // a1
      const float *bone_mats,  // a2 (array of 16-float matrices)
      const float *in_verts,   // a3
      int vertex_count,        // a4
      const int *bone_indices, // a5
      int vertex_stride        // a6
  ) {
      for (int i = 0; i < vertex_count; ++i) {
          int bone = *bone_indices; // ONLY READS ONE INDEX!
          const float *m = &bone_mats[16 * bone];
          // Transform vertex directly by single matrix m:
          out_verts[0] = m[0]*in[0] + m[4]*in[1] + m[8]*in[2]  + m[12];
          out_verts[1] = m[1]*in[0] + m[5]*in[1] + m[9]*in[2]  + m[13];
          out_verts[2] = m[2]*in[0] + m[6]*in[1] + m[10]*in[2] + m[14];
          ...
      }
  }
  ```
* **Implications for Converters**:
  * **Swordigo does NOT support smooth 4-bone vertex blend skinning at runtime.**
  * Vertices are influenced **100% by a single bone** (`bone_indices[0]`).
  * While PowerVR POD files can store `eMeshBoneWeightList` (tag `6013`), Caver discards weights and only uses the primary bone index!
  * If a modern glTF model with smooth 4-bone weights is imported without quantization/rigid assignment, mesh seams and jagged vertex tearing will occur in-game unless vertices are snapped to the bone with the highest weight ($\max(w_i)$).

### Bone Naming & Identification
* In `Caver::PODLoader::CreateSkeleton` (`libswordigo_ida.c:469850-469970`):
  * A node is identified as a skeletal bone if and only if its name begins with `"Bone"`:
    $$\texttt{strncmp}(\text{node\_name},\; \text{"Bone"},\; 4) == 0$$
  * Inverse bind matrices are computed dynamically at load time by concatenating the parent hierarchy of each bone at rest pose and calling `Caver::Matrix4::Inverse`.
* **Bone Batches (`sBoneBatches`, tags 6015–6019)**:
  * If `sBoneBatches` is present (`libswordigo_ida.c:470895-471015`), Caver rewrites the vertex buffer's bone indices in-place using the batch table so that each vertex index directly addresses the skeleton's matrix array.

---

## 7. Animation Archaeology

### Keyframe Tracks & Framerate
* In `Caver::PODLoader::CreateAnimationFromFile` (`libswordigo_ida.c:471176-471350`):
  * Framerate is fixed:
    $$\text{FPS} = 24.0 \quad (\text{float constant } \mathtt{0x41C00000})$$
    $$\text{Duration} = \frac{\text{NumFrames} - 1}{24.0}$$
* **Bone Matching by Name**:
  * Lines 471284–471311 prove that animation POD files (e.g. `hiro_run.POD`) are linked to the base character model (`hiro.POD`) by matching **bone names**, not node indices:
    `skeleton->bone_map.find(anim_node->name)`
  * An animation POD can omit meshes and unused bones; as long as the bone names match the base skeleton, the animation tracks bind correctly.
* **Separation of Concerns**:
  * Base model contains: Mesh geometry, materials, rest-pose skeleton, `"CenterPoint"`.
  * Animation models contain: 24 FPS animation tracks (`anim_translation`, `anim_rotation` quaternion) for each `"Bone*"`.

---

## 8. Materials & Renderer Limitations

### Fixed-Function State Machine
* Swordigo runs a fixed-function OpenGL ES 1.1 pipeline:
  * Diffuse color (`eMaterialDiffuse`, tag `3004`).
  * Opacity / Alpha (`eMaterialOpacity`, tag `3002`).
  * Diffuse Texture (`eMaterialDiffuseTextureIndex`, tag `3001`).
* **Unsupported Modern Material Features**:
  * Normal maps / bump maps (`MatIdxTexBump`).
  * Metallic-roughness / PBR parameters (`MatMetallicity`, `MatRoughness`, tags 3028–3041).
  * Specular power / gloss maps (ignored by Caver mobile shaders).
* **Texture Requirements**:
  * Texture files referenced in POD (`TexName`) must exist in the assets directory without extension or matching `.pvr` / `.tex.png`.
  * Dimensions must be powers of two ($2^n \times 2^m$) up to $2048 \times 2048$ (typically $512 \times 512$ or $1024 \times 1024$).

---

## 9. Geometry Limits & Pathological Models

| Parameter | Limit / Requirement | Consequence of Violation |
|---|---|---|
| **Max Vertices Per Mesh** | 65,535 vertices | 16-bit index overflow; geometry explodes into random spikes. |
| **Index Format** | `GL_UNSIGNED_SHORT` | Indices $> 65535$ wrap around to 0. |
| **Max Bones Per Skeleton** | 100 bones (`dwBoneLimit=100`) | Skeletal matrix buffer overflow in Caver (`MeshBoneBatchBoneMax`). |
| **Bone Influences** | Strictly 1 bone per vertex | Weights $> 1$ are ignored; vertices tear if not pre-rigidified. |
| **Node Names** | Max 64 characters; ASCII | Truncation or lookup failure during animation linking. |
| **Skinning Type** | CPU Skinning via NEON | High vertex counts ($> 5,000$ skinned verts) cause CPU frame drops. |

---

## 10. Precision & Numerical Stability

* **Floating Point Format**: Standard IEEE 754 32-bit single precision throughout. Fixed-point (`bFixedPoint: false`) is disabled in stock Swordigo.
* **Quaternion Normalization**:
  * Decompiled `Caver::Matrix4::FromRotationQuaternion` assumes unit quaternions ($\|\mathbf{q}\| = 1.0$).
  * Non-unit quaternions produce non-orthogonal rotation matrices, distorting mesh scale during animation playback.
* **Unpack Matrix (`MeshUnpackMatrix`, tag 6020)**:
  * PowerVR allows quantized 16-bit vertex attributes that are expanded by `MeshUnpackMatrix`.
  * In all 158 stock Swordigo PODs, `MeshUnpackMatrix` is strictly the $4 \times 4$ **identity matrix**. Vertex positions are pre-unpacked 32-bit floats.

---

## 11. Unit and Scale Model

### The World Unit Standard
* Swordigo does **not** use real-world meters.
* **Empirical Character Dimensions**:
  * Hero (`hiro.POD`): height $\approx 106$ units.
  * Hero radius in scene player: $r \approx 53$ units.
  * Camera view distance: $Z = 2835.6$ units.
  * Torch point light radius: $180$ to $350$ units.
  * Tile / Platform blocks: $50$ to $300$ units wide.
* **Scale Double-Multiplication Bug in Ruby**:
  * In `src/tools/gltf_import.cpp`:
    * Line 244: `pv *= scale;` (scales every vertex coordinate in `m.positions`).
    * Line 381: `n.scale *= scale;` (scales the root node's transform matrix).
  * In `CPVRTModelPOD::GetWorldMatrix`, Caver multiplies the vertex position by the node's local scale.
  * **Result**: When imported with `--scale S`, the model is scaled by $S^2$ in-game!

---

## 12. Round-Trip Invariants

Any lossless round-trip pipeline ($\text{POD} \to \text{GLTF} \to \text{POD}$ or $\text{FBX} \to \text{POD}$) must guarantee:
1. **Coordinate Symmetry**: Vertex positions $(x, y, z)$ must remain unchanged (Right-handed, $+Y$ up, $-Z$ forward).
2. **Node Ordering**: The first `NumMeshNode` nodes must correspond 1:1 with `model.meshes[0..N-1]`.
3. **Bone Prefixes**: Skeletal bone nodes must preserve the `"Bone"` prefix (`"BoneHead"`, `"BoneArm"`, etc.).
4. **Pivot Preservation**: A node named `"CenterPoint"` must maintain its exact translation vector relative to model root.
5. **Animation Track Alignment**: Animation keys must sample at exactly 24 FPS with quaternion components $(x, y, z, w)$.
6. **Skin Index Rigidity**: Every vertex must have its primary bone index stored in `bone_indices[0]` with weight `1.0`.

---

## 13. Conversion Validation Corpus

A comprehensive test corpus of stock models covers all edge cases:

| Asset Name | Geometry Type | Node Structure | Validation Purpose |
|---|---|---|---|
| `hiro.POD` | Interleaved, Skinned (82 verts) | 61 nodes, CenterPoint present | Verifies skinned character, bone batches, pivot offset. |
| `hiro_run.POD` | Animation only (0 verts) | 25 frames @ 24 FPS | Verifies animation extraction, bone name binding, FPS. |
| `bat.POD` | Non-interleaved, Skinned | 2 meshes (348 & 217 verts), 21 nodes | Verifies multi-mesh, non-interleaved packing, bone controllers. |
| `bush.POD` | Static prop | 1 mesh, 1 node | Verifies basic static mesh, single material, UV coords. |
| `chest.POD` | Static prop with pivot | 1 mesh, CenterPoint present | Verifies static pivot placement. |
| `iceboss.POD` | Large complex boss | Skinned, 100 bones | Verifies bone limit boundary (100 bones). |

---

## 14. Differential Analysis: Stock Assets vs. Ruby Output

Comparing `bat.POD` / `hiro.POD` against Ruby's `pod_writer.cpp` output:

| Feature | Stock Swordigo POD (`temp.jpod`) | Ruby's `pod_writer.cpp` | Status |
|---|---|---|---|
| **Version Tag** | Tag 1000 with 11-byte string payload | Tag 1000 string + container close tag (`0x800003E8`) | ⚠️ Bug: Tag 1000 is not a container |
| **Vertex Layout** | Interleaved or Non-interleaved depending on asset | Non-interleaved (separate 6008, 6009, 6010 blocks) | Compatible (Caver repacks non-interleaved) |
| **Bone Weight Data** | Single float `1.0` per vertex | Multi-float array ($N$ influences) | ⚠️ Misunderstanding: Caver only reads 1 bone |
| **Node Layout** | Mesh nodes first, then bones | Mesh nodes first, then bones | ✅ Matches |
| **Texture Names** | Base name without `.pvr` (e.g. `bat_orange.png`) | Relative path / filename | ✅ Matches |

---

## 15. Runtime-Grounded Verification Summary

* **Disassembly Proof Locations in `OpenSwordigo/arm64_13/libswordigo_ida.c`**:
  * Forward direction: Line `477100` (`(0, 0, -1, 0)`).
  * Matrix multiply: Line `552814` (`C_Matrix4Mul`).
  * Model pivot: Lines `19115–19166` (`Caver::ModelComponent::WorldMatrix` with `-origin`).
  * Skeleton creation: Lines `469798–470398` (`Caver::PODLoader::CreateSkeleton`).
  * 1-bone rigid skinning: Line `552991` (`C_Matrix4Vector3ArraySkin`).
  * Mesh creation & VBO upload: Lines `470432–471019` (`Caver::PODLoader::CreateMesh`).
  * Animation extraction: Lines `471176–471350` (`Caver::PODLoader::CreateAnimationFromFile`).
  * Animation framerate: Line `471340` (`24.0f`).

---

## 16. Final Synthesized Report

### 1. Confirmed Facts
1. **Coordinate System**: Right-Handed OpenGL system ($+X$ right, $+Y$ up, $+Z$ toward viewer, $-Z$ forward). Exactly matches glTF 2.0.
2. **Animation Framerate**: Hardcoded to 24.0 FPS (`0x41C00000`) in `Caver::PODLoader::CreateAnimationFromFile`.
3. **Animation Binding**: Animations bind to skeletons strictly **by node name matching** (not by node index).
4. **Skeletal Bones**: Identified strictly by the prefix `"Bone"` (`strncmp(name, "Bone", 4) == 0`).
5. **Model Pivot / Origin**: Defined by a helper node named `"CenterPoint"`, subtracted at render time.
6. **Tangent Space**: Completely ignored by the engine (no tangent arrays or GLES shader inputs).
7. **Mesh Indices**: 16-bit unsigned shorts (`GL_UNSIGNED_SHORT`), capping meshes at 65,535 vertices.

### 2. Strongly Supported Conclusions
1. **Skinning is Rigid (1-bone per vertex)**: `C_Matrix4Vector3ArraySkin` ignores weights and transforms each vertex by exactly one matrix index. Smooth weights from Blender/Maya must be pre-baked to the dominant bone.
2. **Scale Exponentiation Bug**: Multiplying both vertex positions and node scale by `--scale` in `gltf_import.cpp` produces a quadratic scale ($S^2$) in-game.
3. **UV Vertical Flip**: Stock models keep $V=0$ at bottom of texture coordinates; texture images in `.pvr` are stored top-down.

### 3. Suspected Ruby Bugs
1. **Scale Double-Multiplication**: In `src/tools/gltf_import.cpp:244` and `:381`, `scale` is applied twice.
2. **Double V-Flip**: `pod_convert.cpp` flips image pixels vertically during texture encoding, but if `--flip-v` is passed, UV coordinates are inverted again, resulting in upside-down textures.
3. **Malformed Version Block Close Tag**: `pod_writer.cpp:260` writes a closing tag `0x800003E8` for tag 1000 (`eFormatVersion`), which is a leaf string chunk in PowerVR spec, not a container.
4. **Empty Bone Batch Indices**: `gltf_import.cpp:236-241` sets `has_bone_batches = true` and `count = 1` but leaves `bone_batches.indices` unpopulated.

### 4. Recommended Implementation Order for Converter Refactoring
1. **Phase 1: Fix Core Math & Format Invariants**:
   * Remove scale double-multiplication in `gltf_import.cpp`.
   * Correct POD version chunk writing in `pod_writer.cpp`.
   * Standardize UV flip policy (flip texture buffer OR flip UVs, never both).
2. **Phase 2: Skeletal & Skinning Pipeline**:
   * Implement dominant-bone quantization ($\max(w_i) \to \text{bone}_0$, weight = 1.0) during glTF/FBX import.
   * Ensure bone nodes retain the `"Bone"` prefix and `"CenterPoint"` pivot helper.
3. **Phase 3: Animation Pipeline**:
   * Resample glTF/FBX animation tracks to exactly 24.0 FPS.
   * Export animation-only POD files with bone name matching.
4. **Phase 4: Validation & Round-Trip Suite**:
   * Run differential tests against stock assets (`hiro.POD`, `bat.POD`, `bush.POD`).
   * Verify rendered models in both Ruby Asset Viewer and native game engine.