# Investigative Report: Analysis of Swordigo Modding Tooling

This report evaluates three specialized Swordigo development and reverse-engineering tools: **Decompiled Swordigo**, **FileRift**, and **Boulder**. It contrasts their features and implementation strategies against our **Ruby** host platform and outlines architectural plans to remaster our own scene and PowerVR Object Document (POD) parsers.

---

## 1. Tool Profiles

### Tool A: Decompiled Swordigo
* **Overview:** A complete interactive 3D level viewer and editor web application utilizing Three.js.
* **Core Capabilities:**
  * Interactive 3D scene graph rendering of levels directly in the browser.
  * Hierarchical recursive block tree parsing (`Wf` parser) for `.POD` 3D models.
  * Client-side decompression and translation of raw PVR textures, including hardware-compressed `ETC1` texture pixel arrays (`DS` decoder).
  * Dynamic layout assembly of level actors, lights, particle systems, and water planes from `.scene` sources.

### Tool B: FileRift
* **Overview:** A Protocol Buffers decoder and recoder wrapper built in Python.
* **Core Capabilities:**
  * Decodes and recodes Swordigo binary protocol buffer files (`.scene`, `.scl`, `.gdata`, `.gplayer`, etc.) into a custom plain text markup language.
  * Employs a custom tag-to-field schema map (`block_formats.py`) to give human-readable names to raw protobuf tag numbers.
  * Auto-compiles Lua string chunks (`@comp` trigger) into 32-bit bytecode, patching cross-endian/cross-bitness compatibility.
  * Provides template-based macro expansion (`$obj[...]`) to accelerate scene asset definitions.
  * Handles full Android APK decompilation, asset injection, signature matching, and rebuilding.

### Tool C: Boulder
* **Overview:** A custom ground geometry compiler written in Go.
* **Core Capabilities:**
  * Accepts custom 2D polygon coordinate paths from `.gmesh` text files.
  * Triangulates, extrudes, and compiles these paths into complete 3D meshes (top surfaces, side walls, and front faces).
  * Computes vertex coordinates, normal vectors, and UV coordinates for every vertex.
  * Outputs pre-compiled, raw binary buffer strings (`VertexData` and `IndexData`) embedded inside `GroundPolygon`, `GroundMesh`, and `CollisionShape` components in FileRift markup.

---

## 2. Competitive Comparison: What They Have vs. Ruby

| Feature Area | Decompiled Swordigo / FileRift / Boulder | Our Ruby Platform | Assessment & Opportunities |
| :--- | :--- | :--- | :--- |
| **Scene Parsing** | **FileRift** translates raw binary protobuf files into a named-tag markup format. **Decompiled Swordigo** dynamically instantiates actors and lights into a 3D scene graph. | Parses and loads level files via guest-side JNI hooks or raw byte translation. | **They do better:** Readability. We should adopt FileRift's tag schema naming mapping on the host side to print readable debug structures. |
| **POD Parsing** | **Decompiled Swordigo** parses hierarchical binary TLV (Tag-Length-Value) blocks recursively to reconstruct vertex, normal, UV, and skeletal animation lists. | Relies on PowerVR SDK loaders or guest-side engine loading. | **They do better:** Native block-level inspection. Remastering our POD loader to parse recursively will let us view and scale models natively. |
| **Ground Mesh Generation** | **Boulder** pre-compiles 3D meshes (top, side, front) from a 2D path and embeds raw vertex buffers inside the level. | Relies on the guest engine's runtime `GroundMeshGenerator` component. | **They do better:** Custom geometries. Pre-compiling meshes allows arbitrary shapes (slopes, arches, bridges) that the game's generator rejects. |
| **Texture Decoding** | **Decompiled Swordigo** natively decodes ETC1 and RGBA PVR textures in software. | Relies on GPU upload or guest-side loader hooks. | **They do better:** Software extraction. We can port the ETC1 decoder to quickly display textures in host-side tooling. |
| **Script Handling** | **FileRift** compiles Lua on-the-fly and patches both 64-bit string and 32-bit bytecode segments (`@comp`). | Intercepts Lua execution via JNI hooks but doesn't compile offline. | **They do better:** Bytecode patching. We can implement similar compilation tooling in our Lua console. |

---

## 3. Scope of Remastering Plans

### Phase 1: Remastering the POD Parser
To view and manipulate 3D models natively on the host, we will write a recursive TLV binary block parser based on Decompiled Swordigo's block structure.
* **Format Architecture:**
  * Every block has an 8-byte header: `tag` (32-bit uint) and `length` (32-bit uint).
  * If `tag & 0x80000000` is true, it is an end-of-list marker.
  * Otherwise, the block contains `length` bytes of raw data, followed by recursively nested child blocks.
* **Implementation Plan:**
  1. Define a C++ structure `PODBlock` containing the tag, raw data vector, and a list of child blocks.
  2. Implement a recursive parser:
     ```cpp
     PODBlock parse_block(BinaryReader& reader) {
         uint32_t tag = reader.read_u32();
         uint32_t len = reader.read_u32();
         std::vector<uint8_t> data = reader.read_bytes(len);
         PODBlock block(tag, data);
         while (reader.has_more() && !(reader.peek_tag() & 0x80000000)) {
             block.children.push_back(parse_block(reader));
         }
         if (reader.peek_tag() & 0x80000000) reader.skip(8); // consume end marker
         return block;
     }
     ```
  3. Extract vertex lists (tag `6006`), index arrays (tag `6003`), UV coordinate lists (tag `6010`), and texture names (tag `4000`).

### Phase 2: Remastering the Scene Parser
Instead of reading raw binary bytes, we will incorporate a protobuf reader that uses FileRift's tag mappings to parse scene levels, libraries, and savegames.
* **Implementation Plan:**
  1. Port the `block_formats.py` tag definitions into a static lookup map in `src/platform/protobuf_reader.h`.
  2. Write a protobuf stream reader that decodes Varints, Fixed32, Fixed64, and Length-delimited fields.
  3. Match tag numbers to class names (e.g. tag `0a` -> `SceneObject`, tag `1a` -> `Rectangle`) to construct a readable text representation of any binary asset on the fly.

---

## 4. Architectural Boundaries: What We Do NOT Want

While these tools are highly capable, we must avoid importing certain aspects into Ruby:
1. **APK Packaging Scripts:** FileRift contains extensive logic for unpacking/repacking APKs and running jar signers. Ruby operates as a desktop native runner/JIT environment, so we do not want APK packaging complexity.
2. **Web-Based Rendering Constraints:** Decompiled Swordigo uses a browser environment with Three.js. Our rendering must remain close to the OpenGL compatibility profile to ensure seamless interface overlays and shader pipelines.
3. **Manual Checksum Manifests:** FileRift uses static manifests to track modified files. Ruby can monitor file modifications dynamically using OS file watches (e.g. `inotify`) for instant hot-reloading.
