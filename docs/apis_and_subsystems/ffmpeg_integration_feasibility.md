# Feasibility Report: Local FFmpeg Source Integration & Static Build

## 1. Goal
To bundle the FFmpeg source tree directly in the repository under [src/tools/ffmpeg](file:///home/quantumcreeper/SwordigoDesktop/src/tools/ffmpeg) and compile it locally as a static library. This:
- Eliminates the need for users to install any external FFmpeg development packages (e.g. `libavcodec-dev`).
- Allows us to write a direct, memory-to-GPU video decoder in `video_background.cpp`, completely eliminating temporary `/tmp` PNG writing.

---

## 2. Directory Tree Analysis: What to Keep vs. Delete

FFmpeg's `./configure` script is highly dynamic. It scans the source files of libraries to discover plugins. We cannot delete files that the configure script actively reads during configuration, even if those components are disabled.

### 🗑️ Directories Safe to Delete
These folders can be completely removed from [src/tools/ffmpeg](file:///home/quantumcreeper/SwordigoDesktop/src/tools/ffmpeg) to save disk space without affecting `./configure`:
- **`fftools/`** (Source of `ffmpeg`, `ffplay`, and `ffprobe` CLI binaries). Since we build with `--disable-programs`, we do not need the command-line executables.
- **`doc/`** (Contains user manuals, HTML/man pages). Safe to delete.
- **`tests/`** (regression tests and mock media files). Safe to delete.
- **`presets/`** (FFmpeg encoder presets). Safe to delete.
- **`tools/`** (Debugging and developer scripts). Safe to delete.

### ⚠️ Directories We MUST Keep (But Not Compile)
These libraries must remain in the source tree because `./configure` parses their files (e.g. `allfilters.c`, `alldevices.c`) at configuration time to generate header dependencies:
- **`libavdevice/`** (Capture devices). Keep the directory, compile disabled via `--disable-avdevice`.
- **`libavfilter/`** (Video/Audio filters). Keep the directory, compile disabled via `--disable-avfilter`.
- **`libswresample/`** (Audio resampler). Keep the directory, compile disabled via `--disable-swresample`.

---

## 3. Minimized `./configure` Profile
To make compiling extremely fast (~15–30 seconds) and the binary tiny, we will run configure with `--disable-everything` and explicitly enable only the absolute minimum components required to play an H.264/MP4 background video:

```bash
./configure \
    --prefix=build \
    --enable-static \
    --disable-shared \
    --disable-all \
    --enable-avformat \
    --enable-avcodec \
    --enable-swscale \
    --enable-decoder=h264 \
    --enable-demuxer=mov \
    --enable-parser=h264 \
    --disable-programs \
    --disable-doc \
    --disable-avdevice \
    --disable-avfilter \
    --disable-swresample \
    --disable-postproc \
    --disable-network \
    --disable-iconv \
    --disable-bzlib \
    --disable-libxcb \
    --disable-lzma \
    --disable-sdl2 \
    --disable-xlib \
    --disable-zlib \
    --disable-securetransport \
    --disable-audiotoolbox \
    --disable-videotoolbox \
    --disable-coreimage \
    --disable-avfoundation
```

### Enabled Libraries After Build:
- `libavformat.a` (handles opening the `.mp4` container and demuxing packets)
- `libavcodec.a` (contains the lightweight H.264 software decoder)
- `libswscale.a` (performs YUV420P to RGB/RGBA pixel color space conversion)
- `libavutil.a` (core utilities and memory buffers)

---

## 4. Integration into Makefile

We can automate the local compilation of FFmpeg by adding a recipe to the root [Makefile](file:///home/quantumcreeper/SwordigoDesktop/Makefile):

```makefile
FFMPEG_DIR   := src/tools/ffmpeg
FFMPEG_BUILD := $(FFMPEG_DIR)/build

# Add local FFmpeg headers to include path
ALL_CXXFLAGS += -I$(FFMPEG_BUILD)/include

# Link against the local static libraries
LIBS += -L$(FFMPEG_BUILD)/lib -lavformat -lavcodec -lswscale -lavutil

# Target to build FFmpeg statically from source
ffmpeg-build:
	@echo "[FFMPEG] Configuring and building local FFmpeg..."
	@mkdir -p $(FFMPEG_BUILD)
	@cd $(FFMPEG_DIR) && ./configure \
		--prefix=build \
		--enable-static \
		--disable-shared \
		--disable-all \
		--enable-avformat \
		--enable-avcodec \
		--enable-swscale \
		--enable-decoder=h264 \
		--enable-demuxer=mov \
		--enable-parser=h264 \
		--disable-programs \
		--disable-doc \
		--disable-avdevice \
		--disable-avfilter \
		--disable-swresample \
		--disable-postproc \
		--disable-network \
		--disable-iconv \
		--disable-bzlib \
		--disable-libxcb \
		--disable-lzma \
		--disable-sdl2 \
		--disable-xlib \
		--disable-zlib
	@cd $(FFMPEG_DIR) && make -j$$(nproc) && make install
	@echo "[FFMPEG] Build successful!"
```

---

## 5. Architectural Improvements in `video_background.cpp`
Once the static libraries are linked, we can replace the legacy PNG-dumping approach with **real-time decoding**:

1. **Direct Stream**: `register_texture_maybe` opens the `.mp4` video stream directly using `avformat_open_input`.
2. **First Frame Sync**: Extract the first frame using `avcodec_receive_frame` immediately on load and upload it to the OpenGL texture. This takes `< 10ms`.
3. **Background Decodes**: Spawn a worker thread that decodes subsequent frames into a ring buffer in memory. No files are written to the disk.
4. **Flawless Playback**: Each update frame, pop a decoded frame from the ring buffer and upload it via `glTexSubImage2D`.

---

## 6. Verification & Steps to Proceed
If this looks good, we can:
1. Delete the unused folders (`fftools`, `doc`, `tests`, `presets`, `tools`) to clean up space.
2. Add the `ffmpeg-build` rules to the [Makefile](file:///home/quantumcreeper/SwordigoDesktop/Makefile).
3. Ask you for permission to compile it.
4. Refactor `video_background.cpp` to use the FFmpeg C APIs.

Let me know if you would like me to proceed with cleaning up the directories and updating the Makefile!
