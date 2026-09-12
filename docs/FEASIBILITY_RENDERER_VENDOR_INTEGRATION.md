# Feasibility Study: Vendoring `zauonlok/renderer` into the Ruby SDK

> **Date:** 2026-08-09 · **Status:** Study complete — integration not yet started
> **Source:** `renderer-master/` (extracted from `renderer-master.zip`, Jan 18 2021) · upstream https://github.com/zauonlok/renderer

---

## 1. Executive Summary

`renderer-master` is **zauonlok/renderer** — a shader-based **software renderer** written from
scratch in **C89 with minimal dependencies** (X11 + libm on Linux). It is **MIT-licensed**
(verified: `LICENSE`, Copyright (c) 2020 Zhou Le). It builds cleanly on this machine with plain
GCC (`BUILD_OK`, zero warnings with `-Wall -Wextra -pedantic`).

It is NOT a competing "engine" — it is a **~7,800-line reference implementation of a modern
rendering pipeline** that runs entirely on the CPU. Its crown jewels are algorithms, not
infrastructure:

- **PBR** (metallic-roughness **and** specular-glossiness workflows)
- **Image-Based Lighting (IBL)** with prefiltered environment maps (split-sum approximation)
- **Directional shadow mapping**
- **Tangent-space normal mapping**
- **glTF-style skeletal animation** (4-bone skinning, joint + normal matrices)
- **ACES tone mapping** (the renderer's entire "post-FX")
- A complete, dependency-free software rasterizer (homogeneous clipping, back-face culling,
  perspective-correct interpolation, depth/alpha test, alpha blend, cubemap skybox)

**Verdict: vendor it, but as a *reference + headless capability*, not as a replacement for
ruby's GPU renderer.** Ruby's real-time OpenGL 3.3 pipeline (av_renderer) with its PostFX chain
(bloom, DOF, SSAO, color grade, FSR) is strictly *more* capable in post-processing. What the
vendor adds is the **physically-based lighting math, the GPU-independent render path, and a
vendor-neutral asset abstraction** — exactly the stepping stone needed to grow ruby from a
Swordigo-only asset viewer into a general-purpose SDK that renders non-Swordigo models.

---

## 2. What The Vendor Actually Is (verified facts)

| Property | Value |
| :--- | :--- |
| Upstream | github.com/zauonlok/renderer (Zhou Le) |
| License | **MIT** (permissive → fully compatible with this GPLv2 repo, like the vendored ufbx) |
| Language | C89 (`-std=c89 -pedantic`), ~7,791 lines |
| Dependencies | Xlib + libm only (Linux); Cocoa (macOS); Win32 (Windows) |
| Build | `build_linux.sh`, `CMakeLists.txt` (LANGUAGES C, C_STANDARD 90) |
| Local build | ✅ verified clean on GCC 16.1.1 with X11 dev headers |
| Runtime | CPU rasterization into a software framebuffer (color + depth) |
| Shader model | Vertex/fragment shaders as **C functions** (no GLSL) |
| Source layout | `renderer/core/*` (graphics, maths, mesh, texture, skeleton, scene, camera, darray, image, draw2d) · `renderer/shaders/*` (blinn, pbr, skybox, cache) · `renderer/scenes/*` (25 canned scenes) · `renderer/tests/*` (scene builders) · `renderer/platforms/*` (linux/macos/win32) |
| Scripts | Python pipeline `scripts/` + `utils/{gltf,cmgen,hdr}.py` — converts **glTF 2.0 → .obj/.ani/.scn + .tga/.hdr** |
| Tools | Bundled Filament **cmgen** binaries (Apache-2.0) for IBL environment prefiltering |
| Assets | `test_assets/` (173 MB, 23 PBR/Blinn demo models with animation + screenshots) |
| Formats read in C | OBJ (mesh), TGA + Radiance HDR (textures), `.ani` (skeleton), `.scn` (JSON scene) |
| Formats read in Python | glTF 2.0 (full: meshes, skins, animations, materials, textures) |

### Architecture at a glance

```
glTF 2.0 ──(scripts/utils/gltf.py)──▶ .obj + .ani + .scn + .tga/.hdr
                                          │
                          mesh_load(obj) · skeleton_load(ani) · scene_from_file(scn)
                                          ▼
    scene_t { skybox, models[], shadow_buffer, ambient/punctual intensity }
                                          ▼
    program_t { C-function vertex + fragment shaders }  ──▶  software framebuffer
```

The core API surface (`renderer/core/api.h`) is tiny and clean:

- `framebuffer_create/clear_*` — software color+depth buffer
- `program_create(vertex_shader_fn, fragment_shader_fn, ...)` — shader programs as C callbacks
- `graphics_draw_triangle()` — the rasterizer (homogeneous clip, cull, perspective-correct,
  depth test, alpha test/blend)
- `mesh_load/skeleton_load/texture_load` — asset loaders with a ref-counted cache
  (`shaders/cache_helper.c`)
- `scene_create/release` — a renderable scene with a skybox, sorted models and a shadow pass

---

## 3. Head-to-Head: renderer vs ruby's current stack

| Capability | zauonlok renderer | Ruby SDK today (av_renderer + scene_loader) | Who wins |
| :--- | :--- | :--- | :--- |
| API/host | SDL-less, X11, pure C89, no GL | ImGui + SDL3 + OpenGL 3.3 | Ruby (editor) |
| Rasterization | CPU software, portable, deterministic | GPU, fast, real-time | Draw — each has its use |
| Blinn-Phong shading | ✅ (spec-gloss oriented) | ✅ (key+fill+rim+spec+hemisphere) | Tie |
| **PBR metallic-roughness** | ✅ full workflow | ❌ none | **vendor** |
| **PBR specular-glossiness** | ✅ full workflow | ❌ none | **vendor** |
| **Image-based lighting** | ✅ prefiltered env maps + cmgen | ❌ none (hemisphere ambient only) | **vendor** |
| **Shadow mapping** | ✅ directional depth shadow map | ❌ blob/contact-shadow ellipses only | **vendor** |
| **Tangent-space normal mapping** | ✅ tangent/bitangent reconstruction | ❌ per-vertex normals only | **vendor** |
| **Skeletal animation** | ✅ 4-bone skinning (joint+n matrices) | ⚠️ POD animation playback, no glTF skinning in renderer | vendor (as math reference) |
| Skybox / environment | ✅ cubemap skybox | ⚠️ background layer quads | vendor (env) / ruby (game) |
| Post-FX: bloom / DOF / SSAO / grain / vignette / grade / FSR | ❌ ACES only | ✅ full PostFX chain in `postfx_apply()` | **ruby** |
| ACES tone mapping | ✅ | ✅ ("hd" mode in PostFX) | Tie |
| Material inspector (Marmoset-like) | ✅ built-in demo | ⚠️ ImGui material tweaks | vendor (UI pattern) |
| Scene format | custom `.scn` JSON | Swordigo `.scene` protobuf + `.scl` libs | ruby (domain) |
| Non-Swordigo assets | glTF / OBJ / TGA / HDR | glTF in/out, OBJ, FBX (ufbx), POD, PVR, filerift formats | ruby (wider) |
| Rendering without GPU | ✅ fully headless-capable | ❌ requires GL context | **vendor** |
| License | MIT | GPLv2+ (repo) | compatible |
| Size | ~7.8k LOC, self-contained | ruby renderer alone ~13k LOC + GL | vendor is lean |

**Honest conclusion:** the vendor is *not* "better than ruby" overall — it is better at exactly
four things ruby lacks today (**PBR, IBL, shadow maps, tangent normal mapping, GPU-free
rendering**), and it is the cleanest MIT-licensed reference implementation of those algorithms
that exists for a C/C++ codebase. Ruby is already ahead in post-FX, editor tooling, format
support and real-time GPU throughput.

---

## 4. What We Get From Inheriting This As A Vendor

### 4.1 Capability wins (new features ruby can offer)

1. **PBR material pipeline** — metallic-roughness + specular-glossiness with proper Fresnel,
   GGX-ish specular response. This is what makes modern game/Blender assets look right.
2. **IBL** — irradiance + prefiltered radiance from an HDR environment (bundled Filament
   `cmgen` tool does the offline prefiltering). Turns "hemisphere ambient" into real
   environment lighting.
3. **Directional shadow mapping** — real depth-based shadows instead of blob ellipses.
4. **Tangent-space normal mapping** — normal detail on non-Swordigo assets (PBR pipeline
   requirement).
5. **GPU-independent rendering** — thumbnails, batch previews, CI screenshot tests, headless
   `ruby --render model.gltf out.png` without a display/GPU. This is a **product differentiator**
   and unlocks headless MCP/CLI rendering.
6. **glTF asset pipeline** (scripts) — a proven glTF 2.0 → engine-asset path with skinning and
   animation extraction, complementing ruby's existing `gltf_import.cpp`.

### 4.2 Engineering wins

7. **Reference implementation for the GLSL port** — the C shaders are readable, dependency-free
   and *deterministic*; render a frame in software and compare pixel-by-pixel with the GPU port
   (regression testing without guesswork).
8. **Clean, testable math** — `core/maths.c` (951 lines) is a standalone SIMD-free math lib that
   can be unit-tested and shared.
9. **Ref-counted asset cache pattern** (`cache_helper.c`) — reusable pattern for ruby's asset
   layer.
10. **Scene abstraction** (`scene_t`/`model_t`/`perframe_t`) — a clean vendor-neutral scene
    model that is exactly the "stepping stone" abstraction ruby needs for non-Swordigo content:
    a mesh, a transform, a skeleton, a program — no Swordigo assumptions.
11. **MIT license** — no obligations beyond keeping the copyright notice; precedent already
    exists in the repo (ufbx).

### 4.3 What it does NOT give us (don't expect this)

- No replacement for ruby's PostFX chain (bloom/SSAO/DoF/FSR) — ruby is ahead here.
- No engine/game-loop/input/scene-graph editor — that is ruby's domain.
- No GPU acceleration — the software path is for offline/reference/fallback only (expect
  interactive but modest framerates at 960×544).
- No direct `.scene`/POD support — Swordigo formats stay ruby's job.

---

## 5. Integration Strategies (3 viable paths)

### Option A — Vendor + headless software-render service *(recommended first step)*
Copy `renderer/` into the repo under `vendor/zauonlok-renderer/` (keep MIT notice), build it as
a static lib (`libzrender.a`) with its own tiny Makefile/CMake hook, and expose a thin C API
(`renderer_api.h` — framebuffer create/clear, mesh load, scene build, render-to-buffer). Ruby
links it and gains:
- `ruby --render <gltf|obj> <out.png>` headless rendering (thumbnails, batch, CI)
- a "Software render" preview mode inside the asset viewer (compare GL vs CPU output)
- a reference implementation for the GLSL ports in Option B

**Effort: ~1–2 days.** No changes to ruby's existing pipeline; purely additive.

### Option B — Port the lighting math to GLSL (real-time PBR/IBL/shadows in av_renderer)
Translate the vendor's C shaders to GLSL 330 programs in `av_renderer.cpp`:
- new `pbr_program` (metallic-roughness), `ibl` env-map sampling, depth shadow-map pass, and
  tangent normal-mapping vertex layout (extend `GPUMesh` with tangent/joint/weight attributes).
- Use the software renderer as the oracle for regression frames.

**Effort: ~3–5 days** for PBR+normal mapping, +2–3 days for IBL + shadow maps. This is the
highest-value upgrade for the *previewer*.

### Option C — Full backend swap (NOT recommended)
Replacing the GL path with the CPU rasterizer for interactive use would regress performance,
drop PostFX, and throw away the editor. Only sensible as a *fallback* mode when no GL context
exists.

### Option D — Asset pipeline adoption (can ride along with A/B)
Adopt `scripts/utils/gltf.py` + cmgen into the toolchain so any glTF/OBJ/HDR asset lands in a
form both the software renderer and ruby can consume. Ruby already has native glTF import, so
this is mostly for the *renderer's* demo scenes and IBL env prefiltering.

---

## 6. Stepping Stone: Ruby as a General-Purpose SDK (non-Swordigo assets)

The end-state goal — ruby rendering **any** model with **any** material — decomposes exactly
into what the vendor contributes:

1. **Asset layer** (ruby already has): glTF/OBJ/FBX importers → intermediate mesh (positions,
   normals, UVs, tangents, joints, weights) — this is *identical* to the vendor's `vertex_t`.
2. **Material layer** (vendor contributes): base color/metalness/roughness/emissive maps,
   occlusion, normal maps, alpha modes — the two PBR workflows + Blinn fallback.
3. **Lighting layer** (vendor contributes): punctual lights + IBL + shadow maps + ACES output.
4. **Animation layer** (vendor contributes as reference): joint matrices, normal matrices,
   4-bone blend — portable directly into ruby's GL path or kept CPU-side for POD-style playback.
5. **Presentation layer** (ruby has): ImGui inspector, PostFX chain, FSR upscaler, camera ports.

The vendor's `mesh_t`/`skeleton_t`/`program_t`/`scene_t` types are Swordigo-agnostic by design —
they are the natural blueprint for a `SwordigoSDK` intermediate representation that can be fed
by *any* importer and drawn by *either* the GL or the software backend.

---

## 7. Risks & Mitigations

| Risk | Level | Mitigation |
| :--- | :--- | :--- |
| Software rendering too slow for interactive use | Medium | Use only for headless/thumbnails/reference; keep GL as the real-time path |
| C89 code style clashes with C++ codebase | Low | Build as a C static lib with an extern "C" bridge — no source merge needed |
| IBL pipeline (cmgen) is a big external tool | Low | Apache-2.0, bundled binaries already present; only needed for env-map authoring |
| `-ffast-math` numerical differences vs GLSL | Low | Reference comparisons use tolerance; port math carefully (NaN guards exist in both) |
| Maintaining a fork of the vendor | Low | It is 7.8k LOC, MIT, and effectively frozen upstream (2021) — low drift risk |
| Asset licensing (test_assets) | Info | Demo models are third-party samples (glTF sample assets); don't ship in the SDK product without checking each model's license |

---

## 8. Recommended Phased Plan

| Phase | Work | Est. |
| :--- | :--- | :--- |
| **P0** | Vendor source into `vendor/zauonlok-renderer/`, add build target, smoke-build | 0.5 d |
| **P1** | Thin C bridge `renderer_api.{h,c}` + `ruby --render` headless mode (glTF/OBJ → PNG) | 1–1.5 d |
| **P2** | Software-preview mode inside asset viewer + screenshot regression harness (GL vs CPU) | 1 d |
| **P3** | Port PBR + tangent normal mapping to av_renderer GLSL (renderer as oracle) | 3–5 d |
| **P4** | Port IBL (cmgen prefilter) + directional shadow maps | 2–3 d |
| **P5** | Generalize: shared intermediate mesh/material structs → SDK `SpModel`/`SpMaterial` | 2–3 d |

Total ≈ **10–14 focused days** to full capability, with working, shippable value after P1.

---

## 9. License / Compliance Notes

- Vendor license: **MIT** (Copyright (c) 2020 Zhou Le). Vendoring into this GPLv2+ repo is
  permitted; MIT text must be preserved with the vendored code (same pattern as ufbx).
- Bundled `cmgen` + `vswhere` binaries carry their own licenses (Apache-2.0 / MIT) — keep their
  notices if they are redistributed.
- `test_assets/` demo models are third-party sample assets — usable for internal dev/testing;
  verify individual licenses before shipping them in a product.

---

---

## 5A. Algorithm-Only Inheritance — Verified Extraction Map (2026-08-09)

**Question:** can we inherit just the *algorithms* (PBR, IBL, DSM, glTF2 render/viewer, skeletal
animation player) to improve ruby's GPU renderer, without vendoring the whole codebase?

### Implementation status (2026-08-09 — shipped)

| Item | Status | Where |
| :--- | :--- | :--- |
| PBR (metal-rough + spec-gloss, GGX/Smith/Fresnel) | ✅ live | `av_renderer.cpp` `PBR_VS/PBR_FS`, `pbr_render_mesh()` |
| Tangent normal mapping (TBN + handedness) | ✅ live | `PBR_VS/PBR_FS`, `upload_mesh_ex()` tangents |
| IBL split-sum (BRDF LUT + prefiltered env) | ✅ live | `generate_brdf_lut()`, `create_default_env_cubemap()`, `pbr_set_environment()` |
| Directional shadow mapping (DSM) | ✅ live | `create_shadow_fbo()`, `begin/end_shadow_pass()`, `shadow_render_mesh()`, `pbr_set_shadow()` |
| GPU 4-bone skinning (joint matrices) | ✅ live | `PBR_VS` `uJoints[32]`, `pbr_set_joint_matrices()` |
| ACES tone mapping | ✅ live | `PBR_FS` main() |
| glTF2 viewing (`*.glb`) | ✅ live | `asset_viewer.cpp` `classify_file`/`select_file` (animations + CPU skinning ride existing machinery) |
| FBX texture mapping fix | ✅ live | `fbx_import.cpp` `resolve_texture` + `uv_v_flipped`; `asset_viewer.cpp` `flip_surface_vertical` + shader `uFlipV` |
| Reference sources + MIT notice | ✅ live | `src/ruby/zauonlok/` |
| Radiance HDR reader | 📦 reference only | `src/ruby/zauonlok/image.c` — next step: native env-map pipeline into `pbr_set_environment()` |
| `.ani` skeleton evaluator | 📦 reference only | `src/ruby/zauonlok/skeleton.c` — next step: feed `pbr_set_joint_matrices()` |

Verified: `make bin/ruby` builds clean; runtime smoke test on the real GL driver reports
`[av_renderer] PBR program ready (BRDF LUT=1, env=2)` + `Initialized`.

**Answer: Yes — this is the cleanest path.** The vendor's shaders are pure functions of
(attribs, varyings, uniforms) — the *same contract GLSL uses* — with zero coupling to the
software rasterizer. Verified module-by-module:

### Port to GLSL (near line-for-line; no rasterizer coupling)

| Algorithm | Vendor source | Ruby target |
| :--- | :--- | :--- |
| PBR metallic-roughness material | `pbr_shader.c` → `get_pbrm_material` | new `pbr_program` in av_renderer |
| PBR specular-glossiness material | `get_pbrs_material` | same program |
| GGX normal distribution | `get_distribution` | same |
| Smith visibility (GGX) | `get_visibility` | same |
| Schlick Fresnel w/ F90 | `get_fresnel` | same |
| IBL split-sum (brdf LUT + prefiltered cubemaps) | `get_ibl_shade` | same + cubemap texture support |
| Tangent-space normal mapping (TBN + `tangent.w`) | `get_normal_dir` | vertex + fragment GLSL |
| DSM shadow mapping (light-VP pass, 0.05·(1−N·L) bias) | `shadow_*` shaders + `is_in_shadow` | shadow FBO pass + depth sampler |
| Skybox | `skybox_shader.c` | skybox program |
| ACES tone mapping | end of PBR/Blinn shaders | reuse ruby's existing ACES in PostFX |

### Port to C++/runtime (adapted — ruby already has the *data* side)

| Algorithm | Vendor source | Note |
| :--- | :--- | :--- |
| Skeletal animation evaluator | `skeleton.c` + `quat_slerp`/`mat4_from_trs`/`mat4_from_quat` (maths.c) | ruby already imports glTF skins/anims (gltf_import.cpp) and has a CPU POD skinner (pod_loader.h); the vendor's joint-matrix evaluator is the reference for a GPU-skinning path (joint UBO, vertex-shader 4-bone blend) |
| glTF2 render + viewer | **no native C glTF loader** — Python pipeline (`scripts/utils/gltf.py`) only | ruby's `gltf_import.cpp` is the better base (already parses skins, animations, IBM, JOINTS_0/WEIGHTS_0); the vendor contributes shading + skinning math only |
| HDR (Radiance) reader | `image.c` | genuinely useful — ruby/filerift has no .hdr reader; needed for IBL environment input |

### Skip entirely (coupled to the rasterizer, or redundant)

`graphics.c` (rasterizer), `framebuffer_*`, `platforms/`, `main.c`, `scenes/`, `tests/`, `draw2d.c`,
`darray.c` (ruby has std::vector), `texture.c` (software sampling), `cache_helper.c` (ruby has
asset caches), `scripts/` Python pipeline (ruby's native import supersedes it).

### IBL external dependency

Filament `cmgen` (bundled binaries, Apache-2.0) pre-filters HDR → irradiance + prefiltered
specular cubemaps. The split-sum BRDF LUT can be generated procedurally (~50 lines) or taken
from cmgen output.

### Ruby-side changes required

1. `GPUMesh`/`upload_mesh`: add optional tangent (vec4), joint (vec4), weight (vec4) attributes
   + cubemap texture support.
2. `av_renderer.cpp`: `pbr_program`, skybox program, shadow pass (reuse existing FBO infra),
   joint-matrix UBO for vertex-shader skinning.
3. `gltf_import.cpp`: expose *runtime* materials (factors + texture refs) and a runtime
   skeleton for the viewer (currently a Blender round-trip tool only).
4. cmgen environment pipeline (offline, once per environment).

### Effort (algorithm-only, additive — PostFX untouched)

PBR + tangent normal mapping ≈ 3–4 d · IBL ≈ 2–3 d · DSM ≈ 1–2 d · glTF skinned viewer
≈ 3–4 d → **≈ 10–13 focused days**, all additive to ruby's existing GPU stack.

---

*Prepared for the Ruby SDK roadmap. Source of truth: `renderer-master/` + this repo's
`src/tools/av_renderer.*`, `src/tools/scene_loader.*`, `docs/graphics_and_rendering/*`.*
