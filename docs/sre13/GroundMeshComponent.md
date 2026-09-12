# GroundMeshComponent (`Caver::GroundMeshComponent`)

## Summary

`GroundMeshComponent` is the terrain component: it owns two collections of
`Caver::Mesh` objects — **surface meshes** (the walkable ground, added via
`AddSurfaceMesh`) and **front meshes** (the decorative/background layer in
front of objects, added via `AddFrontMesh`). The Load creates each `Mesh`
(0x170 bytes) inside a 0x18-byte control block (vtable `off_6240F8`,
weak+strong refcount `0x100000001` at +8, `Mesh*` at +16) and calls
`Mesh::LoadFromProtobufMessage` per mesh. It also stores a bounds/offset
vector, a color, and a flag.

Allocation size: **0xD0** (`operator new(0xD0)` in `Create`). Vtable at
**0x623F18**.

## Struct layout

64-bit verified from ctor (`0x3A39E0`), `LoadFromProtobufMessage`
(`0x3A3D6C`), `Create`. (Vector layout inferred from size + Add*Mesh names.)

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x00 | 0x68 | — | `Component` base | See Component.md. |
| 0x68 | 24 | `std::vector<shared_ptr<Mesh>>` | `surfaceMeshes` | `{begin, end, cap}` — zeroed by ctor, filled by `AddSurfaceMesh` per proto mesh. **Inferred layout.** |
| 0x80 | 24 | `std::vector<shared_ptr<Mesh>>` | `frontMeshes` | Filled by `AddFrontMesh` (proto msg+152/160). **Inferred layout.** |
| 0x98 | 16 | — | (bounds/offset) | Load copies an OWORD from the proto's sub-message (msg+88, bytes at +12). **Inferred: world offset or AABB.** |
| 0xA8 | 16 | `FloatColor` | `color` | Ctor default (1,1,1,1) (`FMOV V0.4S, #1.0`); Load from proto `FloatColor` (msg+208). Terrain tint. |
| 0xB8 | 1 | `bool` | (unknown) | Ctor `= 0`; Load from proto byte (msg+216). **Inferred: castsShadow / isFrontLayer.** |
| 0xB9 | 3 | — | (unknown) | — |
| 0xBC | 16 | — | (default (1,1,1,1)) | Ctor long-double default (`FMOV V0.4S, #1.0`). Second color-ish vector. |
| 0xCC | 4 | — | (end) | Size 0xD0. |

## Vtable (0x623F18)

33-slot `Component` layout; overridden: dtor, `Clone`, `Load`/`Save`,
`ShouldSave`, **`Process` (+0x38 — overridden)**, bindings (+0x70..+0x90),
`componentCategories` (+0x98), `ImplementsInterface`/`RegisterInterfaces`
(+0xA0/+0xA8), `localAABB` (+0xE0 — overridden; the terrain's bounds feed
culling). `Prepare` stays base.

## Exported functions

### GroundMeshComponent::Create()
- Mangled: `_ZN5Caver19GroundMeshComponent6CreateEv`
- Behavior: `operator new(0xD0)` + ctor. Size anchor.
- Confidence: **verified**.

### GroundMeshComponent::LoadFromProtobufMessage(Proto::Component const&)
- Mangled: `_ZN5Caver19GroundMeshComponent23LoadFromProtobufMessageERKNS_5Proto9ComponentE` (0x3A3D6C).
- Behavior: `Component::LoadFromProtobufMessage`; for each proto mesh in the
  surface list (msg+96/+104) and the front list (msg+152/+160): allocate a
  `Mesh` (0x170) + control block, `Mesh::LoadFromProtobufMessage`, then
  `AddSurfaceMesh`/`AddFrontMesh`; then the +0x98 vector, the color (+0xA8)
  and the +0xB8 flag.
- Side effects: owns the meshes (released via the control-block refcount
  when the component is destroyed or reloaded).
- Confidence: **verified** (offsets); vector locations inferred.

## Open questions / unresolved offsets

- `AddSurfaceMesh`/`AddFrontMesh` bodies weren't read — the exact vector
  offsets (+0x68/+0x80) and the shared_ptr control-block shape come from the
  Load allocation and are inferred.
- The +0xBC default (1,1,1,1): a second color (e.g. back-face tint) or a
  scale — unconfirmed.
- `Caver::Mesh` (0x170 bytes) is its own class needing a dedicated doc.

## Proposed SRE hooks

- `GroundMeshComponent_GetMeshCount(comp)` / `_GetSurfaceMesh(comp, i)` —
  enumerate terrain meshes for a "wireframe the level" debug view.
- Color hook: `GroundMeshComponent_SetColor(comp, r,g,b,a)` — tint terrain
  live for level-design overlays.
- **Safe**: never mutate the mesh vectors directly; the meshes are
  refcounted and freed by the control block — always go through
  `AddSurfaceMesh`/`AddFrontMesh` once their addresses are recovered.