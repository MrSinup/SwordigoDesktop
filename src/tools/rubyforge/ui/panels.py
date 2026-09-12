# rubyforge.ui.panels — Stage 5: 3D Viewport N-panel ("Swordigo Studio").

from __future__ import annotations

try:
    import bpy
    from bpy.types import Panel
    HAS_BPY = True
except Exception:  # pragma: no cover
    bpy = None
    Panel = object
    HAS_BPY = False

REGISTER = []


def _scene_stats_line(context):
    meshes = [o for o in context.scene.objects if o.type == "MESH"]
    verts = sum(len(o.data.vertices) for o in meshes)
    faces = sum(len(o.data.polygons) for o in meshes)
    bones = sum(len(o.data.bones) for o in context.scene.objects
                if o.type == "ARMATURE")
    center = any(o.type == "EMPTY" and o.name == "CenterPoint"
                 for o in context.scene.objects)
    return verts, faces, bones, center


if HAS_BPY:

    class VIEW3D_PT_swordigo_studio(Panel):
        bl_space_type = "VIEW_3D"
        bl_region_type = "UI"
        bl_category = "Swordigo Studio"
        bl_label = "Swordigo Studio"

        def draw(self, context):
            layout = self.layout
            verts, faces, bones, center = _scene_stats_line(context)
            col = layout.column(align=True)
            col.label(text="Vertices: %d" % verts)
            col.label(text="Faces:    %d" % faces)
            col.label(text="Bones:    %d" % bones)
            row = col.row()
            row.label(text="CenterPoint: %s" % ("yes" if center else "missing"))
            if not center:
                row.operator("swordigo.ot_add_center_point", text="Add", icon="ADD")

            col = layout.column(align=True)
            col.label(text="Tooling")
            col.operator("swordigo.ot_prepare", icon="CHECKMARK")
            col.operator("swordigo.ot_auto_prefix_bones", icon="BONE_DATA")
            col.operator("swordigo.ot_rigidify_weights", icon="MOD_VERTEX_WEIGHT")
            col.operator("swordigo.ot_apply_transforms", icon="ORIENTATION_GLOBAL")

            col = layout.column(align=True)
            col.label(text="Animation (24 FPS)")
            col.operator("swordigo.ot_lock_fps", icon="TIME")
            col.operator("swordigo.ot_import_animation", icon="IMPORT")
            col.operator("swordigo.ot_export_action", icon="EXPORT")

            col = layout.column(align=True)
            col.label(text="Health")
            col.operator("swordigo.ot_validate", icon="SPAM")

            scene = context.scene
            col = layout.column(align=True)
            col.label(text="Scene FPS: %d %s" % (
                scene.render.fps,
                "(ok)" if abs(scene.render.fps - 24) < 0.01 else "(must be 24)"))

    REGISTER.append(VIEW3D_PT_swordigo_studio)
