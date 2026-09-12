"""rubyforge.tests.test_roundtrip — differential POD round-trip tests.

Validates the pure-Python reader/writer against the real stock asset corpus
(report §16):

    POD_orig -> IR -> POD_new

and asserts the IR is preserved across the round-trip to the same tolerances
the native C++ pipeline aims for (pos 1e-4, norm 1e-3, uv 1e-5, exact
index/vertex counts).

Assets are located via ``$SWORDIGO_ASSET_DIR`` or the default stock asset
directory for this machine. If no assets are found the suite SKIPs instead of
failing (so the addon can be developed on machines without the game assets).
"""

from __future__ import annotations

import os
import sys
import unittest

# Ensure ``rubyforge`` resolves when this module is run directly as a file.
_HERE = os.path.dirname(os.path.abspath(__file__))
_PKG_ROOT = os.path.dirname(_HERE)          # .../src/tools/rubyforge
_TOOLS_ROOT = os.path.dirname(_PKG_ROOT)    # .../src/tools
for _p in (_TOOLS_ROOT, _PKG_ROOT):
    if _p not in sys.path:
        sys.path.insert(0, _p)

from rubyforge.core.ir import PODMaterial, PODModel, PODNode  # noqa: E402
from rubyforge.core.pod_reader import pod_parse  # noqa: E402
from rubyforge.core.pod_writer import pod_dumps  # noqa: E402
from rubyforge.core.transforms import (  # noqa: E402
    apply_pod_to_blender_basis,
    get_node_matrix,
    mat4_identity,
    mat4_mul,
)

DEFAULT_ASSET_DIR = os.path.expanduser(
    "~/.local/share/swordigo-desktopsss/assets/resources")

# (asset, max_pos_tol, max_norm_tol, max_uv_tol)
ROUND_TRIP_CASES = [
    ("hiro.POD", 1e-4, 1e-3, 1e-5),
    ("bat.POD", 1e-4, 1e-3, 1e-5),
    ("bush.POD", 1e-4, 1e-3, 1e-5),
    ("chest.POD", 1e-4, 1e-3, 1e-5),
    ("pot.POD", 1e-4, 1e-3, 1e-5),
    ("tree1.POD", 1e-4, 1e-3, 1e-5),
    ("hiro_run.POD", 1e-4, 1e-3, 1e-5),
    ("hiro_jump.POD", 1e-4, 1e-3, 1e-5),
]


def asset_dir() -> str:
    env = os.environ.get("SWORDIGO_ASSET_DIR")
    if env and os.path.isdir(env):
        return env
    if os.path.isdir(DEFAULT_ASSET_DIR):
        return DEFAULT_ASSET_DIR
    return ""


def asset_path(name: str) -> str:
    return os.path.join(asset_dir(), name)


def _same_floats(a, b, tol):
    return len(a) == len(b) and all(abs(x - y) <= tol for x, y in zip(a, b))


class TestWriterStructure(unittest.TestCase):
    """Whitebox checks against the documented chunk grammar."""

    def test_header_magic(self):
        model = PODModel()
        model.version = "AB.POD.2.0"
        blob = pod_dumps(model)
        # FormatVersion (1000), len 11, "AB.POD.2.0\0"
        self.assertEqual(blob[0:8], b"\xe8\x03\x00\x00\x0b\x00\x00\x00")
        self.assertEqual(blob[8:19], b"AB.POD.2.0\x00")
        # closing format tag 0x800003e8
        self.assertEqual(blob[19:23], b"\xe8\x03\x00\x80")
        # scene tag 1001 = 0x3e9
        self.assertEqual(blob[27:31], b"\xe9\x03\x00\x00")

    def test_roundtrip_empty_model(self):
        model = PODModel()
        model.version = "AB.POD.2.0"
        model.fps = 24.0
        again = pod_parse(pod_dumps(model))
        self.assertEqual(again.version, "AB.POD.2.0")
        self.assertEqual(again.meshes, [])
        self.assertEqual(again.nodes, [])
        self.assertEqual(again.fps, 24.0)

    def test_material_index_roundtrip(self):
        # -1 (no texture) survives as 0xFFFFFFFF through the writer losslessly.
        model = PODModel()
        model.materials.append(PODMaterial(name="Default", diffuse_texture_index=-1))
        again = pod_parse(pod_dumps(model))
        self.assertEqual(again.materials[0].diffuse_texture_index, -1)


class TestTransforms(unittest.TestCase):
    def test_basis_roundtrip_is_identity(self):
        pts = [0.0, 1.0, 2.0, 3.0, 4.0, 5.0, -1.0, -2.0, -3.0]
        # POD -> Blender -> POD must be the identity permutation.
        to_blender = apply_pod_to_blender_basis(pts)          # (x, -z, y)
        from rubyforge.operators.io_pod import blender_to_pod_basis_flat
        back = blender_to_pod_basis_flat(to_blender)          # (x, z, -y)
        self.assertTrue(_same_floats(back, pts, 0.0))

    def test_basis_up_axis_maps_up(self):
        # +Y in POD (up) must land on +Z in Blender (up), and vice versa.
        from rubyforge.core.transforms import pod_to_blender_basis, blender_to_pod_basis
        self.assertEqual(pod_to_blender_basis([0.0, 1.0, 0.0]), [0.0, 0.0, 1.0])
        self.assertEqual(blender_to_pod_basis([0.0, 0.0, 1.0]), [0.0, 1.0, 0.0])

    def test_mat4_mul_identity(self):
        ident = mat4_identity()
        self.assertTrue(_same_floats(mat4_mul(ident, ident), ident, 0.0))

    def test_get_node_matrix_no_anim(self):
        model = PODModel()
        node = PODNode(name="BoneRoot")
        node.has_translation = True
        node.translation = [1.0, 2.0, 3.0]
        model.nodes.append(node)
        model.num_frames = 1
        m = get_node_matrix(model, 0, 0.0)
        self.assertAlmostEqual(m[12], 1.0)
        self.assertAlmostEqual(m[13], 2.0)
        self.assertAlmostEqual(m[14], 3.0)


class TestRoundTrip(unittest.TestCase):
    """Differential round-trip across the real stock assets."""

    def _roundtrip_one(self, name, pos_tol, norm_tol, uv_tol):
        path = asset_path(name)
        with open(path, "rb") as f:
            blob = f.read()
        model = pod_parse(blob)
        self.assertEqual(model.version, "AB.POD.2.0")

        again = pod_parse(pod_dumps(model))

        self.assertEqual(len(again.meshes), len(model.meshes))
        self.assertEqual(len(again.nodes), len(model.nodes))
        self.assertEqual(len(again.materials), len(model.materials))
        self.assertEqual(len(again.texture_filenames), len(model.texture_filenames))
        self.assertEqual(again.num_mesh_nodes, model.num_mesh_nodes)
        self.assertEqual(again.num_frames, model.num_frames)

        # Animation-only PODs (NumMesh == 0, e.g. hiro_run.POD) carry node
        # tracks only; merging them with a base model is a Stage-3 feature.
        if not model.meshes:
            self.assertTrue(model.nodes, "%s: expected animation nodes" % name)
            for a, b in zip(model.nodes, again.nodes):
                self.assertEqual(a.name, b.name)
                self.assertEqual(a.anim_translation, b.anim_translation)
                self.assertEqual(a.anim_rotation, b.anim_rotation)
                self.assertEqual(a.anim_translation_idx, b.anim_translation_idx)
                self.assertEqual(a.anim_rotation_idx, b.anim_rotation_idx)
            return

        for a, b in zip(model.meshes, again.meshes):
            self.assertEqual(a.num_vertices, b.num_vertices)
            self.assertEqual(a.num_faces, b.num_faces)
            self.assertEqual(a.bones_per_vertex, b.bones_per_vertex)
            self.assertEqual(a.indices, b.indices)
            self.assertTrue(_same_floats(a.positions, b.positions, pos_tol),
                            "%s: positions drifted" % name)
            self.assertTrue(_same_floats(a.normals, b.normals, norm_tol),
                            "%s: normals drifted" % name)
            self.assertTrue(_same_floats(a.uvs, b.uvs, uv_tol),
                            "%s: uvs drifted" % name)

        for a, b in zip(model.nodes, again.nodes):
            self.assertEqual(a.name, b.name)
            self.assertEqual(a.object_index, b.object_index)
            self.assertEqual(a.parent_index, b.parent_index)

        self.assertEqual(again.center_point, model.center_point)
        self.assertEqual(again.has_center_point, model.has_center_point)

    def test_hiro(self):
        if not asset_dir():
            self.skipTest("no assets")
        self._roundtrip_one("hiro.POD", 1e-4, 1e-3, 1e-5)
def _load_tests(loader, tests, pattern):
    suite = unittest.TestSuite()
    if asset_dir():
        for name, pt, nt, ut in ROUND_TRIP_CASES:
            method = "test_roundtrip_%s" % name.replace(".", "_")

            def make(name=name, pt=pt, nt=nt, ut=ut):
                def _fn(self):
                    self._roundtrip_one(name, pt, nt, ut)
                _fn.__name__ = method
                return _fn

            setattr(TestRoundTrip, method, make())
    suite.addTests(loader.loadTestsFromTestCase(TestWriterStructure))
    suite.addTests(loader.loadTestsFromTestCase(TestTransforms))
    suite.addTests(loader.loadTestsFromTestCase(TestRoundTrip))
    return suite


def load_tests(loader, tests, pattern):
    return _load_tests(loader, tests, pattern)


if __name__ == "__main__":
    unittest.main(verbosity=2)
