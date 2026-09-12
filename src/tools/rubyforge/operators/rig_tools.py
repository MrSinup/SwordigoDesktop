# rubyforge.operators.rig_tools — Stage 2: rigging & pivot tools.
#
# One-click conformance with the engine's skeletal contract:
#   * Auto-Prefix Bones        — every deform bone must start with "Bone"
#   * Rigidify Vertex Weights  — one dominant influence per vertex
#   * Add CenterPoint          — the model pivot empty
#   * Apply Object Transforms  — non-uniform scale breaks CPU skinning
#   * Prepare for Swordigo     — triangulate + all of the above
#
# All decisions come from rubyforge.core (pure modules); this file is the
# bpy wiring only.

from __future__ import annotations

import json

from ..core.rigging import (
    BONE_PREFIX,
    CENTER_POINT_NAME,
    auto_prefix_bone_name,
    is_valid_bone_name,
)

try:
    import bpy
    from bpy.props import BoolProperty
    from bpy.types import Operator
    HAS_BPY = True
except Exception:  # pragma: no cover
    bpy = None
    Operator = object
    BoolProperty = lambda default=False, **kw: default
    HAS_BPY = False

REGISTER = []


def scene_center_point_empty():
    """The first Empty named CenterPoint in the scene, if any."""
    if not HAS_BPY:
        return None
    for obj in bpy.data.objects:
        if obj.type == "EMPTY" and obj.name == CENTER_POINT_NAME:
            return obj
    return None


def deforming_armature(obj):
    """The armature modifier's armature for a mesh object (or None)."""
    if not HAS_BPY or obj is None:
        return None
    for mod in obj.modifiers:
        if mod.type == "ARMATURE" and mod.object is not None:
            return mod.object
    if obj.parent is not None and obj.parent.type == "ARMATURE":
        return obj.parent
    return None


def armature_bone_names(arm):
    return {b.name for b in arm.data.bones} if arm is not None else set()


def _mesh_vertex_group_weights(obj):
    """Yield (vertex_index, [(group_index, weight)...]) for a mesh object."""
    for v in obj.data.vertices:
        yield v.index, [(g.group, g.weight) for g in v.groups]


if HAS_BPY:

    class SWORDIGO_OT_auto_prefix_bones(Operator):
        """Rename deform bones so they all start with 'Bone'."""
        bl_idname = "swordigo.ot_auto_prefix_bones"
        bl_label = "Auto-Prefix Bones"
        bl_description = ("Rename armature bones to carry the required 'Bone' "
                          "prefix (the runtime ignores unprefixed bones)")
        bl_options = {"REGISTER", "UNDO"}

        def execute(self, context):
            renamed = 0
            for arm in context.scene.objects:
                if arm.type != "ARMATURE":
                    continue
                for bone in arm.data.bones:
                    new = auto_prefix_bone_name(bone.name)
                    if new != bone.name:
                        bone.name = new
                        renamed += 1
            # Vertex groups follow bone names; sync deform groups on meshes.
            for obj in context.scene.objects:
                if obj.type != "MESH":
                    continue
                for vg in obj.vertex_groups:
                    new = auto_prefix_bone_name(vg.name)
                    if new != vg.name and new not in obj.vertex_groups:
                        vg.name = new
            self.report({"INFO"}, "Renamed %d bones" % renamed)
            return {"FINISHED"}


    class SWORDIGO_OT_rigidify_weights(Operator):
        """Snap every vertex to a single dominant bone influence."""
        bl_idname = "swordigo.ot_rigidify_weights"
        bl_label = "Rigidify Vertex Weights"
        bl_description = ("Keep only the strongest bone influence per vertex "
                          "at weight 1.0 (the runtime is rigid-skinned)")
        bl_options = {"REGISTER", "UNDO"}

        only_valid_bones: BoolProperty(
            name="Only 'Bone*' groups",
            default=True,
            description="Ignore influences from groups that are not bones")

        def execute(self, context):
            changed = 0
            for obj in context.selected_objects:
                if obj.type != "MESH":
                    continue
                arm = deforming_armature(obj)
                valid = armature_bone_names(arm) if self.only_valid_bones else None
                groups = obj.vertex_groups
                for v in obj.data.vertices:
                    influences = [(g.group, g.weight) for g in v.groups]
                    if valid is not None:
                        influences = [(gi, w) for gi, w in influences
                                      if groups[gi].name in valid]
                    if len(influences) <= 1:
                        continue
                    best_gi, _ = max(influences, key=lambda p: p[1])
                    for gi, _w in influences:
                        groups[gi].add([v.index], 1.0 if gi == best_gi else 0.0,
                                       "REPLACE")
                    changed += 1
            self.report({"INFO"}, "Rigidified %d vertices" % changed)
            return {"FINISHED"}


    class SWORDIGO_OT_add_center_point(Operator):
        """Add the CenterPoint pivot empty at the 3D cursor."""
        bl_idname = "swordigo.ot_add_center_point"
        bl_label = "Add CenterPoint"
        bl_description = ("Create the CenterPoint pivot empty (the engine "
                          "offsets models by its local translation)")
        bl_options = {"REGISTER", "UNDO"}

        def execute(self, context):
            existing = scene_center_point_empty()
            if existing is not None:
                self.report({"WARNING"}, "CenterPoint already exists")
                return {"CANCELLED"}
            empty = bpy.data.objects.new(CENTER_POINT_NAME, None)
            empty.location = context.scene.cursor.location
            context.collection.objects.link(empty)
            self.report({"INFO"}, "CenterPoint created at the cursor")
            return {"FINISHED"}


    class SWORDIGO_OT_apply_transforms(Operator):
        """Apply location/rotation/scale of selected mesh objects."""
        bl_idname = "swordigo.ot_apply_transforms"
        bl_label = "Apply Object Transforms"
        bl_description = ("Bake object transforms into the meshes (non-uniform "
                          "scale distorts CPU skinning)")
        bl_options = {"REGISTER", "UNDO"}

        def execute(self, context):
            prev = list(context.selected_objects)
            for obj in context.scene.objects:
                if obj.type != "MESH":
                    continue
                for o in context.selected_objects:
                    o.select_set(False)
                obj.select_set(True)
                context.view_layer.objects.active = obj
                bpy.ops.object.transform_apply(
                    location=True, rotation=True, scale=True)
            for o in prev:
                o.select_set(True)
            self.report({"INFO"}, "Transforms applied")
            return {"FINISHED"}


    class SWORDIGO_OT_prepare(Operator):
        """Full 'Prepare for Swordigo' conformance pass."""
        bl_idname = "swordigo.ot_prepare"
        bl_label = "Prepare for Swordigo"
        bl_description = ("Triangulate meshes, apply transforms, auto-prefix "
                          "bones, rigidify weights and add a CenterPoint")
        bl_options = {"REGISTER", "UNDO"}

        def execute(self, context):
            # 1) triangulate every selected/visible mesh (POD is triangles).
            for obj in context.scene.objects:
                if obj.type != "MESH":
                    continue
                context.view_layer.objects.active = obj
                if any(len(p.vertices) != 3 for p in obj.data.polygons):
                    bpy.ops.object.mode_set(mode="EDIT")
                    bpy.ops.mesh.select_all(action="SELECT")
                    bpy.ops.mesh.quads_convert_to_tris(quad_method="BEAUTY")
                    bpy.ops.object.mode_set(mode="OBJECT")
            # 2) transforms, 3) bone names, 4) rigid weights.
            bpy.ops.swordigo.ot_apply_transforms()
            bpy.ops.swordigo.ot_auto_prefix_bones()
            bpy.ops.swordigo.ot_rigidify_weights()
            if scene_center_point_empty() is None:
                bpy.ops.swordigo.ot_add_center_point()
            self.report({"INFO"}, "Scene prepared for Swordigo export")
            return {"FINISHED"}


    class SWORDIGO_OT_lock_fps(Operator):
        """Lock the scene to 24 FPS (the engine's animation rate)."""
        bl_idname = "swordigo.ot_lock_fps"
        bl_label = "Lock 24 FPS"
        bl_description = "Set the scene framerate to 24 (Swordigo contract)"

        def execute(self, context):
            context.scene.render.fps = 24
            self.report({"INFO"}, "Scene locked to 24 FPS")
            return {"FINISHED"}

    REGISTER.extend([
        SWORDIGO_OT_auto_prefix_bones,
        SWORDIGO_OT_rigidify_weights,
        SWORDIGO_OT_add_center_point,
        SWORDIGO_OT_apply_transforms,
        SWORDIGO_OT_prepare,
        SWORDIGO_OT_lock_fps,
    ])
