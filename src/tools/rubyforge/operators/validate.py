# rubyforge.operators.validate — Stage 5: asset health diagnostics operator.

from __future__ import annotations

import json

from ..core.validator import format_report, validate_scene

try:
    import bpy
    from bpy.types import Operator
    HAS_BPY = True
except Exception:  # pragma: no cover
    bpy = None
    Operator = object
    HAS_BPY = False

REGISTER = []


def collect_scene_stats(scene=None):
    """Gather the Blender-side stats the rule engine consumes (JSON-able)."""
    scene = scene or bpy.context.scene
    textures = []
    for image in bpy.data.images:
        if image.source == "FILE" and image.size[0] > 0:
            textures.append({"name": image.name,
                             "width": image.size[0], "height": image.size[1]})

    objects = []
    for obj in scene.objects:
        if obj.type != "MESH":
            continue
        smooth = 0
        for v in obj.data.vertices:
            if len(v.groups) > 1 and sum(1 for g in v.groups if g.weight > 0.05) > 1:
                smooth += 1
        arm = None
        for mod in obj.modifiers:
            if mod.type == "ARMATURE" and mod.object is not None:
                arm = mod.object
                break
        if arm is None and obj.parent is not None and obj.parent.type == "ARMATURE":
            arm = obj.parent
        objects.append({
            "name": obj.name,
            "vertices": len(obj.data.vertices),
            "faces": len(obj.data.polygons),
            "scale": [obj.scale.x, obj.scale.y, obj.scale.z],
            "smooth_weight_vertices": smooth,
            "bone_groups": [vg.name for vg in obj.vertex_groups],
            "armature": arm.name if arm is not None else None,
        })

    armatures = []
    for obj in scene.objects:
        if obj.type != "ARMATURE":
            continue
        names = [b.name for b in obj.data.bones]
        armatures.append({
            "name": obj.name,
            "bones": len(names),
            "invalid_names": [n for n in names if not n.startswith("Bone")],
        })

    has_center = any(o.type == "EMPTY" and o.name == "CenterPoint"
                     for o in scene.objects)
    return {
        "fps": float(scene.render.fps),
        "objects": objects,
        "armatures": armatures,
        "textures": textures,
        "has_center_point": has_center,
    }


if HAS_BPY:

    class SWORDIGO_OT_validate(Operator):
        """Run the Swordigo asset health diagnostics on the scene."""
        bl_idname = "swordigo.ot_validate"
        bl_label = "Validate Swordigo Asset"
        bl_description = ("Check vertex/bone caps, Bone prefixes, rigid "
                          "weights, 24 FPS, CenterPoint and texture POT")

        def execute(self, context):
            report = validate_scene(collect_scene_stats(context.scene))
            lines = format_report(report).split("\\n")
            for line in lines:
                print("[rubyforge]", line)
            try:
                context.window_manager.clipboard = format_report(report)
            except Exception:
                pass
            if report.ok:
                self.report({"WARNING"}, "Validation PASS (%d warnings) — report copied"
                            % len(report.warnings))
            else:
                self.report({"ERROR"}, "Validation FAIL: %d errors — report copied"
                            % len(report.errors))
            return {"FINISHED"}

    REGISTER.append(SWORDIGO_OT_validate)
