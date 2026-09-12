"""rubyforge.core.pod_reader — pure-Python PowerVR POD chunk reader.

A faithful, zero-dependency port of ``av::pod_parse`` / ``av::pod_load``
from `src/tools/pod_loader.cpp` (the golden C++ reference).

Reproduces exactly:

  * the little-endian tag-length-data chunk walker (gracefully skipping the
    stock 1002 / 1003 metadata-options string blocks),
  * vertex-block parsing (9000-9003) and data-type decoding for interleaved
    and non-interleaved payloads,
  * index widening (16-bit -> Python ints) and quad->triangle expansion,
  * normal synthesis for meshes with missing / all-zero normal blocks,
  * bone batches (6015-6019), MeshUnpackMatrix (6020) application,
  * node, texture and material block parsing,
  * CenterPoint pivot extraction and model bounds finalization.

The output is a `rubyforge.core.ir.PODModel`, byte-compatible with the IR the
native reader fills.
"""

from __future__ import annotations

import math
import struct
from typing import List, Optional, Tuple

from . import tags as T
from .ir import BoneBatch, PODMaterial, PODMesh, PODModel, PODNode
from .transforms import finalize_model_bounds


class PODError(Exception):
    """Raised for structurally invalid / unusable POD blobs."""


# Vertex data-type -> element byte size (mirrors pod_loader.cpp comp_size).
_COMP_SIZE = {
    T.DT_FLOAT: 4,
    T.DT_INT: 4,
    T.DT_UNSIGNED_SHORT: 2,
    T.DT_UNSIGNED_BYTE: 1,
    T.DT_SHORT: 2,
    T.DT_SHORT_NORM: 2,
    T.DT_BYTE: 1,
    T.DT_BYTE_NORM: 1,
    T.DT_UNSIGNED_BYTE_NORM: 1,
    T.DT_UNSIGNED_SHORT_NORM: 2,
    T.DT_UNSIGNED_INT: 4,
}


class _DataElement:
    __slots__ = ("type", "num_components", "stride", "payload", "payload_size")

    def __init__(self) -> None:
        self.type = 0
        self.num_components = 0
        self.stride = 0
        self.payload: Optional[bytes] = None
        self.payload_size = 0


def _u32(data: bytes, off: int, size: int) -> Tuple[int, int]:
    if off + 4 > size:
        return 0, size
    return struct.unpack_from("<I", data, off)[0], off + 4


def _f32(data: bytes, off: int) -> float:
    return struct.unpack_from("<f", data, off)[0]


def _to_i32(value: int) -> int:
    """Reinterpret an unsigned 32-bit field as signed (C++ static_cast<int>).

    POD stores -1 (no parent / no texture) as 0xFFFFFFFF.
    """
    return value - 0x100000000 if value >= 0x80000000 else value


def _read_cstr(data: bytes, off: int, length: int) -> Tuple[str, int]:
    """Null-terminated payload -> str (latin-1 lossless), mirrors strnlen."""
    raw = data[off:off + length]
    s = raw.split(b"\x00", 1)[0].decode("latin-1")
    return s, off + length


def _decode_scalar(typ: int, data: bytes, off: int, comp_size: int) -> float:
    """Decode one component of a given data type (mirrors unpack_vertex_data)."""
    if typ == T.DT_FLOAT:
        return struct.unpack_from("<f", data, off)[0]
    if typ == T.DT_UNSIGNED_SHORT:
        return float(struct.unpack_from("<H", data, off)[0])
    if typ == T.DT_UNSIGNED_SHORT_NORM:
        return struct.unpack_from("<H", data, off)[0] / 65535.0
    if typ == T.DT_SHORT:
        return float(struct.unpack_from("<h", data, off)[0])
    if typ == T.DT_SHORT_NORM:
        return struct.unpack_from("<h", data, off)[0] / 32767.0
    if typ == T.DT_UNSIGNED_BYTE:
        return float(data[off])
    if typ == T.DT_UNSIGNED_BYTE_NORM:
        return data[off] / 255.0
    if typ == T.DT_BYTE:
        return float(struct.unpack_from("<b", data, off)[0])
    if typ == T.DT_BYTE_NORM:
        return struct.unpack_from("<b", data, off)[0] / 127.0
    if typ == T.DT_INT:
        return float(struct.unpack_from("<i", data, off)[0])
    if typ == T.DT_UNSIGNED_INT:
        return float(struct.unpack_from("<I", data, off)[0])
    return 0.0


def _unpack_vertex_data(interleaved: Optional[bytes], de: _DataElement,
                        num_vertices: int, num_components: int) -> List[float]:
    """Convert a vertex component array (interleaved or standalone) to floats.

    Faithful port of ``unpack_vertex_data`` from pod_loader.cpp.
    """
    result = [0.0] * (num_vertices * num_components)
    if num_vertices <= 0 or num_components <= 0:
        return result

    comp_size = _COMP_SIZE.get(de.type, 4)

    if interleaved is not None:
        if de.payload is None or de.payload_size < 4:
            return result
        offset = struct.unpack_from("<I", de.payload, 0)[0]
        if offset >= len(interleaved):
            return result
        src = interleaved[offset:]
        limit = len(interleaved)
    else:
        if de.payload is None:
            return result
        src = de.payload
        limit = de.payload_size

    if limit <= 0:
        return result

    block_components = de.num_components if de.num_components > 0 else num_components
    stride = de.stride
    if stride == 0:
        stride = block_components * comp_size
    read_components = min(block_components, num_components)

    for i in range(num_vertices):
        vert_off = i * stride
        if vert_off + stride > limit:
            break
        for c in range(read_components):
            comp_off = vert_off + c * comp_size
            if comp_off + comp_size > limit:
                continue
            result[i * num_components + c] = _decode_scalar(de.type, src, comp_off, comp_size)
    return result


def _parse_indices(de: _DataElement, num_faces: int, verts_per_face: int,
                   mesh: PODMesh) -> None:
    """Decode the face index list, widening to u32 and expanding quads."""
    if de.payload is None or de.payload_size == 0:
        return
    index_count = num_faces * verts_per_face
    comp_size = 4
    if de.type in (T.DT_UNSIGNED_SHORT, T.DT_SHORT, T.DT_SHORT_NORM, T.DT_UNSIGNED_SHORT_NORM):
        comp_size = 2
    elif de.type in (T.DT_UNSIGNED_BYTE, T.DT_BYTE, T.DT_BYTE_NORM,
                     T.DT_UNSIGNED_BYTE_NORM):
        comp_size = 1

    indices: List[int] = []
    for i in range(index_count):
        if (i + 1) * comp_size > de.payload_size:
            break
        if comp_size == 4:
            if de.type == T.DT_INT:
                val = struct.unpack_from("<i", de.payload, i * 4)[0]
                indices.append(val & 0xFFFFFFFF)
            else:
                indices.append(struct.unpack_from("<I", de.payload, i * 4)[0])
        elif comp_size == 2:
            indices.append(struct.unpack_from("<H", de.payload, i * 2)[0])
        else:
            indices.append(de.payload[i])

    if verts_per_face == 4:
        tris: List[int] = []
        for f in range(num_faces):
            q = indices[f * 4:f * 4 + 4]
            tris += [q[0], q[1], q[2], q[0], q[2], q[3]]
        indices = tris
    mesh.indices = indices


def _compute_mesh_aabb(mesh: PODMesh) -> None:
    if not mesh.positions:
        return
    p = mesh.positions
    mesh.min_x = mesh.min_y = mesh.min_z = 1e9
    mesh.max_x = mesh.max_y = mesh.max_z = -1e9
    for i in range(0, len(p) - 2, 3):
        x, y, z = p[i], p[i + 1], p[i + 2]
        if x < mesh.min_x: mesh.min_x = x
        if y < mesh.min_y: mesh.min_y = y
        if z < mesh.min_z: mesh.min_z = z
        if x > mesh.max_x: mesh.max_x = x
        if y > mesh.max_y: mesh.max_y = y
        if z > mesh.max_z: mesh.max_z = z
def _parse_vertex_block(data: bytes, off: int, size: int,
                        block_id: int) -> Tuple[_DataElement, int]:
    """Parse a vertex block container (data type / components / stride / data)."""
    de = _DataElement()
    end_tag = block_id | T.END_TAG
    while off < size:
        tag, off = _u32(data, off, size)
        length, off = _u32(data, off, size)
        if off + length > size:
            length = size - off
        if tag == end_tag:
            return de, off
        if tag == T.eBlockDataType:
            if length >= 4:
                de.type, off = _u32(data, off, size)
            else:
                off += length
        elif tag == T.eBlockNumComponents:
            if length >= 4:
                de.num_components, off = _u32(data, off, size)
            else:
                off += length
        elif tag == T.eBlockStride:
            if length >= 4:
                de.stride, off = _u32(data, off, size)
            else:
                off += length
        elif tag == T.eBlockData:
            de.payload = data[off:off + length]
            de.payload_size = length
            off += length
        else:
            off += length
    return de, off


def _unpack_matrix_is_identity_or_degenerate(m: List[float]) -> bool:
    ident = [1.0, 0.0, 0.0, 0.0,
             0.0, 1.0, 0.0, 0.0,
             0.0, 0.0, 1.0, 0.0,
             0.0, 0.0, 0.0, 1.0]
    ident_ok = True
    zero_ok = True
    for i in range(16):
        if m[i] != ident[i]:
            ident_ok = False
        if m[i] != 0.0:
            zero_ok = False
    return ident_ok or zero_ok


def _apply_unpack_matrix(positions: List[float], normals: List[float], m: List[float]) -> None:
    """Apply the column-major unpack matrix to positions (w=1) and normals
    (rotational part only, w=0).
    """
    for i in range(0, len(positions) - 2, 3):
        x, y, z = positions[i], positions[i + 1], positions[i + 2]
        positions[i] = m[0] * x + m[4] * y + m[8] * z + m[12]
        positions[i + 1] = m[1] * x + m[5] * y + m[9] * z + m[13]
        positions[i + 2] = m[2] * x + m[6] * y + m[10] * z + m[14]
    for i in range(0, len(normals) - 2, 3):
        x, y, z = normals[i], normals[i + 1], normals[i + 2]
        normals[i] = m[0] * x + m[4] * y + m[8] * z
        normals[i + 1] = m[1] * x + m[5] * y + m[9] * z
        normals[i + 2] = m[2] * x + m[6] * y + m[10] * z


def _synthesize_normals(mesh: PODMesh) -> None:
    """Build per-face-averaged unit normals for meshes lacking them.

    Mirrors pod_loader.cpp: missing or all-zero normal blocks are replaced by
    face cross-product normals accumulated per vertex and normalized.
    """
    positions = mesh.positions
    indices = mesh.indices
    num_vertices = mesh.num_vertices
    if not positions or not indices or num_vertices <= 0:
        return
    normals = [0.0] * (num_vertices * 3)
    for t in range(0, len(indices) - 2, 3):
        i0, i1, i2 = indices[t], indices[t + 1], indices[t + 2]
        if i0 >= num_vertices or i1 >= num_vertices or i2 >= num_vertices:
            continue
        p0 = positions[i0 * 3:i0 * 3 + 3]
        p1 = positions[i1 * 3:i1 * 3 + 3]
        p2 = positions[i2 * 3:i2 * 3 + 3]
        ux, uy, uz = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
        vx, vy, vz = p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]
        nx = uy * vz - uz * vy
        ny = uz * vx - ux * vz
        nz = ux * vy - uy * vx
        for idx in (i0, i1, i2):
            normals[idx * 3 + 0] += nx
            normals[idx * 3 + 1] += ny
            normals[idx * 3 + 2] += nz
    for v in range(num_vertices):
        n = normals[v * 3:v * 3 + 3]
        length = math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2])
        if length > 1e-8:
            normals[v * 3] = n[0] / length
            normals[v * 3 + 1] = n[1] / length
            normals[v * 3 + 2] = n[2] / length
        else:
            normals[v * 3] = 0.0
            normals[v * 3 + 1] = 1.0
            normals[v * 3 + 2] = 0.0
    mesh.normals = normals
def _read_u32_array(data: bytes, off: int, length: int) -> List[int]:
    vals = struct.unpack_from("<%dI" % (length // 4), data, off) if length >= 4 else ()
    return list(vals)


def _read_mesh_block(data: bytes, off: int, size: int) -> Tuple[PODMesh, int]:
    """Parse an eSceneMesh (2012) container into a PODMesh."""
    mesh = PODMesh()
    end_tag = T.eSceneMesh | T.END_TAG

    interleaved: Optional[bytes] = None
    idx_element = _DataElement()
    pos_element = _DataElement()
    nrm_element = _DataElement()
    uv_element: Optional[_DataElement] = None
    bone_idx_element = _DataElement()
    bone_wgt_element = _DataElement()

    bone_batch_indices: List[int] = []
    bone_batch_counts: List[int] = []
    bone_batch_offsets: List[int] = []
    max_bones_per_batch = 0
    num_bone_batches = 0

    while off < size:
        tag, off = _u32(data, off, size)
        length, off = _u32(data, off, size)
        if off + length > size:
            length = size - off

        if tag == end_tag:
            break

        if tag == T.eMeshNumVertices:
            if length >= 4:
                mesh.num_vertices, off = _u32(data, off, size)
            else:
                off += length
        elif tag == T.eMeshNumFaces:
            if length >= 4:
                mesh.num_faces, off = _u32(data, off, size)
            else:
                off += length
        elif tag == T.eMeshInteravedDataList:
            interleaved = data[off:off + length]
            off += length
        elif tag == T.eMeshVertexIndexList:
            idx_element, off = _parse_vertex_block(data, off, size, tag)
        elif tag == T.eMeshVertexList:
            pos_element, off = _parse_vertex_block(data, off, size, tag)
        elif tag == T.eMeshNormalList:
            nrm_element, off = _parse_vertex_block(data, off, size, tag)
        elif tag == T.eMeshUVWList:
            current_uv, off = _parse_vertex_block(data, off, size, tag)
            if uv_element is None:
                uv_element = current_uv
        elif tag == T.eMeshBoneIndexList:
            bone_idx_element, off = _parse_vertex_block(data, off, size, tag)
        elif tag == T.eMeshBoneWeightList:
            bone_wgt_element, off = _parse_vertex_block(data, off, size, tag)
        elif tag == T.eMeshBoneBatchIndexList:
            bone_batch_indices = _read_u32_array(data, off, length)
            off += length
        elif tag == T.eMeshNumBoneIndicesPerBatch:
            bone_batch_counts = _read_u32_array(data, off, length)
            off += length
        elif tag == T.eMeshBoneOffsetPerBatch:
            bone_batch_offsets = _read_u32_array(data, off, length)
            off += length
        elif tag == T.eMeshMaxNumBonesPerBatch:
            if length >= 4:
                max_bones_per_batch, off = _u32(data, off, size)
            else:
                off += length
        elif tag == T.eMeshNumBoneBatches:
            if length >= 4:
                num_bone_batches, off = _u32(data, off, size)
            else:
                off += length
        elif tag == T.eMeshUnpackMatrix:
            if length >= 64:
                mesh.unpack_matrix = list(struct.unpack_from("<16f", data, off))
                mesh.has_unpack_matrix = True
            off += length
        elif tag == T.eMeshType:
            if length >= 4:
                mesh.mesh_type, off = _u32(data, off, size)
                if length > 4:
                    off += length - 4
            else:
                off += length
        else:
            off += length

    # Indices: quads (mesh_type 1) expand to triangles.
    _parse_indices(idx_element, mesh.num_faces, 4 if mesh.mesh_type == 1 else 3, mesh)

    mesh.positions = _unpack_vertex_data(interleaved, pos_element, mesh.num_vertices, 3)
    mesh.normals = _unpack_vertex_data(interleaved, nrm_element, mesh.num_vertices, 3)
    mesh.uvs = _unpack_vertex_data(interleaved, uv_element or _DataElement(), mesh.num_vertices, 2)

    # Synthesize normals when the normal block is missing or all-zero.
    normals_missing = (nrm_element.payload is None) or not mesh.normals
    if not normals_missing:
        sumsq = sum(float(n) * float(n) for n in mesh.normals)
        if sumsq < 1e-12:
            normals_missing = True
    if normals_missing:
        _synthesize_normals(mesh)

    mesh.bones_per_vertex = bone_idx_element.num_components
    if mesh.bones_per_vertex > 0:
        mesh.bone_indices = _unpack_vertex_data(interleaved, bone_idx_element,
                                                mesh.num_vertices, mesh.bones_per_vertex)
        mesh.bone_weights = _unpack_vertex_data(interleaved, bone_wgt_element,
                                                mesh.num_vertices, mesh.bones_per_vertex)

    if num_bone_batches > 0:
        mesh.has_bone_batches = True
        mesh.bone_batches = BoneBatch(
            indices=bone_batch_indices,
            counts=bone_batch_counts,
            offsets=bone_batch_offsets,
            max_bones=max_bones_per_batch,
            count=num_bone_batches,
        )

    if mesh.has_unpack_matrix and not _unpack_matrix_is_identity_or_degenerate(mesh.unpack_matrix):
        _apply_unpack_matrix(mesh.positions, mesh.normals, mesh.unpack_matrix)

    _compute_mesh_aabb(mesh)
    return mesh, off
def _read_node_block(data: bytes, off: int, size: int) -> Tuple[PODNode, int]:
    """Parse an eSceneNode (2013) container into a PODNode."""
    node = PODNode()
    end_tag = T.eSceneNode | T.END_TAG

    while off < size:
        tag, off = _u32(data, off, size)
        length, off = _u32(data, off, size)
        if off + length > size:
            length = size - off

        if tag == end_tag:
            break

        if tag == T.eNodeIndex:
            if length >= 4:
                raw, off = _u32(data, off, size)
                node.object_index = _to_i32(raw)
            else:
                off += length
        elif tag == T.eNodeName:
            node.name, off = _read_cstr(data, off, length)
        elif tag == T.eNodeMaterialIndex:
            if length >= 4:
                raw, off = _u32(data, off, size)
                node.material_index = _to_i32(raw)
            else:
                off += length
        elif tag == T.eNodeParentIndex:
            if length >= 4:
                raw, off = _u32(data, off, size)
                node.parent_index = _to_i32(raw)
            else:
                off += length
        elif tag == T.eNodePosition:
            if length >= 12:
                node.translation = [struct.unpack_from("<f", data, off + k)[0] for k in (0, 4, 8)]
                node.has_translation = True
                off += length
            else:
                off += length
        elif tag == T.eNodeRotation:
            if length >= 16:
                node.rotation = list(struct.unpack_from("<4f", data, off))
                node.has_rotation = True
                off += length
            else:
                off += length
        elif tag == T.eNodeScale:
            if length >= 12:
                node.scale = [struct.unpack_from("<f", data, off + k)[0] for k in (0, 4, 8)]
                node.has_scale = True
                off += length
            else:
                off += length
        elif tag == T.eNodeMatrix:
            if length >= 64:
                node.matrix = list(struct.unpack_from("<16f", data, off))
                node.has_matrix = True
                off += length
            else:
                off += length
        elif tag == T.eNodeAnimationPosition:
            node.anim_translation = list(struct.unpack_from("<%df" % (length // 4), data, off))
            off += length
        elif tag == T.eNodeAnimationRotation:
            node.anim_rotation = list(struct.unpack_from("<%df" % (length // 4), data, off))
            off += length
        elif tag == T.eNodeAnimationScale:
            node.anim_scale = list(struct.unpack_from("<%df" % (length // 4), data, off))
            off += length
        elif tag == T.eNodeAnimationMatrix:
            node.anim_matrix = list(struct.unpack_from("<%df" % (length // 4), data, off))
            off += length
        elif tag == T.eNodeAnimationFlags:
            if length >= 4:
                node.anim_flags, off = _u32(data, off, size)
            else:
                off += length
        elif tag == T.eNodeAnimationPositionIndex:
            node.anim_translation_idx = _read_u32_array(data, off, length)
            off += length
        elif tag == T.eNodeAnimationRotationIndex:
            node.anim_rotation_idx = _read_u32_array(data, off, length)
            off += length
        elif tag == T.eNodeAnimationScaleIndex:
            node.anim_scale_idx = _read_u32_array(data, off, length)
            off += length
        elif tag == T.eNodeAnimationMatrixIndex:
            node.anim_matrix_idx = _read_u32_array(data, off, length)
            off += length
        else:
            off += length

    return node, off


def _read_texture_block(data: bytes, off: int, size: int) -> Tuple[str, int]:
    """Parse an eSceneTexture (2014) container; returns the texture filename."""
    name = ""
    end_tag = T.eSceneTexture | T.END_TAG
    while off < size:
        tag, off = _u32(data, off, size)
        length, off = _u32(data, off, size)
        if off + length > size:
            length = size - off
        if tag == end_tag:
            break
        if tag == T.eTextureFilename:
            name, off = _read_cstr(data, off, length)
        else:
            off += length
    return name, off


def _read_material_block(data: bytes, off: int, size: int) -> Tuple[PODMaterial, int]:
    """Parse an eSceneMaterial (2015) container into a PODMaterial."""
    mat = PODMaterial()
    end_tag = T.eSceneMaterial | T.END_TAG
    while off < size:
        tag, off = _u32(data, off, size)
        length, off = _u32(data, off, size)
        if off + length > size:
            length = size - off
        if tag == end_tag:
            break
        if tag == T.eMaterialName:
            mat.name, off = _read_cstr(data, off, length)
        elif tag == T.eMaterialDiffuseTextureIndex:
            if length >= 4:
                raw, off = _u32(data, off, size)
                mat.diffuse_texture_index = _to_i32(raw)
            else:
                off += length
        elif tag == T.eMaterialOpacity:
            if length >= 4:
                mat.opacity = _f32(data, off)
                off += length
            else:
                off += length
        elif tag == T.eMaterialDiffuse:
            if length >= 12:
                mat.diffuse = list(struct.unpack_from("<3f", data, off))
                off += length
            else:
                off += length
        else:
            off += length
    return mat, off
def _read_scene_block(data: bytes, off: int, size: int, model: PODModel) -> int:
    """Parse the eScene (1001) container; fills `model`."""
    end_tag = T.eScene | T.END_TAG
    declared_meshes = declared_nodes = declared_textures = declared_materials = -1

    while off < size:
        tag, off = _u32(data, off, size)
        length, off = _u32(data, off, size)
        if off + length > size:
            length = size - off

        if tag == end_tag:
            def warn_if(label, decl, got):
                if decl >= 0 and decl != got:
                    model.warnings.append(
                        "scene declared %d %s but parsed %d" % (decl, label, got))
            warn_if("meshes", declared_meshes, len(model.meshes))
            warn_if("nodes", declared_nodes, len(model.nodes))
            warn_if("textures", declared_textures, len(model.texture_filenames))
            warn_if("materials", declared_materials, len(model.materials))
            break

        if tag == T.eSceneNumMeshes:
            if length >= 4:
                declared_meshes, off = _u32(data, off, size)
            else:
                off += length
        elif tag == T.eSceneNumNodes:
            if length >= 4:
                declared_nodes, off = _u32(data, off, size)
            else:
                off += length
        elif tag == T.eSceneNumMeshNodes:
            if length >= 4:
                model.num_mesh_nodes, off = _u32(data, off, size)
            else:
                off += length
        elif tag == T.eSceneNumTextures:
            if length >= 4:
                declared_textures, off = _u32(data, off, size)
            else:
                off += length
        elif tag == T.eSceneNumMaterials:
            if length >= 4:
                declared_materials, off = _u32(data, off, size)
            else:
                off += length
        elif tag == T.eSceneNumFrames:
            if length >= 4:
                model.num_frames, off = _u32(data, off, size)
            else:
                off += length
        elif tag == T.eSceneFPS:
            if length >= 4:
                raw, off = _u32(data, off, size)
                model.fps = float(raw)
                if model.fps <= 0.0:
                    model.fps = 30.0
            else:
                off += length
        elif tag == T.eSceneMesh:
            mesh, off = _read_mesh_block(data, off, size)
            model.meshes.append(mesh)
        elif tag == T.eSceneNode:
            node, off = _read_node_block(data, off, size)
            model.nodes.append(node)
        elif tag == T.eSceneTexture:
            name, off = _read_texture_block(data, off, size)
            model.texture_filenames.append(name)
        elif tag == T.eSceneMaterial:
            mat, off = _read_material_block(data, off, size)
            model.materials.append(mat)
        else:
            off += length

    return off


def pod_parse(data: bytes) -> PODModel:
    """Parse a POD model from an in-memory ``bytes`` buffer.

    Faithful port of ``av::pod_parse``, including the big-endian guard and
    graceful skipping of unknown top-level metadata blocks.
    """
    model = PODModel()
    size = len(data)
    off = 0

    # Endianness guard: the format is little-endian; a byte-swapped first tag
    # means a big-endian file, which the reference engine rejects.
    if size >= 4:
        first = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24)
        if first != T.eFormatVersion:
            swapped = ((first & 0xFF) << 24) | ((first & 0xFF00) << 8) | \
                      ((first & 0xFF0000) >> 8) | ((first & 0xFF000000) >> 24)
            if swapped == T.eFormatVersion:
                raise PODError(
                    "POD appears big-endian / not little-endian "
                    "(first tag 0x%08X). Refusing to parse." % first)

    while off < size:
        tag, off = _u32(data, off, size)
        length, off = _u32(data, off, size)
        if off + length > size:
            length = size - off

        if tag == T.eFormatVersion:
            if length > 0:
                model.version, off = _read_cstr(data, off, length)
            else:
                off += length
        elif tag == T.eScene:
            off = _read_scene_block(data, off, size, model)
        elif tag == T.eLoaderOptions:
            model.loader_options = bytes(data[off:off + length]).decode("latin-1", "replace")
            off += length
        elif tag == T.eLoaderOptions2:
            model.loader_options2 = bytes(data[off:off + length]).decode("latin-1", "replace")
            off += length
        else:
            off += length

    finalize_model_bounds(model)
    return model


def pod_load(path: str) -> PODModel:
    """Load and parse a POD file from disk.

    Animation-only POD merging (report Stage 3) is intentionally out of scope
    for Stage 1; ``pod_parse`` is the pure, faithful reader.
    """
    with open(path, "rb") as f:
        data = f.read()
    return pod_parse(data)


# Re-export for a uniform, small public surface.
__all__ = ["PODError", "pod_parse", "pod_load"]
