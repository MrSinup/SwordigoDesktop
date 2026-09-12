# RubyForge: Swordigo Studio — Blender extension.
#
# Stage 1 (core foundation):
#   * Pure-Python, zero-dependency POD reader/writer (rubyforge.core)
#   * File -> Import/Export Swordigo Model (.pod) operators
#
# The addon is structured as a modern Blender 4.2+ extension:
#
#   rubyforge/
#   ├── blender_manifest.toml
#   ├── __init__.py
#   ├── core/          # tags, IR, transforms, pod_reader, pod_writer, CLI
#   └── operators/     # io_pod (File -> Import/Export)
#
# Reload-safety: every bpy class is tracked in ``ordered_classes`` and
# unregistered in reverse order so hot-reloads never leave dangling refs.

from __future__ import annotations

try:
    import bpy
except Exception:
    bpy=None


from .operators.io_pod import REGISTER as _REGISTER

bl_info = {
    "name": "RubyForge: Swordigo Studio",
    "author": "OpenSwordigo Team",
    "version": (1, 0, 0),
    "blender": (4, 2, 0),
    "location": "File > Import / Export",
    "description": ("Round-trip Swordigo POD assets: pure-Python POD "
                    "reader/writer, 24 FPS, Y-up <-> Z-up basis aware."),
    "category": "Import-Export",
}

# Reload-safe registration order (reversed in unregister()).
ordered_classes = list(_REGISTER)


def register():
    if bpy is None:
        return
    for cls in ordered_classes:
        try:
            bpy.utils.register_class(cls)
        except Exception as exc:  # noqa: BLE001
            print("[rubyforge] failed to register %s: %s" % (cls.__name__, exc))
    print("[rubyforge] RubyForge: Swordigo Studio active "
          "(Stage-1 POD import/export ready)")


def unregister():
    if bpy is None:
        return
    for cls in reversed(ordered_classes):
        try:
            bpy.utils.unregister_class(cls)
        except Exception:  # noqa: BLE001
            pass


if __name__ == "__main__":
    register()