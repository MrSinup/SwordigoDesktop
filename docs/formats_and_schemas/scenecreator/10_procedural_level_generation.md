# 10. Procedural Level Generation in Swordigo

> Evidence-based analysis of how the vanilla engine builds level geometry at
> runtime, and how Ruby's `scene_generator` reproduces the same technique from
> plain `.scene` data.

---

## 1. The core insight: Swordigo is NOT pre-baked

Vanilla Swordigo scenes contain **almost no baked vertex data**. A typical
`GroundMesh` object in a shipped scene carries only:

| Field | Meaning |
|-------|---------|
| `MeshType : 0` | flat or profile-driven mesh |
| `RandomSeed : 12345` | deterministic noise seed |
| `HorizNoise : 1.5` | horizontal noise amplitude |
| `TextureMapping` | `{tex_id, u0, v0, u1, v1}` UV rect |
| `TextureName : "g_"` | texture atlas prefix (see `TextureName : 'g_'`) |
| 0–2 `Path` arrays | optional 2D profile points |

The **entire triangle soup is generated at load time** by the engine's
`GroundMeshGenerator` (`Caver/GroundMeshGenerator/`). Ruby's old loader
re-implemented this for display; `scene_generator.cpp` re-implements it for
**authoring**.

---

## 2. The vanilla generation pipeline (from decompiled engine)

`GroundMeshGenerator::GenerateMesh(...)` runs, in order:

1. **InitializeMeshBuilder** — set up the index/vertex buffers and UV scale
   from `TextureMapping`.
2. **GenerateSurfaceMesh** — walk the profile path:
   - if `MeshType == 0` and no profile: emit a flat strip at `Y = surfaceY`
     between `X ∈ [x0, x1]`.
   - otherwise: sample the profile polyline (X,Y pairs), applying
     **HorizNoise** as a deterministic pseudo-random y-offset.
3. **InsertRoundHatVertices** — for hat profiles, insert the rounded-cap
   vertices that close the ends of the surface.
4. **GenerateSurfaceMeshWithRoundHat** — build the top cap fan triangulation
   (used by `MeshType 1`, the "hat" meshes like floating islands).
5. **GenerateFrontMesh** — extrude the surface down to `MaxDepth` and
   triangulate the front strip (the visible cliff face).
6. **TexCoordForPosition** — assign atlas UVs from the `TextureMapping` rect,
   tiling with the surface length.

`RandomSeed` feeds the engine's `Random()` which is a **seeded LCG**, so the
same seed + params → identical mesh, every time. This is why the game can
store seeds instead of vertices.

> ⚠️ The exact LCG constants were not fully recovered from the decompile —
> Ruby uses its own deterministic hash-noise (`Hash1D`) so meshes are
> **structurally** identical (same silhouette families) but not bit-identical
> to the vanilla seed → mesh mapping. Documented as **Observed/Inferred**.

---

## 3. Why this design is genius (and what we copy)

- **Tiny files**: a whole level's terrain = a few hundred bytes of paths +
  seeds instead of megabytes of vertices.
- **Deterministic**: same scene file renders identically on all devices —
  critical for the mobile GPUs of the era.
- **Data-driven tuning**: designers tweak `HorizNoise`, `HatHeight`,
  `HatWidthOffset1/2`, `MinDepth`, `MaxDepth` without touching code.
- **Reproducible authoring**: a tool can generate a scene, the engine
  generates the mesh — no format drift.

Ruby's `scene_generator` keeps this philosophy: **it writes the same compact
component bundles** the engine understands, so generated scenes run in
vanilla-compatible contexts.

---

## 4. Heightfield & noise synthesis (Ruby approach)

`scene_generator` replaces the engine's in-memory mesh builder with a
**2D heightfield** stage, then derives profile paths from it:

```
HeightField grid (size x size, spacing step)
    y(x) = base + amp1 * hashNoise(x * f1, seed)
                + amp2 * hashNoise(x * f2, seed ^ 0x9e37)
                + fbm(x, seed)           // optional multi-octave
```

- `hashNoise` = deterministic integer hash → [0,1), no external deps.
- `fbm` = 3 octaves of `hashNoise` at frequencies 1, 2, 4.
- `randomize(c)` = move every point by `±c * hashNoise(i, seed^salt)`.

**Heightfield → path**: sample the terrain at `step` intervals to produce the
`Path` polyline; then `Path → GroundMesh` (profile mesh) or
`Path → SurfaceMesh + round hat` (island). The same heightfield drives
`MakePlatform`, `MakeHills`, and the auto-bounds computation — one source of
truth, three outputs.

---

## 5. Scene-level composition (what a generated scene contains)

`GenerateTerrainScene` produces a **complete, loadable scene**:

| Object | Purpose |
|--------|---------|
| `Background` | `BackgroundComponent`, atlas texture |
| `DirectionalLight` | `Light` triple: key (Type 2, I2), ambient (Type 1, I0.3), black fill (Type 4, I1) |
| `ground1..N` | `GroundPolygon` + `GroundMesh` + `GroundMeshGenerator` + `CollisionShape` + `TextureMapping` x2 (baked via boulder) |
| `water` (optional) | `WaterMesh` sheet at `rect.y` with `Material "water"` |
| `spawn_default` | `SpawnPoint` + `SpawnPointComponent` at first platform top + 56 |
| auto `Bounds` | root field 3 = `(minX - pad, minY - pad, w + 2·pad, h + 2·pad)` |`ValidateTerrainScene` (implemented as `sgen::validate_scene`) re-checks: bounds cover the ground AABB, spawn is above the highest platform, water is below it, light and background are present.

---

## 6. Why this matters for the Scene Creator

- **Docs 06 (templates)** list what a scene *needs*; this doc explains the
  *engine algorithm* behind `GroundMeshGenerator` so authors understand what
  `RandomSeed`/`HorizNoise` actually do.
- **`scene_generator`** is the authoring twin: type a seed, get terrain —
  the way the original designers worked, minus the closed tooling.

---

## 7. Open questions / Unknown

| Question | Status |
|----------|--------|
| Exact LCG constants of `Random()` | **Unknown** — see 2 |
| `HatWidthOffset1/2` exact semantics | **Inferred** (shoulder widths) |
| Whether `TextureMapping` v-flip matters for `GenerateFrontMesh` | **Unknown** |
| `MeshType` full enum (0,1 seen; others?) | **Unknown** — only 0/1 observed |
