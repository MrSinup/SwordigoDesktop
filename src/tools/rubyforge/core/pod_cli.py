"""rubyforge.core.pod_cli — tiny headless CLI for the pure-Python POD I/O.

Lets packagers / CI / artists exercise the reader and writer without Blender
or the native toolchain:

    python -m rubyforge.core.pod_cli dump  <file.pod>
    python -m rubyforge.core.pod_cli roundtrip <file.pod> [out.pod]
    python -m rubyforge.core.pod_cli stats <file.pod>
"""

from __future__ import annotations

import sys

from .ir import PODModel
from .pod_reader import PODError, pod_parse
from .pod_writer import pod_dumps


def _stats(model: PODModel, path: str) -> None:
    print("file        :", path)
    print("version     :", model.version or "(empty)")
    print("meshes      :", len(model.meshes))
    print("nodes       :", len(model.nodes), "(mesh nodes:", model.num_mesh_nodes, ")")
    print("materials   :", len(model.materials))
    print("textures    :", len(model.texture_filenames))
    print("frames      :", model.num_frames, "fps:", model.fps)
    print("verts/faces :", model.total_vertices, "/", model.total_faces)
    print("center point:", model.center_point if model.has_center_point else "(none)")
    print("bounds      : min", (model.min_x, model.min_y, model.min_z),
          "max", (model.max_x, model.max_y, model.max_z))
    bones = [n.name for n in model.nodes if n.is_bone]
    print("bones       :", len(bones), sorted(bones)[:12], "...")
    for w in model.warnings:
        print("warning     :", w)


def cmd_dump(args) -> int:
    if not args:
        print("usage: rubyforge.core.pod_cli dump <file.pod>", file=sys.stderr)
        return 2
    path = args[0]
    with open(path, "rb") as f:
        data = f.read()
    model = pod_parse(data)
    _stats(model, path)
    for (i, mesh) in enumerate(model.meshes):
        print("mesh[%d]      : verts=%d faces=%d bpv=%d uv=%d pos=%d nrm=%d idx=%d" % (
            i, mesh.num_vertices, mesh.num_faces, mesh.bones_per_vertex,
            len(mesh.uvs) // 2, len(mesh.positions) // 3, len(mesh.normals) // 3,
            len(mesh.indices)))
    return 0


def cmd_roundtrip(args) -> int:
    if not args:
        print("usage: rubyforge.core.pod_cli roundtrip <file.pod> [out.pod]", file=sys.stderr)
        return 2
    src = args[0]
    dst = args[1] if len(args) > 1 else (src.rsplit(".", 1)[0] + "_rt.pod")
    with open(src, "rb") as f:
        data = f.read()
    model = pod_parse(data)
    out = pod_dumps(model)
    with open(dst, "wb") as f:
        f.write(out)
    print("read %d bytes, wrote %d bytes -> %s" % (len(data), len(out), dst))
    return 0


def cmd_stats(args) -> int:
    if not args:
        print("usage: rubyforge.core.pod_cli stats <file.pod>", file=sys.stderr)
        return 2
    path = args[0]
    with open(path, "rb") as f:
        data = f.read()
    _stats(pod_parse(data), path)
    return 0


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv:
        print("rubyforge POD CLI: dump|roundtrip|stats <file.pod>", file=sys.stderr)
        return 2
    cmd, rest = argv[0], argv[1:]
    try:
        if cmd == "dump":
            return cmd_dump(rest)
        if cmd == "roundtrip":
            return cmd_roundtrip(rest)
        if cmd == "stats":
            return cmd_stats(rest)
        print("unknown command: %s" % cmd, file=sys.stderr)
        return 2
    except (PODError, OSError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())