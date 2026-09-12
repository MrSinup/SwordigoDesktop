# PostFX Pipeline Deep Research Report: V7.1 Classic vs. Current Architecture

> **Document Path**: `docs/graphics_and_rendering/08_postfx_v7_vs_current_pipeline_analysis.md`
> **Reference File**: `/run/media/quantumcreeper/TVPG/Prenxy Packages/SwordigoDesktop/old/src_V7.1/platform/fbo_scaler.cpp`
> **Active Target File**: `src/platform/fbo_scaler.cpp`

---

## 1. Executive Summary

A comparative reverse-engineering audit between **V7.1 (`old/src_V7.1/platform/fbo_scaler.cpp`)** and the current desktop PostFX pipeline (`src/platform/fbo_scaler.cpp`) reveals key visual differences explaining why the classic V7 "vibe" was lost in recent builds:

1. **Tone Mapping & Color Grading Discrepancy**:
   - **V7.1 Classic**: Utilized Extended Reinhard Tone Mapping with $L_{\text{white}} = 2.5$, a +15% saturation boost (`mix(luma, scene, 1.15)`), and a smoothstep micro-contrast curve (`smoothstep(0.0, 1.0, scene)`).
   - **Current Build**: Replaced Reinhard with ACES / Linear tonemapping and procedural LabPBR material response, which shifted the warm, punchy, nostalgic Swordigo color grade to a cooler, metallic look.

2. **Contour Outline Pipeline**:
   - **V7.1 Classic**: Implemented **Color-Aware Contours**. Outlines were calculated as a darkened (30% brightness) and 30% saturation-boosted average of neighbor colors (`green grass \to dark green outline`, `brown dirt \to dark brown outline`).
   - **Current Build**: Simplified outline pass to monochrome depth edges.

3. **Bloom Extraction Mechanics**:
   - **V7.1 Classic**: Soft-knee luminance extraction:
     ```glsl
     float brightness = dot(col, vec3(0.2126, 0.7152, 0.0722));
     float knee = max(0.0, brightness - u_threshold) / max(brightness, 0.001);
     ```
     This created soft, ethereal, warm halos around torches and magic spell effects without blowing out highlights.

4. **LabPBR Over-Saturation in Standard Presets**:
   - Procedural LabPBR material detection (water, lava, gold, foliage) was implicitly active inside `SW_PLUS_MEDIUM` and `SW_PLUS_HIGH`, interfering with vanilla level art textures.

---

## 2. Technical Comparison Matrix

| Feature Subsystem | V7.1 Classic Pipeline (`src_V7.1/fbo_scaler.cpp`) | Current Build (`src/platform/fbo_scaler.cpp`) | "Best of Both Worlds" Target |
| :--- | :--- | :--- | :--- |
| **Tone Mapping** | Extended Reinhard ($L_{\text{white}} = 2.5$) | ACES / Linear Tonemap | **Extended Reinhard** in SW+ Normal/High |
| **Color Grading** | +15% Saturation + Micro-Contrast Curve | Neutral / Linear | **V7.1 Punchy Color Grade** in SW+ |
| **Contour Outlines** | **Color-Aware Outlines** (Tinted local color) | Monochrome Depth Edge | **Color-Aware Outlines** restored |
| **Bloom Extraction** | Soft-knee Luma Extraction | Hard-Threshold Linear | **Soft-Knee Ethereal Bloom** restored |
| **LabPBR Materials** | Off | Enabled in SW+ High | **Isolated into dedicated `LPBR` Preset** |
| **Sun & Volumetric Lights** | 100% Unchanged | 100% Unchanged | **100% Untouched & Preserved** |

---

## 3. Presets & Architecture Strategy

### 3.1 Preset Order & Configuration
1. **`OFF`**: Vanilla 1:1 render.
2. **`SW_PLUS_NORMAL` (SW+ Normal)**:
   - Classic V7.1 Reinhard Tone Mapping ($L_{\text{white}} = 2.5$).
   - V7.1 +15% Saturation & Micro-contrast curve.
   - Soft-knee ethereal bloom & Color-aware contour outlines.
   - **LabPBR Disabled** (Pure vanilla art fidelity!).
3. **`SW_PLUS_HIGH` (SW+ High)**:
   - Everything in `SW_PLUS_NORMAL` plus High-Res SSAO, God Rays, and Soft Shadows.
   - **LabPBR Disabled**.
4. **`LAB_PBR` (LPBR)** (New dedicated mode placed immediately after SW+ series):
   - Full procedural LabPBR material detection (water reflections, metallic specular, emissive lava, foliage PBR).
5. **`ATMOSPHERIC` / `ETHEREAL` / `CINEMATIC` / `RETRO` / etc.**: Remaining specialty presets preserved.

---

## 4. Summary of Code Preservation
No existing code will be deleted. The current `FRAG_COMPOSITE` and material shaders will remain intact for the `LPBR` and specialty presets, while `SW_PLUS_NORMAL` and `SW_PLUS_HIGH` will utilize the restored V7.1 Classic PostFX shader pipeline!
