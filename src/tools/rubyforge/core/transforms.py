"""rubyforge.core.transforms — matrix math, node matrices, model bounds.

Faithful Python ports of the math helpers in `src/tools/pod_loader.cpp`:

  * ``local_mat4_identity`` / ``local_mat4_mul`` / ``local_mat4_from_quat``
  * ``animation_key_index`` / ``lerp3`` / ``slerp_quat``
  * ``get_node_matrix``           — absolute world matrix at a frame
  * ``finalize_model_bounds``     — CenterPoint pivot + global AABB/sphere

It also exposes the Blender <-> POD basis-change contract (research §7):

  POD / glTF / Caver runtime are +Y-up, right-handed; Blender is +Z-up.
  The basis conversion is a +-90 degree rotation about X:

      M(POD -> Blender) = [ 1 0  0 ]        M(Blender -> POD) = [ 1  0 0 ]
                          [ 0 0  1 ]                              [ 0  0 1 ]
                          [ 0 -1 0 ]                              [ 0 -1 0 ]

  i.e. POD (x, y, z) -> Blender (x, -z, y)  and the inverse.

IMPORTANT: When using Blender's official glTF addon ("io_scene_gltf2") the
importer applies the Y-up conversion itself, so RubyForge must NOT apply it
again — that double conversion yields a 180-degree inversion. RubyForge only
applies the explicit basis change on the direct, pure-Python POD path (the
standalone File -> Import/Export operators).
"""

from __future__ import annotations

import math
from typing import List, Optional, Sequence

from .ir import PODModel

_IDENTITY = [1.0, 0.0, 0.0, 0.0,
             0.0, 1.0, 0.0, 0.0,
             0.0, 0.0, 1.0, 0.0,
             0.0, 0.0, 0.0, 1.0]


def mat4_identity() -> List[float]:
    return list(_IDENTITY)


def mat4_mul(a: Sequence[float], b: Sequence[float]) -> List[float]:
    """Column-major 4x4 multiply ``out = a * b`` (mirrors local_mat4_mul)."""
    out = [0.0] * 16
    for c in range(4):
        for r in range(4):
            out[c * 4 + r] = (
                a[0 * 4 + r] * b[c * 4 + 0] +
                a[1 * 4 + r] * b[c * 4 + 1] +
                a[2 * 4 + r] * b[c * 4 + 2] +
                a[3 * 4 + r] * b[c * 4 + 3]
            )
    return out


def mat4_from_quat(q: Sequence[float]) -> List[float]:
    """Quaternion (xyzw) -> column-major rotation matrix.

    Mirrors local_mat4_from_quat, which negates xyz (matching the engine's
    editor-coordinate convention) while preserving w.
    """
    x, y, z, w = -q[0], -q[1], -q[2], q[3]
    m = [0.0] * 16
    m[0] = 1.0 - 2.0 * (y * y + z * z)
    m[1] = 2.0 * (x * y + z * w)
    m[2] = 2.0 * (x * z - y * w)
    m[4] = 2.0 * (x * y - z * w)
    m[5] = 1.0 - 2.0 * (x * x + z * z)
    m[6] = 2.0 * (y * z + x * w)
    m[8] = 2.0 * (x * z + y * w)
    m[9] = 2.0 * (y * z - x * w)
    m[10] = 1.0 - 2.0 * (x * x + y * y)
    m[15] = 1.0
    return m


def mat4_inverse(m: Sequence[float]) -> Optional[List[float]]:
    """Invert a column-major 4x4 (Gauss-Jordan). None if singular."""
    a = list(m)
    out = mat4_identity()
    for col in range(4):
        pivot = col
        for row in range(col + 1, 4):
            if abs(a[col * 4 + row]) > abs(a[col * 4 + pivot]):
                pivot = row
        if abs(a[col * 4 + pivot]) < 1e-8:
            return None
        if pivot != col:
            for c in range(4):
                a[c * 4 + col], a[c * 4 + pivot] = a[c * 4 + pivot], a[c * 4 + col]
                out[c * 4 + col], out[c * 4 + pivot] = out[c * 4 + pivot], out[c * 4 + col]
        scale = 1.0 / a[col * 4 + col]
        for c in range(4):
            a[c * 4 + col] *= scale
            out[c * 4 + col] *= scale
        for row in range(4):
            if row == col:
                continue
            factor = a[col * 4 + row]
            for c in range(4):
                a[c * 4 + row] -= factor * a[c * 4 + col]
                out[c * 4 + row] -= factor * out[c * 4 + col]
    return out


def transform_point(m: Sequence[float], x: float, y: float, z: float) -> List[float]:
    return [
        m[0] * x + m[4] * y + m[8] * z + m[12],
        m[1] * x + m[5] * y + m[9] * z + m[13],
        m[2] * x + m[6] * y + m[10] * z + m[14],
    ]


# ─── Animation helpers ─────────────────────────────────────────────────
def _animation_key_index(indices: Sequence[int], value_count: int,
                         frame: int, components: int) -> int:
    """Resolve a frame to a keyframe bucket (mirrors animation_key_index)."""
    if value_count < components or components <= 0:
        return -1
    frame_index = max(frame, 0)
    if indices and frame_index < len(indices):
        frame_index = indices[frame_index]
    key_count = value_count // components
    return max(0, min(frame_index, key_count - 1))


def _lerp3(a: Sequence[float], b: Sequence[float], t: float) -> List[float]:
    return [a[i] + (b[i] - a[i]) * t for i in range(3)]


def _slerp_quat(a: Sequence[float], b: Sequence[float], t: float) -> List[float]:
    end = list(b)
    dot = a[0] * end[0] + a[1] * end[1] + a[2] * end[2] + a[3] * end[3]
    if dot < 0.0:
        dot = -dot
        end = [-v for v in end]
    if dot > 0.9995:
        out = [a[i] + (end[i] - a[i]) * t for i in range(4)]
        length_sq = sum(v * v for v in out)
        inv = (1.0 / math.sqrt(length_sq)) if length_sq > 0.0 else 1.0
        return [v * inv for v in out]
    theta_0 = math.acos(max(-1.0, min(1.0, dot)))
    theta = theta_0 * t
    sin_theta = math.sin(theta)
    sin_theta_0 = math.sin(theta_0)
    s0 = math.cos(theta) - dot * sin_theta / sin_theta_0
    s1 = sin_theta / sin_theta_0
    return [a[i] * s0 + end[i] * s1 for i in range(4)]


def _stream_has_animation(flag: int, values: int, components: int, anim_flags: int) -> bool:
    return values >= components and (anim_flags & flag != 0 or anim_flags == 0)
def get_node_matrix(model: PODModel, node_idx: int, frame: float,
                    depth: int = 0) -> List[float]:
    """Absolute world matrix for a node at a (possibly fractional) frame.

    Faithful port of av::get_node_matrix / get_node_matrix_internal,
    including recursive parent accumulation and the engine's negated-xyz
    quaternion convention.
    """
    if depth > 64 or node_idx < 0 or node_idx >= len(model.nodes):
        return mat4_identity()

    node = model.nodes[node_idx]
    whole_frame = max(0, int(frame))
    next_frame = min(whole_frame + 1, model.num_frames - 1) if model.num_frames > 0 else whole_frame
    fraction = max(0.0, min(frame - float(whole_frame), 1.0))

    local = mat4_identity()

    has_anim_matrix = _stream_has_animation(8, len(node.anim_matrix), 16, node.anim_flags)
    has_anim_translation = _stream_has_animation(1, len(node.anim_translation), 3, node.anim_flags)
    has_anim_rotation = _stream_has_animation(2, len(node.anim_rotation), 4, node.anim_flags)
    has_anim_scale = _stream_has_animation(4, len(node.anim_scale), 3, node.anim_flags)

    if has_anim_matrix:
        key = _animation_key_index(node.anim_matrix_idx, len(node.anim_matrix), whole_frame, 16)
        if key >= 0:
            local = node.anim_matrix[key * 16:(key + 1) * 16]
    elif node.has_matrix:
        local = list(node.matrix)
    else:
        S = mat4_identity()
        R = mat4_identity()
        T = mat4_identity()

        t_val = list(node.translation)
        if node.anim_translation:
            t_val = node.anim_translation[0:3]
        if has_anim_translation:
            key0 = _animation_key_index(node.anim_translation_idx, len(node.anim_translation), whole_frame, 3)
            key1 = _animation_key_index(node.anim_translation_idx, len(node.anim_translation), next_frame, 3)
            if key0 >= 0 and key1 >= 0:
                t_val = _lerp3(node.anim_translation[key0 * 3:(key0 + 1) * 3],
                               node.anim_translation[key1 * 3:(key1 + 1) * 3], fraction)
        T[12], T[13], T[14] = t_val[0], t_val[1], t_val[2]

        r_val = list(node.rotation)
        if node.anim_rotation:
            r_val = node.anim_rotation[0:4]
        if has_anim_rotation:
            key0 = _animation_key_index(node.anim_rotation_idx, len(node.anim_rotation), whole_frame, 4)
            key1 = _animation_key_index(node.anim_rotation_idx, len(node.anim_rotation), next_frame, 4)
            if key0 >= 0 and key1 >= 0:
                r_val = _slerp_quat(node.anim_rotation[key0 * 4:(key0 + 1) * 4],
                                    node.anim_rotation[key1 * 4:(key1 + 1) * 4], fraction)
        R = mat4_from_quat(r_val)

        s_val = list(node.scale)
        if len(node.anim_scale) >= 3:
            s_val = node.anim_scale[0:3]
        if has_anim_scale:
            stride = 7 if (len(node.anim_scale) % 7 == 0) else 3
            key0 = _animation_key_index(node.anim_scale_idx, len(node.anim_scale), whole_frame, stride)
            key1 = _animation_key_index(node.anim_scale_idx, len(node.anim_scale), next_frame, stride)
            if key0 >= 0 and key1 >= 0:
                s_val = _lerp3(node.anim_scale[key0 * stride:(key0 + 1) * stride],
                               node.anim_scale[key1 * stride:(key1 + 1) * stride], fraction)
        S[0], S[5], S[10] = s_val[0], s_val[1], s_val[2]

        temp = mat4_mul(T, R)
        local = mat4_mul(temp, S)

    if node.parent_index != -1 and node.parent_index != node_idx:
        parent_world = get_node_matrix(model, node.parent_index, frame, depth + 1)
        return mat4_mul(parent_world, local)
    return list(local)
def finalize_model_bounds(model: PODModel) -> None:
    """Populate CenterPoint pivot, totals and global AABB/sphere.

    Faithful port of av::finalize_model_bounds. The CenterPoint node's OWN
    LOCAL translation (not the accumulated world chain) is used as the pivot
    — matching libswordigo's CenterPoint handling.
    """
    model.total_vertices = 0
    model.total_faces = 0
    model.min_x = model.min_y = model.min_z = 1e9
    model.max_x = model.max_y = model.max_z = -1e9
    model.has_center_point = False

    for node in model.nodes:
        if node.name != "CenterPoint":
            continue
        if node.has_matrix:
            model.center_point = [node.matrix[12], node.matrix[13], node.matrix[14]]
        else:
            model.center_point = list(node.translation)
        model.has_center_point = True
        break

    for mesh in model.meshes:
        model.total_vertices += mesh.num_vertices
        model.total_faces += mesh.num_faces

    for node_index in range(min(model.num_mesh_nodes, len(model.nodes))):
        node = model.nodes[node_index]
        if node.object_index < 0 or node.object_index >= len(model.meshes):
            continue
        mesh = model.meshes[node.object_index]
        if not mesh.positions:
            continue
        matrix = get_node_matrix(model, node_index, 0.0)
        positions = mesh.positions
        for i in range(0, len(positions) - 2, 3):
            point = transform_point(matrix, positions[i], positions[i + 1], positions[i + 2])
            if model.has_center_point:
                for k in range(3):
                    point[k] -= model.center_point[k]
            model.min_x = min(model.min_x, point[0])
            model.min_y = min(model.min_y, point[1])
            model.min_z = min(model.min_z, point[2])
            model.max_x = max(model.max_x, point[0])
            model.max_y = max(model.max_y, point[1])
            model.max_z = max(model.max_z, point[2])

    # Node-less PODs are uncommon but valid; fall back to mesh-space bounds.
    if model.min_x > model.max_x:
        for mesh in model.meshes:
            if not mesh.positions:
                continue
            model.min_x = min(model.min_x, mesh.min_x)
            model.max_x = max(model.max_x, mesh.max_x)
            model.min_y = min(model.min_y, mesh.min_y)
            model.max_y = max(model.max_y, mesh.max_y)
            model.min_z = min(model.min_z, mesh.min_z)
            model.max_z = max(model.max_z, mesh.max_z)

    if model.min_x <= model.max_x:
        model.center_x = (model.min_x + model.max_x) * 0.5
        model.center_y = (model.min_y + model.max_y) * 0.5
        model.center_z = (model.min_z + model.max_z) * 0.5
        dx = model.max_x - model.min_x
        dy = model.max_y - model.min_y
        dz = model.max_z - model.min_z
        model.radius = max(0.5 * math.sqrt(dx * dx + dy * dy + dz * dz), 1.0)


# ─── Blender <-> POD basis change (research §7) ────────────────────────
# POD / glTF / Caver runtime are +Y-up; Blender is +Z-up. The conversion is a
# +-90 degree rotation about X. POD (x,y,z) -> Blender (x, -z, y); inverse
# Blender (x,y,z) -> POD (x, -z, y). Applying both is the identity.

def pod_to_blender_basis(point: Sequence[float]) -> List[float]:
    """POD (Y-up) -> Blender (Z-up): (x, y, z) -> (x, -z, y)."""
    return [point[0], -point[2], point[1]]


def blender_to_pod_basis(point: Sequence[float]) -> List[float]:
    """Blender (Z-up) -> POD (Y-up): (x, y, z) -> (x, z, -y).

    The exact inverse of :func:`pod_to_blender_basis` (M_Blender->POD in the
    research contract): Blender's +Z (up) maps to POD's +Y (up).
    """
    return [point[0], point[2], -point[1]]


def apply_pod_to_blender_basis(flat_xyz: Sequence[float]) -> List[float]:
    """Apply the Y-up -> Z-up basis change to a flat vec3 array.

    Permutes component order only (no arithmetic), so float values are
    preserved exactly across a round-trip.
    """
    out: List[float] = [0.0] * len(flat_xyz)
    for i in range(0, len(flat_xyz) - 2, 3):
        out[i], out[i + 1], out[i + 2] = flat_xyz[i], -flat_xyz[i + 2], flat_xyz[i + 1]
    return out