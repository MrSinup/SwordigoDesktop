# Feasibility Study: Raylib 6.0 Code Extraction for Ruby GG 3D Workspace

## 1. Executive Summary

Ruby GG (the studio tool and 3D scene/asset editor for Swordigo Desktop) currently operates on a hybrid architecture:
- **Qt6 (`QOpenGLWidget`)** for the main IDE chrome, docking, and viewport frame.
- **`av_renderer`** for OpenGL 3.3 Core mesh rendering (PBR, shaders, post-processing).
- **`RubyGizmo` / `scene_workspace`** for custom gizmos, ground-mesh editing, and transform math.

While Godot was evaluated, it is monolithic, heavy, and hard to embed into a lightweight specialized tool without dragging in an entire engine runtime. 
**Raylib 6.0** (`/home/quantumcreeper/SwordigoDesktop/raylib-6.0`) presents the exact opposite: **modular, self-contained, header-only or single-file C99/C++ libraries with zero runtime baggage**.

We do **NOT** compile or link Raylib as a whole library (which would bring windowing, audio, platform drivers, and duplicate GL contexts). Instead, we cherry-pick and integrate the standalone mathematical, raycasting, and geometric kernels directly into Ruby GG.

---

## 2. What's in Raylib 6.0: Bloat vs. Pure Gold

| Module / File | Size | Role | Verdict for Ruby GG |
| :--- | :--- | :--- | :--- |
| **`raymath.h`** | ~3,140 lines | Single-header 3D Math (Vector2/3/4, Matrix 4x4, Quaternion, Ray, BoundingBox, Transform) | **Pristine Gold**: Zero dependencies, C++ friendly, optional SIMD/SSE intrinsics. Complete solution for rotations and quaternions. |
| **`rcamera.h`** | ~563 lines | Camera control math & basis vectors (Orbital, Free-fly WASD, First/Third-person) | **Pristine Gold**: Standalone mode (`RCAMERA_STANDALONE`), full basis extraction (`Forward`, `Up`, `Right`), Euler yaw/pitch/roll clamping. |
| **`rmodels.c` (Ray Collision)** | ~300 lines | Exact 3D intersection math (`GetRayCollisionBox`, `GetRayCollisionSphere`, `GetRayCollisionTriangle`, `GetRayCollisionMesh`) | **Pristine Gold**: Möller–Trumbore ray-triangle tests, slab AABB tests. Essential for pixel-perfect picking. |
| **`rmodels.c` (Primitives)** | ~400 lines | Wireframe / procedural primitive drawing (`DrawCubeWires`, `DrawSphereWires`, `DrawCylinderWires`, `DrawCircle3D`, `DrawGrid`) | **High Value**: Can be adapted into immediate debug render buffers for gizmos and collision bounds. |
| **`external/par_shapes.h`** | ~2,156 lines | Single-header parametric 3D mesh generator, platonic solids, tube/torus/cone, mesh weld/boolean | **High Value**: Perfect for generating collision hulls, trigger zones, and gizmo mesh geometry (torus rings, arrows). |
| **`external/cgltf.h`** | ~4,500 lines | Single-header glTF 2.0 parser & exporter | **Already in Ruby / High Value** for scene export/import. |
| **`rcore.c`, `rglfw.c`, `platforms/`** | 10,000+ lines | Window creation, event loop, input polling | **Bloat — Discard**. Ruby already uses Qt6 / SDL3. |
| **`raudio.c`, `miniaudio.h`** | 15,000+ lines | Sound engine, streaming, audio decoders | **Bloat — Discard**. Ruby already has audio tools. |
| **`rlgl.h`** | ~5,400 lines | Immediate-mode OpenGL abstraction layer | **Optional / Partially Redundant**: `av_renderer` and `QOpenGLFunctions` already talk directly to GL 3.3 Core. |

---

## 3. Current Pain Points in Ruby GG vs. Raylib Solutions

### Problem A: Rotations & Gimbal Lock in `RubyGizmo`
- **Current State**: `RubyGizmo` and `scene_workspace.cpp` represent rotations as raw 3-element Euler float arrays with engine convention $R_x \cdot R_y \cdot R_z$. When dragging rotation rings:
  ```cpp
  // Current: manual 3x3 delta matrix multiplication
  float combined[9];
  for (int r2=0;r2<3;r2++) for (int c=0;c<3;c++) { ... }
  rotation_matrix_to_euler(combined, rot_deg);
  ```
  When the pitch approaches $\pm 90^\circ$, `asin(m[0*3+2])` encounters numerical instability and gimbal flip. Local/World switching is cumbersome.
- **Raylib Solution**:
  - `raymath.h` provides complete **Quaternion** support:
    - `QuaternionFromEuler(pitch, yaw, roll)` / `QuaternionToEuler(q)`
    - `QuaternionMultiply(q1, q2)`
    - `QuaternionSlerp(q1, q2, t)` / `QuaternionNlerp(q1, q2, t)`
    - `QuaternionFromAxisAngle(axis, angle)`
    - `QuaternionFromMatrix(mat)` / `QuaternionToMatrix(q)`
  - **Result**: In the rotation gizmo, user mouse movement on a ring translates directly to an angle $\Delta \theta$ around handle axis $\vec{u}$. We construct $\Delta q = \text{QuaternionFromAxisAngle}(\vec{u}, \Delta \theta)$ and multiply $q_{\text{new}} = \Delta q \cdot q_{\text{start}}$. Gimbal lock is completely eliminated during interaction.

### Problem B: Scene Object & Ground-Mesh Picking
- **Current State**: `scene_workspace.cpp` uses 2D screen-space projection distance hacks (`world_to_screen` + pixel distance thresholds). This produces false positives when objects overlap in depth, and makes picking rotated or non-axis-aligned meshes unreliable.
- **Raylib Solution**:
  - Extract `GetRayCollisionBox()` (branchless slab method) and `GetRayCollisionTriangle()` (Möller–Trumbore algorithm).
  - Unproject mouse coordinates into a true 3D `Ray = { origin, direction }`.
  - Pass 1: Test `Ray` vs. `BoundingBox` of all scene objects $\rightarrow$ sort by distance $t$.
  - Pass 2: For candidate ground meshes or detailed models, test `Ray` vs. triangles using `GetRayCollisionTriangle()`.
  - **Result**: Sub-millimeter accurate picking, ray-to-surface hit point, and surface normal identification for auto-aligning placed objects to ground slopes!

### Problem C: Transform Gizmo Handle Geometry
- **Current State**: `ruby_gizmo.cpp` draws handles using GL line loops and quad fans. It lacks 3D solid handles (cones for arrows, boxes for scale, tori for rotation rings).
- **Raylib Solution**:
  - Use `par_shapes.h` or Raylib's primitive generators to bake compact VBOs for:
    - Translation arrow: Cylinder shaft + Cone head.
    - Scale handle: Cylinder shaft + Cube head.
    - Rotation ring: Smooth Torus slice or circle strip.
  - Raycast picking of gizmo handles uses `GetRayCollisionBox()` on the handle tips and `GetRayCollisionSphere()` / plane-project on the rings.
  - **Result**: Industry-standard, solid-shaded gizmo handles matching Blender / Unreal / Unity.

### Problem D: Camera Navigation & Multi-Mode Viewport
- **Current State**: `Viewport3DWidget` only supports orbit navigation (`yaw`, `pitch`, `dist`, `target`). You cannot fly through large outdoor scenes or underground dungeons.
- **Raylib Solution**:
  - `rcamera.h` includes pre-built controllers:
    - `CAMERA_ORBITAL`: Turntable around selected object (existing, but enhanced with smooth zoom & frame selection).
    - `CAMERA_FREE`: Blender-style fly-through (Hold RMB + WASD + Shift to sprint, mouse look).
    - `CameraMoveForward`, `CameraMoveRight`, `CameraMoveUp` with delta time.
  - `GetCameraViewMatrix()` and `GetCameraProjectionMatrix()` are standardized.

---

## 4. Proposed Architecture: `src/ruby/math/` & `src/ruby/3d/`

Rather than copying files indiscriminately, we organize the extraction into clean, focused headers:

```
src/ruby/
├── math/
│   ├── ruby_math.h         # raymath.h v2.0 (renamed/namespaced or directly included with RL prefixes)
│   └── ruby_camera_math.h  # rcamera.h standalone navigation math
└── viewport/
    ├── ruby_gizmo.h        # Enhanced with Quaternions & Raylib hit tests
    ├── ruby_gizmo.cpp
    ├── ruby_picking.h      # Möller-Trumbore ray-triangle & slab AABB raycasting
    ├── ruby_picking.cpp
    ├── viewport_3d_widget.h
    └── viewport_3d_widget.cpp
```

### Type Bridge (Qt6 $\leftrightarrow$ Raylib Math):
```cpp
namespace ruby::math {
    using Vec2 = Vector2;
    using Vec3 = Vector3;
    using Vec4 = Vector4;
    using Quat = Quaternion;
    using Mat4 = Matrix;
    using Ray  = ::Ray;
    using AABB = BoundingBox;

    // Zero-copy conversions to/from Qt
    inline QVector3D to_qt(const Vec3& v) { return QVector3D(v.x, v.y, v.z); }
    inline Vec3 from_qt(const QVector3D& v) { return Vec3{v.x(), v.y(), v.z()}; }
}
```

---

## 5. Summary of What Can Be Made Better

1. **True Quaternion Transform Pipeline**:
   - Zero gimbal lock.
   - Smooth rotation interpolation (Slerp) for animated scene objects.
   - Local vs. World space rotation handles toggle with zero math degradation.
2. **Universal Gizmo**:
   - Combine Move + Rotate + Scale into a single active transform widget (Blender Universal Gizmo).
3. **Raycast Snapping to Surface**:
   - When translating objects, raycast against the ground terrain mesh: objects snap flush to the terrain surface and orient along the hit normal automatically.
4. **Blender "Fly Mode" (Shift + ~)**:
   - Effortless navigation through complex multi-room Swordigo dungeon scenes.
5. **Procedural Geometry Primitives**:
   - Instantly create trigger volumes, spawn zones, light influence spheres, and bounding hulls without authoring POD files in Blender.
