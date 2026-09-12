"""rubyforge.core.pod_writer — pure-Python PowerVR POD chunk serializer.

A faithful, zero-dependency port of ``av::pod_write`` from
`src/tools/pod_writer.cpp` (the golden C++ reference).

It emits the chunk-exact little-endian tag-length-data layout:

  * FormatVersion (1000) "AB.POD.2.0" + closing tag
  * Scene (1001) container with counts + material/texture/mesh/node blocks

Vertex blocks use float data (eType 1), bone indices use int (eType 2) and
the face index list is written as UNSIGNED_SHORT when every index fits in
u16 and UNSIGNED_INT otherwise (index-width preservation, per pod_master §5).

NOTE: the stock 1002 / 1003 metadata-options string blocks are preserved on
the IR by the reader but not re-emitted by the Stage-1 writer (matching the
C++ reference, which produces loader-accepted PODs without them).
"""

from __future__ import annotations

import struct
from typing import List

from . import tags as T
from .ir import PODMaterial, PODMesh, PODModel, PODNode

__all__ = ["pod_write", "pod_dumps"]


class _Sink:
    """Little-endian byte emitter mirroring pod_writer.cpp's Sink."""

    __slots__ = ("_buf",)

    def __init__(self) -> None:
        self._buf = bytearray()

    def u32(self, v: int) -> None:
        self._buf += v.to_bytes(4, "little")

    def f32(self, v: float) -> None:
        self._buf += struct.pack("<f", v)

    def bytes(self, data: bytes) -> None:
        self._buf += data

    def floats(self, values: List[float]) -> None:
        if values:
            self._buf += struct.pack("<%df" % len(values), *values)

    def u32s(self, values: List[int]) -> None:
        if values:
            self._buf += struct.pack("<%dI" % len(values), *values)

    def chunk(self, tag: int, payload_len: int) -> None:
        self.u32(tag)
        self.u32(payload_len)

    def str_field(self, tag: int, s: str) -> None:
        raw = s.encode("latin-1")
        self.u32(tag)
        self.u32(len(raw) + 1)          # NUL-terminated like the SDK
        self._buf += raw
        self._buf += b"\x00"

    def end_tag(self, tag: int) -> None:
        self.chunk(tag | T.END_TAG, 0)

    def begin(self, tag: int) -> None:
        self.u32(tag)
        self.u32(0)                     # container blocks have length 0

    def finish(self, tag: int) -> None:
        self.end_tag(tag)

    def data(self) -> bytes:
        return bytes(self._buf)


def _write_vertex_block(sink: _Sink, block_id: int, data: List[float],
                        components: int) -> None:
    """Emit a float vertex block (9000-9003), mirrors write_vertex_block."""
    if not data or components <= 0:
        return
    sink.begin(block_id)
    sink.u32(T.eBlockDataType); sink.u32(4); sink.u32(T.DT_FLOAT)
    sink.u32(T.eBlockNumComponents); sink.u32(4); sink.u32(components)
    sink.u32(T.eBlockStride); sink.u32(4); sink.u32(components * 4)
    sink.u32(T.eBlockData); sink.u32(len(data) * 4); sink.floats(data)
    sink.finish(block_id)


def _write_bone_index_block(sink: _Sink, data: List[float], components: int) -> None:
    """Emit the bone-index vertex block (6012) as ints (eType 2)."""
    if not data or components <= 0:
        return
    sink.begin(T.eMeshBoneIndexList)
    sink.u32(T.eBlockDataType); sink.u32(4); sink.u32(T.DT_INT)
    sink.u32(T.eBlockNumComponents); sink.u32(4); sink.u32(components)
    sink.u32(T.eBlockStride); sink.u32(4); sink.u32(components * 4)
    sink.u32(T.eBlockData); sink.u32(len(data) * 4)
    for value in data:
        sink.u32(int(round(value)) & 0xFFFFFFFF)
    sink.finish(T.eMeshBoneIndexList)


def _write_index_block(sink: _Sink, indices: List[int]) -> None:
    """Emit the face index list (6003), widening to u32 when required."""
    if not indices:
        return
    needs_u32 = any(idx >= 65536 for idx in indices)

    sink.begin(T.eMeshVertexIndexList)
    if not needs_u32:
        sink.u32(T.eBlockDataType); sink.u32(4); sink.u32(T.DT_UNSIGNED_SHORT)
        sink.u32(T.eBlockNumComponents); sink.u32(4); sink.u32(1)
        sink.u32(T.eBlockStride); sink.u32(4); sink.u32(2)
        sink.u32(T.eBlockData); sink.u32(len(indices) * 2)
        for idx in indices:
            sink._buf += (idx & 0xFFFF).to_bytes(2, "little")
    else:
        sink.u32(T.eBlockDataType); sink.u32(4); sink.u32(T.DT_INT)
        sink.u32(T.eBlockNumComponents); sink.u32(4); sink.u32(1)
        sink.u32(T.eBlockStride); sink.u32(4); sink.u32(4)
        sink.u32(T.eBlockData); sink.u32(len(indices) * 4)
        sink.u32s([idx & 0xFFFFFFFF for idx in indices])
    sink.finish(T.eMeshVertexIndexList)


def _write_mesh(sink: _Sink, m: PODMesh) -> None:
    """Serialize one PODMesh into a SceneMesh (2012) container."""
    sink.begin(T.eSceneMesh)
    sink.u32(T.eMeshNumVertices); sink.u32(4); sink.u32(m.num_vertices)
    sink.u32(T.eMeshNumFaces); sink.u32(4); sink.u32(m.num_faces)
    sink.u32(T.eMeshNumUVWChannels); sink.u32(4); sink.u32(1 if m.uvs else 0)

    if m.has_bone_batches:
        bb = m.bone_batches
        if bb.max_bones > 0:
            sink.u32(T.eMeshMaxNumBonesPerBatch); sink.u32(4); sink.u32(bb.max_bones)
        if bb.count > 0:
            sink.u32(T.eMeshNumBoneBatches); sink.u32(4); sink.u32(bb.count)
        if bb.indices:
            sink.u32(T.eMeshBoneBatchIndexList); sink.u32(len(bb.indices) * 4); sink.u32s(bb.indices)
        if bb.counts:
            sink.u32(T.eMeshNumBoneIndicesPerBatch); sink.u32(len(bb.counts) * 4); sink.u32s(bb.counts)
        if bb.offsets:
            sink.u32(T.eMeshBoneOffsetPerBatch); sink.u32(len(bb.offsets) * 4); sink.u32s(bb.offsets)

    _write_index_block(sink, m.indices)
    _write_vertex_block(sink, T.eMeshVertexList, m.positions, 3)
    _write_vertex_block(sink, T.eMeshNormalList, m.normals, 3)
    if m.uvs:
        _write_vertex_block(sink, T.eMeshUVWList, list(m.uvs), 2)
    if m.bones_per_vertex > 0:
        _write_bone_index_block(sink, m.bone_indices, m.bones_per_vertex)
        _write_vertex_block(sink, T.eMeshBoneWeightList, m.bone_weights, m.bones_per_vertex)
    sink.finish(T.eSceneMesh)


def _write_node(sink: _Sink, n: PODNode) -> None:
    """Serialize one PODNode into a SceneNode (2013) container."""
    sink.begin(T.eSceneNode)
    sink.u32(T.eNodeIndex); sink.u32(4); sink.u32(n.object_index & 0xFFFFFFFF)
    sink.str_field(T.eNodeName, n.name if n.name else "Node")
    sink.u32(T.eNodeMaterialIndex); sink.u32(4); sink.u32(n.material_index & 0xFFFFFFFF)
    sink.u32(T.eNodeParentIndex); sink.u32(4); sink.u32(n.parent_index & 0xFFFFFFFF)
    sink.u32(T.eNodeAnimationFlags); sink.u32(4); sink.u32(n.anim_flags)

    if n.has_translation:
        sink.u32(T.eNodePosition); sink.u32(12); sink.floats(list(n.translation))
    if n.has_rotation:
        sink.u32(T.eNodeRotation); sink.u32(16); sink.floats(list(n.rotation))
    if n.has_scale:
        sink.u32(T.eNodeScale); sink.u32(12); sink.floats(list(n.scale))
    if n.has_matrix:
        sink.u32(T.eNodeMatrix); sink.u32(64); sink.floats(list(n.matrix))

    if n.anim_translation:
        sink.u32(T.eNodeAnimationPosition); sink.u32(len(n.anim_translation) * 4); sink.floats(list(n.anim_translation))
    if n.anim_rotation:
        sink.u32(T.eNodeAnimationRotation); sink.u32(len(n.anim_rotation) * 4); sink.floats(list(n.anim_rotation))
    if n.anim_scale:
        scale_out = list(n.anim_scale)
        if len(scale_out) % 7 != 0 and len(scale_out) % 3 == 0:
            # Normalize 3-float scale keys to the SDK's 7-float [s, quat] stride.
            keys = len(scale_out) // 3
            normalized = []
            for k in range(keys):
                normalized += [scale_out[k * 3 + 0], scale_out[k * 3 + 1], scale_out[k * 3 + 2],
                               0.0, 0.0, 0.0, 1.0]
            scale_out = normalized
        sink.u32(T.eNodeAnimationScale); sink.u32(len(scale_out) * 4); sink.floats(scale_out)
    if n.anim_matrix:
        sink.u32(T.eNodeAnimationMatrix); sink.u32(len(n.anim_matrix) * 4); sink.floats(list(n.anim_matrix))

    if n.anim_translation_idx:
        sink.u32(T.eNodeAnimationPositionIndex); sink.u32(len(n.anim_translation_idx) * 4); sink.u32s(list(n.anim_translation_idx))
    if n.anim_rotation_idx:
        sink.u32(T.eNodeAnimationRotationIndex); sink.u32(len(n.anim_rotation_idx) * 4); sink.u32s(list(n.anim_rotation_idx))
    if n.anim_scale_idx:
        sink.u32(T.eNodeAnimationScaleIndex); sink.u32(len(n.anim_scale_idx) * 4); sink.u32s(list(n.anim_scale_idx))
    if n.anim_matrix_idx:
        sink.u32(T.eNodeAnimationMatrixIndex); sink.u32(len(n.anim_matrix_idx) * 4); sink.u32s(list(n.anim_matrix_idx))

    sink.finish(T.eSceneNode)


def _write_texture(sink: _Sink, name: str) -> None:
    sink.begin(T.eSceneTexture)
    if name:
        sink.str_field(T.eTextureFilename, name)
    sink.finish(T.eSceneTexture)


def _write_material(sink: _Sink, mat: PODMaterial) -> None:
    sink.begin(T.eSceneMaterial)
    sink.str_field(T.eMaterialName, mat.name if mat.name else "Default")
    sink.u32(T.eMaterialDiffuseTextureIndex); sink.u32(4); sink.u32(mat.diffuse_texture_index & 0xFFFFFFFF)
    sink.u32(T.eMaterialOpacity); sink.u32(4); sink.f32(mat.opacity if mat.opacity > 0.0 else 1.0)
    sink.u32(T.eMaterialAmbient); sink.u32(12); sink.floats([0.2, 0.2, 0.2])
    sink.u32(T.eMaterialDiffuse); sink.u32(12); sink.floats(list(mat.diffuse))
    sink.u32(T.eMaterialSpecular); sink.u32(12); sink.floats([0.0, 0.0, 0.0])
    sink.u32(T.eMaterialShininess); sink.u32(4); sink.f32(0.0)
    sink.finish(T.eSceneMaterial)
def pod_dumps(model: PODModel) -> bytes:
    """Serialize a PODModel to the binary POD chunk format (in memory)."""
    sink = _Sink()

    # Top-level version block.
    sink.u32(T.eFormatVersion)
    sink.u32(11)
    sink.bytes(b"AB.POD.2.0\x00")
    sink.end_tag(T.eFormatVersion)

    # Scene block.
    sink.begin(T.eScene)
    sink.u32(T.eSceneClearColour); sink.u32(12); sink.floats([0.0, 0.0, 0.0])
    sink.u32(T.eSceneAmbientColour); sink.u32(12); sink.floats([0.2, 0.2, 0.2])
    sink.u32(T.eSceneNumCameras); sink.u32(4); sink.u32(0)
    sink.u32(T.eSceneNumLights); sink.u32(4); sink.u32(0)
    sink.u32(T.eSceneNumMeshes); sink.u32(4); sink.u32(len(model.meshes))
    sink.u32(T.eSceneNumNodes); sink.u32(4); sink.u32(len(model.nodes))
    sink.u32(T.eSceneNumMeshNodes); sink.u32(4); sink.u32(max(0, model.num_mesh_nodes))
    sink.u32(T.eSceneNumTextures); sink.u32(4); sink.u32(len(model.texture_filenames))
    sink.u32(T.eSceneNumMaterials); sink.u32(4); sink.u32(len(model.materials))
    sink.u32(T.eSceneNumFrames); sink.u32(4); sink.u32(max(0, model.num_frames))
    # FPS is stored as an integer field (matches the C++ reference and the
    # loader, which both treat eSceneFPS as a u32).
    sink.u32(T.eSceneFPS); sink.u32(4); sink.u32(int(model.fps) & 0xFFFFFFFF)
    sink.u32(T.eSceneFlags); sink.u32(4); sink.u32(0)

    for mat in model.materials:
        _write_material(sink, mat)
    for tex in model.texture_filenames:
        _write_texture(sink, tex)
    for mesh in model.meshes:
        _write_mesh(sink, mesh)
    for node in model.nodes:
        _write_node(sink, node)
    sink.finish(T.eScene)

    return sink.data()


def pod_write(model: PODModel, path: str):
    """Serialize ``model`` to ``path`` as a POD file. Raises OSError on failure."""
    data = pod_dumps(model)
    with open(path, "wb") as f:
        f.write(data)
    return True