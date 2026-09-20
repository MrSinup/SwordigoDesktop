# Swordigo Textures & The PVR Pipeline

## 1. Overview: PowerVR Graphics Architecture

Swordigo relies extensively on the **PowerVR Texture (PVR)** format for all 2D UI sprites, environment atlas textures, particle sheets, and 3D character skins. 

PVR is optimized for hardware decompression directly inside mobile GPU silicon, drastically reducing memory bandwidth and RAM footprint compared to standard PNG or JPEG assets.

---

## 2. Supported Compression Codecs

Swordigo supports multiple texture compression schemes tailored for different hardware generations:

| Codec | Bits / Pixel | Alpha Support | Best Used For |
| :--- | :--- | :--- | :--- |
| **PVRTC 4bpp** | 4 bits | Yes (premultiplied) | Character skins, iOS builds, organic terrain |
| **PVRTC 2bpp** | 2 bits | Yes | Distant background layers, large low-detail clouds |
| **ETC1** | 4 bits | No (RGB only) | Android universal hardware compression |
| **RGBA4444** | 16 bits | Yes (4-bit alpha) | UI elements, sharp particle effects |
| **RGBA8888** | 32 bits | Yes (Full 8-bit) | Hero UI, high-fidelity HUD icons |

---

## 3. The Power-of-Two Rule (POT)

> **Critical Constraint**: Hardware PVRTC decompression requires textures to have **square, power-of-two dimensions**:
> - Valid: `128x128`, `256x256`, `512x512`, `1024x1024`, `2048x2048`
> - Invalid: `512x256` (non-square), `800x600` (non-power-of-two)

If you attempt to load a non-square texture compressed with PVRTC, OpenGL ES will fail texture validation and render completely black or corrupt garbage on device.

ETC1 and uncompressed RGBA allow rectangular textures as long as dimensions are multiples of 4.

---

## 4. UV Orientation & The Vertical Inversion Glitch

Different graphics APIs define vertical texture coordinates differently:
- **DirectX / Vulkan / PNG**: Origin `(0, 0)` is at the **top-left**.
- **OpenGL / OpenGL ES (Swordigo)**: Origin `(0, 0)` is at the **bottom-left**.

When authoring 3D models in Blender or Maya and exporting textures:
- Ensure the texture coordinates are either inverted vertically during export, or
- Enable **Flip UVs** in Ruby GG's **POD Converter** dialog before packing!

---

## 5. Naming Conventions & Asset Resolution

Swordigo employs resolution suffixes to differentiate display scaling:
- Base assets (320x480 era): `char_hero.pvr`
- High-resolution / Retina (Android & modern devices): `char_hero_2x.pvr`

When creating a new custom character or prop, always package both or supply the `_2x` version, as the engine's asset resolver defaults to looking for the `_2x` asset first on modern high-DPI displays.
