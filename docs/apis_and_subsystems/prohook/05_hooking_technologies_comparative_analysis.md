# Comparative Analysis: SRE Hooking Technologies

> **Document Path**: `docs/prohook/05_hooking_technologies_comparative_analysis.md`
> **Evaluated Technologies**:
> 1. **Current SRE Offset-Based Hooking** (Vtable / Function Pointer Swap)
> 2. **Inline Assembly Trampoline Hooking** (GlossHook / Opcode Patching)
> 3. **Next-Gen SRE Host Execution Platform** (`libsrehost.so` + Dynarmic Translation Hooks + `SVC` Gateway)

---

## 1. High-Level Comparison Matrix

| Evaluation Dimension | 1. Current SRE Offset Hooking | 2. Inline Trampoline Hooking (GlossHook) | 3. Next-Gen Host Execution Platform (`libsrehost.so`) |
| :--- | :--- | :--- | :--- |
| **Raw Hooking Power** | ⭐⭐ (Limited to vtables & function pointers) | ⭐⭐⭐⭐ (Can patch any function head or `BL` call site) | ⭐⭐⭐⭐⭐ (Full control over JIT translation, execution flow, & IR) |
| **Flexibility & API Surface** | ⭐⭐ (Hardcoded offsets in C) | ⭐⭐⭐⭐ (Dynamic handles `GHook`, multi-hook chaining) | ⭐⭐⭐⭐⭐ (Unified 4-backend manager, translation hooks, `SVC` host ABI) |
| **Inspection Depth & Real-time State** | ⭐ (Only function arguments via C call) | ⭐⭐⭐⭐ (Access to GPRs `X0-X30` and SIMD `Q0-Q31`) | ⭐⭐⭐⭐⭐ (Complete host JIT state, guest registers, JIT IR blocks, & execution ticks) |
| **Memory Safety & Crash Risk** | ⭐⭐⭐⭐⭐ (Zero memory patching; 100% safe) | ⭐⭐ (Risks smashing small functions, vtables, or data constants) | ⭐⭐⭐⭐⭐ (100% Zero-Patch in RAM; guest memory remains untampered) |
| **Short Function & Small Block Support** | ❌ Cannot hook non-vtable short functions | ⚠️ Requires 4-byte short hook fallback or proximity trampolines | ✅ 100% supported (Hooks single instructions or 4-byte functions effortlessly) |
| **Execution Speed & Overhead** | 🚀 Native speed (0 ns overhead) | 🚀 Native speed (~2-4 ns jump overhead) | 🚀 Ultra-Fast JIT emission (~0-12 ns host context switch via `SVC`) |
| **JIT Cache Coherency** | ⚠️ Manual cache flush required | ⚠️ Manual `InvalidateCacheRange` required | ✅ Automatic JIT cache invalidation & block cache coherency |
| **Debugging & Profiling Depth** | ❌ None | ❌ Limited | ✅ Real-time JIT instruction tracing, profiling counters, & single-stepping |
| **Host Plugin & Mod Extensibility** | ❌ Guest ARM64 only | ⚠️ Limited to C proxy callbacks | ✅ Full x86_64 Host Plugin Architecture (`libsrehost.so`) |

---

## 2. Detailed Subsystem Breakdown

### 1. Current SRE Offset-Based Hooking
- **Mechanism**: Swaps function pointers in C++ virtual tables or global function pointer tables.
- **Strengths**: Zero risk of memory corruption; simple to write in C (`libsre.so`).
- **Weaknesses**: Cannot hook non-virtual functions, internal static helpers, or single call sites (`BL`).

### 2. Inline Trampoline Hooking (GlossHook)
- **Mechanism**: Writes 16-byte (`LDR X16; BR X16`) or 4-byte (`B <offset>`) opcodes directly into guest RAM code sections.
- **Strengths**: Can hook non-virtual C++ functions and single `BL` call sites; exposes full register state (`gloss_reg`).
- **Weaknesses**: Risk of overwriting small functions; requires manual mprotect and manual Dynarmic cache invalidation.

### 3. Next-Gen Host Execution Platform (`libsrehost.so` + Dynarmic Translation Hooks)
- **Mechanism**: Intercepts basic block compilation inside Dynarmic (`TranslateBlock`) and routes execution via an `SVC #0x5352` host ABI.
- **Strengths**:
  1. **Zero Memory Corruption**: Guest RAM remains untouched.
  2. **Unlimited Inspection Depth**: Inspect/mutate IR blocks, GPRs `X0-X30`, `SP`, `PC`, and NEON `Q0-Q31`.
  3. **Multi-Backend**: Supports Offset, Inline Trampoline, Translation, and IR Instrumentation hooks in a single API.
  4. **Host Plugin System**: Third-party x86_64 host plugins can extend SRE features without recompiling guest binaries.
