"""rubyforge.core.ir — RubyForge Intermediate Representation.

A plain-Python, zero-dependency data model for Swordigo POD models.

It is a 1:1 mirror of the native structs in `src/tools/pod_loader.h`:

    PODMesh, BoneBatch, PODMaterial, PODNode, PODModel

Because every field mirrors the C++ structs, `core/pod_reader.py` and
`core/pod_writer.py` can round-trip through the same IR the native
`av::pod_parse` / `av::pod_write` use — which is what the Stage-1
differential test suite validates.

Matrices are flat lists of 16 floats in **column-major** order
(``[c * 4 + r]``), exactly as the engine stores them.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Optional

# Column-major 4x4 identity, matching av::PODMesh.unpack_matrix / PODNode.matrix.
IDENTITY_MAT4 = [1.0, 0.0, 0.0, 0.0,
                 0.0, 1.0, 0.0, 0.0,
                 0.0, 0.0, 1.0, 0.0,
                 0.0, 0.0, 0.0, 1.0]


@dataclass
class BoneBatch:
    """PowerVR bone-batch tables (tags 6015-6019).

    Historical PVRGeoPOD export optimisation that splits bone matrices into
    local registers for early PowerVR mobile GPUs. Stock Swordigo PODs rarely
    use it; kept for round-trips of 3ds Max exported models.
    """
    indices: List[int] = field(default_factory=list)
    counts: List[int] = field(default_factory=list)
    offsets: List[int] = field(default_factory=list)
    max_bones: int = 0
    count: int = 0


@dataclass
class PODMesh:
    # Geometry — flat arrays ("vec3 per vertex", so len = num_vertices * 3).
    positions: List[float] = field(default_factory=list)   # xyz
    normals: List[float] = field(default_factory=list)     # xyz
    uvs: List[float] = field(default_factory=list)         # uv (2 per vertex)
    tangents: List[float] = field(default_factory=list)    # xyz+w (4 per vertex)
    indices: List[int] = field(default_factory=list)       # triangle index list

    # Skinning. Each vertex contributes ``bones_per_vertex`` floats.
    bone_indices: List[float] = field(default_factory=list)
    bone_weights: List[float] = field(default_factory=list)
    bones_per_vertex: int = 0

    bone_batches: BoneBatch = field(default_factory=BoneBatch)
    has_bone_batches: bool = False

    # MeshUnpackMatrix (6020): column-major 4x4 that unpacks packed vertex
    # data. Identity in all stock Swordigo PODs (verified); preserved for
    # round-trips of externally exported models.
    has_unpack_matrix: bool = False
    unpack_matrix: List[float] = field(default_factory=lambda: list(IDENTITY_MAT4))
    mesh_type: int = 0      # 0 triangles, 1 quads, 2 lines

    num_vertices: int = 0
    num_faces: int = 0

    # Per-mesh AABB (populated by the reader).
    min_x: float = 1e9
    min_y: float = 1e9
    min_z: float = 1e9
    max_x: float = -1e9
    max_y: float = -1e9
    max_z: float = -1e9


@dataclass
class PODMaterial:
    name: str = ""
    diffuse_texture_index: int = -1   # index into PODModel.texture_filenames
    opacity: float = 1.0              # matOpacity (3002)
    diffuse: List[float] = field(default_factory=lambda: [1.0, 1.0, 1.0])


@dataclass
class PODNode:
    name: str = ""
    object_index: int = -1   # index into meshes if it is a mesh node
    parent_index: int = -1   # index into nodes (-1 = root)
    material_index: int = -1

    # Static (deprecated) transform fields.
    has_matrix: bool = False
    matrix: List[float] = field(default_factory=lambda: list(IDENTITY_MAT4))

    has_translation: bool = False
    translation: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])

    has_rotation: bool = False
    rotation: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0, 1.0])

    has_scale: bool = False
    scale: List[float] = field(default_factory=lambda: [1.0, 1.0, 1.0])

    # Animation keyframes — full arrays, one key per frame.
    anim_translation: List[float] = field(default_factory=list)   # 3 * frames
    anim_rotation: List[float] = field(default_factory=list)      # 4 * frames
    anim_scale: List[float] = field(default_factory=list)         # 3 or 7 per key
    anim_matrix: List[float] = field(default_factory=list)        # 16 * frames

    # Optional sparse keyframe index arrays.
    anim_translation_idx: List[int] = field(default_factory=list)
    anim_rotation_idx: List[int] = field(default_factory=list)
    anim_scale_idx: List[int] = field(default_factory=list)
    anim_matrix_idx: List[int] = field(default_factory=list)

    anim_flags: int = 0

    # TRUE bind (rest) pose — captured by the animation merge; used by
    # CPU skinning. Empty (identity / False) in plain non-merged files.
    has_bind_matrix: bool = False
    bind_matrix: List[float] = field(default_factory=lambda: list(IDENTITY_MAT4))

    @property
    def is_bone(self) -> bool:
        return self.name.startswith("Bone")

    @property
    def is_center_point(self) -> bool:
        return self.name == "CenterPoint"
@dataclass
class PODModel:
    meshes: List[PODMesh] = field(default_factory=list)
    nodes: List[PODNode] = field(default_factory=list)
    materials: List[PODMaterial] = field(default_factory=list)
    texture_filenames: List[str] = field(default_factory=list)

    version: str = ""
    num_frames: int = 0
    fps: float = 30.0
    num_mesh_nodes: int = 0   # first num_mesh_nodes nodes are mesh nodes

    # Whole-model geometry.
    center_x: float = 0.0
    center_y: float = 0.0
    center_z: float = 0.0
    radius: float = 1.0
    center_point: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    has_center_point: bool = False

    total_vertices: int = 0
    total_faces: int = 0

    min_x: float = 1e9
    min_y: float = 1e9
    min_z: float = 1e9
    max_x: float = -1e9
    max_y: float = -1e9
    max_z: float = -1e9

    uv_v_flipped: bool = False

    # Opaque metadata-options strings (stock tags 1002 / 1003) preserved on
    # read for future re-emission. Not written back by the Stage-1 writer.
    loader_options: str = ""
    loader_options2: str = ""

    # Non-fatal parse diagnostics collected by the reader.
    warnings: List[str] = field(default_factory=list)

    # ─── Convenience accessors used by Blender operators & tests ──────
    @property
    def center_point_vector(self) -> Optional[List[float]]:
        return list(self.center_point) if self.has_center_point else None

    def node_by_name(self, name: str) -> Optional[PODNode]:
        for node in self.nodes:
            if node.name == name:
                return node
        return None


def copy_mat4(m: List[float]) -> List[float]:
    """Return a fresh copy of a column-major 4x4 matrix (16 floats)."""
    return list(m)


def is_identity_mat4(m) -> bool:
    return all(a == b for a, b in zip(m, IDENTITY_MAT4))