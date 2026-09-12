# GlossHook Architecture & Technical Analysis Report

> **Document Path**: `docs/inline-trampline-for-sre/01_glosshook_architecture_and_deep_code_analysis.md`
> **Reference Source**: `/home/quantumcreeper/SwordigoDesktop/GlossHook-main/GlossHook/`
> **Target Platform**: SRE (Swordigo Runtime Engine) / ARM64 PC Port Architecture

---

## 1. Executive Summary

GlossHook is a high-performance Android Native Hooking library designed for ARM32 (Thumb/ARM) and ARM64 architectures. It powers modding frameworks such as **AndroidModLoader (AML)** across large-scale C++ game codebases.

This document presents a comprehensive reverse-engineering and architectural decomposition of GlossHook's source code, examining its internal primitives, memory manipulation routines, instruction relocation strategies, and multi-hook cascading pipeline.

---

## 2. Core Data Structures & Interfaces

### 2.1 Context Register State (`gloss_reg`)
GlossHook defines an architectural register context mirror used during mid-function internal callbacks (`GlossHookInternal`):

```cpp
typedef struct GlossRegister {
#ifdef __aarch64__
    enum e_reg {
        X0 = 0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13, X14, X15, 
        X16, X17, X18, X19, X20, X21, X22, X23, X24, X25, X26, X27, X28, X29, FP = X29,
        X30, LR = X30, X31, SP = X31, PC, CPSR, MAX_REG,
        Q0 = X0, Q1 = X1, ..., Q31 = X31, MAX_Q_REG
    };
    union {
        struct {
            __uint128_t q[MAX_Q_REG];  /* 32 x 128-bit NEON/FP registers */
            uint64_t x[MAX_REG];        /* 31 x 64-bit General Purpose Registers */
        } r;
        struct {
            __qreg q0, q1, ..., q31;
            __xreg x0, x1, ..., x29;
            __xreg lr, sp; uint64_t pc, cpsr;
        } regs;
    };
#endif
} gloss_reg;
```

**Key Architectural Notes**:
- `X18` is designated by GlossHook as an internal scratch jump register for inline trampolines.
- Full 128-bit NEON/SIMD vector register state (`Q0-Q31`) is preserved across callbacks to prevent floating-point register corruption during game physics and matrix transforms.

---

## 3. Hooking Primitives & Implementation Mechanisms

### 3.1 Standard Function Head Hooking (`GlossHook` / `GlossHookAddr`)
- **Default Trampoline Layout (ARM64)**:
  - Overwrites the target function entry point with an absolute 64-bit indirect branch:
    ```assembly
    LDR X16, #8       ; Load 64-bit destination address into X16
    BR  X16           ; Jump to destination
    .quad <NEW_FUNC>  ; 64-bit target address
    ```
  - Total sequence size: **16 bytes**.

### 3.2 Short 4-Byte Hooking (`is_4_byte_hook = true`)
- **Problem**: Small C++ functions or closely packed vtables may contain fewer than 16 bytes before neighboring function entry points.
- **GlossHook Solution**:
  - Overwrites only the first 32-bit instruction with a relative branch instruction `B <TRAMPOLINE_OFFSET>` (jump range $\pm 128\text{ MB}$).
  - Allocates a nearby executable trampoline stub within range that performs the full 64-bit jump to `new_func`.

### 3.3 Single Call-Site Hooking (`GlossHookBranchBL` / `GlossHookBranchBLX`)
- **Use Case**: Modifying behavior at a single call site inside a massive function without altering all callers across the codebase.
- **Mechanism**:
  - Locates the specific `BL <OFFSET>` instruction (32-bit ARM64 opcode `0x94000000 | imm26`).
  - Rewrites the `BL` opcode to point to a trampoline stub that saves `LR` (link register), executes the proxy hook function, and returns cleanly to `LR`.

### 3.4 Arbitrary Mid-Function Interception (`GlossHookInternal`)
- **Mechanism**:
  - Overwrites instructions at any arbitrary offset inside a function with a jump to a register-saving assembly stub.
  - The assembly stub pushes `X0-X30`, `SP`, `CPSR`, and `Q0-Q31` onto the stack, constructs a `gloss_reg` pointer, and calls `GlossHookInternalCallback(gloss_reg* regs, GHook hook)`.
  - Upon return, modified registers are restored into hardware state, allowing real-time register mutation and stack patching.

---

## 4. Multi-Hook Cascading & Chaining

GlossHook supports multiple independent hooks attached to the exact same memory location:

```mermaid
graph LR
    TargetFunction["Target Function Entry Point"] --> Hook1["Proxy Hook 1 (my_test1)"]
    Hook1 -->|"old_test1()"| Hook2["Proxy Hook 2 (my_test2)"]
    Hook2 -->|"old_test2()"| Hook3["Proxy Hook 3 (my_test3)"]
    Hook3 -->|"old_test3()"| OriginalCode["Original Relocated Instructions + Branch Back"]
```

- Each `GHook` handle represents a node in a doubly-linked list.
- `GlossHookGetOriglFunc(handle)` bypasses all proxy hooks in the chain and invokes the un-hooked original code directly.
- Individual hooks can be dynamically enabled (`GlossHookEnable`), disabled (`GlossHookDisable`), or deleted (`GlossHookDelete`) without corrupting neighboring hooks at the same location.

---

## 5. Memory Safety & Re-entrancy Protection

### 5.1 Re-entrancy Risk
If a proxy hook function directly or indirectly calls the target function again (e.g., hooking `malloc` and calling a function inside the hook that invokes `malloc`), an infinite recursion stack overflow crash occurs.

### 5.2 GlossHook Thread-Local Storage Guard
GlossHook provides POSIX TLS-based re-entrancy prevention:
```cpp
static pthread_key_t in_hook_key;

void* my_malloc(size_t size) {
    void* flag = pthread_getspecific(in_hook_key);
    if (!flag) {
        pthread_setspecific(in_hook_key, (void*)1);
        /* Execute custom logic safely without infinite loop */
        pthread_setspecific(in_hook_key, nullptr);
    }
    return old_malloc(size); /* Fallback to un-hooked execution path */
}
```

---

## 6. Summary of Architectural Strengths for SRE Porting

1. **Extensible API Surface**: Unified handle-based lifecycle (`GHook`, `Enable`, `Disable`, `Delete`).
2. **Minimal Code Footprint**: Supports 4-byte short function patching.
3. **Arbitrary Mid-Function Inspection**: Direct register state access via `gloss_reg`.
4. **Site-Specific Hooking**: `BL`/`BLX` branch site patching.
