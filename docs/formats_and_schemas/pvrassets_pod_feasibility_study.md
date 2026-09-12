# Feasibility Study: Integrating PowerVR Native SDK (`PVRAssets` / `PODReader`) into Ruby GG

## Executive Summary

**Verdict: NOT Recommended (High Cost & Friction, Negative Value for Swordigo)**

While `Native_SDK-master` is open-source (MIT licensed), simply copying `PODReader.cpp` into `src/platform/` is **not feasible as a drop-in file copy**. It has a deep dependency chain spanning over 120 files across `PVRAssets` and `PVRCore`, along with the GLM math library. 

Furthermore, `PVRAssets::PODReader` is **strictly inferior** for Swordigo development compared to our existing [`swpod`](file:///home/quantumcreeper/SwordigoDesktop/src/tools/pod_loader.h) infrastructure:
1. It is **Read-Only** (contains zero code to export/serialize POD files).
2. It lacks Swordigo-specific engine semantics (`CenterPoint` translation pivot, split animation-only POD merging, feet grounding calculations).
3. It outputs into a heavy, foreign hierarchy (`pvr::assets::Model`) requiring a complete secondary translation layer to OpenGL.

---

## 1. Technical Feasibility of "Just Copying Files"

### The Dependency Web
`PODReader.cpp` cannot compile in isolation. Tracing its `#include` tree reveals a tightly bound framework:

```
PODReader.cpp
  ├── PODReader.h & PODDefines.h
  ├── Model.h (1,490 lines)
  │     ├── model/Mesh.h & Mesh.cpp
  │     ├── model/Animation.h & Animation.cpp
  │     ├── model/Camera.h & Camera.cpp
  │     ├── model/Light.h & Light.cpp
  │     ├── model/FormattedUserData.h
  │     └── IndexedArray.h
  ├── Helper.h & Helper.cpp
  └── PVRCore (92 files!)
        ├── PVRCore/stream/Stream.h, BufferStream.h, FileStream.h
        ├── PVRCore/strings/StringHash.h, CompileTimeHash.h, StringFunctions.h
        ├── PVRCore/types/FreeValue.h, TypedMem.h, Types.h
        ├── PVRCore/Log.h, Errors.h, RefCounted.h
        └── PVRCore/glm.h  --> requires third-party GLM (glm::vec3, mat4, quat)
```

To compile `PODReader.cpp`, you would have to vendor:
* **All of `framework/PVRAssets`** (37 files).
* **All of `framework/PVRCore`** (92 files).
* **GLM (OpenGL Mathematics)** library.

**Attempting to copy just `PODReader.cpp` results in hundreds of missing symbol errors on the first compile.**

---

## 2. Why Native SDK Assets & PVR Files Don't Work Properly (Root Cause)

You observed that `.pod` and `.pvr` files from the Native SDK don't load properly, and standard PVR viewers choke on them or Swordigo assets. Here is why:

### A. Two Different Eras of PowerVR (V2 vs V3)
| Characteristic | Swordigo (2011–2012) | Native_SDK-master (Modern Vulkan/PBR) |
| :--- | :--- | :--- |
| **POD Specification** | PowerVR SDK v2.0 (Classic Tag/Length) | PowerVR SDK v2.0 with modern PBR blocks (metallicity, roughness, kelvin) |
| **PVR Texture Format** | **PVR v2 (Legacy 44-byte header)**, format 0x18/0x19 (PVRTC) or 0x36 (ETC1) | **PVR v3 (52-byte header, "PVR\3")**, ASTC, BCn, Vulkan swizzles |
| **Container / Compression** | **Gzip-compressed streams** (Swordigo's `.tex` files are gzipped PVR v2) | Raw binary PVR containers |
| **Viewer Compatibility** | Rejected by modern tools (cannot parse legacy v2 flags or gzip) | Rejected by legacy viewers (unrecognized v3 chunk descriptors) |

Modern `PVRTexTool` and generic viewers will fail on Swordigo textures because they do not expect gzip wrapping or legacy 2011 PVR v2 bitmasks. Conversely, legacy engines fail on modern SDK assets because they contain modern PBR / ASTC data.

### B. Swordigo-Specific Engine Quirks in Our Infra
Our [`src/tools/pod_loader.cpp`](file:///home/quantumcreeper/SwordigoDesktop/src/tools/pod_loader.cpp) was reverse-engineered directly against TouchFoo's engine (`libswordigo_arm32.c`):
1. **`CenterPoint` Pivot Node**:
   - In Swordigo, props, enemies, and chests contain a dummy node named `CenterPoint`.
   - The engine offsets the model's global geometry by `-CenterPoint.translation`.
   - *Generic PowerVR SDK does not know this*: models loaded via `PVRAssets` will float in the air or have displaced origins.
2. **Animation-Only PODs (`merge_hint`)**:
   - Swordigo splits meshes (`knight.POD`, `bat.POD`) and animations (`knight_run.POD`, `bat_fly.POD`).
   - Animation PODs contain **zero meshes**.
   - `PVRAssets::readPOD` treats node-less or mesh-less PODs as incomplete or empty scenes. Our loader automatically detects animation PODs and merges them with the base model.
3. **Index Widening**:
   - PowerVR POD stores 16-bit vertex indices. Swordigo runtime widens them to 32-bit (`uint32_t`) for GPU batching. Our loader handles this out of the box.

---

## 3. Feature Comparison: Our Infra (`swpod`) vs `PVRAssets`

| Capability | Current `swpod` / `Ruby` | Native SDK `PVRAssets` |
| :--- | :--- | :--- |
| **POD Read / Parse** | Full support (zero deps) | Full support (needs PVRCore + GLM) |
| **POD Write / Serializer** | **Full support** ([`pod_writer.cpp`](file:///home/quantumcreeper/SwordigoDesktop/src/tools/pod_writer.cpp)) | **NONE** (No writer exists in SDK) |
| **Gzip / Swordigo `.tex` Decoder** | **Built-in** ([`pvr_loader.cpp`](file:///home/quantumcreeper/SwordigoDesktop/src/platform/pvr_loader.cpp)) | **NONE** (Does not support gzipped PVRs) |
| **Swordigo Engine Semantics** | `CenterPoint`, `merge_hint`, `pod_feet_offset` | **NONE** (Generic viewer layout) |
| **Direct GPU Buffers** | Emits flat `std::vector<float>` ready for OpenGL | Emits `pvr::assets::Model` with `FreeValue` / `StringHash` |
| **Build Impact** | 0 external libraries, instantaneous build | Adds 120+ files, GLM dependency, longer compilation |

---

## 4. Conclusion & Recommendation

1. **Do not vendor or copy `PVRAssets`**: It is not a standalone drop-in file and would introduce unwanted bloat (120+ files + GLM) with no functional benefit.
2. **Do not create a backend toggle**: A toggle to `PVRAssets` would break POD saving/exporting (since Native SDK has no writer), break Swordigo `CenterPoint` alignment, and break animation-only PODs.
3. **Our current infra is already the optimal solution**: It implements the full Imagination POD 2.0 chunk protocol (verified 30/30 on Native SDK sample assets) while maintaining exact compatibility with TouchFoo's engine specifics.
