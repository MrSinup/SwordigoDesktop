"""rubyforge.core.validator — Stage 5: asset health diagnostics.

Implements the research report §15 diagnostic rule matrix. Rules operate on
two inputs so they can run both from an IR (headless / tests) and from a
live Blender scene (the ``rubyforge.validate`` operator feeds it
:func:`collect_scene_stats` output):

  * :func:`validate_model`    — a :class:`rubyforge.core.ir.PODModel`
  * :func:`validate_scene`    — a plain stats dict collected by bpy

Every rule maps 1:1 to a runtime constraint decoded from libswordigo
(65,535 vertex cap, 100-bone cap, "Bone" prefix, 24 FPS, rigid skinning).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, List, Optional

from .ir import PODModel
from .materials import texture_pot_warning
from .ir import PODMesh
from .rigging import is_valid_bone_name, resolve_bone_node_index

__all__ = [
    "MAX_VERTICES", "MAX_BONES", "SMOOTH_WEIGHT_EPSILON",
    "ValidationReport",
    "validate_model", "validate_scene", "format_report", "report_to_lines",
]

MAX_VERTICES = 65535       # 16-bit indices (Caver::Mesh::AllocIndexBuffer)
MAX_BONES = 100            # dwBoneLimit (temp.jpod / engine skeleton)
SMOOTH_WEIGHT_EPSILON = 0.05


@dataclass
class ValidationReport:
    errors: List[str] = field(default_factory=list)    # blocking
    warnings: List[str] = field(default_factory=list)  # non-blocking
    info: List[str] = field(default_factory=list)      # statistics

    @property
    def ok(self) -> bool:
        return not self.errors

    def extend(self, other: "ValidationReport") -> None:
        self.errors.extend(other.errors)
        self.warnings.extend(other.warnings)
        self.info.extend(other.info)


def validate_model(model: PODModel) -> ValidationReport:
    """Run the IR-level rules (headless; also the round-trip safety net)."""
    rep = ValidationReport()
    rep.info.append("version: %s" % (model.version or "(none)"))
    rep.info.append("meshes: %d, nodes: %d (mesh nodes %d)" % (
        len(model.meshes), len(model.nodes), model.num_mesh_nodes))
    rep.info.append("vertices: %d, faces: %d" % (model.total_vertices, model.total_faces))
    rep.info.append("frames: %d @ %.1f fps" % (model.num_frames, model.fps))

    if not model.has_center_point:
        rep.warnings.append("MISSING_CENTER_POINT: no 'CenterPoint' node — "
                            "the model will pivot at the origin in-game")
    else:
        rep.info.append("center point: (%.3f, %.3f, %.3f)" % tuple(model.center_point))

    if model.total_vertices > MAX_VERTICES:
        rep.errors.append("VERTEX_OVERFLOW: %d vertices exceed the %d "
                          "16-bit index cap" % (model.total_vertices, MAX_VERTICES))

    bones = [n for n in model.nodes if n.name.startswith("Bone")]
    if len(bones) > MAX_BONES:
        rep.errors.append("BONE_COUNT_EXCEEDED: %d bones exceed the %d limit"
                          % (len(bones), MAX_BONES))

    # Skinned meshes must reference "Bone"-prefixed nodes only.
    for mi, mesh in enumerate(model.meshes):
        if mesh.bones_per_vertex <= 0:
            if mesh.num_vertices:
                rep.info.append("mesh[%d]: rigid (%d verts, no skin)" % (mi, mesh.num_vertices))
            continue
        rep.info.append("mesh[%d]: %d verts, %d influences/vertex"
                        % (mi, mesh.num_vertices, mesh.bones_per_vertex))
        if mesh.bones_per_vertex > 1:
            rep.warnings.append(
                "SMOOTH_WEIGHTS: mesh[%d] carries %d influences per vertex; "
                "the runtime uses only the first (rigid skinning)"
                % (mi, mesh.bones_per_vertex))
        reported_nodes = set()
        n_slots = len(mesh.bone_indices) // mesh.bones_per_vertex
        for slot in range(n_slots):
            for k in range(mesh.bones_per_vertex):
                raw = int(mesh.bone_indices[slot * mesh.bones_per_vertex + k])
                idx = resolve_bone_node_index(mesh, raw)
                if idx < 0 or idx >= len(model.nodes):
                    key = ("oob", idx)
                    if key not in reported_nodes:
                        reported_nodes.add(key)
                        rep.errors.append(
                            "INVALID_BONE_NAME: mesh[%d] references node %d "
                            "outside the node list" % (mi, idx))
                    break
                name = model.nodes[idx].name
                if not is_valid_bone_name(name):
                    key = ("name", idx)
                    if key not in reported_nodes:
                        reported_nodes.add(key)
                        rep.errors.append(
                            "INVALID_BONE_NAME: mesh[%d] references node %d '%s' "
                            "without the 'Bone' prefix — it would be ignored by "
                            "CreateSkeleton" % (mi, idx, name))
                    break

    if model.num_frames > 0 and abs(model.fps - 24.0) > 0.01:
        rep.warnings.append("FRAMERATE_MISMATCH: clip is %.1f fps; the engine "
                            "plays everything at 24" % model.fps)

    for ti, tex in enumerate(model.texture_filenames):
        rep.info.append("texture[%d]: %s" % (ti, tex or "(unnamed)"))

    return rep


def validate_scene(stats: Dict) -> ValidationReport:
    """Run the scene-level rules on :func:`collect_scene_stats` output."""
    rep = ValidationReport()

    fps = stats.get("fps")
    if fps is not None and abs(fps - 24.0) > 0.01:
        rep.warnings.append("FRAMERATE_MISMATCH: scene is %.1f fps — click "
                            "'Lock 24 FPS' (the engine is hardcoded to 24)" % fps)

    center = stats.get("has_center_point", False)
    if not center:
        rep.warnings.append("MISSING_CENTER_POINT: no 'CenterPoint' empty in "
                            "the scene — run 'Add CenterPoint'")

    for obj in stats.get("objects", []):
        name = obj.get("name", "?")
        scale = obj.get("scale") or [1.0, 1.0, 1.0]
        if any(abs(s - 1.0) > 1e-4 for s in scale):
            rep.warnings.append(
                "NON_UNIT_SCALE: object '%s' has scale (%.3f, %.3f, %.3f) — "
                "apply transforms (Ctrl+A) or CPU skinning distorts"
                % (name, scale[0], scale[1], scale[2]))

        verts = obj.get("vertices", 0)
        if verts > MAX_VERTICES:
            rep.errors.append("VERTEX_OVERFLOW: '%s' has %d vertices (cap %d) "
                              "— decimate or split the mesh" % (name, verts, MAX_VERTICES))
        else:
            rep.info.append("object '%s': %d verts, %d faces"
                            % (name, verts, obj.get("faces", 0)))

        smooth = obj.get("smooth_weight_vertices", 0)
        if smooth > 0:
            rep.warnings.append(
                "SMOOTH_WEIGHTS: '%s' has %d vertices with multiple bone "
                "influences — run 'Rigidify Vertex Weights'" % (name, smooth))

        for group in obj.get("bone_groups", []):
            if not is_valid_bone_name(group):
                rep.errors.append(
                    "INVALID_BONE_NAME: '%s' deform group '%s' lacks the "
                    "'Bone' prefix — rename via 'Auto-Prefix Bones'"
                    % (name, group))

    for arm in stats.get("armatures", []):
        count = arm.get("bones", 0)
        rep.info.append("armature '%s': %d bones" % (arm.get("name", "?"), count))
        if count > MAX_BONES:
            rep.errors.append("BONE_COUNT_EXCEEDED: '%s' has %d bones (cap %d)"
                              % (arm.get("name", "?"), count, MAX_BONES))
        for bone in arm.get("invalid_names", []):
            rep.errors.append("INVALID_BONE_NAME: armature bone '%s' lacks the "
                              "'Bone' prefix" % bone)

    for tex in stats.get("textures", []):
        warn = texture_pot_warning(tex.get("width", 0), tex.get("height", 0))
        if warn:
            rep.warnings.append("NON_POT_TEXTURE: '%s' — %s"
                                % (tex.get("name", "?"), warn))

    if not stats.get("objects"):
        rep.warnings.append("No mesh objects found in the scene")

    return rep


def report_to_lines(report: ValidationReport) -> List[str]:
    lines = []
    lines.append("RubyForge validation: %s" %
                 ("PASS" if report.ok else "FAIL (%d errors)" % len(report.errors)))
    for e in report.errors:
        lines.append("  [ERROR]   " + e)
    for w in report.warnings:
        lines.append("  [WARNING] " + w)
    for i in report.info:
        lines.append("  [info]    " + i)
    return lines


def format_report(report: ValidationReport) -> str:
    return "\\n".join(report_to_lines(report))
