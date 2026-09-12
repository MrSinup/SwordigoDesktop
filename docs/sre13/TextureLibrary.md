# TextureLibrary

## Summary

`Caver::TextureLibrary` is the engine's **texture asset cache** (a singleton
behind `qword_651AD8`). It maps texture names to
`boost::intrusive_ptr<Texture>`, tracks a byte budget for texture memory
(default `1,000,000,000` bytes), keeps an unused-textures vector for
eviction, and provides load/reload/purge/stat helpers. `TextureForName` is the
hot path: on a miss it constructs a `Texture` (0xC8 bytes), initializes it from
the resource (trying a `.png`-suffixed variant first when a flag at +0x21 is
set), and inserts it into the map.

Header: `src/sre/sre13/caver/TextureLibrary.h`.

## Struct layout

Verified from the ARM64 v1.4.13 ctor (`0x550B84`) and `TextureForName`
(`0x550BBC`). 64-bit ABI. The header's `texturesBegin/texturesEnd` pair guess
is close but the map lives at +0x08, and `someCounter` at +0x20 is actually the
memory budget at +0x24.

| Offset | Size | Type | Field | Notes |
|---|---|---|---|---|
| 0x00 | 8 | `void*` | `vtable` | `off_636D60`. |
| 0x08 | 24 | `std::map<std::string, boost::intrusive_ptr<Texture>>` header | `textures` | Header: begin-node sentinel `= &this+0x10` when empty, size at +0x18. `TextureForName` calls `find(this+8)`. Node value: intrusive_ptr<Texture> at node+56 (refcount bumped at texture+8). |
| 0x20 | 2 | `uint16` | `= 0` | Written as a WORD (`this+0x20`). |
| 0x22 | 1 | `bool` | `usePNGExtension` | `TextureForName` checks byte +0x21 to decide whether to try `name + ".png"` before the bare name. **Inferred.** |
| 0x24 | 8 | `uint64` | `textureMemoryBudget` | `= 0x3B9ACA00` = 1,000,000,000 (bytes). Read by `PurgeTexturesIfNecessary`/`PrintMemoryUsageStats` paths. |
| 0x2C | 4 | — | (unknown) | Unmapped. |
| 0x30 | 24 | `std::vector<Texture*>` | `unusedTextures` | `{begin@0x30, end@0x38, cap@0x40}` all zero (empty). `RefreshUnusedTexturesList`/`TextureUnloaded` maintain it. |
| 0x48 | — | — | (end) | Size 0x48. |

## Exported functions

### TextureLibrary::sharedLibrary()
- Mangled: `_ZN5Caver14TextureLibrary13sharedLibraryEv` (0x550A7C).
- Behavior: lazy singleton — `operator new(0x48)`, ctor, store in `qword_651AD8`.
- Confidence: **verified**.

### TextureLibrary::TextureLibrary()
- Mangled: `_ZN5Caver14TextureLibraryC2Ev` (0x550B84).
- Behavior: sets the vtable, textures map header, budget (`1e9`), empty
  unused-textures vector.
- Confidence: **verified**.

### TextureLibrary::TextureForName(std::string const&, bool load)
- Mangled: `_ZN5Caver14TextureLibrary14TextureForNameERKNSt6__ndk112basic_string...Eb` (0x550BBC).
- Behavior: `find(this+0x08)`; on hit returns the cached `intrusive_ptr<Texture>`
  (refcount bump). On miss: allocates a `Texture` (0xC8), copies the name into
  texture+0x40, and — when byte +0x21 is set — first tries `name + ".png"` via
  `Texture::InitWithResource`; on failure falls back to the bare name. On
  success inserts via `SetTextureForName` and (if the `load` arg is true and the
  texture isn't loaded) calls `Texture::Load()`. On failure, releases the
  temporary texture (refcount to zero ⇒ vtable+8 dtor).
- Side effects: file/asset I/O, GPU upload, map growth.
- Confidence: **verified**.

### TextureLibrary::Clear() / ReloadTextures(bool) / PurgeTexturesIfNecessary()
- `Clear` (0x5515E0): empties the map, releasing every texture.
- `ReloadTextures` (0x551544): reloads all cached textures from disk (used on
  GL context loss).
- `PurgeTexturesIfNecessary` (0x551664): evicts unused textures when the budget
  (+0x24) is exceeded.
- `TotalByteSize` (0x551AD8) / `PrintMemoryUsageStats` (0x551AF4): memory
  accounting. `TextureLoaded`/`TextureUnloaded` (0x5517C4/0x5517E4) are the
  notification entry points that maintain the totals.
- `TextureFromProtobufMessage` (0x551000): deserializes a texture definition.
- `LoadTextureAtlasWithName` (0x551258): loads a sprite atlas.
- Confidence: **verified** (symbols + ctor/find bodies; Clear/purge bodies
  summarized).

## Open questions

- Byte +0x21's exact role (the `.png` fallback switch) is inferred from one
  branch; the flag may instead gate "append extension" globally.
- The textures map node value offset (+56) is consistent within this class
  (unlike ModelLibrary) — no conflict found.

## Proposed SRE hooks

- `TextureLibrary_TextureForName` (already declared) is the supported entry
  point for preloading and introspection; always use it over map walking
  (intrusive_ptr refcounts live inside the nodes).
- Expose `TextureLibrary_TotalByteSize` (declared) and a new
  `TextureLibrary_PrintMemoryUsageStats` wrapper for memory profiling of mods.
- **Do not** write `textureMemoryBudget` (+0x24) from scripts without a
  wrapper — the purge path expects a stable budget between frames; expose
  `TextureLibrary_SetMemoryBudget(uint64)` instead if needed.