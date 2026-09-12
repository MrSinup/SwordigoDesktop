# RubyForge: Swordigo Studio — Blender Extension

Professional Blender <-> Swordigo POD round-trip toolchain.

**Stage 1 (this drop): core foundation** — a pure-Python, zero-dependency
POD reader/writer plus `File -> Import / Export` operators. No native
toolchain, no wheels, no Ruby executable required.

## Layout

```
rubyforge/
├── blender_manifest.toml    # Blender 4.2+ extension manifest
├── __init__.py              # reload-safe register()/unregister()
├── core/
│   ├── tags.py              # chunk tag constants (pod_loader/pod_writer mirror)
│   ├── ir.py                # Intermediate Representation (mirrors pod_loader.h)
│   ├── transforms.py        # matrix math, node matrices, bounds, basis change
│   ├── pod_reader.py        # pure-Python POD reader  (port of av::pod_parse)
│   ├── pod_writer.py        # pure-Python POD writer  (port of av::pod_write)
│   └── pod_cli.py           # headless CLI: dump | roundtrip | stats
├── operators/
│   └── io_pod.py            # File -> Import/Export Swordigo Model (.pod)
└── tests/
    └── test_roundtrip.py    # differential round-trip suite (report §16)
```

## Usage in Blender

* `File -> Import -> Swordigo Model (.pod)` — via the *RubyForge* operator
  (`swordigo.ot_import_pod`). Meshes arrive in Blender's Z-up space with the
  POD -> Blender basis applied, bones become vertex groups named after their
  `Bone*` nodes, and the `CenterPoint` pivot appears as an Empty.
* `File -> Export -> Swordigo Model (.pod)` — rebuilds a POD from the scene
  (round-trip metadata on object custom properties preserves nodes,
  transforms and animation streams for untouched data).
* The scene FPS is locked to 24 on import (the engine's animation contract).

## Headless CLI (no Blender required)

```bash
cd src/tools
python3 -m rubyforge.core.pod_cli stats   path/to/hiro.POD
python3 -m rubyforge.core.pod_cli dump    path/to/hiro.POD
python3 -m rubyforge.core.pod_cli roundtrip path/to/hiro.POD out.POD
```

## Differential test suite

Validates `POD_orig -> IR -> POD_new` across the real stock assets
(hiro, bat, bush, chest, pot, tree1 + animation-only hiro_run / hiro_jump)
to the tolerances from the research report (positions 1e-4, normals 1e-3,
UVs 1e-5, exact index/vertex counts):

```bash
cd src/tools
python3 -m unittest rubyforge.tests.test_roundtrip -v
# assets auto-discovered from ~/.local/share/swordigo-desktopsss/assets
# or override with SWORDIGO_ASSET_DIR=/path/to/resources
```

## Coordinate contract

POD / glTF / Caver runtime are +Y-up right-handed; Blender is +Z-up.
The direct POD path applies the explicit basis change
(`(x, y, z) -> (x, -z, y)` on import, `(x, y, z) -> (x, z, -y)` on export).
Do **not** apply it again when routing through Blender's glTF addon
(`io_scene_gltf2`) — that double conversion yields a 180-degree inversion.

## Roadmap (from the research report)

* Stage 2: CenterPoint/bone-prefix rig tools, dominant-weight quantization.
* Stage 3: animation-only POD export, 24 FPS action pipeline.
* Stage 4: Swordigo mobile material node, PVR texture flip parity.
* Stage 5: Live-Link IPC bridge, asset validator, batch CLI.
