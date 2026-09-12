#!/usr/bin/env python3
"""
pod_dump.py — standalone POD (PowerVR Object Data) chunk dumper / verifier.

Parses a Caver/Swordigo `.pod` file using ONLY the tag table recovered in
docs/formats_and_schemas/pod_master/01_container_and_chunk_tags.md and prints
every chunk it finds (tag id, name, container/leaf, length, nesting, and a
decoded preview of the payload). Use it to confirm the documented tags and
offsets against real bytes.

No dependencies. Python 3.8+.

Usage:
    python3 pod_dump.py MODEL.pod            # dump a real file
    python3 pod_dump.py MODEL.pod --hex 64   # also show 64 bytes of each leaf payload
    python3 pod_dump.py --make-sample out.pod  # write a synthetic POD (self-test)
    python3 pod_dump.py --selftest           # build a sample in memory and dump it

Framing (verified): every chunk is an 8-byte header  [uint32 tag][uint32 length]
followed by `length` payload bytes. A container's close tag = open | 0x80000000.
All scalars little-endian (the engine rejects big-endian files).
"""

import sys
import struct
import io

CLOSE_BIT = 0x80000000

# ── Tag table — mirrors pod_master/01_container_and_chunk_tags.md ────────────
# name, is_container, payload_kind
#   payload_kind: 'str', 'u32', 'i32', 'f32', 'f32[]', 'u32[]', 'floatN', 'raw', 'container', 'enum'
TAGS = {
    # File / scene framing
    1000: ("FormatVersion",        False, "str"),
    1001: ("Scene",                True,  "container"),
    1002: ("ExportFlags1",         False, "raw"),
    1003: ("ExportFlags2",         False, "raw"),
    # Scene fields
    2000: ("Colour.Background",    False, "floatN"),
    2001: ("Colour.Ambient",       False, "floatN"),
    2002: ("NumCamera",            False, "u32"),
    2003: ("NumLight",             False, "u32"),
    2004: ("NumMesh",              False, "u32"),
    2005: ("NumNode",              False, "u32"),
    2006: ("NumMeshNode",          False, "u32"),
    2007: ("NumTexture",           False, "u32"),
    2008: ("NumMaterial",          False, "u32"),
    2009: ("NumFrame",             False, "u32"),
    2010: ("Camera",               True,  "container"),
    2011: ("Light",                True,  "container"),
    2012: ("Mesh",                 True,  "container"),
    2013: ("Node",                 True,  "container"),
    2014: ("Texture",              True,  "container"),
    2015: ("Material",             True,  "container"),
    2016: ("Scene.Flags?",         False, "u32"),   # always 0 across the 394-POD
    #   stock corpus — NOT the fps (earlier docs mislabeled this as FPS/0x7E0).
    2017: ("FPS",                  False, "u32"),   # real fps: 30 in animated
    #   stock models, 0 in static ones. pod_loader.cpp reads fps from here.
    # Camera block
    8000: ("Camera.TargetIdx",     False, "i32"),
    8001: ("Camera.FOV",           False, "f32"),
    8002: ("Camera.Far",           False, "f32"),
    8003: ("Camera.Near",          False, "f32"),
    8004: ("Camera.FOVAnim",       False, "f32[]"),
    # Light block
    7000: ("Light.TargetIdx",      False, "i32"),
    7001: ("Light.Colour",         False, "floatN"),
    7002: ("Light.Type",           False, "enum"),
    7003: ("Light.ConstAtten",     False, "f32"),
    7004: ("Light.LinearAtten",    False, "f32"),
    7005: ("Light.QuadAtten",      False, "f32"),
    7006: ("Light.FalloffAngle",   False, "f32"),
    7007: ("Light.FalloffExp",     False, "f32"),
    # Mesh block
    6000: ("Mesh.NumVertices",     False, "u32"),
    6001: ("Mesh.NumFaces",        False, "u32"),
    6002: ("Mesh.NumUVWChannels",  False, "u32"),
    6003: ("Mesh.VertexIndexList", True,  "container"),
    6004: ("Mesh.StripLengths",    False, "u32[]"),
    6005: ("Mesh.NumStrips",       False, "u32"),
    6006: ("Mesh.VertexList",      True,  "container"),
    6007: ("Mesh.NormalList",      True,  "container"),
    6008: ("Mesh.TangentList",     True,  "container"),
    6009: ("Mesh.BinormalList",    True,  "container"),
    6010: ("Mesh.UVWList",         True,  "container"),
    6011: ("Mesh.VertexColourList",True,  "container"),
    6012: ("Mesh.BoneIndexList",   True,  "container"),
    6013: ("Mesh.BoneWeightList",  True,  "container"),
    6014: ("Mesh.InterleavedData", False, "raw"),
    6015: ("Mesh.BoneBatchIndex",  False, "i32[]"),
    6016: ("Mesh.BoneBatchCounts", False, "i32[]"),
    6017: ("Mesh.BoneBatchOffsets",False, "i32[]"),
    6018: ("Mesh.MaxBonesPerBatch",False, "i32"),
    6019: ("Mesh.NumBoneBatches",  False, "i32"),
    6020: ("Mesh.UnpackMatrix",    False, "floatN"),
    6021: ("Mesh.Type(indigenous)",False, "u32"),
    6022: ("Mesh.Adjacency(indig.)",False,"i32[]"),
    # Node block
    5000: ("Node.Index",           False, "i32"),
    5001: ("Node.Name",            False, "str"),
    5002: ("Node.MaterialIndex",   False, "i32"),
    5003: ("Node.ParentIndex",     False, "i32"),
    5004: ("Node.Position",        False, "floatN"),
    5005: ("Node.Rotation",        False, "floatN"),
    5006: ("Node.Scale",           False, "floatN"),
    5007: ("Node.AnimPosition",    False, "floatN"),
    5008: ("Node.AnimRotation",    False, "floatN"),
    5009: ("Node.AnimScale",       False, "floatN"),
    5010: ("Node.Matrix",          False, "floatN"),
    5011: ("Node.AnimMatrix",      False, "f32[]"),
    5012: ("Node.AnimFlags",       False, "u32"),
    5013: ("Node.AnimPositionIdx", False, "u32[]"),
    5014: ("Node.AnimRotationIdx", False, "u32[]"),
    5015: ("Node.AnimScaleIdx",    False, "u32[]"),
    5016: ("Node.AnimMatrixIdx",   False, "u32[]"),
    # Texture block
    4000: ("Texture.Filename",     False, "str"),
    # Material block
    3000: ("Material.Name",        False, "str"),
    3001: ("Material.DiffuseTexIdx",False,"i32"),
    3002: ("Material.Opacity",     False, "f32"),
    3003: ("Material.Ambient",     False, "floatN"),
    3004: ("Material.Diffuse",     False, "floatN"),
    3005: ("Material.Specular",    False, "floatN"),
    3006: ("Material.Shininess",   False, "f32"),
    3007: ("Material.EffectFile",  False, "str"),
    3008: ("Material.EffectName",  False, "str"),
    3026: ("Material.Flags",       False, "u32"),
    # CPODData sub-block (children of any stream container)
    9000: ("CPODData.DataType",    False, "enum"),
    9001: ("CPODData.NumComponents",False,"u32"),
    9002: ("CPODData.Stride",       False,"u32"),
    9003: ("CPODData.Data",         False,"raw"),
}
# Material blend-state range 3009..3025 (offsets known, names unverified — see 10_...md)
for _t in range(3009, 3026):
    TAGS.setdefault(_t, (f"Material.Blend?{_t}", False, "raw"))

# EPVRTDataType names (order per PowerVR SDK; per-index binding UNVERIFIED — doc 03/10)
EPVRT = {
    1: "FLOAT", 2: "INT", 3: "UNSIGNED_SHORT", 4: "RGBA", 5: "ARGB",
    6: "D3DCOLOR", 7: "UBYTE4", 8: "DEC3N", 9: "FIXED16_16",
    10: "UNSIGNED_BYTE", 11: "SHORT", 12: "SHORT_NORM", 13: "BYTE", 14: "BYTE_NORM",
}


def _decode(kind, payload):
    """Return a short human-readable preview of a leaf payload."""
    n = len(payload)
    try:
        if kind == "str":
            s = payload.split(b"\x00", 1)[0].decode("latin-1", "replace")
            return f'"{s}"'
        if kind in ("u32", "i32", "f32", "enum") and n >= 4:
            if kind == "f32":
                return f"{struct.unpack('<f', payload[:4])[0]:.6g}"
            val = struct.unpack("<I" if kind != "i32" else "<i", payload[:4])[0]
            if kind == "enum":
                return f"{val} ({EPVRT.get(val, '?')})" if val in EPVRT or True else str(val)
            return str(val)
        if kind in ("floatN",) and n % 4 == 0 and n <= 64:
            vals = struct.unpack(f"<{n // 4}f", payload)
            return "[" + ", ".join(f"{v:.4g}" for v in vals) + "]"
        if kind in ("f32[]",) and n >= 4:
            cnt = struct.unpack("<I", payload[:4])[0]
            return f"count={cnt}, {n - 4} data bytes"
        if kind in ("u32[]", "i32[]") and n >= 4:
            cnt = struct.unpack("<I", payload[:4])[0]
            return f"count={cnt}, {n - 4} data bytes"
    except struct.error:
        pass
    return f"{n} bytes"


def dump(data, hexbytes=0, out=sys.stdout):
    """Walk the whole file as a marker stream and print nested chunks.

    Framing confirmed against stock PODs (hiro.POD): EVERY chunk — container
    AND leaf — is bracketed by an open marker and a matching close marker
    ``open | 0x80000000``:

        [open tag][len][ payload OR nested children ][close tag = open|BIT][0]

    A *container's* payload region holds nested markers; a *leaf's* payload
    region holds `len` raw bytes. So we recurse: on any open marker we read its
    `len`; if the tag is a known leaf we consume `len` payload bytes, otherwise
    we treat the region as nested markers. Either way we then expect the close
    marker for that tag.
    """
    buf = memoryview(data)
    stats = {"chunks": 0, "unknown": 0, "containers": 0, "leaves": 0}
    pos = [0]

    def line(depth, s):
        out.write("  " * depth + s + "\n")

    def walk(depth, region_end):
        while pos[0] + 8 <= region_end:
            tag, length = struct.unpack_from("<II", buf, pos[0])
            # A close marker for our parent ends this region.
            if tag & CLOSE_BIT:
                return tag
            pos[0] += 8
            stats["chunks"] += 1

            entry = TAGS.get(tag)
            if entry is None:
                stats["unknown"] += 1
                name, is_ctr, kind = f"UNKNOWN_{tag}", False, "raw"
            else:
                name, is_ctr, kind = entry

            payload_start = pos[0]
            if is_ctr:
                stats["containers"] += 1
                line(depth, f"<{name}>  tag={tag} ({tag:#06x})  len={length}")
                # children occupy [payload_start, payload_start+len) when len>0,
                # otherwise until the close marker.
                child_end = payload_start + length if length else region_end
                closed = walk(depth + 1, child_end)
                _consume_close(depth, tag, name)
            else:
                stats["leaves"] += 1
                payload = bytes(buf[payload_start:payload_start + length])
                pos[0] = payload_start + length
                preview = _decode(kind, payload)
                flag = "" if entry is not None else "  [UNKNOWN TAG]"
                line(depth, f"{name:<26} tag={tag:<5} ({tag:#06x})  "
                            f"len={length:<6} {preview}{flag}")
                if hexbytes and payload:
                    hx = " ".join(f"{b:02x}" for b in payload[:hexbytes])
                    line(depth + 1, hx + (" …" if length > hexbytes else ""))
                _consume_close(depth, tag, name, quiet=True)
        return None

    def _consume_close(depth, open_tag, name, quiet=False):
        if pos[0] + 8 > len(buf):
            return
        tag, length = struct.unpack_from("<II", buf, pos[0])
        if tag == (open_tag | CLOSE_BIT):
            pos[0] += 8
            stats["chunks"] += 1
            if not quiet:
                line(depth, f"</{name}>  (close {tag:#010x} = "
                            f"{open_tag} | 0x80000000)")

    walk(0, len(buf))

    out.write("\n")
    out.write(f"— parsed {stats['chunks']} markers "
              f"({stats['containers']} containers, {stats['leaves']} leaves, "
              f"{stats['unknown']} unknown), "
              f"{pos[0]}/{len(buf)} bytes consumed —\n")
    if pos[0] != len(buf):
        out.write(f"  WARNING: {len(buf) - pos[0]} trailing bytes not consumed\n")
    return stats


# ── Synthetic POD builder (for self-test / demo) ─────────────────────────────
def _u32(v): return struct.pack("<I", v & 0xFFFFFFFF)
def _i32(v): return struct.pack("<i", v)
def _f32(v): return struct.pack("<f", v)


def _chunk(tag, payload=b""):
    return _u32(tag) + _u32(len(payload)) + payload


def _open(tag):    return _u32(tag) + _u32(0)
def _close(tag):   return _u32(tag | CLOSE_BIT) + _u32(0)


def _cpoddata(dtype, ncomp, stride, data):
    """Emit a CPODData sub-block (9000..9003) — a leaf-only nested set."""
    b = _chunk(9000, _u32(dtype))
    b += _chunk(9001, _u32(ncomp))
    b += _chunk(9002, _u32(stride))
    b += _chunk(9003, data)
    return b


def make_sample():
    """Build a minimal valid POD: version + scene(1 mesh, 1 node)."""
    # one triangle, 3 verts (xyz float), indices u16
    verts = struct.pack("<9f", 0,0,0,  1,0,0,  0,1,0)
    idx   = struct.pack("<3H", 0, 1, 2)

    mesh  = _open(2012)
    mesh += _chunk(6000, _u32(3))                     # NumVertices
    mesh += _chunk(6001, _u32(1))                     # NumFaces
    mesh += _chunk(6002, _u32(0))                     # NumUVWChannels
    mesh += _open(6003) + _cpoddata(3, 3, 6, idx) + _close(6003)   # indices: UNSIGNED_SHORT
    mesh += _open(6006) + _cpoddata(1, 3, 12, verts) + _close(6006) # vertex: FLOAT xyz
    mesh += _chunk(6020, struct.pack("<16f", 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1))  # UnpackMatrix
    mesh += _close(2012)

    node  = _open(2013)
    node += _chunk(5000, _i32(0))                     # object index → mesh 0
    node += _chunk(5001, b"root\x00")                 # name
    node += _chunk(5002, _i32(-1))                    # material
    node += _chunk(5003, _i32(-1))                    # parent = root
    node += _chunk(5006, struct.pack("<7f", 1,1,1, 0,0,0,1))  # scale (7 floats/key)
    node += _chunk(5012, _u32(0))                     # anim flags
    node += _close(2013)

    scene  = _open(1001)
    scene += _chunk(2004, _u32(1))                    # NumMesh
    scene += _chunk(2005, _u32(1))                    # NumNode
    scene += _chunk(2006, _u32(1))                    # NumMeshNode
    scene += _chunk(2009, _u32(1))                    # NumFrame
    scene += _chunk(2017, _u32(30))                   # FPS. Verified against the
    #   394-POD stock corpus: tag 2017 carries the real fps (30 in animated
    #   models); tag 2016 is a *separate* field that is always 0 in stock assets
    #   and is NOT the fps. pod_loader.cpp agrees (eSceneFPS = 2017).
    scene += mesh
    scene += node
    scene += _close(1001)

    return _chunk(1000, b"AB.POD.2.0\x00") + scene


def main(argv):
    args = argv[1:]
    hexbytes = 0
    if "--hex" in args:
        i = args.index("--hex")
        hexbytes = int(args[i + 1]); del args[i:i + 2]

    if "--selftest" in args:
        data = make_sample()
        print(f"[selftest] synthetic POD = {len(data)} bytes\n")
        dump(data, hexbytes or 24)
        return 0

    if "--make-sample" in args:
        i = args.index("--make-sample")
        path = args[i + 1]
        with open(path, "wb") as f:
            f.write(make_sample())
        print(f"wrote synthetic POD → {path}")
        return 0

    if not args:
        print(__doc__)
        return 1

    path = args[0]
    with open(path, "rb") as f:
        data = f.read()
    print(f"# {path}  ({len(data)} bytes)\n")
    # Sanity: a real POD starts with the 1000 (0x3E8) FormatVersion marker.
    if len(data) >= 8:
        tag0 = struct.unpack_from("<I", data, 0)[0]
        if tag0 != 1000:
            print(f"WARNING: first tag is {tag0}, expected 1000 (FormatVersion). "
                  f"File may not be a POD or may be wrapped/compressed.\n")
    dump(data, hexbytes)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
