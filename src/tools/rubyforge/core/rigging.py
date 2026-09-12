"""rubyforge.core.rigging — Stage 2: rigging, skinning & pivot semantics.

Pure (Blender-free) math for the Swordigo skeletal contract, decoded from
``libswordigo.so`` (research report §9; C++ golden reference
``src/tools/pod_loader.cpp``):

  * A node is a skeletal bone **iff** its name starts with the literal
    ``"Bone"`` (``Caver::PODLoader::CreateSkeleton``). Anything else is
    silently ignored by the runtime.
  * The ``CenterPoint`` node's OWN LOCAL translation is the model pivot
    (the engine does NOT walk the parent chain for it).
  * Runtime skinning is rigid: exactly one bone influence per vertex
    (``C_Matrix4Vector3ArraySkin``); weights are discarded.

Blender <=> POD transform conversion uses basis conjugation:

    M_blender = C . M_pod . C^-1          (POD -> Blender)
    M_pod     = C^-1 . M_blender . C      (Blender -> POD)

with C the +90-degree-about-X basis change. Conjugating each bone's LOCAL
matrix is valid because the parent chain composes identically in both
spaces.
"""

from __future__ import annotations

import math
from typing import Dict, List, Optional, Sequence, Tuple

from .ir import PODModel, PODNode
from .transforms import (
    get_node_matrix,
    mat4_from_quat,
    mat4_identity,
    mat4_inverse,
    mat4_mul,
    pod_to_blender_basis,
)

__all__ = [
    "BONE_PREFIX", "CENTER_POINT_NAME",
    "BLENDER_TO_POD_MAT4", "POD_TO_BLENDER_MAT4",
    "auto_prefix_bone_name", "is_valid_bone_name", "bone_node_indices",
    "dominant_weight_quantize", "pod_local_matrix", "pod_local_to_trs",
    "mat4_to_quat_pod", "blender_local_to_pod", "pod_world_head",
    "node_local_matrix_at", "build_rig_spec", "center_point_spec",
]

BONE_PREFIX = "Bone"
CENTER_POINT_NAME = "CenterPoint"

# Basis-change matrices, column-major (m[c * 4 + r]).
#   POD -> Blender: (x, y, z) -> (x, -z, y)
POD_TO_BLENDER_MAT4 = [1.0, 0.0, 0.0, 0.0,
                       0.0, 0.0, 1.0, 0.0,
                       0.0, -1.0, 0.0, 0.0,
                       0.0, 0.0, 0.0, 1.0]
#   Blender -> POD: (x, y, z) -> (x, z, -y)   (exact inverse / transpose)
BLENDER_TO_POD_MAT4 = [1.0, 0.0, 0.0, 0.0,
                       0.0, 0.0, -1.0, 0.0,
                       0.0, 1.0, 0.0, 0.0,
                       0.0, 0.0, 0.0, 1.0]


# ---- Bone naming (libswordigo CreateSkeleton contract) ----------------
def is_valid_bone_name(name: str) -> bool:
    """True when the runtime will treat this node as a skeletal bone."""
    return bool(name) and name.startswith(BONE_PREFIX)


def auto_prefix_bone_name(name: str) -> str:
    """Ensure the ``Bone`` prefix the engine requires (Spine -> BoneSpine)."""
    name = (name or "").strip()
    if not name:
        return BONE_PREFIX
    if is_valid_bone_name(name):
        return name
    return BONE_PREFIX + name


def bone_node_indices(model: PODModel) -> List[int]:
    """Indices of every node the engine's CreateSkeleton would pick up."""
    return [i for i, node in enumerate(model.nodes) if is_valid_bone_name(node.name)]


# ---- Rigid skinning (report §9 dominant-weight quantization) -----------
def dominant_weight_quantize(bone_indices: Sequence[float],
                             bone_weights: Sequence[float],
                             bones_per_vertex: int,
                             vertex_count: int) -> Tuple[List[float], List[float]]:
    """Snap smooth multi-bone weights to one dominant influence per vertex.

    The Swordigo runtime evaluates exactly one bone index per vertex and
    ignores weights, so a Blender smooth rig must be rigidified before
    export: keep the strongest influence at weight 1.0 and drop the rest.

    Returns ``(bone_indices, bone_weights)`` with exactly one component per
    vertex (``bones_per_vertex`` collapses to 1).
    """
    if bones_per_vertex <= 0 or vertex_count <= 0:
        return [], []
    out_idx: List[float] = []
    for v in range(vertex_count):
        base = v * bones_per_vertex
        best_slot = 0
        best_weight = -1.0
        for k in range(bones_per_vertex):
            w = bone_weights[base + k] if base + k < len(bone_weights) else 0.0
            if w > best_weight:
                best_weight = w
                best_slot = k
        idx = bone_indices[base + best_slot] if base + best_slot < len(bone_indices) else 0.0
        out_idx.append(float(idx))
    return out_idx, [1.0] * len(out_idx)


def resolve_bone_node_index(mesh, raw_index: int) -> int:
    """Resolve a per-vertex bone reference to a scene node index.

    Mirrors ``av::skin_mesh``: when the mesh carries PowerVR bone batches
    (tags 6015-6019), the per-vertex value indexes the batch table first;
    only the table entries are true node indices. Without batches the raw
    value is already a node index.
    """
    if mesh.has_bone_batches and mesh.bone_batches.indices:
        table = mesh.bone_batches.indices
        if 0 <= raw_index < len(table):
            return int(table[raw_index])
    return int(raw_index)


# ---- POD node local transforms ----------------------------------------
def pod_local_matrix(node: PODNode) -> List[float]:
    """The node's LOCAL transform as a column-major 4x4 (POD convention).

    Mirrors ``get_node_matrix``'s static path: local = T . R . S.
    """
    if node.has_matrix:
        return list(node.matrix)
    t = mat4_identity()
    t[12], t[13], t[14] = node.translation[0], node.translation[1], node.translation[2]
    r = mat4_from_quat(node.rotation)
    s = mat4_identity()
    s[0], s[5], s[10] = node.scale[0], node.scale[1], node.scale[2]
    return mat4_mul(mat4_mul(t, r), s)


def mat4_to_quat_pod(m: Sequence[float]) -> List[float]:
    """Extract a POD-convention quaternion (xyzw) from a rotation matrix.

    The engine builds rotation matrices with negated xyz
    (``local_mat4_from_quat``), so the standard extraction result has its
    xyz negated on the way out to round-trip through ``mat4_from_quat``.
    """
    r00, r01, r02 = m[0], m[4], m[8]
    r10, r11, r12 = m[1], m[5], m[9]
    r20, r21, r22 = m[2], m[6], m[10]
    trace = r00 + r11 + r22
    if trace > 0.0:
        s = math.sqrt(trace + 1.0) * 2.0
        qw = 0.25 * s
        qx = (r21 - r12) / s
        qy = (r02 - r20) / s
        qz = (r10 - r01) / s
    elif r00 > r11 and r00 > r22:
        s = math.sqrt(1.0 + r00 - r11 - r22) * 2.0
        qw = (r21 - r12) / s
        qx = 0.25 * s
        qy = (r01 + r10) / s
        qz = (r02 + r20) / s
    elif r11 > r22:
        s = math.sqrt(1.0 + r11 - r00 - r22) * 2.0
        qw = (r02 - r20) / s
        qx = (r01 + r10) / s
        qy = 0.25 * s
        qz = (r12 + r21) / s
    else:
        s = math.sqrt(1.0 + r22 - r00 - r11) * 2.0
        qw = (r10 - r01) / s
        qx = (r02 + r20) / s
        qy = (r12 + r21) / s
        qz = 0.25 * s
    length = math.sqrt(qx * qx + qy * qy + qz * qz + qw * qw)
    if length > 1e-12:
        qx, qy, qz, qw = qx / length, qy / length, qz / length, qw / length
    # Negate xyz to match the engine's matrix-from-quaternion convention.
    return [-qx, -qy, -qz, qw]


def pod_local_to_trs(m: Sequence[float]) -> Tuple[List[float], List[float], List[float]]:
    """Decompose a POD-convention local matrix into (translation, quat, scale).

    Assumes no shear (true for every node transform the engine composes as
    T . R . S). Scale is the length of each rotation-matrix column.
    """
    translation = [m[12], m[13], m[14]]
    c0 = math.sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2])
    c1 = math.sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6])
    c2 = math.sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10])
    scale = [c0 or 1.0, c1 or 1.0, c2 or 1.0]
    rot = list(m)
    for col, length in ((0, scale[0]), (1, scale[1]), (2, scale[2])):
        if length > 1e-12:
            for r in range(3):
                rot[col * 4 + r] = m[col * 4 + r] / length
    return translation, mat4_to_quat_pod(rot), scale


def blender_local_to_pod(local_blender: Sequence[float]) -> List[float]:
    """Convert a Blender-space LOCAL matrix to the POD space (conjugation)."""
    return mat4_mul(mat4_mul(BLENDER_TO_POD_MAT4, list(local_blender)),
                    POD_TO_BLENDER_MAT4)


def pod_world_head(model: PODModel, node_index: int) -> List[float]:
    """The node's frame-0 world position, converted to Blender space."""
    world = get_node_matrix(model, node_index, 0.0)
    return pod_to_blender_basis([world[12], world[13], world[14]])


def node_local_matrix_at(model: PODModel, node_index: int,
                         frame: float) -> List[float]:
    """The node's LOCAL matrix at a frame (world . parent_world^-1)."""
    world = get_node_matrix(model, node_index, frame)
    node = model.nodes[node_index]
    if node.parent_index == -1 or node.parent_index == node_index:
        return list(world)
    parent_world = get_node_matrix(model, node.parent_index, frame)
    inv = mat4_inverse(parent_world)
    if inv is None:
        return list(world)
    return mat4_mul(inv, world)


# ---- Rig specification (Blender armature <-> POD nodes) ----------------
def _world_pod_to_blender(model: PODModel, node_index: int) -> List[float]:
    """Frame-0 world matrix conjugated into Blender space (armature rest)."""
    world = get_node_matrix(model, node_index, 0.0)
    return mat4_mul(mat4_mul(POD_TO_BLENDER_MAT4, world), BLENDER_TO_POD_MAT4)


def build_rig_spec(model: PODModel) -> Dict:
    """Describe the POD skeleton as a Blender-buildable rig specification.

    Returns a JSON-able dict::

        bones: [{name, node_index, parent, pod_local_matrix,
                 rest_blender, head, tail}]
        center_point: {name, translation} | None

    Heads/tails are in Blender space (Z-up); matrices stay in POD space.
    """
    bone_idx = bone_node_indices(model)
    heads: Dict[int, List[float]] = {i: pod_world_head(model, i) for i in bone_idx}

    children: Dict[int, List[int]] = {}
    for i in bone_idx:
        parent = model.nodes[i].parent_index
        if parent in heads:
            children.setdefault(parent, []).append(i)

    specs = []
    for i in bone_idx:
        node = model.nodes[i]
        head = heads[i]
        kids = children.get(i, [])
        if kids:
            tail = list(heads[kids[0]])
        else:
            tail = [head[0], head[1] + 1.0, head[2]]
        if tail == head:
            tail = [head[0], head[1] + 1.0, head[2]]
        specs.append({
            "name": node.name,
            "node_index": i,
            "parent": model.nodes[node.parent_index].name
            if node.parent_index in heads else None,
            "pod_local_matrix": pod_local_matrix(node),
            "rest_blender": _world_pod_to_blender(model, i),
            "head": head,
            "tail": tail,
        })

    return {"bones": specs, "center_point": center_point_spec(model)}


def center_point_spec(model: PODModel) -> Optional[Dict]:
    """The CenterPoint pivot (LOCAL translation, Blender space) or None."""
    for node in model.nodes:
        if node.name == CENTER_POINT_NAME:
            if node.has_matrix:
                t = [node.matrix[12], node.matrix[13], node.matrix[14]]
            else:
                t = list(node.translation)
            return {"name": node.name, "translation": pod_to_blender_basis(t)}
    return None


def pack_skin(influences, vertex_count: int,
              max_influences: int = 1) -> Tuple[List[float], List[float], int]:
    """Pack Blender vertex-group influences into POD skin arrays.

    ``influences`` is one list of ``(node_index, weight)`` pairs per vertex
    (already filtered to real ``Bone*`` nodes, zero weights dropped).

    Profile ``max_influences=1`` (stock Swordigo) keeps only the dominant
    influence at weight 1.0 — exactly :func:`dominant_weight_quantize`.
    Larger profiles (OpenSwordigo) keep up to N influences, normalised.

    Returns ``(bone_indices, bone_weights, bones_per_vertex)``.
    """
    if vertex_count <= 0:
        return [], [], 0
    if max_influences <= 1:
        flat_idx: List[float] = []
        for v in range(vertex_count):
            pairs = influences[v] if v < len(influences) else []
            if not pairs:
                flat_idx.append(0.0)
                continue
            best_node, best_w = pairs[0]
            for node, w in pairs[1:]:
                if w > best_w:
                    best_node, best_w = node, w
            flat_idx.append(float(best_node))
        return flat_idx, [1.0] * len(flat_idx), 1

    width = max(1, min(max_influences, max((len(p) for p in influences), default=1)))
    idx: List[float] = []
    wgt: List[float] = []
    for v in range(vertex_count):
        pairs = sorted(influences[v] if v < len(influences) else [],
                       key=lambda p: -p[1])[:width]
        total = sum(w for _n, w in pairs) or 1.0
        for k in range(width):
            if k < len(pairs):
                idx.append(float(pairs[k][0]))
                wgt.append(pairs[k][1] / total)
            else:
                idx.append(0.0)
                wgt.append(0.0)
    return idx, wgt, width
