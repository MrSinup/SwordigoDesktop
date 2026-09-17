#!/usr/bin/env python3
"""Recover the Swordigo component schema from a libswordigo.so symbol table.

WHY
---
Every component is a protobuf *extension* of `Caver::Proto::Component`, and every
extension field is a `static const int` global in the binary:

    Caver::Proto::SpriteComponent::kExtensionFieldNumber   -> 100
    Caver::Proto::SpriteComponent::kTextureNameFieldNumber -> 1

`nm -D` gives the symbols and their addresses; the value at each address is the
field number. That makes the schema *measurable* instead of transcribed, and it
is the only way to recover the classes `scene_schemas.cpp` never had
(ParticleField, DimensionObject, DimensionSpell, …).

This is the same information `generate_schemas.py` once produced, but it takes
the shared object as input, so it can be re-run per engine version and diffed.

USAGE
-----
    tools/extract_component_schema.py <libswordigo.so> [--components] [--json]

* `--components` : ClassName -> extension slot, sorted by slot (the registry).
* `--fields`     : per-class field name -> tag, for every Proto message (default).
* `--missing`    : only classes/fields not already present in scene_schemas.cpp.
"""

import argparse
import json
import struct
import subprocess
import sys

PREFIX = "_ZN5Caver5Proto"

# Protobuf wire type per field, pinned by the codebase's own convention: tag =
# (number << 3) | wire. The symbol table does not carry the type, so it has to be
# inferred from the field name the way scene_schemas.cpp already does; the
# caller only ever needs the *number*.
WIRE_LEN = 2
WIRE_VARINT = 0
WIRE_F32 = 5
WIRE_F64 = 1


def load_segments(data):
    """PT_LOAD segments as (vaddr, file_offset, file_size) — VA -> file mapping."""
    assert data[:4] == b"\x7fELF", "not an ELF file"
    is64 = data[4] == 2
    if is64:
        phoff, = struct.unpack_from("<Q", data, 0x20)
        phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
    else:
        phoff, = struct.unpack_from("<I", data, 0x1C)
        phentsize, phnum = struct.unpack_from("<HH", data, 0x2A)
    segments = []
    for index in range(phnum):
        offset = phoff + index * phentsize
        if is64:
            p_type, _flags, p_offset, p_vaddr, _paddr, p_filesz, _memsz = \
                struct.unpack_from("<IIQQQQQ", data, offset)
        else:
            p_type, p_offset, p_vaddr, _paddr, p_filesz, _memsz, _flags, _align = \
                struct.unpack_from("<IIIIIIII", data, offset)
        if p_type == 1:
            segments.append((p_vaddr, p_offset, p_filesz))
    return segments, is64


def make_reader(data, segments):
    def read_int32(vaddr):
        for vaddr0, offset, size in segments:
            if vaddr0 <= vaddr < vaddr0 + size:
                file_offset = offset + (vaddr - vaddr0)
                if file_offset + 4 > len(data):
                    return None
                return struct.unpack_from("<i", data, file_offset)[0]
        return None
    return read_int32


def dynamic_symbols(path):
    try:
        out = subprocess.run(["nm", "-D", path], capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        print("nm -D failed on %s: %s" % (path, exc), file=sys.stderr)
        return
    for line in out.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            yield parts[0], parts[1], parts[2]


def _read_mangled_identifier(text, pos):
    """Itanium mangling writes `<length><identifier>`. Returns (identifier, next)."""
    end = pos
    while end < len(text) and text[end].isdigit():
        end += 1
    if end == pos:
        return None, pos
    length = int(text[pos:end])
    if end + length > len(text):
        return None, pos
    return text[end:end + length], end + length


def parse_constant_symbol(mangled):
    """`_ZN5Caver5Proto<Class><kFieldFieldNumber>E` -> (Class, Field).

    Parsing by hand rather than by regex because the class name may itself end in
    digits (`Vector3`) and a greedy regex silently mis-splits the length prefix.
    """
    if not mangled.startswith(PREFIX) or not mangled.endswith("E"):
        return None
    body = mangled[len(PREFIX):-1]
    class_name, pos = _read_mangled_identifier(body, 0)
    if class_name is None:
        return None
    member, pos = _read_mangled_identifier(body, pos)
    if member is None or not member.startswith("k") or not member.endswith("FieldNumber"):
        return None
    return class_name, member[1:-len("FieldNumber")]


def recover(path):
    with open(path, "rb") as handle:
        data = handle.read()
    segments, _is64 = load_segments(data)
    read_int32 = make_reader(data, segments)

    slots = {}                 # ClassName -> extension field number
    fields = {}                # ClassName -> {field_name: number}
    for address, _kind, mangled in dynamic_symbols(path):
        parsed = parse_constant_symbol(mangled)
        if not parsed:
            continue
        name, field_name = parsed
        value = read_int32(int(address, 16))
        if value is None:
            continue
        if field_name == "Extension":
            slots[name] = value
        else:
            fields.setdefault(name, {})[field_name] = value
    return slots, fields


def tag_for(name):
    """The wire tag the codebase keys on. Only used for reporting — the schema in
    scene_schemas.cpp carries the real wire type, recovered per field."""
    return (name << 3) | WIRE_LEN


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("library")
    ap.add_argument("--components", action="store_true")
    ap.add_argument("--fields", action="store_true")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    slots, fields = recover(args.library)
    if not slots:
        print("no Caver.Proto extension symbols in %s" % args.library, file=sys.stderr)
        return 1

    if args.json:
        json.dump({"slots": slots, "fields": fields}, sys.stdout, indent=1, sort_keys=True)
        return 0

    if args.components or not args.fields:
        print("# %d component extensions" % len(slots))
        print("# %-10s %-8s %s" % ("field", "tag", "class"))
        for name, number in sorted(slots.items(), key=lambda kv: kv[1]):
            print("%-12d %-8d %s" % (number, tag_for(number), name))

    if args.fields:
        print()
        for name in sorted(fields):
            print("%s {" % name)
            for field_name, number in sorted(fields[name].items(), key=lambda kv: kv[1]):
                print("    %-28s %-8d tag=%d" % (field_name, number, tag_for(number)))
            print("}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
