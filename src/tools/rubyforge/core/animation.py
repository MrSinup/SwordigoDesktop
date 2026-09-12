"""rubyforge.core.animation — Stage 3: the 24 FPS action pipeline.

Pure (Blender-free) conversion between POD node animation streams and a
Blender-action "pose specification":

  * POD streams: per-node key arrays (translation 3, rotation 4 xyzw,
    scale 3 or 7 [xyz + orientation quat]) with optional sparse indices.
  * Pose spec:   per-bone, per-frame ``matrix_basis`` (the pose-bone LOCAL
    transform relative to the rest pose) as location / quaternion (wxyz,
    Blender channel order) / scale — exactly what the bpy keyframe
    insertion path consumes.

The engine hardcodes animation FPS at 24.0 (0x41C00000 in
``CreateAnimationFromFile``) and binds tracks to bones strictly by NAME,
so the pose spec keys everything by bone name.

Space conventions:

  * ``matrix_basis`` lives in Blender bone-local space (Z-up).
  * POD node locals live in the engine's Y-up space.
  * Conversion is basis conjugation via :mod:`rubyforge.core.rigging`.
"""

from __future__ import annotations

import math
from typing import Dict, List, Optional, Sequence

from .ir import PODModel, PODNode
from .rigging import (
    BLENDER_TO_POD_MAT4,
    POD_TO_BLENDER_MAT4,
    mat4_to_quat_pod,
    pod_local_to_trs,
)
from .transforms import (
    get_node_matrix,
    mat4_from_quat,
    mat4_identity,
    mat4_inverse,
    mat4_mul,
)

__all__ = [
    "SWORDIGO_FPS",
    "mat4_to_quat_blender",
    "blender_local_to_trs",
    "local_pod_from_trs",
    "pose_spec_from_model",
    "anim_tracks_from_locals",
    "animation_model_from_tracks",
]

# libswordigo CreateAnimationFromFile hardcodes this (0x41C00000).
SWORDIGO_FPS = 24.0


def mat4_to_quat_blender(m: Sequence[float]) -> List[float]:
    """Standard quaternion (x, y, z, w) from a Blender-space rotation matrix.

    (``mat4_to_quat_pod`` negates xyz for the engine's convention; this
    undoes that for Blender's.)
    """
    q = mat4_to_quat_pod(m)
    return [-q[0], -q[1], -q[2], q[3]]


def blender_local_to_trs(m: Sequence[float]):
    """Decompose a Blender-space local matrix into (loc, quat_xyzw, scale)."""
    loc = [m[12], m[13], m[14]]
    c0 = math.sqrt(m[0] ** 2 + m[1] ** 2 + m[2] ** 2)
    c1 = math.sqrt(m[4] ** 2 + m[5] ** 2 + m[6] ** 2)
    c2 = math.sqrt(m[8] ** 2 + m[9] ** 2 + m[10] ** 2)
    scale = [c0 or 1.0, c1 or 1.0, c2 or 1.0]
    rot = list(m)
    for col, length in ((0, scale[0]), (1, scale[1]), (2, scale[2])):
        if length > 1e-12:
            for r in range(3):
                rot[col * 4 + r] = m[col * 4 + r] / length
    return loc, mat4_to_quat_blender(rot), scale


def local_pod_from_trs(translation: Sequence[float], quat_xyzw: Sequence[float],
                       scale: Sequence[float]) -> List[float]:
    """Compose a POD-convention local matrix from T / R / S."""
    t = mat4_identity()
    t[12], t[13], t[14] = translation[0], translation[1], translation[2]
    r = mat4_from_quat(quat_xyzw)
    s = mat4_identity()
    s[0], s[5], s[10] = scale[0], scale[1], scale[2]
    return mat4_mul(mat4_mul(t, r), s)


# ---- POD model -> Blender pose spec (import path) ----------------------
def _quat_to_wxyz(q: Sequence[float]) -> List[float]:
    """POD/xyzw order -> Blender fcurve order (w, x, y, z)."""
    return [q[3], q[0], q[1], q[2]]


def pose_spec_from_model(model: PODModel, rig_spec: Dict,
                         frames: Optional[int] = None) -> Dict:
    """Sample every animated bone into a Blender-action pose specification.

    For each frame the node's world matrix is conjugated into Blender
    space and pushed through the rest pose to produce a ``matrix_basis``;
    the result is decomposed into loc / quat (wxyz) / scale per bone.

    Returns a JSON-able dict::

        {fps, frame_count,
         bones: {name: {location: [[x,y,z]..], rotation_quaternion:
                        [[w,x,y,z]..], scale: [[x,y,z]..]}}}
    """
    frame_count = frames if frames is not None else max(model.num_frames, 1)
    fps = model.fps if model.fps and model.fps > 0 else SWORDIGO_FPS
    rest = {b["name"]: b.get("rest_blender") for b in rig_spec.get("bones", [])}

    bones: Dict[str, Dict[str, List]] = {}
    for node_index, node in enumerate(model.nodes):
        if not node.anim_translation and not node.anim_rotation \
                and not node.anim_matrix and node.name not in rest:
            continue
        if node.name not in rest:
            continue  # not part of this armature
        rest_bl = rest[node.name]
        rest_inv = mat4_inverse(rest_bl) or mat4_identity()

        locs: List[List[float]] = []
        quats: List[List[float]] = []
        scales: List[List[float]] = []
        for f in range(frame_count):
            world_pod = get_node_matrix(model, node_index, float(f))
            world_bl = mat4_mul(mat4_mul(POD_TO_BLENDER_MAT4, world_pod),
                                BLENDER_TO_POD_MAT4)
            basis = mat4_mul(rest_inv, world_bl)
            loc, q, s = blender_local_to_trs(basis)
            locs.append(loc)
            quats.append(_quat_to_wxyz(q))
            scales.append(s)
        bones[node.name] = {
            "location": locs,
            "rotation_quaternion": quats,
            "scale": scales,
        }

    return {"fps": fps, "frame_count": frame_count, "bones": bones}


# ---- Blender pose -> POD anim tracks (export path) ---------------------
def anim_tracks_from_locals(locals_per_bone: Dict[str, List[Sequence[float]]]) -> Dict:
    """Convert per-frame POD-space LOCAL matrices into anim track arrays.

    ``locals_per_bone`` maps bone name -> one LOCAL POD matrix per frame.
    Returns a JSON-able dict of dense key arrays ready for ``PODNode``:

        {name: {"translation": [3/frm], "rotation": [4/frm],
                "scale": [7/frm]}}

    Scale keys use the SDK's 7-float stride (xyz + identity orientation
    quaternion), matching both the stock files and the C++ writer's
    normalisation rule.
    """
    tracks: Dict[str, Dict[str, List[float]]] = {}
    for name, matrices in locals_per_bone.items():
        translation: List[float] = []
        rotation: List[float] = []
        scale: List[float] = []
        for m in matrices:
            t, q, s = pod_local_to_trs(m)
            translation += [t[0], t[1], t[2]]
            rotation += [q[0], q[1], q[2], q[3]]
            scale += [s[0], s[1], s[2], 0.0, 0.0, 0.0, 1.0]
        tracks[name] = {"translation": translation, "rotation": rotation,
                        "scale": scale}
    return tracks


def animation_model_from_tracks(tracks: Dict[str, Dict[str, List[float]]],
                                bone_order: Sequence[str],
                                rest_locals: Dict[str, Sequence[float]],
                                frame_count: int,
                                fps: float = SWORDIGO_FPS) -> PODModel:
    """Build an animation-only PODModel (0 meshes) from anim tracks.

    ``bone_order`` fixes the node order; ``rest_locals`` supplies each
    bone's rest LOCAL POD matrix so the clip also carries a valid bind
    pose (stock clips store the rest pose in the static fields and the
    animation in 1+-frame streams).
    """
    model = PODModel()
    model.version = "AB.POD.2.0"
    model.num_frames = max(0, frame_count)
    model.fps = float(fps)
    model.num_mesh_nodes = 0

    for name in bone_order:
        node = PODNode(name=name)
        rest = rest_locals.get(name)
        if rest is not None:
            t, q, s = pod_local_to_trs(rest)
            node.has_translation = True
            node.translation = list(t)
            node.has_rotation = True
            node.rotation = list(q)
            node.has_scale = True
            node.scale = list(s)
            # Mirror stock files: rest pose as a 1-frame anim stream too.
            node.anim_translation = list(t)
            node.anim_rotation = list(q)
            node.anim_scale = [s[0], s[1], s[2], 0.0, 0.0, 0.0, 1.0]
        track = tracks.get(name)
        if track:
            node.anim_translation = list(track["translation"])
            node.anim_rotation = list(track["rotation"])
            node.anim_scale = list(track["scale"])
        model.nodes.append(node)

    return model
