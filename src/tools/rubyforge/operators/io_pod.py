"""rubyforge.operators.io_pod — File -> Import/Export Swordigo POD operators.

Blender 4.2+ extension operators that bridge the pure-Python POD reader /
writer (``rubyforge.core``) to the Blender scene graph.

  * ``SWORDIGO_OT_import_pod`` — File -> Import -> Swordigo Model (.pod)
  * ``SWORDIGO_OT_export_pod`` — File -> Export -> Swordigo Model (.pod)

Geometry is converted between the POD / Caver +Y-up space and Blender's
+Z-up space using the explicit basis change from ``core.transforms`` (this is
the direct, non-glTF path — do NOT apply it again if the user goes through
``io_scene_gltf2``).

Round-trip metadata (node hierarchy, transforms, animation streams,
materials, textures) is stashed on object / material custom properties
(``swordigo_node`` etc.) so untouched data survives an edit + re-export.

All pure mapping helpers live below the bpy import so the module can be
imported (for lint / unit tests) even where bpy is unavailable.
"""

from __future__ import annotations

import json
import os
from typing import List, Optional

from ..core.ir import PODMaterial, PODMesh, PODModel, PODNode
from ..core.pod_reader import PODError, pod_load
from ..core.pod_writer import pod_write
from ..core.transforms import (
    apply_pod_to_blender_basis,
    blender_to_pod_basis,
)

# ─── bpy availability ─────────────────────────────────────────────────
try:
    import bpy  # type: ignore
    from bpy.types import Operator
    from bpy.props import BoolProperty, StringProperty  # type: ignore
    HAS_BPY = True
except Exception:  # pragma: no cover - only when bpy is absent
    bpy = None
    Operator = object
    BoolProperty = StringProperty = lambda default=0, **kw: default
    HAS_BPY = False

_NODE_KEY = "swordigo_node"
_MATERIAL_KEY = "swordigo_material"
_AUTO_BASIS = "auto"

# Reload-safe registration order (Blender 4.x prefers an ordered list that is
# reversed in unregister()).
REGISTER = []


# ─── Pure helpers (Blender-free) ──────────────────────────────────────
def sanitize_object_name(name: str) -> str:
    """Blender-safe object name derived from a POD node name."""
    cleaned = "".join(ch if ch.isalnum() or ch in "._-" else "_" for ch in name)
    cleaned = cleaned.strip("._ ") or "SwordigoMesh"
    return cleaned


def pod_to_blender_geometry(mesh: PODMesh) -> dict:
    """Return Blender-ready flat arrays (positions/normals, Z-up) + indices."""
    return {
        "positions": apply_pod_to_blender_basis(mesh.positions),
        "normals": apply_pod_to_blender_basis(mesh.normals),
        "uvs": list(mesh.uvs),
        "indices": list(mesh.indices),
    }


# ─── Export-side helpers ──────────────────────────────────────────────
def blender_to_pod_mesh(bpy_mesh) -> PODMesh:
    """Rebuild a PODMesh from a Blender mesh, converting back to Y-up."""
    mesh = PODMesh()
    mesh.num_vertices = len(bpy_mesh.vertices)
    mesh.num_faces = len(bpy_mesh.polygons)

    positions = []
    normals = []
    for v in bpy_mesh.vertices:
        positions += [v.co.x, v.co.y, v.co.z]
        n = v.normal if hasattr(v, "normal") else None
        if n is not None:
            normals += [n[0], n[1], n[2]]
    # Normals default: v.normal may be a tuple; fall back to 0,0,0 -> reader
    # would synthesize; better to emit actual per-vertex normals when present.
    if normals and len(normals) == len(positions):
        mesh.normals = blender_to_pod_basis_array(normals)
    mesh.positions = blender_to_pod_basis_array(positions)

    uvs = []
    try:
        uv_layer = bpy_mesh.uv_layers[0] if bpy_mesh.uv_layers else None
        if uv_layer is not None:
            for uv in uv_layer.data:
                uvs += [uv.uv.x, uv.uv.y]
    except (IndexError, AttributeError):
        uvs = []
    mesh.uvs = uvs

    indices = []
    for poly in bpy_mesh.polygons:
        indices += list(poly.vertices)
    mesh.indices = indices

    return mesh


def blender_to_pod_basis_array(flat_xyz: List[float]) -> List[float]:
    """Blender (Z-up) -> POD (Y-up): (x, y, z) -> (x, z, -y)."""
    return list(blender_to_pod_basis_flat(flat_xyz))


def blender_to_pod_basis_flat(flat_xyz: List[float]) -> List[float]:
    """Inverse of :func:`apply_pod_to_blender_basis`: (x, y, z) -> (x, z, -y)."""
    out = [0.0] * len(flat_xyz)
    for i in range(0, len(flat_xyz) - 2, 3):
        out[i], out[i + 1], out[i + 2] = flat_xyz[i], flat_xyz[i + 2], -flat_xyz[i + 1]
    return out


def _bpy_clear_scene() -> None:
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for collection in list(bpy.data.collections):
        bpy.data.collections.remove(collection)


def _node_to_metadata(node: PODNode) -> dict:
    """Serialize a PODNode's round-trip metadata (json-able)."""
    return {
        "name": node.name,
        "object_index": node.object_index,
        "parent_index": node.parent_index,
        "material_index": node.material_index,
        "has_matrix": node.has_matrix,
        "matrix": list(node.matrix),
        "has_translation": node.has_translation,
        "translation": list(node.translation),
        "has_rotation": node.has_rotation,
        "rotation": list(node.rotation),
        "has_scale": node.has_scale,
        "scale": list(node.scale),
        "anim_translation": list(node.anim_translation),
        "anim_rotation": list(node.anim_rotation),
        "anim_scale": list(node.anim_scale),
        "anim_matrix": list(node.anim_matrix),
        "anim_flags": node.anim_flags,
        "anim_translation_idx": list(node.anim_translation_idx),
        "anim_rotation_idx": list(node.anim_rotation_idx),
        "anim_scale_idx": list(node.anim_scale_idx),
        "anim_matrix_idx": list(node.anim_matrix_idx),
    }


def _metadata_to_node(data: dict, obj) -> PODNode:
    node = PODNode()
    node.name = data.get("name") or (obj.name if obj is not None else "Node")
    node.object_index = data.get("object_index", -1)
    node.parent_index = data.get("parent_index", -1)
    node.material_index = data.get("material_index", -1)
    node.has_matrix = data.get("has_matrix", False)
    node.matrix = data.get("matrix", [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])
    node.has_translation = data.get("has_translation", False)
    node.translation = data.get("translation", [0, 0, 0])
    node.has_rotation = data.get("has_rotation", False)
    node.rotation = data.get("rotation", [0, 0, 0, 1])
    node.has_scale = data.get("has_scale", False)
    node.scale = data.get("scale", [1, 1, 1])
    node.anim_translation = data.get("anim_translation", [])
    node.anim_rotation = data.get("anim_rotation", [])
    node.anim_scale = data.get("anim_scale", [])
    node.anim_matrix = data.get("anim_matrix", [])
    node.anim_flags = data.get("anim_flags", 0)
    node.anim_translation_idx = data.get("anim_translation_idx", [])
    node.anim_rotation_idx = data.get("anim_rotation_idx", [])
    node.anim_scale_idx = data.get("anim_scale_idx", [])
    node.anim_matrix_idx = data.get("anim_matrix_idx", [])
    return node


def _apply_bpy_mesh(blender_mesh, geom: dict) -> None:
    """Push geometry into an existing bpy mesh datablock."""
    positions = geom["positions"]
    indices = geom["indices"]
    n_verts = len(positions) // 3

    # Triangle list -> polygon tuples (POD is strictly triangle-based).
    faces = [tuple(indices[i:i + 3]) for i in range(0, len(indices) - 2, 3)]
    try:
        blender_mesh.clear()
        blender_mesh.from_pydata(positions, [], faces)
    except Exception:
        # Fallback: explicit vertex/polygon construction.
        blender_mesh.vertices.add(n_verts)
        blender_mesh.polygons.add(len(faces))
        for i in range(n_verts):
            blender_mesh.vertices[i].co = positions[i * 3:i * 3 + 3]
        for f, face in enumerate(faces):
            blender_mesh.polygons[f].vertices = list(face)

    # UV layer. Blender stores V=0 at the bottom; POD UVs are authored in
    # standard bottom-left space already, so no flip here — the invariant is
    # UV_pod == UV_blender. UVs are per-vertex in POD, so they are written
    # through each polygon loop's vertex index.
    uvs = geom["uvs"]
    if uvs and n_verts > 0:
        try:
            uv = blender_mesh.uv_layers[0] if blender_mesh.uv_layers else blender_mesh.uv_layers.new()
            for loop in blender_mesh.loops:
                v = loop.vertex_index
                if v * 2 + 1 < len(uvs):
                    uv.data[loop.index].uv = (uvs[v * 2], uvs[v * 2 + 1])
        except Exception:
            pass

    # Per-vertex normals (split-normal friendly). Best-effort; the POD reader
    # synthesizes normals on export if none are present.
    try:
        normals = geom.get("normals") or []
        if normals and hasattr(blender_mesh.vertices, "foreach_set"):
            blender_mesh.vertices.foreach_set("normal", normals)
    except Exception:
        pass
# ─── Import operator ──────────────────────────────────────────────────
if HAS_BPY:

    class SWORDIGO_OT_import_pod(Operator):
        """Import a Swordigo POD model into the active scene."""
        bl_idname = "swordigo.ot_import_pod"
        bl_label = "Import Swordigo Model (.pod)"
        bl_description = "Import a Swordigo POD model, preserving POD metadata."
        bl_category = "Import-Export"

        filepath: StringProperty(
            name="POD file",
            subtype="FILE_PATH",
            description="Path to a Swordigo .pod file")

        clear_scene: BoolProperty(
            name="Clear Scene",
            default=False,
            description="Remove all existing objects before importing")

        def execute(self, context):
            if not os.path.isfile(self.filepath):
                self.report({"ERROR"}, "No POD file at: %s" % self.filepath)
                return {"CANCELLED"}

            try:
                model = pod_load(self.filepath)
            except (PODError, OSError) as exc:
                self.report({"ERROR"}, "Failed to read POD: %s" % exc)
                return {"CANCELLED"}

            if not model.meshes:
                self.report({"ERROR"},
                            "POD has no meshes (animation-only PODs are a "
                            "Stage-3 feature on the direct path).")
                return {"CANCELLED"}

            if self.clear_scene:
                _bpy_clear_scene()

            _lock_scene_fps()

            imported = import_model_to_blender(model)
            self.report({"INFO"}, "Imported %d mesh nodes, %d bones from %s" % (
                imported["mesh_nodes"], imported["bones"], os.path.basename(self.filepath)))
            return {"FINISHED"}


    class SWORDIGO_OT_export_pod(Operator):
        """Export the active scene back to a Swordigo POD file."""
        bl_idname = "swordigo.ot_export_pod"
        bl_label = "Export Swordigo Model (.pod)"
        bl_description = "Export mesh objects to a Swordigo POD file."
        bl_category = "Import-Export"

        filepath: StringProperty(
            name="POD file",
            subtype="FILE_PATH",
            description="Path to write the Swordigo .pod")

        def execute(self, context):
            model = export_blender_to_model()
            try:
                pod_write(model, self.filepath)
            except OSError as exc:
                self.report({"ERROR"}, "Write failed: %s" % exc)
                return {"CANCELLED"}
            self.report({"INFO"}, "Exported %s (%d vertices, %d faces)" % (
                os.path.basename(self.filepath), model.total_vertices, model.total_faces))
            return {"FINISHED"}
def _lock_scene_fps() -> None:
    """Force the active scene to 24 FPS (Swordigo animation contract)."""
    try:
        for sc in bpy.data.scenes:
            for attr in ("fps", "frame_rate"):
                if hasattr(sc.render, attr):
                    setattr(sc.render, attr, 24)
    except Exception:
        pass


def import_model_to_blender(model: PODModel) -> dict:
    """Build Blender objects from a PODModel; returns import statistics."""
    stats = {"mesh_nodes": 0, "bones": 0, "meshes": 0}
    collection = (bpy.data.collections.get("Swordigo")
                  or bpy.data.collections.new("Swordigo"))

    # Materials / textures (minimal: diffuse colour + opacity).
    for mat in model.materials:
        bmat = bpy.data.materials.new(mat.name or "Default")
        try:
            bmat.diffuse_color = (mat.diffuse[0], mat.diffuse[1], mat.diffuse[2])
            bmat.opacity = mat.opacity
        except Exception:
            pass
        bmat[_MATERIAL_KEY] = {
            "name": mat.name,
            "diffuse_texture_index": mat.diffuse_texture_index,
            "opacity": mat.opacity,
            "diffuse": list(mat.diffuse),
        }

    # Mesh nodes (first num_mesh_nodes nodes reference meshes).
    for node_index in range(min(model.num_mesh_nodes, len(model.nodes))):
        node = model.nodes[node_index]
        if node.object_index < 0 or node.object_index >= len(model.meshes):
            continue
        mesh = model.meshes[node.object_index]
        geom = pod_to_blender_geometry(mesh)

        name = sanitize_object_name(node.name) or "SwordigoMesh"
        bdata = bpy.data.meshes.new(name + "_mesh")
        _apply_bpy_mesh(bdata, geom)
        obj = bpy.data.objects.new(name, bdata)
        collection.objects.link(obj)
        obj[_NODE_KEY] = json.dumps(_node_to_metadata(node))

        # Bone vertex groups (rigid single-bone skinning). POD bone indices
        # reference scene nodes; use the node's own "Bone*" name when valid.
        if mesh.bones_per_vertex > 0:
            bpv = mesh.bones_per_vertex
            bone_idx = mesh.bone_indices
            n_slots = len(bone_idx) // bpv
            for b in range(bpv):
                vg_name = "Bone%d" % b
                if b < len(model.nodes) and model.nodes[b].name:
                    vg_name = sanitize_object_name(model.nodes[b].name)
                vg = bdata.vertex_groups.new(name=vg_name)
                verts = [i for i in range(min(mesh.num_vertices, n_slots))
                         if bone_idx[i * bpv + b] > 0.0]
                if verts:
                    vg.add(verts, 1.0, "REPLACE")
                stats["bones"] += 1

        stats["meshes"] += 1
        stats["mesh_nodes"] += 1

    # CenterPoint pivot.
    for node in model.nodes:
        if not node.is_center_point:
            continue
        empty = bpy.data.objects.new("CenterPoint", None)
        if node.has_matrix:
            empty.location = (node.matrix[12], node.matrix[13], node.matrix[14])
        else:
            empty.location = tuple(node.translation)
        collection.objects.link(empty)
        empty[_NODE_KEY] = json.dumps(_node_to_metadata(node))
        break

    return stats

def export_blender_to_model() -> PODModel:
    """Rebuild a PODModel from the active scene (round-trip aware)."""
    model = PODModel()
    mesh_count = 0

    for obj in bpy.data.objects:
        if obj.type != "MESH":
            continue
        bdata = obj.data

        node = PODNode()
        meta = obj.get(_NODE_KEY)
        if meta:
            node = _metadata_to_node(json.loads(meta), obj)
            node.object_index = -1
        else:
            node.name = obj.name
            node.has_translation = True
            node.translation = [obj.location.x, obj.location.y, obj.location.z]

        mesh = blender_to_pod_mesh(bdata)
        model.meshes.append(mesh)
        node.object_index = mesh_count
        model.nodes.append(node)
        mesh_count += 1

    model.num_mesh_nodes = mesh_count
    model.version = "AB.POD.2.0"
    model.fps = 24.0
    model.num_frames = 0

    _recompute_bounds(model)
    return model


def _recompute_bounds(model: PODModel) -> None:
    from ..core.transforms import finalize_model_bounds
    finalize_model_bounds(model)


if HAS_BPY:
    REGISTER.extend([SWORDIGO_OT_import_pod, SWORDIGO_OT_export_pod])