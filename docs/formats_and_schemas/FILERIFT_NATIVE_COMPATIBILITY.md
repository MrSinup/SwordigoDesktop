# Native FileRift Compatibility Report

Date: 2026-08-08

Reference implementation: `FileRift 5.8.5` at
`/run/media/quantumcreeper/TVPG/Prenxy Packages/SwordigoTools/FileRift5.8.5`

Native implementation: `src/tools/filerift.cpp`, integrated into Ruby and
Swordfare through `libfilerift`.

## Scope

This report compares the native C++ backend used by SwordigoDesktop with the
FileRift 5.8.5 source. No Python module or Python subprocess is used by the
native implementation. Embedded Lua 5.1 performs Lua parsing and bytecode
generation in process.

## Compatibility Matrix

| Capability | Native status | Evidence / notes |
| --- | --- | --- |
| `.fr` generic root | Supported | Explicit `All` root mapping; no ambiguous cross-message fallback. |
| `.scene` | Supported | Scene root schema, Ruby preview/edit/save integration, smoke round trip. |
| `.scl` | Supported | ObjectLibrary root schema and Ruby/Boulder integration. |
| `.gdata` | Supported | GameData root mapping. |
| `.gopt` | Supported | GameOptions root mapping. |
| `.gplayer` | Supported | PlayerProfile root mapping. |
| `.gstate` | Supported | GameState root mapping. |
| `.scmap` | Supported | Map root mapping. |
| `.sounds` | Supported | SoundLibrary root mapping. |
| `.fnt` | Supported | Font root mapping. |
| `.atlas` | Supported | Texture root mapping. |
| Protobuf varint/fixed32/fixed64/length fields | Supported | Native `proto::Reader`/`proto::Writer`; tested nested scene fields. |
| Unknown protobuf fields | Supported, lossless | Decoded as `Tag_<number>_<wire-type>` and recoded with the original wire type. |
| Single/double quoted strings | Supported | Escape-aware lexer and byte escaping (`\\xNN`, quotes, slash, CR/LF/tab). |
| Nested messages | Supported | Schema-checked recursive parsing with missing-brace errors. |
| FileRift separators | Supported | `:`, `=`, comma, semicolon, and whitespace accepted. |
| FileRift comments | Supported | `#`, `--`, and `//`; single markers persist through fields 513/`0x100a`, doubled markers do not persist. |
| Degree suffix (`90d`) | Supported, lossless | Converts to radians and preserves source notation through field 515/`0x101a`. |
| Lua `$ ... $end` source chunks | Supported | Exact multiline source retained. |
| `@compile` / `@comp` | Supported | Embedded Lua 5.1 compiles the preceding source; field 514/`0x1010` preserves the trigger. |
| `@line` | Accepted | Native library ignores this diagnostic-only trigger because it has no CLI output stream. |
| `@stop` | Supported | Stops native recoding at the trigger. |
| `@halt`, `@print(...)` | Not integrated | Interactive/debug-only reference behaviors; unsuitable for an in-process GUI library. |
| Snake-case input tags | Not supported | Native editor and decoder use canonical Swordigo tag names. |
| Snake-case decode style | Not supported | Presentation-only FileRift CLI option. |
| Decode style profiles | Not supported | Ruby styling belongs to Styx rather than FileRift `config.py`. |
| Templates (`$obj[...]`) | Not integrated | Reference templates require a configured templates directory and expansion context; native Ruby currently edits expanded markup. |
| Manifest/checksum skip | Not integrated | Ruby saves one selected asset transactionally; batch manifest behavior is a separate workflow. |
| stdin/stdout CLI | Supported | `ruby_cli --decode-stdin` / `--recode-stdin` read markup/binary from stdin and emit to stdout. |
| File scanning (`de_in`, `re_in`) | Supported | `ruby_cli --decode` / `--recode` / `--both` scan `de_in`/`re_in` below the working dir. |
| APK extract | Supported | `ruby_cli apk extract` unzips byte-identical to system `unzip` (verified 1433-file APK). |
| APK build | Supported | `ruby_cli apk build <project.frproject>` parses base/out/add/recode sections, recodes and injects assets; embedded output is byte-identical to the reference build. |
| APK sign | Supported | `ruby_cli apk sign` invokes the UberAPKSigner `apksigner.jar` via `java`. |
| Headless batch texture conversion | Supported | `ruby_cli batch [--import] <src> <dst>` drives the native `batch_converter` (ETC1/PVRTC/RGBA8888), no SDL UI. |
| Tag/template information CLI | Not integrated | Schemas are native but no public schema-query UI/API exists yet. |

## Ruby And Editor Changes

- Ruby now recognizes `.scene` as an editable protobuf type in the common text
  editor path.
- Binary files are not opened with truncation until markup compilation has
  succeeded. Invalid markup therefore cannot erase the selected asset.
- Failed writes and partial stream writes are surfaced as compile failures.
- FileRift exceptions include the source line and no longer silently discard
  unknown or invalid tags.
- Compile status, timing, and errors are passed to the unified editor pane.
- The `filerift`, `swpod`, `swfmt`, and OpenGL dependencies are explicit in
  CMake, allowing native test executables to link outside Ruby's incidental
  all-components link set.

## Styx Changes

- Embedded Grove and BatSyntax definitions remain available and are parsed by
  the same path as imported `.styx` files.
- Style replacement clears token caches and default-style loading fully resets
  prior styles, keywords, extensions, and patterns.
- Invalid style documents now fail validation unless they define a name,
  extension list, and styles.
- Escaped pattern strings use escape-aware quote matching in both file and
  memory loaders.
- Syntax checking uses the active Styx line and block comment delimiters.
- Quote state handles odd/even backslash parity, preventing escaped quotes from
  corrupting bracket diagnostics.

## Verification

Commands executed:

```sh
cmake -S . -B build-cmake
cmake --build build-cmake --target filerift_smoke styx_smoke ruby -j2
ctest --test-dir build-cmake --output-on-failure -R 'filerift_smoke|styx_smoke'
```

Result: Ruby built successfully; 2 of 2 native tests passed.

`filerift_smoke` proves nested scene encode/decode, escaped strings, comments,
degrees, unknown wire fields, exact binary round trips, embedded Lua bytecode,
malformed-input rejection, and String-chunk decode/recode fixed-point stability
(each cycle reproduces the identical binary and markup). `styx_smoke` proves
embedded style loading, invalid-style rejection, configured comment handling,
escaped-quote syntax checking, and clean default-style reset.

## Native CLI (ruby_cli)

`src/tools/ruby_cli.cpp` builds `bin/ruby_cli` (Makefile and CMake targets).
Modes:

- `--decode` / `--recode` / `--both`: scan `de_in`/`re_in` under the working
  dir; decode output keeps the original filename and gains the
  `## FileRift decoded Swordigo file type: <type>` header.
- `--decode-stdin` / `--recode-stdin [-t <type>]`: single-file pipe mode.
- `apk extract <apk> <dest>`: unzips all entries (no executable bit preserved,
  matching zipfile default), verified byte-identical against system `unzip`.
- `apk build <project.frproject> [--apksigner PATH]`: reads
  `base`/`out`/`add { }`/`recode { }`/`sign` sections. Relative paths resolve
  under the working dir: base APK under `projects/apks`, add sources under
  `source`, recode sources under `re_in`. Assets land in `assets/resources/`
  unless the target starts with `/`. Recode supports the `@compile` Lua trigger.
- `apk sign <apk> --apksigner PATH`: runs the UberAPKSigner jar.
- `batch [--import] <src> <dst>`: headless texture conversion.

Project-file parsing matches the reference `build.py` regex semantics,
including `#`/`--`/`//` comment stripping, `>` rename targets, wildcard/dir
expansion, and `sign "true"|"false"`. Note the reference's `build_apk` calls
`recode.recode_start`, which is never defined in FileRift 5.8.5, so any
reference build with a recode section raises `AttributeError`; the native CLI
does not share this bug.

## Remaining Parity Work

The native backend now covers FileRift's core decoder/recoder behavior used by
Ruby and Swordfare, plus a standalone CLI with directory scanning, stdin pipes,
APK extract/sign/build, and headless batch texture conversion. Templates,
schema-information queries, directory manifests, and batch CLI style options
remain separate, explicitly unported workflows. They should only be added
behind native APIs with configured paths and test fixtures rather than by
invoking the Python reference implementation.
