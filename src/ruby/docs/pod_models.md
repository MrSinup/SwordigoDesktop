# Inside a Swordigo POD Model

## 1. Overview & Heritage

Swordigo utilizes the **PowerVR Object Data (POD) version 2.0** file format for all 3D geometry, skeletal rigs, and skeletal animation tracks. POD is a lightweight, game-ready binary container created by Imagination Technologies specifically designed for early PowerVR MBX and SGX mobile GPUs.

In Swordigo's native engine (`Caver::PODLoader`), POD models are mapped directly into OpenGL ES 1.1 vertex buffer arrays with zero parsing overhead at runtime.

---

## 2. Asset Inventory & Architecture

Swordigo organizes its POD assets into two distinct functional categories:

### 2.1 Static & Base Geometry PODs
Contain the mesh topology, vertex attributes (position, normal, UV coordinates), and optional skeletal bind-pose hierarchy:
- **Characters & Enemies**: `hiro.POD`, `skelly.POD`, `npc_male1.POD`, `bat.POD`
- **Environmental Props**: `chest.POD`, `house.POD`, `bush.POD`, `rock1.POD`
- **Weapons & Equipment**: `brass_sword.POD`, `item_platearmor.POD`

### 2.2 Dedicated Animation PODs
Swordigo **never bundles animations inside the base character POD**. Instead, each animation clip is isolated in its own dedicated POD file:
- `hiro_idle.POD`
- `hiro_run.POD`
- `hiro_jump.POD`
- `hiro_attack1.POD`
- `skelly_walk.POD`

The game engine binds these animation tracks to the base model skeleton dynamically at runtime through configuration in `.scl` entity templates.

---

## 3. The Binary Block Grammar

Every section within a POD file is structured as a length-delimited binary packet:

```text
┌──────────────┬──────────────┬────────────────────────┬──────────────┬──────────────┐
│  TAG (u32)   │ Length (u32) │    Payload (N bytes)   │ EndTag (u32) │   0x0 (u32)  │
└──────────────┴──────────────┴────────────────────────┴──────────────┴──────────────┘
```

- **Open Block**: A 32-bit integer tag (`u16 tag`, `u16 0`) followed by 32-bit byte length.
- **Payload**: Raw binary data or nested child blocks.
- **Close Block**: The same tag masked with `0x80000000`, followed by `0x00000000`.

### Key Block Identifiers:
- `0x1000` (`ePODFileScene`): Root container block
- `0x2000` (`ePODFileMaterials`): Material color, opacity, and texture slot definitions
- `0x3000` (`ePODFileMeshes`): Vertex buffers, indices, UV coordinates, and vertex normals
- `0x4000` (`ePODFileNodes`): Scene graph nodes, transforms, bone parents, and visibility
- `0x5000` (`ePODFileTextures`): External texture string references

---

## 4. Texture Bindings & PVR Mapping

POD files **never embed image data**. Instead, the material block stores a string reference to an external texture:

```c
// Example material definition inside hiro.POD
Material {
    Name: "hero_body"
    TextureFilename: "char_beta2_2x"   // references char_beta2_2x.pvr
    DiffuseColor: [1.0, 1.0, 1.0]
    Opacity: 1.0
}
```

The game engine searches for a matching `.pvr` file in the same asset directory. If the texture is missing or named incorrectly, the model will render with a pure untextured white or magenta silhouette.

---

## 5. Skeletal Hierarchy & Bone Limits

Swordigo characters use a hierarchical bone rig:
1. **Root Bone (`b_root`)**: Ground placement origin.
2. **Pelvis / Spine**: Primary torso movement.
3. **Limbs & Weapons**: Attached to socket nodes (e.g. `b_hand_R` for sword attachments).

> **Important**: Swordigo's hardware skinning vertex shader limits each mesh cluster to a maximum of **100 bones** (`dwBoneLimit = 100`). Meshes exceeding this limit must be partitioned into multiple submeshes before conversion.

---

## 6. Ruby GG Modding & Conversion Pipeline

Converting modern 3D models (`.glb`, `.gltf`, `.obj`, `.fbx`) into Swordigo PODs is handled directly by Ruby's built-in converter:

1. **Geometry Triangulation**: Ensure all polygons are planar triangles.
2. **Scale Fitting**:
   - Hero scale: ~70 engine units height
   - Prop scale: ~100 engine units height
   - Large structure / decor: ~250 engine units height
3. **UV Alignment**: OpenGL uses lower-left origin `(0, 0)`. If textures appear inverted vertically, enable **Flip UV Coordinates** during conversion.
4. **Compression**: Pair with an ETC1 or PVRTC compressed `.pvr` texture.
