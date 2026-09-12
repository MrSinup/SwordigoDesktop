"""rubyforge.core — pure-Python, zero-dependency Swordigo POD I/O + tooling.

Stage 1: tags, IR, transforms, pod_reader, pod_writer.
Stage 2: rigging (armature spec, rigid skinning, CenterPoint).
Stage 3: animation (pose specs, anim tracks, 24 FPS action pipeline).
Stage 4: materials (fixed-function viewport spec, texture resolution).
Stage 5: validator (asset health rule engine).
"""

from __future__ import annotations

from .ir import BoneBatch, PODMaterial, PODMesh, PODModel, PODNode
from .pod_reader import PODError, pod_load, pod_parse
from .pod_writer import pod_dumps, pod_write

__all__ = [
    "BoneBatch", "PODError", "PODMaterial", "PODMesh", "PODModel", "PODNode",
    "pod_dumps", "pod_load", "pod_parse", "pod_write",
]
