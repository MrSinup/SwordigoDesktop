# Research: SRE FFI Infrastructure Upgrade — FFI as Closed-Source Feature

## 1. Current Architecture

### Two FFI Systems Exist Today

| | **SRE Core** (`sre_ffi.c`) | **SRE Extras** (`ffi.c` + `libffi/`) |
|---|---|---|
| **File** | `src/sre/sre_ffi.c` | `src/sre-extras-closed-source/ffi.c` |
| **Libffi** | ❌ None — hand-rolled | ✅ Vendored libffi (`libffi/` dir) |
| **Dispatch** | Function pointer casts (`pfn_i0..pfn_i8`, mixed int/float typedefs) | `ffi_prep_cif()` + `ffi_call()` — ABI-correct |
| **Struct support** | ❌ None | ✅ Vector3, Vector2, Quaternion, Matrix4, FloatColor, Rectangle, CppString |
| **Signature** | Single-char args: `"iif"` = int,int,float | `"args:ret"` with `*` ptr syntax: `"pV*ib:v"` = void*, Vector3*, int, bool → void |
| **Pointer arg** | Manual `lua_islightuserdata` check | `ffi_extract_ptr()` handles userdata, light userdata, metatable `__ptr`, nil |
| **Result marshaling** | `ffi_push_result()` — basic int/float/ptr/str | `push_result()` — full struct table construction |
| **Built into** | `libsre.so` (always) | `libsre-extras.so` (optional, loaded if present) |
| **Stubs** | N/A (always present) | `sre_extras_stubs.c` — stub `MemoryAddress.call()` + stub read/write when extras absent |

### What Each API Exposes to Lua

**SRE Core (`_G.ffi`):**
- `ffi.call(addr, ret_type, arg_types, ...)` — basic typed call
- `ffi.bind(addr, ret_type, arg_types)` — closure cache (64 slots)
- `ffi.peek8/16/32/64/f`, `ffi.poke8/16/32/64/f`, `ffi.peekstr`
- `ffi.readf32/f64/i32/i64`, `ffi.writef32/i32` — struct field helpers
- `ffi.offset(base, ...)`, `ffi.deref(addr)`, `ffi.null()`, `ffi.base()`, `ffi.at(offset)`
- `ffi.memcpy(dst, src, n)`, `ffi.memset(dst, byte, n)`
- `ffi.typeof(addr)` — debug type info

**SRE Extras (`Mini.MemoryAddress` + `_G.ffi`):**
- `MemoryAddress:call(sig, ...)` — signature-based dispatch (e.g., `"pV*ib:v"`)
- `MemoryAddress` userdata with `readBool/Int8/.../readCppString/Vector3` methods
- `MemoryAddress` userdata with `writeBool/Int8/.../writeCppString/Vector3` methods
- `MemoryAddress:pointer()` → `void*`
- `Mini.GetAddress(name)`, `Mini.Dlsym(name)`, `Mini.Malloc(size)`, `Mini.Free(ptr)`
- `Mini.GetComponentAddress(gameObject, type)`, `Mini.GetObjectAddress(id)`
- `ffi.call(addr, sig, ...)` — same as `MemoryAddress:call`

### Current Dependency Graph

```
libsre.so (always loaded)
  ├── sre_ffi.c          ← hand-rolled ARM64 dispatch, no libffi
  ├── sre_extras_stubs.c ← stub MemoryAddress + stub ffi.call_sig
  └── (no libffi linked)

libsre-extras.so (optional)
  ├── ffi.c              ← libffi-backed dispatch (superior)
  ├── libffi/             ← vendored libffi (sysv.S, ffi.c, prep_cif.c, types.c)
  ├── memory.c            ← MemoryAddress userdata (read/write methods)
  ├── mod_button_hooks.c  ← button lifecycle
  ├── mod_fs.c            ← file system extensions
  └── mod_saves.c         ← save extensions
```

---

## 2. Key Findings

### 2.1 libffi Is Already In Extras — Compiles Standalone

Extras has a self-contained libffi directory:
```
src/sre-extras-closed-source/libffi/
├── ffi.c           ← main dispatch + FFI_DEFAULT_ABI
├── ffi.h           ← public header (ffi_type, ffi_cif, ffi_call, ffi_prep_cif)
├── ffi_cfi.h       ← control flow integrity
├── ffi_common.h    ← internal macros
├── ffitarget.h     ← architecture target
├── fficonfig.h     ← build config
├── internal.h      ← internals
├── prep_cif.c      ← ffi_prep_cif implementation
├── sysv.S          ← ARM64 assembly trampoline
├── tramp.h         ← trampoline support
└── types.c         ← built-in type definitions
```

It compiles with: `aarch64-linux-gnu-gcc -shared -fPIC -O2 -nostdlib` and links into `libsre-extras.so`.

**This exact copy stays in extras — it's the closed-source FFI engine.**

### 2.2 libffi-master (Full Source) Exists at Repo Root

`libffi-master/` contains the complete upstream libffi with support for all architectures (ARM64, x86_64, ARM32, MIPS, etc.). This is a superset of what extras has — extras only has the ARM64 slice.

**If we want cross-architecture FFI for x86_64 desktop builds, `libffi-master` is the right source.**

### 2.3 SRE Core's Hand-Rolled Dispatch Is Limited

Current `ffi_dispatch_int()` uses hardcoded typedef combinations:
```c
typedef uint64_t (*pfn_i0)(void);
typedef uint64_t (*pfn_i1)(uint64_t);
// ... up to pfn_i8
typedef uint64_t (*pfn_f0v)(double);
typedef uint64_t (*pfn_f1v)(uint64_t, double);
// ... 13 mixed int/float combinations
```

**Limitations:**
1. **Max 8 int args + 8 float args** — covers most Swordigo APIs, but not all
2. **No struct-by-value** — can't pass Vector3, Quaternion, etc. as args
3. **No struct-by-value return** — functions returning structs crash or corrupt
4. **Fallback for unknown combos** is all-integer (float args won't be marshaled correctly)
5. **ARM64 only** — won't work on x86_64 (different ABI: System V AMD64 uses different register allocation)

### 2.4 Extras FFI Is Strictly Superior

Raijin's libffi-backed FFI:
1. **ABI-correct for all call shapes** — `ffi_prep_cif` computes correct register/memory placement
2. **Full struct support** — Vector3, Vector2, Quaternion, Matrix4, FloatColor, Rectangle, CppString
3. **Signature grammar** — `"pV*ib:v"` is more expressive than single-char arg types
4. **`*` pointer syntax** — `V*` vs `V` (by-pointer vs by-value) distinction
5. **Lua table marshaling** — Vector3 from `{x=1, y=2, z=3}`, Matrix4 from flat array
6. **Works on x86_64** — libffi handles all ABIs

---

## 3. Revised Architecture Plan — FFI as Closed-Source Feature

### 3.1 Decision

**FFI is a closed-source feature.** Move the real FFI entirely into SRE extras. SRE core gets only stubs — the same pattern as MemoryAddress, button hooks, mod_fs, mod_saves.

**Rationale:**
- FFI is the single most powerful modding primitive — it gives full process memory access
- Keeping it closed-source means the open-source SRE shell can't be used to bypass restrictions
- Consistent with existing extras pattern (MemoryAddress, mod_fs, mod_saves are all closed-source)
- The hand-rolled dispatch in `sre_ffi.c` is inferior — no reason to maintain it in OSS
- Reduces SRE core code size and maintenance burden

### 3.2 New Architecture — Two FFI Files in Extras

```
libsre.so (always loaded) — OPEN SOURCE
  ├── sre_extras_stubs.c     ← STUBS for all extras features (FFI, MemoryAddress, etc.)
  ├── sre_mini_api.c         ← unchanged
  ├── sre_scene_update.c     ← unchanged
  └── (no libffi, no FFI implementation)

libsre-extras.so (optional) — CLOSED SOURCE
  │
  ├── sre_ffi.c              ← UPGRADED — moved from SRE core, now libffi-backed
  │   │                         Contains ALL _G.ffi table functions (basic + advanced + risky)
  │   │                         Uses libffi for ABI-correct dispatch
  │   │                         Adds advanced/risky commands not in core today
  │   │
  │   │  Basic functions (moved from core, now libffi-backed):
  │   │    ffi.call(addr, ret_type, arg_types, ...)
  │   │    ffi.bind(addr, ret_type, arg_types)
  │   │    ffi.peek8/16/32/64/f, ffi.poke8/16/32/64/f, ffi.peekstr
  │   │    ffi.readf32/f64/i32/i64, ffi.writef32/i32
  │   │    ffi.offset(base, ...), ffi.deref(addr), ffi.null(), ffi.base(), ffi.at(offset)
  │   │    ffi.memcpy(dst, src, n), ffi.memset(dst, byte, n)
  │   │    ffi.typeof(addr)
  │   │
  │   │  Advanced/risky functions (NEW — not in core today):
  │   │    ffi.cast(type, value)           — type cast between number/ptr/struct
  │   │    ffi.new(type, ...)              — allocate C struct from Lua
  │   │    ffi.sizeof(type)                — get size of a type
  │   │    ffi.cdef[[ C declarations ]]    — parse C function signatures from Lua
  │   │    ffi.metatype(type, mt)          — attach methods to a C type
  │   │    ffi.load(libname)               — dlopen a shared library
  │   │    ffi.addr(ptr, offset)           — pointer arithmetic with bounds
  │   │    ffi.read(type, ptr)             — typed pointer read (any struct)
  │   │    ffi.write(type, ptr, value)     — typed pointer write (any struct)
  │   │    ffi.copy(dst, src, n)           — ffi.memcpy alias (LuaJIT compat)
  │   │    ffi.fill(dst, n, val)           — ffi.memset alias (LuaJIT compat)
  │   │    ffi.gc(ptr, finalizer)          — attach GC finalizer to pointer
  │   │    ffi.errno()                     — get errno after FFI call
  │   │    ffi.abi()                       — query ABI info (arch, endianness, etc.)
  │   │
  │   │  Kernel-level functions (DANGEROUS — direct memory/game engine):
  │   │    ffi.peek_raw(addr, n)           — read N bytes as raw string
  │   │    ffi.poke_raw(addr, data)        — write raw string to memory
  │   │    ffi.patch(addr, bytes)          — write instruction bytes (hook/jump)
  │   │    ffi.alloc(size)                 — allocate guest memory (wraps Mini.Malloc)
  │   │    ffi.free(ptr)                   — free guest memory (wraps Mini.Free)
  │   │    ffi.seal(addr, size)            — mark memory as execute+read
  │   │    ffi.unseal(addr, size)          — remove execute permission
  │   │    ffi.search(start, end, pattern) — byte pattern search in memory range
  │   │    ffi.dump(addr, n)               — hex dump memory region (debug)
  │   │
  │   │  Type system:
  │   │    ffi.typeof(addr)                — detect Lua value type
  │   │    ffi.istype(type, val)           — type checking
  │   │    ffi.alignment(type)             — alignment of a type
  │   │    ffi.offsetof(type, field)       — struct field offset
  │   │    ffi.string(ptr, len)            — read C string from pointer
  │   │    ffi.wstring(ptr, len)           — read wide string (UTF-16)
  │   │    ffi.tonumber(val)               — convert to number
  │   │    ffi.tobool(val)                 — convert to boolean
  │   │    ffi.tostring(ptr)               — read null-terminated string
  │   │
  │   │  NOTE: All functions check for NULL/invalid addresses and log errors.
  │   │        Risky functions (patch, seal, search) require explicit enable
  │   │        via ffi.risky_mode(true) or a config flag.
  │   │
  │   ├── Depends on: libffi/ (vendored)
  │   └── Depends on: raijin_ffi.h (for Raijin type definitions)
  │
  ├── raijin_ffi.c           ← RAIJIN'S SIGNATURE API (renamed from ffi.c)
  │   │                         Raijin's signature-based dispatch system
  │   │                         Builds on top of sre_ffi.c's libffi infrastructure
  │   │
  │   │  Raijin-specific functions:
  │   │    ffi.call(addr, sig, ...)        — override ffi.call with signature parsing
  │   │    MemoryAddress:call(sig, ...)    — method on MemoryAddress userdata
  │   │    MemoryAddress:read*/write*      — all read/write methods per type
  │   │    MemoryAddress:pointer()         — get raw void*
  │   │    Mini.GetAddress(name)           — resolve symbol by name
  │   │    Mini.Dlsym(name)                — dlsym lookup
  │   │    Mini.Malloc(size)               — allocate
  │   │    Mini.Free(ptr)                  — free
  │   │    Mini.GetComponentAddress(go,t)  — component lookup
  │   │    Mini.GetObjectAddress(id)       — object lookup
  │   │
  │   │  Signature grammar: "args:ret"
  │   │    v=void i=int l=int64 f=float d=double b=bool p=pointer
  │   │    V=Vector3 2=Vector2 Q=Quaternion M=Matrix4 C=FloatColor R=Rectangle
  │   │    S=CppString (always passed by pointer)
  │   │    * suffix = pointer (e.g., "V*" = Vector3*, "V" = Vector3 by value)
  │   │
  │   │  Example: "pV*ib:v" → (void*, Vector3*, int, bool) → void
  │   │           "Vi*:v"  → (Vector3 by value, int*) → void
  │   │
  │   ├── Depends on: sre_ffi.c (uses libffi infrastructure internally)
  │   └── Depends on: libffi/ (for struct marshaling)
  │
  ├── libffi/                 ← vendored libffi (unchanged)
  ├── memory.c                ← MemoryAddress userdata
  ├── mod_button_hooks.c      ← button lifecycle
  ├── mod_fs.c                ← file system
  └── mod_saves.c             ← save extensions
```

### 3.3 Relationship Between the Two FFI Files

```
                    ┌─────────────────────────────────────┐
                    │         libsre-extras.so              │
                    │                                      │
  Lua calls  ──→  │  raijin_ffi.c (Raijin signature API) │
  ffi.call(addr,   │    │                                  │
   "pV*ib:v",...)  │    │ delegates to                     │
                    │    ▼                                  │
                    │  sre_ffi.c (upgraded core FFI)       │
                    │    │                                  │
                    │    │ uses                             │
                    │    ▼                                  │
                    │  libffi/ (ABI-correct dispatch)      │
                    └─────────────────────────────────────┘

  Lua calls  ──→  stubs in sre_extras_stubs.c (when extras absent)
  ffi.call(...)     │
                    ▼
                    nil (logged)
```

**Loading order:**
1. SRE core loads `sre_extras_stubs.c` → registers stub `_G.ffi` table
2. SRE core loads `libsre-extras.so`
3. Extras init calls `sre_ffi_register_lua(L)` → registers real `_G.ffi` table (overwrites stubs)
4. Extras init calls `raijin_ffi_register_lua(L)` → extends `_G.ffi` with Raijin-specific functions + MemoryAddress

### 3.4 What Happens to Core's `sre_ffi.c`

**Current `sre_ffi.c` (~450 lines):** Deleted from SRE core. Moved to extras as an upgraded file.

**In extras, `sre_ffi.c` becomes:**
- Uses `libffi` instead of hand-rolled dispatch
- Keeps ALL existing function names and Lua API signatures
- Adds new advanced/risky functions (see section 3.2)
- Registers `_G.ffi` base table

### 3.5 What Happens to Extras' `ffi.c`

**Current extras `ffi.c`:** Renamed to `raijin_ffi.c`.

**In extras, `raijin_ffi.c` contains:**
- Raijin signature parser (`ffi_parse_signature`)
- `caver_ffi_dispatch` (Raijin's signature-based call)
- `ffi_lua_call` (MemoryAddress:call method)
- MemoryAddress read/write methods
- Mini.* functions (GetAddress, Dlsym, Malloc, Free, etc.)
- Extends `_G.ffi` with signature-based `ffi.call` override

### 3.6 What Happens to `sre_extras_stubs.c`

**Updated stubs** — add `_G.ffi` table with all functions:

```c
/* ============================================================
 * Stub _G.ffi — registered when libsre-extras.so is absent.
 *
 * Some functions work for real (pure math/memory — no ABI dispatch):
 *   ffi.offset, ffi.deref, ffi.null, ffi.base, ffi.at, ffi.typeof
 *
 * All dispatch/memory-access functions are safe stubs:
 *   ffi.call, ffi.bind, ffi.peek*, ffi.poke*, ffi.read*, ffi.write*
 *   ffi.memcpy, ffi.memset, ffi.patch, ffi.seal, ffi.search, etc.
 *
 * Advanced/risky functions also stubbed:
 *   ffi.cast, ffi.new, ffi.cdef, ffi.load, ffi.gc, ffi.errno, etc.
 * ============================================================ */
```

**Stub behavior matrix:**

| Function | Stub behavior | Why |
|----------|--------------|-----|
| **Pure math (always work):** | | |
| `ffi.offset(base, ...)` | **Real** — base + sum(offsets) | No ABI, no memory |
| `ffi.null()` | **Real** — returns 0 | Constant |
| `ffi.base()` | **Real** — returns g_swordigo_base | Global variable |
| `ffi.at(offset)` | **Real** — base + offset | Pure math |
| `ffi.typeof(val)` | **Real** — lua type string | Lua introspection |
| `ffi.tonumber(val)` | **Real** — lua_tonumber | Lua conversion |
| `ffi.tobool(val)` | **Real** — lua_toboolean | Lua conversion |
| `ffi.abi()` | **Real** — compile-time constants | Preprocessor |
| `ffi.sizeof(type)` | **Real** — sizeof for known types | Compile-time |
| `ffi.alignment(type)` | **Real** — alignof for known types | Compile-time |
| `ffi.offsetof(type, field)` | **Real** — known struct offsets | Compile-time |
| **Memory read (safe stubs):** | | |
| `ffi.peek8/16/32/64/f()` | Returns nil | Needs memory |
| `ffi.peekstr()` | Returns nil | Needs memory |
| `ffi.peek_raw()` | Returns nil | Needs memory |
| `ffi.readf32/f64/i32/i64()` | Returns nil | Needs memory |
| `ffi.read(type, ptr)` | Returns nil | Needs memory |
| `ffi.string(ptr, len)` | Returns nil | Needs memory |
| `ffi.wstring(ptr, len)` | Returns nil | Needs memory |
| `ffi.tostring(ptr)` | Returns nil | Needs memory |
| `ffi.dump(addr, n)` | No-op | Needs memory |
| `ffi.deref(addr)` | **Real** — *(uint64_t*)addr | Direct deref (risky but functional) |
| **Memory write (no-ops):** | | |
| `ffi.poke8/16/32/64/f()` | No-op | Needs memory |
| `ffi.poke_raw()` | No-op | Needs memory |
| `ffi.writef32/i32()` | No-op | Needs memory |
| `ffi.write(type, ptr, val)` | No-op | Needs memory |
| **Dispatch (nil stubs):** | | |
| `ffi.call()` | Returns nil | Needs libffi |
| `ffi.bind()` | Returns nil | Needs libffi |
| `ffi.cast()` | Returns nil | Needs libffi |
| `ffi.new()` | Returns nil | Needs allocation |
| `ffi.cdef()` | No-op | Needs parser |
| `ffi.metatype()` | No-op | Needs type system |
| `ffi.load()` | Returns nil | Needs dlopen |
| `ffi.gc()` | No-op | Needs GC integration |
| `ffi.errno()` | Returns 0 | No-op |
| **Dangerous (no-ops):** | | |
| `ffi.patch()` | No-op | Needs JIT memory |
| `ffi.seal()` | No-op | Needs mprotect |
| `ffi.unseal()` | No-op | Needs mprotect |
| `ffi.search()` | Returns nil | Needs memory |
| `ffi.addr(ptr, off)` | **Real** — ptr + off | Pure math |
| `ffi.copy(dst, src, n)` | No-op | Needs memory |
| `ffi.fill(dst, n, val)` | No-op | Needs memory |
| `ffi.alloc(size)` | Returns nil | Needs allocation |
| `ffi.free(ptr)` | No-op | Needs free |

---

## 4. Benefits of the Revised Plan

| Metric | Before | After |
|--------|--------|-------|
| FFI in OSS | ✅ `sre_ffi.c` fully open | ❌ Only stubs in OSS |
| FFI quality | Hand-rolled, ~85% coverage | libffi, 100% coverage |
| Struct support | ❌ | ✅ |
| Advanced/risky FFI | ❌ | ✅ cast, new, cdef, load, patch, search, etc. |
| x86_64 support | ❌ (ARM64 only) | ✅ (libffi) |
| Code in SRE core | ~450 lines of FFI | ~150 lines of stubs |
| Extras files count | 1 FFI file (ffi.c) | 2 FFI files (sre_ffi.c + raijin_ffi.c) |
| Mod breaking changes | N/A | None (same Lua API) |

### Why Two Files (Not Merged)

| Reason | Explanation |
|--------|-------------|
| **Separation of concerns** | `sre_ffi.c` = generic FFI (libffi-backed, LuaJIT-style API). `raijin_ffi.c` = Raijin's specific signature system + MemoryAddress. |
| **Independent development** | Core FFI evolves independently from Raijin's API. Bug fixes in one don't touch the other. |
| **Clear dependency** | `raijin_ffi.c` depends on `sre_ffi.c`. Not the other way around. Easy to understand. |
| **Testing** | Can test `sre_ffi.c` in isolation (basic ffi.call without signature parsing). |
| **Future reuse** | If another modding API is added, it can use `sre_ffi.c` without going through Raijin's signature layer. |

### Why Advanced/Risky FFI

| Feature | Purpose | Risk Level |
|---------|---------|------------|
| `ffi.cast()` | Type casting between number/ptr/struct | Low |
| `ffi.new()` | Allocate C structs from Lua | Medium |
| `ffi.sizeof()` | Query type sizes | Low |
| `ffi.cdef()` | Parse C declarations in Lua | Medium |
| `ffi.metatype()` | Attach methods to C types | Medium |
| `ffi.load()` | dlopen shared libraries | High (loads arbitrary .so) |
| `ffi.patch()` | Write instruction bytes (hook/jump) | **Very High** |
| `ffi.seal()` | Mark memory as execute | **Very High** |
| `ffi.search()` | Byte pattern search in memory | Medium |
| `ffi.dump()` | Hex dump memory region | Low (debug) |
| `ffi.errno()` | Get errno after FFI call | Low |
| `ffi.gc()` | Attach GC finalizer to pointer | Medium |

**Risk mitigation:**
- `ffi.risky_mode(true)` must be called before `patch`, `seal`, `unseal` work
- All functions validate pointers and log errors on invalid addresses
- `ffi.load()` only loads from whitelisted paths (configurable)
- `ffi.patch()` requires explicit address range confirmation

---

## 5. Risks and Considerations

### 5.1 Mod Compatibility

**No breaking changes.** The Lua API surface is identical:
- `ffi.call()` — same name, same args, same returns
- `ffi.peek*()` / `ffi.poke*()` — same
- `ffi.offset()` / `ffi.deref()` / `ffi.base()` / `ffi.at()` — same

The only difference: without extras, `ffi.call()` returns nil instead of dispatching.

### 5.2 Pure-Math Functions in Stubs

`ffi.offset()`, `ffi.null()`, `ffi.base()`, `ffi.at()` are pure math — no libffi needed.
`ffi.deref()` reads arbitrary memory — it's safe in the SRE sandbox (guest process).

**Decision: keep them real in stubs** — useful for basic pointer chasing even without full FFI.

### 5.3 Extras Registration Order

When extras loads, it must register `_G.ffi` **after** SRE core's stubs are registered:
1. SRE core calls `sre_extras_stubs.c` → registers stub `_G.ffi`
2. SRE core loads `libsre-extras.so` → extras init calls `sre_ffi_register_lua(L)` → overwrites `_G.ffi` with real table
3. Extras init calls `raijin_ffi_register_lua(L)` → extends `_G.ffi` with Raijin functions + MemoryAddress

This already works for MemoryAddress — same pattern applies.

### 5.4 Extras Keeps Its Own libffi Copy

No inter-SO dependency. Extras links `libffi/*.c + sysv.S` directly into `libsre-extras.so`. SRE core has zero libffi symbols.

### 5.5 Advanced FFI Requires Opt-In

Risky functions (`patch`, `seal`, `unseal`, `load`) require:
```lua
ffi.risky_mode(true)  -- must be called first
ffi.patch(addr, bytes) -- now works
```
Without opt-in, these return nil/no-op. Prevents accidental use.

---

## 6. Implementation Steps (Future, Not Now)

### Phase 1: Move SRE FFI to Extras
1. **Copy `src/sre/sre_ffi.c` → `src/sre-extras-closed-source/sre_ffi.c`**
2. **Upgrade `sre_ffi.c` in extras:**
   - Replace hand-rolled dispatch with `ffi_prep_cif()` + `ffi_call()`
   - Add `#include <ffi.h>` (from `libffi/`)
   - Remove all `pfn_i0..pfn_i8` typedefs
   - Use `ffi_type` mapping (SreFfiType → `ffi_type*`)
   - Keep all existing Lua API functions
3. **Delete `src/sre/sre_ffi.c`** from SRE core
4. **Update `cmake/components/sre.cmake`:**
   - Remove `sre_ffi` from `SRE_CORE_SRCS`
5. **Update extras `CMakeLists.txt`:**
   - Add `sre_ffi.c` to `EXTRA_SOURCES`

### Phase 2: Rename Raijin FFI
6. **Rename `src/sre-extras-closed-source/ffi.c` → `raijin_ffi.c`**
7. **Update extras `CMakeLists.txt`:**
   - Replace `ffi.c` with `raijin_ffi.c` in source list
8. **Adjust includes if needed** (may reference `sre_ffi.h` from the new core file)

### Phase 3: Add Advanced FFI
9. **Add new functions to `sre_ffi.c` in extras:**
   - `ffi.cast()`, `ffi.new()`, `ffi.sizeof()`, `ffi.cdef()`
   - `ffi.metatype()`, `ffi.load()`, `ffi.addr()`, `ffi.read()`, `ffi.write()`
   - `ffi.copy()`, `ffi.fill()`, `ffi.gc()`, `ffi.errno()`, `ffi.abi()`
   - `ffi.patch()`, `ffi.seal()`, `ffi.unseal()`, `ffi.search()`, `ffi.dump()`
   - `ffi.risky_mode()` gate for dangerous functions
10. **Type system:** Basic `ffi.typeof()` / `ffi.istype()` for known types (i32, f32, ptr, Vector3, etc.)

### Phase 4: Update Stubs
11. **Update `src/sre/sre_extras_stubs.c`:**
    - Add `_G.ffi` stub table with ALL functions
    - Pure-math functions work for real
    - Everything else is safe stub
12. **Remove old `_G.ffi` registration from `sre_ffi.c`** (now in extras)

### Phase 5: Test
13. **Without extras:** `ffi.call()` → nil, `ffi.base()` → works, `ffi.peek32()` → nil
14. **With extras:** everything works as before + advanced functions available

---

## 7. Files Involved

| File | Location | Action |
|------|----------|--------|
| `sre_ffi.c` | `src/sre/` | **DELETE** from SRE core |
| `sre_ffi.c` | `src/sre-extras-closed-source/` | **CREATE** — upgraded copy from core, libffi-backed |
| `ffi.c` | `src/sre-extras-closed-source/` | **RENAME** to `raijin_ffi.c` |
| `libffi/` | `src/sre-extras-closed-source/libffi/` | **KEEP** (unchanged) |
| `sre_extras_stubs.c` | `src/sre/` | **EDIT** — add `_G.ffi` stub table |
| `CMakeLists.txt` | `src/sre-extras-closed-source/` | **EDIT** — rename ffi.c → raijin_ffi.c, add sre_ffi.c |
| `sre.cmake` | `cmake/components/` | **EDIT** — remove `sre_ffi` from core sources |
| `docs/research_ffi_infrastructure_upgrade.md` | `docs/` | **This file** |
