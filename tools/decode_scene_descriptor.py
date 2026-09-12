#!/usr/bin/env python3
"""Decode the Scene.proto descriptor embedded in libswordigo.so.

WHY
---
Every Swordigo component is a protobuf *extension* of the `Component` message:
`extend Component { optional SpriteComponent SpriteComponent = 802; ... }`. The
payload field number (the tag the editor keys everything on) therefore lives in
the binary's serialized FileDescriptorProto, not in any table we can hand-keep.
`src/tools/scene_schemas.cpp` was produced by a `generate_schemas.py` that is not
in this tree; this script replaces it with something checkable, and — because it
takes the .so as input — it can be pointed at any engine version, which is how
the v1.4.13 classes the 1.4.12 schema was missing get recovered.

USAGE
-----
    tools/decode_scene_descriptor.py <libswordigo.so> [--tags] [--schema] [--json]

* `--tags`   : component class -> payload tag (the Component extensions).
* `--schema` : every message's fields as name@number, for regenerating
               scene_schemas.cpp / the caver component registry.
* `--json`   : machine-readable output for the generator.

Field numbers for top-level messages (Vector2/Vector3/Program/Component/…) are
reported the same way the existing schema uses them: `tag = field_number << 3`
for varint fields, which is why the generated table reads 8, 16, 24 … for
consecutive ints and 10, 18, 26 … for strings.
"""

import argparse
import json
import sys


def read_varint(buf, pos):
    result = 0
    shift = 0
    while True:
        if pos >= len(buf):
            raise ValueError("truncated varint")
        b = buf[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, pos
        shift += 7
        if shift > 63:
            raise ValueError("varint too long")


def parse_fields(buf, pos, end):
    """Yield (field_number, wire_type, value_or_bytes, new_pos)."""
    while pos < end:
        key, pos = read_varint(buf, pos)
        field, wire = key >> 3, key & 7
        if wire == 0:
            value, pos = read_varint(buf, pos)
        elif wire == 2:
            length, pos = read_varint(buf, pos)
            value = buf[pos:pos + length]
            pos += length
        elif wire == 5:
            value = buf[pos:pos + 4]
            pos += 4
        elif wire == 1:
            value = buf[pos:pos + 8]
            pos += 8
        else:
            raise ValueError("unsupported wire type %d" % wire)
        yield field, wire, value, pos


def find_descriptor(buf):
    """Locate the FileDescriptorProto for Scene.proto in a mapped ELF image."""
    needle = b"\x0a\x0dScene.proto"
    start = buf.find(needle)
    if start < 0:
        return None
    # Walk back to the beginning of the message: the descriptor is preceded by
    # its own length prefix in the generated table, so the earliest plausible
    # start is the byte after that prefix.
    for back in range(1, 6):
        candidate = start - back
        if candidate < 0:
            continue
        try:
            length, after = read_varint(buf, 2)  # unused; kept for clarity
        except ValueError:
            continue
        del length, after
    return start


def decode_descriptor(buf, start):
    """Parse messages/extensions/enums out of the FileDescriptorProto at `start`."""
    messages = {}
    extensions = []
    package = ""
    order = []
    # The descriptor is self-delimiting; parse until the bytes stop making sense.
    try:
        for field, wire, value, _ in parse_fields(buf, start + 14, len(buf)):
            if field == 2 and wire == 2:          # package
                package = value.decode("utf-8", "replace")
            elif field == 4 and wire == 2:        # message_type
                name, fields = parse_message(value)
                messages[name] = fields
                order.append(name)
            elif field == 7 and wire == 2:        # extension (Component payloads)
                ext = parse_extension(value)
                if ext:
                    extensions.append(ext)
            elif field == 12 and wire == 2:       # syntax
                break
    except (ValueError, UnicodeDecodeError):
        pass
    return package, messages, extensions, order


def parse_message(blob):
    name = ""
    fields = []
    nested = {}
    for field, wire, value, _ in parse_fields(blob, 0, len(blob)):
        if field == 1 and wire == 2:
            name = value.decode("utf-8", "replace")
        elif field == 2 and wire == 2:
            fname, fnum, label, ftype, type_name = parse_field_descriptor(value)
            fields.append({"name": fname, "number": fnum, "label": label,
                           "type": ftype, "type_name": type_name})
        elif field == 3 and wire == 2:
            nname, nfields = parse_message(value)
            nested[nname] = nfields
    return name, {"fields": fields, "nested": nested}


def parse_field_descriptor(blob):
    name, number, label, ftype, type_name = "", 0, 0, 0, ""
    for field, wire, value, _ in parse_fields(blob, 0, len(blob)):
        if field == 1 and wire == 2:
            name = value.decode("utf-8", "replace")
        elif field == 3 and wire == 0:
            number = value
        elif field == 4 and wire == 0:
            label = value
        elif field == 5 and wire == 0:
            ftype = value
        elif field == 6 and wire == 2:
            type_name = value.decode("utf-8", "replace")
    return name, number, label, ftype, type_name


def parse_extension(blob):
    name, number, type_name = "", 0, ""
    extendee = ""
    for field, wire, value, _ in parse_fields(blob, 0, len(blob)):
        if field == 1 and wire == 2:
            name = value.decode("utf-8", "replace")
        elif field == 2 and wire == 2:
            extendee = value.decode("utf-8", "replace")
        elif field == 3 and wire == 0:
            number = value
        elif field == 6 and wire == 2:
            type_name = value.decode("utf-8", "replace")
    if not name:
        return None
    return {"name": name, "number": number, "type_name": type_name, "extendee": extendee}


def wire_tag(field):
    """The tag the rest of the codebase keys on: (number << 3) | wire_type."""
    # 5 = TYPE_STRING, 6 = TYPE_MESSAGE, 4 = TYPE_DOUBLE, 12 = TYPE_BYTES,
    # 13 = TYPE_UINT32, 14 = TYPE_ENUM, 3 = TYPE_INT64, 8 = TYPE_BOOL.
    wire = {5: 2, 6: 2, 12: 2, 3: 0, 4: 1, 13: 0, 14: 0, 8: 0, 1: 1, 2: 1}.get(field["type"])
    if wire is None:
        return field["number"] << 3
    return (field["number"] << 3) | wire


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("library")
    ap.add_argument("--tags", action="store_true")
    ap.add_argument("--schema", action="store_true")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    with open(args.library, "rb") as handle:
        buf = handle.read()

    start = find_descriptor(buf)
    if start is None:
        print("Scene.proto descriptor not found in %s" % args.library, file=sys.stderr)
        return 1
    package, messages, extensions, order = decode_descriptor(buf, start)

    if args.json:
        json.dump({"package": package,
                   "extensions": sorted(extensions, key=lambda e: e["number"]),
                   "messages": messages}, sys.stdout, indent=1)
        return 0

    if args.tags or not (args.tags or args.schema):
        print("# package: %s" % package)
        print("# %d component payload extensions" % len(extensions))
        for ext in sorted(extensions, key=lambda e: e["number"]):
            print("%-6d %-46s %s" % (ext["number"], ext["name"],
                                     ext["type_name"].split(".")[-1]))

    if args.schema:
        for name in order:
            message = messages[name]
            if not message["fields"]:
                continue
            print("\n# %s" % name)
            for field in sorted(message["fields"], key=lambda f: f["number"]):
                print("  %-4d %-40s %s" % (wire_tag(field), field["name"],
                                           field["type_name"].split(".")[-1] or field["type"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
