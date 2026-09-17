#!/usr/bin/env python3
"""
extract_lua_api.py — recover Swordigo's complete Lua API surface from the game binary.

Swordigo never registers its Lua surface as scattered `lua_setfield()` calls.
Each class that exposes an API has a `RegisterLibrary()` (or
`RegisterProgramLibrary()`) method that builds the namespace NAME as a string and
hands a static `Caver::LibFunction const*` table to
`Caver::ProgramState::RegisterLibrary(string, LibFunction const*)`:

    sub_28B2E0((int)v5, "CollisionShape");                            <- namespace
    Caver::ProgramState::RegisterLibrary(v4 + 24, v5, &off_43E6B4);   <- table

The IDA dump in `OpenSwordigo/arm32_13` gives the namespace string and the
table's virtual address for every such function. `Caver::LibFunction` is
`{ const char* name; void* fn; }` — 8 bytes on ARM32 — so dereferencing the table
read straight out of the matching binary yields the exact function names each
namespace publishes. Ground truth, not a reconstruction.

Usage
-----
    tools/extract_lua_api.py                      # human-readable tree
    tools/extract_lua_api.py --header             # emit src/tools/lua_api_table.h
    tools/extract_lua_api.py --json               # machine-readable

The `--header` output is what Ruby GG compiles against, so the visual scripter
can validate a generated call against the API the game actually ships.
"""

import argparse
import json
import os
import re
import struct
import sys
from collections import OrderedDict

HOME = os.path.expanduser("~")
DECOMP_DIR = os.path.join("OpenSwordigo", "arm32_13", "functions")

DEFAULT_BINARY = os.path.join(
    HOME,
    ".local/share/swordigo-desktop/engine/v1.4.13/armeabi-v7a/libswordigo.so",
)

# Namespaces the engine registers that are worth flagging as callable from a
# timeline action marker: these mutate the world at a point in time.
MUTATING_HINT = re.compile(r"^(Set|Add|Remove|Play|Open|Close|Activate|Deactivate|Enter|Goto|Inc|Trigger|Begin|Cancel|Finish|Reset|Revert|Override|Show|Hide|Fade|Flash|Focus|Follow|Jump|Link|Pickup|Drop|Scale|Translate|Rotate|Blend|Cast|Attack|Perform|Register|Destroy|Clone|SetText|SetMode|SetHidden)")


# ── minimal ELF32 reader ─────────────────────────────────────────────────────

class Elf32:
    """Just enough ELF32 to follow pointers in a shared object."""

    def __init__(self, path):
        self.path = path
        with open(path, "rb") as fh:
            self.data = fh.read()
        if self.data[:4] != b"\x7fELF":
            raise ValueError(f"not an ELF file: {path}")
        if self.data[4] != 1:
            raise ValueError(
                f"{path} is not ELF32 (EI_CLASS={self.data[4]}); the arm32_13 "
                "decomp addresses only apply to the armeabi-v7a build"
            )

        self.e_phoff, = struct.unpack_from("<I", self.data, 0x1C)
        self.e_phentsize, = struct.unpack_from("<H", self.data, 0x2A)
        self.e_phnum, = struct.unpack_from("<H", self.data, 0x2C)

        self.segments = []
        for i in range(self.e_phnum):
            off = self.e_phoff + i * self.e_phentsize
            fields = struct.unpack_from("<8I", self.data, off)
            self.segments.append(
                dict(zip(
                    ("type", "offset", "vaddr", "paddr", "filesz", "memsz",
                     "flags", "align"),
                    fields,
                ))
            )

    def loads(self):
        return [s for s in self.segments if s["type"] == 1]

    def va_to_off(self, va, size=1):
        for seg in self.loads():
            if seg["vaddr"] <= va < seg["vaddr"] + seg["filesz"]:
                off = seg["offset"] + (va - seg["vaddr"])
                return off if off + size <= len(self.data) else None
        return None

    def read(self, va, size):
        off = self.va_to_off(va, size)
        return None if off is None else self.data[off:off + size]

    def u32(self, va):
        raw = self.read(va, 4)
        return None if raw is None else struct.unpack("<I", raw)[0]

    def cstring(self, va, limit=256):
        off = self.va_to_off(va, 1)
        if off is None:
            return None
        end = self.data.find(b"\x00", off, off + limit)
        if end < 0:
            return None
        try:
            return self.data[off:end].decode("utf-8")
        except UnicodeDecodeError:
            return None


# ── decomp parsing ───────────────────────────────────────────────────────────

BODY_SPLIT = re.compile(r"^\s*\*/\s*$", re.M)
STR_ARG = re.compile(r'sub_[0-9A-F]+\(\s*\(int\)\s*\w+\s*,\s*"([^"\\]{2,60})"\s*\)')
TABLE_ARG = re.compile(r"Register[A-Za-z]*Library\s*\([^)]*?(off_[0-9A-F]+)\s*\)")
# RegisterClass takes TWO tables: the class/constructor table and the instance
# metatable. That is how Vector3 publishes `New`/`FromAngle` separately from
# `x`/`y`/`z`/`__add`, and Rectangle the same for `New` vs `left`/`top`/...
CLASS_ARG = re.compile(
    r"RegisterClass\s*\([^)]*?&?(off_[0-9A-F]+)\s*,\s*&?(off_[0-9A-F]+)\s*\)"
)
ANY_TABLE = re.compile(r"(off_[0-9A-F]{6,})")
STRING_LIT = re.compile(r'"([A-Za-z_][A-Za-z_0-9]{1,63})"')
IDENT = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")

KIND_LIB = "lib"           # namespace function: `Camera.Rumble`
KIND_CTOR = "ctor"         # class-level constructor: `Vector3.New`
KIND_INSTANCE = "instance"  # metatable member: `v:x()`, `v.__add`


def parse_decomp_file(path):
    """Return [(namespace, kind, table_va)] for one IDA-dumped registrar."""
    with open(path, "r", errors="replace") as fh:
        text = fh.read()

    parts = BODY_SPLIT.split(text, maxsplit=1)
    body = parts[1] if len(parts) > 1 else text

    triples = []
    last_string = None

    for line in body.splitlines():
        m = STR_ARG.search(line)
        if m:
            last_string = m.group(1)
            continue

        m = CLASS_ARG.search(line)
        if m and last_string:
            triples.append((last_string, KIND_CTOR, int(m.group(1)[4:], 16)))
            triples.append((last_string, KIND_INSTANCE, int(m.group(2)[4:], 16)))
            last_string = None
            continue

        m = TABLE_ARG.search(line)
        if m and last_string:
            triples.append((last_string, KIND_LIB, int(m.group(1)[4:], 16)))
            last_string = None
            continue

        if "Register" in line and "Library" in line and last_string:
            tables = ANY_TABLE.findall(line)
            if tables:
                triples.append((last_string, KIND_LIB, int(tables[0][4:], 16)))
                last_string = None

    if not triples:
        tables = ANY_TABLE.findall(body)
        strings = STRING_LIT.findall(body)
        if len(tables) == 1 and strings:
            triples.append((strings[0], KIND_LIB, int(tables[0][4:], 16)))

    return triples


def decode_table(elf, table_va, max_entries=64):
    """Decode a Caver::LibFunction array. Entry = {u32 name_ptr, u32 fn_ptr}."""
    out = []
    for i in range(max_entries):
        entry_va = table_va + i * 8
        name_ptr = elf.u32(entry_va)
        fn_ptr = elf.u32(entry_va + 4)
        if name_ptr is None or fn_ptr is None or name_ptr == 0:
            break
        name = elf.cstring(name_ptr)
        if name is None or not IDENT.fullmatch(name):
            break
        if elf.va_to_off(fn_ptr) is None:
            break
        out.append((name, fn_ptr))
    return out


# ── collection ───────────────────────────────────────────────────────────────

def collect(binary):
    elf = Elf32(binary)

    registrar_paths = []
    for root, _dirs, files in os.walk(DECOMP_DIR):
        for name in files:
            if name.endswith(".c") and "Register" in name and "Library" in name:
                registrar_paths.append(os.path.join(root, name))
    registrar_paths.sort()

    seen, namespaces = set(), OrderedDict()

    for path in registrar_paths:
        cls = os.path.basename(os.path.dirname(path))
        for namespace, kind, table_va in parse_decomp_file(path):
            if (namespace, kind, table_va) in seen:
                continue
            seen.add((namespace, kind, table_va))

            entries = decode_table(elf, table_va)
            if not entries:
                continue

            ns = namespaces.setdefault(namespace, OrderedDict())
            for fname, fn_va in entries:
                key = fname if kind != KIND_INSTANCE else ":" + fname
                if key in ns:
                    continue
                ns[key] = dict(owner=cls, address=fn_va, kind=kind,
                               mutating=bool(MUTATING_HINT.match(fname)))

    return elf, namespaces


# ── output ───────────────────────────────────────────────────────────────────

def emit_tree(namespaces, out):
    total = 0
    instance = 0
    for namespace, funcs in sorted(namespaces.items(), key=lambda kv: kv[0].lower()):
        print(f"## {namespace}  ({len(funcs)} entries)", file=out)
        for fname, meta in sorted(funcs.items()):
            mark = "*" if meta["mutating"] else " "
            member = fname.startswith(":")
            if member:
                instance += 1
            label = f"{namespace}{fname}" if member else f"{namespace}.{fname}"
            print(f"  {mark}{label:<38} {meta['address']:#010x}"
                  f"   [{meta['owner']}]", file=out)
            total += 1
        print(file=out)
    print(f"# {len(namespaces)} namespaces, {total} entries "
          f"({instance} instance members), "
          f"* = mutating / timeline-marker candidate", file=out)


def emit_header(namespaces, out, binary):
    import datetime

    namespaces = OrderedDict(
        (ns, OrderedDict(sorted(f.items())))
        for ns, f in sorted(namespaces.items(), key=lambda kv: kv[0].lower())
    )

    print("// AUTO-GENERATED — do not edit by hand.", file=out)
    print("// Regenerate with: tools/extract_lua_api.py --header > "
          "src/tools/lua_api_table.h", file=out)
    print("//", file=out)
    print("// Source of truth: the Caver::LibFunction tables inside the game's "
          "own ARM32 binary.", file=out)
    print(f"// Binary: {os.path.basename(binary)} (v1.4.13 armeabi-v7a)", file=out)
    print(f"// Generated: {datetime.date.today().isoformat()}", file=out)
    print("//", file=out)
    print("// Every entry is a namespace.function the engine actually "
          "publishes to Lua.", file=out)
    print("// `mutating` marks calls that change world/UI state, i.e. the "
          "candidates a", file=out)
    print("// timeline action marker may drop at a timestamp.", file=out)
    print("#pragma once", file=out)
    print(file=out)
    print("#include <cstdint>", file=out)
    print("#include <string_view>", file=out)
    print(file=out)
    print("namespace rbsrc {", file=out)
    print(file=out)
    print("enum class ApiKind : std::uint8_t {", file=out)
    print("    NamespaceFn,    // a function in a global table: Camera.Rumble",
          file=out)
    print("    ClassCtor,      // class-level constructor: Vector3.New", file=out)
    print("    InstanceMember  // metatable member via ':': v:x(), v.__add",
          file=out)
    print("};\n", file=out)
    print("struct ApiFunction {", file=out)
    print("    std::string_view lua_namespace;", file=out)
    print("    std::string_view name;", file=out)
    print("    std::string_view owner_class;   // Caver class that registers it",
          file=out)
    print("    uint32_t         address;       // VA in the ARM32 1.4.13 image",
          file=out)
    print("    ApiKind          kind;", file=out)
    print("    bool             mutating;      // sets state -> marker candidate",
          file=out)
    print("};", file=out)
    print(file=out)

    kind_enum = {KIND_LIB: "ApiKind::NamespaceFn",
                 KIND_CTOR: "ApiKind::ClassCtor",
                 KIND_INSTANCE: "ApiKind::InstanceMember"}

    table = []
    for namespace, funcs in namespaces.items():
        for fname, meta in funcs.items():
            table.append((namespace, fname, meta["owner"], meta["address"],
                          kind_enum[meta.get("kind", KIND_LIB)],
                          meta["mutating"]))

    print(f"inline constexpr ApiFunction kGameLuaApi[] = {{  // "
          f"{len(table)} entries", file=out)
    for namespace, fname, owner, address, kind, mutating in table:
        print(f'    {{"{namespace}", "{fname}", "{owner}", {address:#010x}u, '
              f'{kind}, {"true" if mutating else "false"}}},', file=out)
    print("};", file=out)
    print(file=out)
    print("inline constexpr std::size_t kGameLuaApiCount = "
          "sizeof(kGameLuaApi) / sizeof(kGameLuaApi[0]);", file=out)
    print(file=out)
    print("} // namespace rbsrc", file=out)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", default=DEFAULT_BINARY,
                    help="ARM32 libswordigo.so matching the arm32_13 decomp")
    ap.add_argument("--header", action="store_true",
                    help="emit src/tools/lua_api_table.h")
    ap.add_argument("--json", action="store_true", help="emit JSON")
    args = ap.parse_args()

    if not os.path.exists(args.binary):
        sys.exit(f"binary not found: {args.binary}")

    _elf, namespaces = collect(args.binary)
    if not namespaces:
        sys.exit("no namespaces recovered — decomp directory layout changed?")

    if args.header:
        emit_header(namespaces, sys.stdout, args.binary)
    elif args.json:
        json.dump({ns: dict(f) for ns, f in namespaces.items()}, sys.stdout, indent=2)
        print()
    else:
        emit_tree(namespaces, sys.stdout)


if __name__ == "__main__":
    main()
