# 09 — The Engine's Ground-Mesh Generator (Reverse-Engineered)

> This is the heart of Swordigo's "procedural" level construction. Every shipped
> level is built from **GroundMeshGeneratorComponent** bundles (2,223 across all
> 118 scenes). The engine can regenerate the visible mesh from the component at
> load time — the shipped scenes carry both the baked mesh **and** the generator
> parameters. Decompiled sources are archived in
> [`docs/scenecreator/source/`](source/).

---

## 1. The call graph (Observed — decompiled symbols)

Entry point: `Caver::GroundMeshGeneratorComponent::GenerateMesh()` (@0x2BC4E8)

```
GenerateMesh()
 ├─ InitializeMeshBuilder(bool)                      @0x202FF0 / @0x2B9E5C
 │    (clears Caver::MeshBuilder; rebuild vertex/normal/texcoord/index data)
 ├─ GenerateSurfaceMesh(int, Vector2 const*, float const*, float const*,
 │                      TextureMapping const*)       @0x1F7420 / @0x2B9A48
 │    — plain surface (MeshType 0) or:
 ├─ GenerateSurfaceMeshWithRoundHat(f,f,f,f)         @0x1F8340 / @0x2BB010
 │    — surface with round-hat domes (MeshType 1)
 │     ├─ InsertRoundHatVertices(Vector2, f, f, Vector3, Vector3, f)  @0x1FDEB0 / @0x2BA45C
 │     ├─ InsertCapForRoundHat(bool, Vector2, f, f, Vector3, Vector3, f) @0x1FF330 / @0x2BA724
 │     └─ Polygon::VertexAtIndexIsConvexCCW(int)     @0x1F9E50
 │         Vector2::Normalize / Vector3::Normalize / sqrtf
 ├─ GeneratePlainSurfaceWithHatGaps(float)           @0x1FE290 / @0x2BAC60
 │    — surface with gaps where hats sit (used with InsertPlainSurfaceVertices @0x202930)
 └─ GenerateFrontMesh()                              @0x1FD800 / @0x2BB564
      ├─ TriangulatePolygon(Vector2 const*, int, ushort*)  @0x1F5150 / @0x2C1CC4
      │    (ear-clipping triangulation of the source polygon)
      └─ TextureMapping::TexCoordForPosition(Vector3 const&)  @0x1FF490
```

Everything is emitted through `Caver::MeshBuilder`:
`MeshBuilder()`, `AddVertex(Vertex const&)` (@0x1F6B80/@0x2BA388),
`AddFace(int,int,int)` (@0x1F8200/@0x2BAADC), `GenerateMesh()` (@0x1F44E0),
`InitializePosition/InitializeNormal/InitializeTexCoordSet/InitializeIndices`,
`TransformVertices`, `InitWithMeshBuilderFaceSubset`, `SetNumTexCoordSets`.

### What the pieces do (Inferred from signatures + call graph)

| Function | Job |
|----------|-----|
| `GenerateSurfaceMesh` | Builds the flat **top surface** mesh of the ground polygon (the walkable area) at constant depth, textured with the surface `TextureMapping`. |
| `GenerateSurfaceMeshWithRoundHat` | Same surface, but `InsertRoundHatVertices` stamps a ring of dome vertices for each round hat and `InsertCapForRoundHat` closes the dome top; `VertexAtIndexIsConvexCCW` decides where hats are inset. |
| `GeneratePlainSurfaceWithHatGaps` | Plain surface with *gaps* where hats will sit (alternative hat handling). |
| `GenerateFrontMesh` | **Front face**: ear-clips the polygon (`TriangulatePolygon`) and extrudes each triangle to the front depth band, textured with the front `TextureMapping` — this is the vertical strip the player sees. |
| `InitializeMeshBuilder` | Prepares the builder; `AddVertex`/`AddFace` accumulate; `GenerateMesh()` materializes the `Caver::Mesh`. |
| `TextureMapping::TexCoordForPosition` | Computes the UV for a world-space vertex — **UVs are derived from world position**, which is why `TextureMapping.Scale` (250) controls texel density. |

---

## 2. GroundMeshGeneratorComponent (the scene-side parameter bundle)

Component payload field **112** on the object's `GroundMeshGenerator` component.
Layout (Observed schema + decoded scenes):

| Field | Name | Meaning | Game range |
|-------|------|---------|-----------|
| 1 | `GroundPolygonId` | local id of the polygon the mesh is generated from | 100 / 980 |
| 2 | `TargetMeshId` | local id of the `GroundMesh` the result is written to | 101 / 981 |
| 3 | `FrontTextureMappingId` | local id of the front-face `TextureMapping` | 107 / 985 |
| 4 | `SurfaceTextureMappingId` | local id of the surface `TextureMapping` | 106 / 984 |
| 5 | `RandomSeed` | seed for any vertex noise | 0, 1291618994, 1573073931 |
| 6 | `HorizNoise` | horizontal displacement amplitude | 0 (1859×), 50 (209×), 250 (62×) |
| 7 | `MeshType` | 0 = plain, 1 = round-hat surfaces | 1 (1759×), 0 (464×) |
| 8 | `SurfaceWidth` | surface detail spacing | 20–250 (50, 100, 150, 120, 80… most common) |
| 9 | `HatHeight` | default round-hat height | 25 (1816×), 20, 40, 33 |
| 10 | `HatWidthOffset1` | hat profile width (start) | 5 (2223×) |
| 11 | `HatWidthOffset2` | hat profile width (end) | 5 (2223×) |

> **Note on the editor default:** `RandomSeed 1291618994` appears 344 times across
> scenes — it is the engine editor's default seed (also what the Ruby GMG bakes).
> `RandomSeed 0` (1652×) means "use a fresh/deterministic-zero seed".

---

## 3. The baked `GroundMesh` layout (what the generator writes)

A generated ground object carries (local ids in parentheses — the "980 bundle"):

```
GroundPolygon (980)      — polygon (f2), Collides (f3), MinDepth/MaxDepth (f4/f5)
GroundMesh    (981)      — LocalAabb (f7),
                           SurfaceMesh (f8, repeated):
                              top surface (top texture)
                              side walls  (front/bottom texture)
                              one SurfaceMesh per round hat (top texture)
                           FrontMesh (f9, non-indexed triangles, front texture)
                           Color (f10, RGBA 1,1,1,1)
GroundMeshGenerator (982) — params above
CollisionShape (983, ParentComponentIdentifier=980)
                           — ShapeComponent.Polygon (f120-f3), CollisionShapeComponent:
                             IsGround (f2=1), MinDepth (f6), MaxDepth (f7), Enabled (f11=1)
TextureMapping (984)      — surface texture, Scale 250
TextureMapping (985)      — front texture, Scale 250
```

### Vertex layout (Observed from `SurfaceMesh.Vertices`)

`ValueType 7` (float32×N), `ValuesPerVertex 3`, `Stride 32`, `DataOffset 0`.
The 32-byte vertex = **position (12 B) + normal (12 B) + UV (8 B)** — confirmed by
`DataOffset 12` on Normals and `DataOffset 24` on TexCoordSet in every scene.
Indices: `ValueType 4` (uint16), `Stride 2`.

---

## 4. Round hats (the domes)

`InsertRoundHatVertices(Vector2 center, float a3, float a4, Vector3 u, Vector3 v, float a7)`
builds a dome on the surface:
- A **profile ring** of vertices standing on the top surface plane (`base + 0.05`
  to avoid z-fighting — same trick in boulder).
- The dome spans the full Min..Max depth like the surface, so it reads as a
  rounded bump the hero can walk over.
- `InsertCapForRoundHat` closes the top of the dome.

Real examples: `plains_part1` ground objects use `HatHeight 30-40`; `new_level.scene`
uses a 60-radius / 25-height dome.

---

## 5. Determinism

Because `RandomSeed` is stored per component and the mesh is regenerated from it,
**the same seed + same polygon + same params → the same mesh**. The Ruby generator
honors this (see [12](12_generation_algorithms.md)): the same blueprint + seed
produces byte-identical scenes.

---

## 6. Archived decompiled sources

`docs/scenecreator/source/` contains 69 IDA/Hex-Rays `.c` files, including:
- `GroundMeshGenerator`: `GenerateSurfaceMesh.c`, `GenerateSurfaceMeshWithRoundHat.c`,
  `GeneratePlainSurfaceWithHatGaps.c`, `GenerateFrontMesh.c`,
  `InsertRoundHatVertices.c`, `InsertCapForRoundHat.c`, `InsertPlainSurfaceVertices.c`
- `MeshBuilder`: `AddVertex.c`, `AddFace.c`, `GenerateMesh.c`, `Initialize*.c`
- `TriangulatePolygon.c`
- `Proto/GroundMeshGeneratorComponent/*.c`

These are **generated evidence** (IDEA decompiler output), not original source.
