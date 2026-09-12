# ModelLibrary

## Summary

`Caver::ModelLibrary` is the engine's **3D model asset cache** (a singleton,
lazily created behind `qword_651358`). It maps model names to
`boost::shared_ptr<const Model>` and animation names to animation resources;
on a cache miss `ModelForName` builds the resource path, loads the `.pod` file
through `PODLoader`, and constructs a `Model` via `PODLoader::CreateModel`.

Header: `src/sre/sre13/caver/ModelLibrary.h` (currently a stub — see layout
below for the real shape).

## Struct layout

Verified from the ARM64 v1.4.13 `sharedLibrary` (`0x4E2708`) and `Clear`
(`0x4E27C4`); `ModelForName` (`0x4E283C`) confirms the model map's location
and value type. The object is **0x48 bytes** (`operator new(0x48)` in
`sharedLibrary`) and contains three 24-byte red-black tree headers at +0x00,
+0x18 and +0x30 (the libc++ `std::map`-style layout: begin-node sentinel at
header+0x08, size at +0x10).

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x00 | 24 | `std::map<std::string, boost::shared_ptr<const Model>>` header | `models` | Header: `begin_node` (+0x00 = `&this+0x08` when empty), size (+0x08), root (+0x10). `ModelForName` calls `std::__tree::find` on it. Node layout: `{left@0, right@8, parent@16, color@24, key_string@32, value_shared_ptr@56/64}` (find-site reads at node+56/64; emplace node alloc 0x38 conflicts — see Open questions). |
| 0x18 | 24 | tree header | `animations` | Second container (`AnimationForModel` cache); contents not examined this pass. |
| 0x30 | 24 | tree header | (unknown) | Third container; no recovered accessor. |
| 0x48 | — | — | (end) | Size 0x48. |

## Exported functions

### ModelLibrary::sharedLibrary()
- Mangled: `_ZN5Caver12ModelLibrary13sharedLibraryEv` (0x4E2708).
- Behavior: lazy singleton — allocates a 0x48-byte instance, zeroes it, sets the
  three tree headers, stores it in `qword_651358`, returns it.
- Confidence: **verified**.

### ModelLibrary::Clear()
- Mangled: `_ZN5Caver12ModelLibrary5ClearEv` (0x4E27C4).
- Behavior: destroys the contents of all three containers and resets each header
  to the empty sentinel state.
- Side effects: releases every cached `Model` shared_ptr (dropping refcounts).
- Confidence: **verified**.

### ModelLibrary::ModelForName(std::string const&)
- Mangled: `_ZN5Caver12ModelLibrary12ModelForNameERKNSt6__ndk112basic_string...E` (0x4E283C).
- Behavior: `find()` in the +0x00 map; on hit, returns the cached
  `shared_ptr<const Model>` (copied out of node+56/64 with a refcount bump). On
  miss: `PathForResourceOfType(name, "pod")` → `PODLoader::ReadModelFromFile`
  → `PODLoader::CreateModel()` → inserts the new model into the map under the
  requested name (emplace; any pre-existing value's shared_ptr released) and
  returns it. Failures return an empty shared_ptr.
- Side effects: filesystem reads, heap allocations, map growth.
- Confidence: **verified**.

### ModelLibrary::AnimationForModel(...)
- Mangled: `_ZN5Caver12ModelLibrary16AnimationForModel...` (0x4E2D08).
- Behavior: animation lookup for a model; presumed to mirror `ModelForName`
  against the second container.
- Confidence: **inferred**.

## Open questions

- Node geometry: `ModelForName`'s find-site reads the shared_ptr value at
  node+56/+64, but the emplace site allocates a 0x38-byte node and writes the
  value at node+40/+48. One of the two sites was mis-decompiled; the map's
  value is certainly `shared_ptr<const Model>` but the exact node offsets need a
  disassembly pass to resolve.
- The purpose of the third container at +0x30 is unknown.

## Proposed SRE hooks

- `ModelLibrary_ModelForName` (already declared in the header) is the safe,
  supported way to force-load models and to introspect the cache — always use
  it rather than walking the map internals (shared_ptr refcounts).
- A `ModelLibrary_TotalByteSize`-style memory-report accessor (mirroring
  `TextureLibrary::TotalByteSize`) would help profiling; the counts can be
  derived from `Clear`-time behavior but a dedicated symbol is cleaner.
- **Do not** expose the raw tree headers: inserting/removing entries outside
  the map methods corrupts the red-black invariants and the refcount bookkeeping.