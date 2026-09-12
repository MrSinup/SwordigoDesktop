#!/usr/bin/env python3
"""Recover the component class -> payload tag map from real .scl corpora.

WHY
---
The editor keys every component on its payload field number (`Component` has
ClassName in field 1 and one payload submessage whose number identifies the
class — Model 101, ParticleEmitter 250, MonsterController 302, …). That map was
hand-carried in `src/tools/scene_schemas.cpp` and `src/ruby/caver/component_registry.cpp`.
This script derives it from shipping data instead, so it can be re-run against a
new engine version's assets and the diff is the honest answer to "what changed?".

It walks the real structure (ObjectLibrary -> ObjectTemplate -> SceneObject ->
Component) rather than pattern-matching strings, so a component's payload field
and a Lua string that happens to look like a class name are never confused.

USAGE
-----
    tools/scan_scl_components.py <dir-or-file> [...] [--min-count N] [--diff field]

Prints `field_number tag class_name count  sizes...`, sorted by field number.
`--diff N` also prints entries seen fewer than N times (the noisy tail).
"""

import argparse
import collections
import glob
import os
import sys


def read_varint(buf, pos):
    result = 0
    shift = 0
    while True:
        if pos >= len(buf):
            raise ValueError("truncated varint")
        byte = buf[pos]
        pos += 1
        result |= (byte & 0x7F) << shift
        if not (byte & 0x80):
            return result, pos
        shift += 7
        if shift > 63:
            raise ValueError("varint too long")


def parse(buf, pos, end):
    """Yield (field_number, wire_type, value, value_offset, new_pos)."""
    while pos < end:
        key, pos = read_varint(buf, pos)
        field, wire = key >> 3, key & 7
        if wire == 0:
            value, pos = read_varint(buf, pos)
            yield field, wire, value, pos, pos
        elif wire == 2:
            length, after = read_varint(buf, pos)
            value = buf[after:after + length]
            offset = after
            pos = after + length
            yield field, wire, value, offset, pos
        elif wire == 5:
            yield field, wire, buf[pos:pos + 4], pos, pos + 4
            pos += 4
        elif wire == 1:
            yield field, wire, buf[pos:pos + 8], pos, pos + 8
            pos += 8
        else:
            raise ValueError("wire type %d" % wire)


def printable(value):
    return all(32 <= c < 127 for c in value)


def scan_component(buf, start, end, tally):
    """One Component message: ClassName (1), Identifier (2), Label (3),
    ParentComponentIdentifier (4), then exactly one payload submessage."""
    class_name = None
    payload_field = None
    payload_size = 0
    pos = start
    while pos < end:
        key, pos = read_varint(buf, pos)
        field, wire = key >> 3, key & 7
        if wire == 0:
            _, pos = read_varint(buf, pos)
            continue
        if wire != 2:
            break
        length, after = read_varint(buf, pos)
        body = buf[after:after + length]
        pos = after + length
        if field == 1 and class_name is None:
            class_name = body.decode("utf-8", "replace")
        elif field == 3:
            continue                       # Label
        elif field >= 8 and payload_field is None:
            # 5-7 are unused by Component; the payload is the only other LEN field.
            payload_field = field
            payload_size = length
    if class_name and payload_field:
        tally[(payload_field, class_name)][payload_size] += 1
    return pos


def scan_object(buf, start, end, tally):
    pos = start
    while pos < end:
        key, pos = read_varint(buf, pos)
        field, wire = key >> 3, key & 7
        if wire == 0:
            _, pos = read_varint(buf, pos)
            continue
        if wire != 2:
            break
        length, after = read_varint(buf, pos)
        pos = after + length
        if field == 3:                     # SceneObject.Component[]
            scan_component(buf, after, after + length, tally)


def scan_template(buf, start, end, tally):
    pos = start
    while pos < end:
        key, pos = read_varint(buf, pos)
        field, wire = key >> 3, key & 7
        if wire == 0:
            _, pos = read_varint(buf, pos)
            continue
        if wire != 2:
            break
        length, after = read_varint(buf, pos)
        pos = after + length
        if field == 1:                     # ObjectTemplate.Object
            scan_object(buf, after, after + length, tally)


def scan_file(path, tally):
    with open(path, "rb") as handle:
        buf = handle.read()
    pos = 0
    while pos < len(buf):
        try:
            key, pos = read_varint(buf, pos)
        except ValueError:
            return
        field, wire = key >> 3, key & 7
        if wire == 0:
            try:
                _, pos = read_varint(buf, pos)
            except ValueError:
                return
            continue
        if wire != 2:
            return
        try:
            length, after = read_varint(buf, pos)
        except ValueError:
            return
        pos = after + length
        if field == 2:                     # ObjectLibrary.Template[]
            scan_template(buf, after, after + length, tally)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--min-count", type=int, default=0)
    args = ap.parse_args()

    files = []
    for path in args.paths:
        if os.path.isdir(path):
            files.extend(sorted(glob.glob(os.path.join(path, "*.scl"))))
            files.extend(sorted(glob.glob(os.path.join(path, "*.scene"))))
        else:
            files.append(path)

    tally = collections.defaultdict(collections.Counter)
    for path in files:
        scan_file(path, tally)

    print("# %d files, %d (field, class) pairs" % (len(files), len(tally)))
    print("# field  tag     class                                   count")
    rows = []
    for (field, name), sizes in tally.items():
        rows.append((field, name, sum(sizes.values()), sorted(sizes)))
    for field, name, count, sizes in sorted(rows):
        if count < args.min_count:
            continue
        print("%-6d %-7d %-46s %-6d sizes=%s" %
              (field, (field << 3) | 2, name, count, sizes[:4]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
