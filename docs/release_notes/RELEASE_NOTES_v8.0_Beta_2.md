# ⚔️ Swordigo Desktop v8.0 Beta 2 Release Notes

Welcome to the **v8.0 Beta 2** release of Swordigo Desktop! Following the groundbreaking release of v8.0 Beta 1, **v8.0 Beta 2** introduces massive advancements across integrated development tools, networking infrastructure, modular engine architecture, OptiX graphics research, and ARM64 JIT stability.

---

## 🌟 Major Highlights

### 1. 🌐 RakNet Networking Engine Foundation
- **RakNet Framework Integration**: Integrated the high-performance RakNet cross-platform UDP networking engine into the core build tree.
- **Network Protocol Architecture**: Built low-level packet serialization routines, GUID session handshake protocols, and peer connection state management, laying the foundation for future multiplayer and world synchronization features.

---

### 2. 💻 Swordfare Editor (IntelliJ) Engine Architecture
The built-in code editor (`src/tools/intellij.cpp`) has been completely overhauled into a powerful, domain-specific IDE engine for Swordigo modding:
- **Stateful Multiline Tokenization (`LexState`)**: Introduced state propagation across line boundaries (`NORMAL`, `IN_BLOCK_COMMENT`, `IN_MULTILINE_STRING`). Multiline block comments (`--[[ ... ]]`) now maintain accurate comment syntax styling across every line.
- **Embedded .styx Theme Engine**: Enhanced the custom relaxed JSON parser with string-quote-aware comment stripping, ensuring theme stylesheets load seamlessly without corrupting string properties.
- **Context-Aware Bracket Error Checking**: The bracket mismatch engine (`check_syntax_errors`) is now fully string- and comment-aware, preventing false error alerts when brackets appear inside quotes or comments.
- **Line-Level Token Caching**: Optimized per-frame rendering by caching tokenized lines by text hash (`token_cache_`), avoiding unnecessary re-execution of regex matchers on visible lines.
- **Swordigo SDK Autocomplete & Symbol Metadata**: Integrated instant auto-completion and documentation tooltips for core Swordigo engine APIs including `Game`, `Character`, `Scene`, `PhysicsObject`, `Vector3`, and `Shardshi`.

---

### 3. 📦 Modular Shared Object Build Architecture
- **Split `.so` Engine Binaries**: Re-architected monolithic engine binaries into 9 modular shared objects (`libswcore.so`, `libswemu.so`, `libswgfx.so`, `libswgui.so`, `libfilerift.so`, `libswordfare.so`, `libswfmt.so`, `libsre.so`, `libswordigo.so`).
- **Binary Directory Restructuring**: Transitioned output executables to `bin/` with `swordfare` as the primary executable and unified packaging support in `builder/package.sh`.

---

### 4. ⚡ OptiX Architectural Framework & Research Integration
- **OptiX Technical Blueprint**: Completed 10 master architectural specifications (`docs/optiX/`) detailing PBR material pipelines, dynamic light reflection hooks, and guest runtime acceleration.
- **Dynarmic ARM64 JIT Tuning**: Expanded JIT code cache allocation to **512 MB** and enabled host x86_64 hardware acceleration flags (`Unsafe_UnfuseFMA`, `Unsafe_ReducedErrorFP`, `Unsafe_InaccurateNaN`, `Unsafe_IgnoreStandardFPCRValue`).
- **Native Host-to-Guest Bridge**: Refined guest stack recovery mechanisms and signal handling to isolate guest context faults, ensuring maximum host application uptime.

---

### 5. 🛡️ Bug Fixes & Stability Improvements

- **Regex Engine Stability**: Resolved `std::regex_error` exceptions in `FileRift (Grove)` theme by converting lookbehind rules to standard capture groups compatible with C++ `std::regex`.
- **Keyword Escape Protection**: Added automatic regex character escaping in `compile_keywords()`, preventing metacharacter collisions during keyword group matching.
- **Modern Memory Allocation**: Replaced non-standard const-pointer coercion with C++17 `buffer->data()` in text buffer interfaces.
- **Packaging Manifests**: Fixed RPM `%files` manifest to register `/usr/bin/ruby` and modular shared libraries.

---

**Happy Modding & Coding!**  
— *The SRE Team & OpenSwordigo Contributors*
