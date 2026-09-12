# Dynarmic JIT Engine Extensions & Translation-Aware Hooking

> **Document Path**: `docs/prohook/02_dynarmic_jit_modifications_and_translation_hooks.md`
> **Target System**: Dynarmic ARM64 JIT Emulation Subsystem
> **Scope**: Detailed source-level analysis of Dynarmic translation pipeline, cache invalidation, and custom hooks.

---

## 1. Dynarmic Source-Level Translation Pipeline Analysis

In Dynarmic, ARM64 guest instruction translation flows through three main stages:

```mermaid
graph LR
    GuestOpcodes["Guest ARM64 Opcodes (Guest RAM)"] -->|1. Decode| A64Decoder["Dynarmic A64 Decoder"]
    A64Decoder -->|2. Emit IR| IRBlock["Dynarmic Intermediate Representation (IR::Block)"]
    IRBlock -->|3. Host JIT Emit| HostMachineCode["x86_64 / ARM64 Host Machine Code"]
    HostMachineCode --> BlockCache["JIT Code Cache (Fast PC Lookup Table)"]
```

### 1.1 Key Translation Functions in Dynarmic
1. `Dynarmic::A64::TranslateBlock(guest_pc, UserConfig)`:
   Entry point for compiling a basic block starting at `guest_pc`.
2. `Dynarmic::A64::Translator::TranslateSingleInstruction(IR::Block& block, u64 guest_pc)`:
   Fetches 32-bit ARM64 opcode at `guest_pc` and appends corresponding IR instructions to `block`.
3. `Dynarmic::A64::Jit::InvalidateCacheRange(u64 guest_pc, size_t size)`:
   Removes translated basic blocks starting within `[guest_pc, guest_pc + size)` from the JIT cache.

---

## 2. Proposed Dynarmic Engine Modifications

To transform Dynarmic from a passive JIT translator into an active participant in SRE's execution framework, we specify the following extensions to `Dynarmic::A64::UserConfig`:

```cpp
namespace Dynarmic::A64 {

struct UserConfig {
    /* Existing Dynarmic UserConfig fields... */

    /* =========================================================================
     * SRE Execution Manager Extensions
     * ========================================================================= */

    /**
     * Callback triggered before compiling a basic block at guest_pc.
     * Return true to suppress native translation and insert custom IR redirection.
     */
    using PreTranslationHook = std::function<bool(u64 guest_pc, IR::Block& block)>;
    PreTranslationHook pre_translation_hook = nullptr;

    /**
     * Callback triggered after completing IR generation for a basic block.
     * Allows IR instrumentation (profiling counters, instruction tracing).
     */
    using PostTranslationHook = std::function<void(u64 guest_pc, IR::Block& block)>;
    PostTranslationHook post_translation_hook = nullptr;

    /**
     * Callback triggered when guest executes SVC opcode.
     */
    using SyscallHandler = std::function<void(u32 svc_num, VectorArray<u64, 31>& regs)>;
    SyscallHandler syscall_handler = nullptr;
};

} // namespace Dynarmic::A64
```

---

## 3. Translation-Aware Hooking ("Zero-Patch Hooking")

### 3.1 Concept & Superiority over Inline Trampolines
Traditional inline hooking overwrites guest opcodes in RAM with `LDR X16; BR X16` (16 bytes). This risks crashing if small functions or data constants are overwritten.

**Translation-Aware Hooking** does **NOT** modify a single byte of guest ARM64 RAM:

```mermaid
sequenceDocument
    participant GuestPC as Guest PC (0x00348B6C)
    participant JIT as Dynarmic TranslateBlock
    participant ExecMgr as SRE ExecutionManager
    participant HookProxy as Host/Guest Proxy Function

    GuestPC->>JIT: Translate basic block at 0x00348B6C
    JIT->>ExecMgr: PreTranslationHook(0x00348B6C, block)
    ExecMgr-->>JIT: Returns true (Hook Registered for 0x00348B6C!)
    Note over JIT: Dynarmic emits direct JIT call to HookProxy instead of translating original ARM64 instructions!
    JIT->>HookProxy: Host code jumps directly to Proxy Function
```

### 3.2 Benefits of Translation Hooks
1. **Zero Memory Corruption**: Guest RAM remains untouched (`libswordigo.so` retains original opcodes).
2. **Instant Removal & Toggling**: Removing a hook simply calls `InvalidateCacheRange(guest_pc, 4)`. No opcode restoration needed.
3. **Unlimited Hook Target Size**: Works seamlessly even on 4-byte functions or single-instruction blocks!

---

## 4. JIT Cache Invalidation & Coherency Protocol

When `SREHost_InvalidateGuestRange(guest_addr, size)` is called:
```cpp
void ExecutionManager::InvalidateRange(uint64_t guest_addr, size_t size) {
    std::lock_guard<std::mutex> lock(m_jit_mutex);

    /* 1. Flush host instruction cache */
    __builtin___clear_cache((char*)guest_addr, (char*)(guest_addr + size));

    /* 2. Invalidate Dynarmic Basic Block Cache */
    if (m_dynarmic_jit) {
        m_dynarmic_jit->InvalidateCacheRange(guest_addr, size);
    }
}
```

---

## 5. Summary of Required Modifications

| Component | Target File in Dynarmic | Required Extension |
| :--- | :--- | :--- |
| **`UserConfig`** | `dynarmic/include/dynarmic/frontend/A64/user_config.h` | Add `pre_translation_hook`, `post_translation_hook`, `syscall_handler`. |
| **`TranslateBlock`** | `dynarmic/src/dynarmic/frontend/A64/translate.cpp` | Query `pre_translation_hook` before opcode decoding. |
| **`UserException`** | `dynarmic/src/dynarmic/backend/x64/emit_x64.cpp` | Route `SVC #0x5352` to `syscall_handler`. |
