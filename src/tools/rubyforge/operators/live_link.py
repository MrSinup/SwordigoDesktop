# rubyforge.operators.live_link — Stage 5: Ruby Desktop Live-Link bridge.
#
# Transport: the file-based staging protocol shared with the C++ host
# (asset_viewer.cpp), kept byte-compatible so the existing Ruby build keeps
# working, upgraded with rich metadata (research report §1 keeps the staging
# architecture and enriches the protocol):
#
#   $SWORDIGO_BLENDER_STAGING/in/request.json   {run, source_pod, format,
#                                                swordigo: {stats,...}}
#   $SWORDIGO_BLENDER_STAGING/in/model.glb      Ruby -> Blender
#   $SWORDIGO_BLENDER_STAGING/out/model.glb     Blender -> Ruby (save_post)
#   $SWORDIGO_BLENDER_STAGING/out/done.json     {run, status, message,
#                                                stats: {vertices, bones,...}}
#
# The host only parses "run" and "status" from done.json, so the extra
# stats fields are purely additive. A socket/JSON-RPC transport can replace
# this later without touching the payload contract.
#
# The bridge activates only when SWORDIGO_BLENDER_STAGING is set (i.e. when
# Ruby launched Blender). It also disables the legacy swordigo_roundtrip
# addon if that is still installed from earlier Ruby builds.

from __future__ import annotations

import json
import os

from .validate import collect_scene_stats

_STAGING_ENV = "SWORDIGO_BLENDER_STAGING"
_POLL_INTERVAL = 0.5
_LEGACY_ADDON = "swordigo_roundtrip"

try:
    import bpy
    HAS_BPY = True
except Exception:  # pragma: no cover
    bpy = None
    HAS_BPY = False

_last_imported_run = 0
_save_handler_installed = False


def staging_dir():
    return os.environ.get(_STAGING_ENV, "")


def _read_request():
    root = staging_dir()
    if not root:
        return None
    path = os.path.join(root, "in", "request.json")
    if not os.path.isfile(path):
        return None
    try:
        with open(path, "r") as f:
            return json.load(f)
    except Exception:
        return None


def _stats_payload():
    """Rich metadata for the host UI (additive; host ignores unknown keys)."""
    try:
        return collect_scene_stats(bpy.context.scene)
    except Exception:
        return {}


def _clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for collection in list(bpy.data.collections):
        bpy.data.collections.remove(collection)


def _import_staged(run_id):
    global _last_imported_run
    root = staging_dir()
    glb = os.path.join(root, "in", "model.glb")
    if not os.path.isfile(glb):
        return
    _clear_scene()
    bpy.ops.import_scene.gltf(filepath=glb)
    # The engine plays everything at 24 FPS.
    try:
        bpy.context.scene.render.fps = 24
    except Exception:
        pass
    _last_imported_run = run_id
    print("[rubyforge] live-link imported", glb, "run", run_id)


def _poll_request():
    if not staging_dir():
        return None  # stop the timer; not launched by Ruby
    req = _read_request()
    if req:
        run_id = req.get("run", 0)
        if run_id != _last_imported_run:
            _import_staged(run_id)
    return _POLL_INTERVAL


def _on_save(_dummy):
    """save_post: export the scene back into the staging out-dir."""
    root = staging_dir()
    if not root:
        return
    req = _read_request()
    run_id = req.get("run", 0) if req else _last_imported_run
    out_dir = os.path.join(root, "out")
    os.makedirs(out_dir, exist_ok=True)
    done = {"run": run_id, "status": "ok", "stats": _stats_payload()}
    try:
        bpy.ops.export_scene.gltf(
            filepath=os.path.join(out_dir, "model.glb"), export_format="GLB")
        print("[rubyforge] live-link exported scene for run", run_id)
    except Exception as exc:  # noqa: BLE001
        done = {"run": run_id, "status": "error",
                "message": str(exc), "stats": _stats_payload()}
    try:
        with open(os.path.join(out_dir, "done.json"), "w") as f:
            json.dump(done, f)
    except OSError as exc:
        print("[rubyforge] live-link done.json write failed:", exc)


def _disable_legacy_addon():
    """Best-effort: the legacy round-trip addon must not double-handle saves."""
    try:
        import addon_utils
        for mod in addon_utils.modules():
            if mod.__name__ == _LEGACY_ADDON and addon_utils.check(mod.__name__)[1]:
                addon_utils.disable(_LEGACY_ADDON, default_set=True)
                print("[rubyforge] disabled legacy addon", _LEGACY_ADDON)
    except Exception:
        pass


def register():
    global _save_handler_installed
    if not HAS_BPY or not staging_dir():
        return
    _disable_legacy_addon()
    if _poll_request() is not None:
        bpy.app.timers.register(_poll_request, first_interval=1.0)
    if _on_save not in bpy.app.handlers.save_post:
        bpy.app.handlers.save_post.append(_on_save)
        _save_handler_installed = True
    print("[rubyforge] live-link active; staging =", staging_dir())


def unregister():
    global _save_handler_installed
    if not HAS_BPY:
        return
    try:
        if bpy.app.timers.is_registered(_poll_request):
            bpy.app.timers.unregister(_poll_request)
    except Exception:  # noqa: BLE001
        pass
    if _save_handler_installed and _on_save in bpy.app.handlers.save_post:
        bpy.app.handlers.save_post.remove(_on_save)
        _save_handler_installed = False
