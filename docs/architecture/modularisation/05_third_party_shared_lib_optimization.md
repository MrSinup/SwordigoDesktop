# Third-Party Shared Library Optimization (Dynarmic & FFmpeg)

## Executive Summary
Beyond extracting internal C++ codebase subsystems into shared libraries, a significant portion of binary bloat in `swordigo_boot` (~50+ MB of code and debug symbols) stems from **static linking** of large third-party C/C++ libraries:
1. **Dynarmic JIT Compiler** (`libdynarmic.a`, `libmcl.a`, `libfmt.a`, `libZydis.a`, `libZycore.a`) (~35 MB static weight).
2. **FFmpeg Video/Audio Decoder Engine** (`libavcodec.a`, `libavformat.a`, `libswscale.a`, `libavutil.a`) (~12 MB static weight).

Converting these libraries from static archive (`.a`) compilation to **dynamic shared ELF objects** (`libdynarmic.so`, `libswd_ffmpeg.so` / `libav*.so`) will reduce the `swordigo_boot` binary footprint from ~80 MB down to under ~10 MB, while speeding up link times by 5x-10x.

---

## 1. Dynarmic JIT Dynamic Shared Library Plan (`libdynarmic.so`)

### Current State (Static Linking)
In `Makefile`:
```makefile
LIBS += -L$(DYNARMIC_BUILD)/src/dynarmic -ldynarmic \
        -L$(DYNARMIC_BUILD)/externals/mcl/src -lmcl \
        -L$(DYNARMIC_BUILD)/externals/fmt -lfmt \
        -L$(DYNARMIC_BUILD)/externals/zydis -lZydis \
        -L$(DYNARMIC_BUILD)/externals/zydis/zycore -lZycore
```
Dynarmic is compiled via CMake with default static library targets (`BUILD_SHARED_LIBS=OFF`). All x86_64 JIT emitter routines, Zydis disassembler tables, and fmt string formatting structures are embedded directly into every target executable.

### Shared Object Re-Architecture (`BUILD_SHARED_LIBS=ON`)
- Update `dynarmic-build` target in `Makefile` to pass `-DBUILD_SHARED_LIBS=ON` to CMake:
  ```makefile
  dynarmic-build:
      @mkdir -p $(DYNARMIC_BUILD)
      @cd $(DYNARMIC_BUILD) && cmake .. \
          -DCMAKE_BUILD_TYPE=Release \
          -DDYNARMIC_TESTS=OFF \
          -DDYNARMIC_FRONTENDS=A64 \
          -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
          -DBUILD_SHARED_LIBS=ON \
          -DDYNARMIC_IGNORE_ASSERTS=ON \
          -DDYNARMIC_WARNINGS_AS_ERRORS=OFF
      @cd $(DYNARMIC_BUILD) && make -j$$(nproc) dynarmic
  ```
- Output shared libraries (`libdynarmic.so`, `libmcl.so`, `libfmt.so`, `libZydis.so`, `libZycore.so`) will be output to `lib/` or linked dynamically at runtime via `RPATH`.

---

## 2. FFmpeg Dynamic Shared Library Plan (`libswd_ffmpeg.so`)

### Current State (Static Linking)
In `Makefile`:
```makefile
ffmpeg-build:
    @cd $(FFMPEG_DIR) && ./configure \
        --prefix=build \
        --enable-static \
        --disable-shared \
        ...
```
Currently, FFmpeg is configured with `--enable-static --disable-shared`, producing `.a` static libraries that are linked directly into `swordigo_boot`.

### Shared Object Re-Architecture (`--enable-shared`)
- Update `ffmpeg-build` target in `Makefile` to generate dynamic shared libraries:
  ```makefile
  ffmpeg-build:
      @cd $(FFMPEG_DIR) && ./configure \
          --prefix=build \
          --enable-shared \
          --disable-static \
          --enable-pic \
          --enable-avformat \
          --enable-avcodec \
          --enable-swscale \
          --enable-decoder=h264 \
          --enable-demuxer=mov \
          --enable-parser=h264 \
          --enable-protocols \
          --enable-protocol=file \
          --disable-programs \
          --disable-doc \
          --disable-avdevice \
          --disable-avfilter \
          --disable-swresample
  ```
- Alternatively, bundle the FFmpeg object code into a consolidated namespaced shared object: `lib/libswd_ffmpeg.so`.

---

## 3. Projected Binary Footprint Reduction

| Target Component | Legacy Build Mode | Modular Build Mode | Target Size Impact |
| :--- | :--- | :--- | :--- |
| **`swordigo_boot` Executable** | Monolithic (80 MB) | Strip & Shared Links | **~6.5 MB** (-92%) |
| **`ruby` Executable** | Monolithic (33 MB) | Shared Links | **~3.2 MB** (-90%) |
| **Dynarmic JIT** | Static Archive (`.a`) | `lib/libdynarmic.so` | ~18.5 MB (shared across processes) |
| **FFmpeg Engine** | Static Archive (`.a`) | `lib/libswd_ffmpeg.so` | ~4.2 MB (shared across processes) |
| **Internal Subsystems** | Static Objects | `libswd_*.so`, `libopensw_*.so` | ~12.0 MB (shared across processes) |
| **Total Repo / Install Footprint** | ~113 MB | Modular Shared Layout | **~44.4 MB** (-61% total disk usage) |
