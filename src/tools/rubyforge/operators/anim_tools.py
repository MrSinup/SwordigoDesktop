# rubyforge.operators.anim_tools — Stage 3: the 24 FPS action pipeline.
#
#   * Export Current Action — writes an animation-only POD (0 meshes,
#     per-bone node tracks, NumFrames/FPS header) exactly like the stock
#     hiro_run.POD / hiro_jump.POD clips.
#   * Import Animation POD  — samples a clip onto the selected armature as
#     a new Action (bones bound strictly by NAME, like CreateAnimation).
#
# Math lives in rubyforge.core.animation / .rigging; this file is bpy wiring.

from __future__ import annotations

import os

from ..core.animation import (
    SWORDIGO_FPS,
    anim_tracks_from_locals,
    animation_model_from_tracks,
    pose_spec_from_model,
)
from ..core.pod_reader import PODError, pod_load
from ..core.pod_writer import pod_write
from ..core.rigging import blender_local_to_pod, is_valid_bone_name

try:
    import bpy
    from bpy.props import BoolProperty, StringProperty
    from bpy.types import Operator
    HAS_BPY = True
except Exception:  # pragma: no cover
    bpy = None
    Operator = object
    BoolProperty = StringProperty = lambda default="", **kw: default
    HAS_BPY = False

REGISTER = []


def rig_spec_from_armature(arm):
    """Build a rig spec from a live Blender armature (no import metadata)."""
    from ..core.rigging import pod_local_matrix as _unused  # noqa: F401
    bones = []
    for bone in arm.data.bones:
        if not is_valid_bone_name(bone.name):
            continue
        local = [c for c in bone.matrix_local]          # 4x4 row-major bpy
        # bpy matrices are row-major 4x4; convert to column-major flat.
        col = _bpy_matrix_to_column_major(local)
        pod_local = blender_local_to_pod(col)
        parent = bone.parent.name if bone.parent is not None else None
        head = [bone.head_local.x, bone.head_local.y, bone.head_local.z]
        tail = [bone.tail_local.x, bone.tail_local.y, bone.tail_local.z]
        bones.append({
            "name": bone.name,
            "node_index": -1,
            "parent": parent,
            "pod_local_matrix": pod_local,
            "rest_blender": col,
            "head": head,
            "tail": tail,
        })
    return {"bones": bones, "center_point": None}


def _bpy_matrix_to_column_major(m):
    """bpy 4x4 (row-major nested rows) -> flat column-major list of 16."""
    return [m[r][c] for c in range(4) for r in range(4)]


def _active_armature(context):
    obj = context.active_object
    if obj is not None and obj.type == "ARMATURE":
        return obj
    for o in context.selected_objects:
        if o.type == "ARMATURE":
            return o
    for o in context.scene.objects:
        if o.type == "ARMATURE":
            return o
    return None


if HAS_BPY:

    class SWORDIGO_OT_export_action(Operator):
        """Export the active armature's Action as an animation-only POD."""
        bl_idname = "swordigo.ot_export_action"
        bl_label = "Export Action as Animation POD"
        bl_description = ("Write the current Action as a Swordigo animation "
                          "clip (0 meshes, per-bone tracks, 24 FPS)")
        bl_options = {"REGISTER"}

        filepath: StringProperty(
            name="POD file", subtype="FILE_PATH",
            description="Path to write the animation .pod")

        def execute(self, context):
            arm = _active_armature(context)
            if arm is None:
                self.report({"ERROR"}, "No armature in the scene")
                return {"CANCELLED"}
            action = arm.animation_data.action if arm.animation_data else None
            if action is None:
                self.report({"ERROR"}, "Armature has no active Action")
                return {"CANCELLED"}

            scene = context.scene
            fps = float(scene.render.fps) or SWORDIGO_FPS
            start, end = action.frame_range
            frame_count = max(1, int(round(end - start)) + 1)
            frame_start = int(round(start))

            rig = rig_spec_from_armature(arm)
            rest = {b["name"]: b["pod_local_matrix"] for b in rig["bones"]}
            if not rest:
                self.report({"ERROR"}, "Armature has no 'Bone*' bones")
                return {"CANCELLED"}

            locals_per_bone = {name: [] for name in rest}
            pose_bones = arm.pose.bones
            for f in range(frame_count):
                scene.frame_set(frame_start + f)
                for name in rest:
                    pb = pose_bones.get(name)
                    if pb is None:
                        locals_per_bone[name].append(rest[name])
                        continue
                    basis = _bpy_matrix_to_column_major(pb.matrix_basis)
                    locals_per_bone[name].append(blender_local_to_pod(basis))
            scene.frame_set(frame_start)

            tracks = anim_tracks_from_locals(locals_per_bone)
            model = animation_model_from_tracks(
                tracks, list(rest.keys()), rest, frame_count, fps)
            try:
                pod_write(model, self.filepath)
            except OSError as exc:
                self.report({"ERROR"}, "Write failed: %s" % exc)
                return {"CANCELLED"}
            self.report({"INFO"}, "Exported %s (%d frames @ %.0f fps, %d bones)" % (
                os.path.basename(self.filepath), frame_count, fps, len(rest)))
            return {"FINISHED"}


    class SWORDIGO_OT_import_animation(Operator):
        """Import an animation POD onto the selected armature as an Action."""
        bl_idname = "swordigo.ot_import_animation"
        bl_label = "Import Swordigo Animation (.pod)"
        bl_description = ("Sample a Swordigo animation clip onto the active "
                          "armature (bones bind by name, 24 FPS)")
        bl_options = {"REGISTER", "UNDO"}

        filepath: StringProperty(
            name="POD file", subtype="FILE_PATH",
            description="Path to an animation .pod")

        def execute(self, context):
            arm = _active_armature(context)
            if arm is None:
                self.report({"ERROR"}, "No armature in the scene")
                return {"CANCELLED"}
            try:
                model = pod_load(self.filepath)
            except (PODError, OSError) as exc:
                self.report({"ERROR"}, "Failed to read POD: %s" % exc)
                return {"CANCELLED"}
            if model.num_frames <= 0:
                self.report({"ERROR"}, "POD carries no animation frames")
                return {"CANCELLED"}

            rig = rig_spec_from_armature(arm)
            pose = pose_spec_from_model(model, rig)
            if not pose["bones"]:
                self.report({"WARNING"}, "No bone names matched the armature")
                return {"CANCELLED"}

            action_name = os.path.splitext(os.path.basename(self.filepath))[0]
            action = bpy.data.actions.new(action_name)
            action.use_fake_user = True
            if arm.animation_data is None:
                arm.animation_data_create()
            arm.animation_data.action = action
            if hasattr(action, "use_frame_range"):
                action.use_frame_range = False

            inserted = 0
            for bone_name, channels in pose["bones"].items():
                pb = arm.pose.bones.get(bone_name)
                if pb is None:
                    continue
                pb.rotation_mode = "QUATERNION"
                path = pb.path_from_id("location")
                for axis in range(3):
                    fc = action.fcurves.new(path, index=axis)
                    for f, val in enumerate(channels["location"]):
                        fc.keyframe_points.add(1)
                        fc.keyframe_points[-1].co = (f, val[axis])
                    fc.update()
                    inserted += 1
                path = pb.path_from_id("rotation_quaternion")
                for axis in range(4):
                    fc = action.fcurves.new(path, index=axis)
                    for f, val in enumerate(channels["rotation_quaternion"]):
                        fc.keyframe_points.add(1)
                        fc.keyframe_points[-1].co = (f, val[axis])
                    fc.update()
                    inserted += 1
                path = pb.path_from_id("scale")
                for axis in range(3):
                    fc = action.fcurves.new(path, index=axis)
                    for f, val in enumerate(channels["scale"]):
                        fc.keyframe_points.add(1)
                        fc.keyframe_points[-1].co = (f, val[axis])
                    fc.update()
                    inserted += 1

            scene = context.scene
            scene.render.fps = 24
            scene.frame_start = 0
            scene.frame_end = max(0, pose["frame_count"] - 1)
            self.report({"INFO"}, "Imported %s (%d bones, %d frames, %d fcurves)" % (
                os.path.basename(self.filepath), len(pose["bones"]),
                pose["frame_count"], inserted))
            return {"FINISHED"}

    REGISTER.extend([SWORDIGO_OT_export_action, SWORDIGO_OT_import_animation])
