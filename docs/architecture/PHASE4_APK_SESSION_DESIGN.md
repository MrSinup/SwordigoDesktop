# Phase 4 Research — APK Session Workflow (Import → Edit → Export) + Native Signer

**Status:** 4.1a shipped (import/session/registry/roots + shared zip lib);
4.1b (repack/export/cleanup) and 4.1c (native v1 signer) remain. Date: 2026-09.
**Request origin:** "we have an apk backend already in filerift — make it stronger and
link it to the GUI in ruby gg. Input APK → extract assets into `~/.ruby/` → internal
identifier = APK session → 'Export as APK' becomes available → user works → export back
to the APK's origin (or a chosen location) → delete the extracted copy. appsigner.jar
is required today — can we write a full native C app signer with a demo sign? If that is
too complex, drop signing and emit an unsigned APK."

---

## 1. What already exists (verified against the code)

| Capability | Where | Notes |
|---|---|---|
| ZIP read/write (store + deflate, central-directory driven) | `src/tools/ruby_cli.cpp` `namespace zip` (≈ line 230) | Extracts byte-identical to system `unzip` (1433-file APK verified); writes entries incl. CRC-32 + deflate via zlib |
| `apk extract <apk> <dest>` | `ruby_cli.cpp` `apk_extract` | Unzips all entries (no executable bit preserved, matching Python `zipfile` default) |
| `apk build <project.frproject>` | `ruby_cli.cpp` `apk_build` | Reads `base` / `out` / `add { }` / `recode { }` / `sign` sections; assets land under `assets/resources/`; recode supports `@compile` Lua |
| `apk sign <apk> --apksigner PATH` | `ruby_cli.cpp` `apk_sign` | `java -jar apksigner.jar -a <apk>` (UberAPKSigner); output `-aligned-debugSigned.apk` is copied over the target |
| Native protobuf scene/scl decode-recode (FileRift) | `src/tools/filerift.cpp` | The "backend" the user means — scene/scl edits are already fully native |
| Bundle ZIP extractor (stored + deflate) | `src/platform/binary_selector.cpp`, `src/platform/mod_manager.cpp` | Redundant readers — candidates to unify behind one shared zip API |

**Gaps for the GUI session flow:**
1. The zip reader/writer is embedded in the CLI binary (`ruby_cli.cpp`), not a
   library the Qt app can link. → refactor to `src/platform/zip_archive.{h,cpp}`.
2. Signing requires `java` + a downloaded `apksigner.jar` — a hard external dep
   for a desktop tool; no crypto library (OpenSSL/mbedtls) exists in the project
   today, so a native signer means a self-contained SHA-1/SHA-256 + RSA + DER
   implementation (see §3).
3. No session concept: no registry of "this workspace was extracted from an APK",
   no origin path, no export-enabled flag.

## 2. Proposed session flow (the user's design, made concrete)

```
File ▸ Import APK…  (or drag an .apk onto the window)
  │
  ├─ read zip entries; locate assets/ (and assets/resources, assets/background)
  ├─ session-id = <apk-name>-<yyyymmdd-HHMMSS>-<rand4>
  ├─ extract EVERYTHING to  ~/.ruby/apk-sessions/<session-id>/
  │     (assets/ tree is what the scene engine + asset browser read)
  ├─ write  ~/.ruby/apk-sessions/<session-id>/session.json
  │     { "session_id": …, "apk_origin": "<abs path of source apk>",
  │       "imported_at": …, "assets_subdir": "assets" }
  └─ ProjectContext: project_dir = extraction dir; open the session
        → Asset Browser root = extraction dir (quick-sync watcher already live)
        → scene/asset roots now resolve from the extracted tree
        → status bar badge: "APK session: <name>" + File ▸ Export as APK… enabled
                │
                ▼   user edits scenes / textures / lua / scripts as usual
                │   (structured 3D edits, FileRift text edits — all write into
                │    the extraction, byte-identical to loose-file editing)
                │
File ▸ Export as APK…
  │
  ├─ target = session.json.apk_origin if it still exists, else QFileDialog
  ├─ repack: zip-write the extraction dir (store + deflate, mtimes preserved)
  │     → same layout as the source APK, so it installs over/alongside it
  ├─ sign:  (§3 — native v1 debug sign, else unsigned + explicit warning)
  ├─ success → ask "Delete the extracted session copy? (Yes / Keep)"
  │     Yes → rm -rf ~/.ruby/apk-sessions/<session-id>/; session cleared
  └─ status: "Exported <name>.apk (signed with demo debug key / unsigned)"
```

Why `~/.ruby/`: it is the tool's own state directory (same place `ruby_cli` and
the engine lookups already use `~/.local/share/swordigo-desktop`), it is a *copy*,
so the original APK is never mutated until export, and the whole session is one
directory that can be deleted atomically. A corrupt/broken session is trivially
"clear sessions" from the File menu.

### Strengthening the backend (the "make it stronger" part)

1. **Shared zip library** — extract `namespace zip` from `ruby_cli.cpp` into
   `src/platform/zip_archive.{h,cpp}` (reader + writer, store + deflate via the
   already-linked zlib), delete the two redundant readers in
   `binary_selector.cpp` / `mod_manager.cpp` (keep their public wrappers,
   reimplemented over the shared API). CLI keeps its commands; the GUI links the
   same code.
2. **Repack semantics** — a full repack is simplest and always byte-correct; an
   optional `--diff` mode skips entries whose CRC-32 is unchanged (faster for
   big APKs). Preserve `external_attr`/executable bits on repack (the extractor
   currently drops them, matching Python `zipfile` — note it in the doc).
3. **APK layout rules** — keep `META-INF/` intact on extract (copy it through on
   repack unchanged when re-signing with the same tool; when re-signing with a
   native v1 signer the META-INF is regenerated). `resources.arsc`,
   `classes.dex` and `AndroidManifest.xml` are binary — repack them verbatim
   (never decode/re-encode them; the web editor's "Decode to Clipboard" is
   read-only).
4. **Validation on import** — fail fast with a clear error when the file is not a
   ZIP, has no `AndroidManifest.xml`, or no `assets/` directory (a moddable
   Swordigo APK always has `assets/resources/*.scene`).

## 3. Native C app signer — feasibility

Android accepts three APK signature schemes; a sideloaded modded APK only needs
**v1 (JAR signing)** because v1 is mandatory and always supported:

| Scheme | What it is | Needed for a modded Swordigo APK |
|---|---|---|
| v1 (JAR) | `META-INF/MANIFEST.MF` + `CERT.SF` + `CERT.RSA` (PKCS#7 over SHA-1/SHA-256 digests) | ✅ Enough — installs on every Android version for `targetSdk < 30` apps (Swordigo is old) |
| v2 (APK Signing Block) | Signature Block inserted before the central directory, ZIP entries must be 4-byte aligned | Only required for `targetSdk ≥ 30`; Swordigo 1.4.13 targets ~19 — optional |
| v3/v4 | Rotation + streaming | Not needed for modding |

### What a native v1 signer needs (self-contained, no external deps)

1. **SHA-1 + SHA-256** — ~120 lines each, standard public-domain implementations.
2. **RSA-2048 signing** — the only real math: modular exponentiation over a
   2048-bit modulus. ~200 lines with a compact bignum (or a fixed
   Montgomery-multiplier). Key material: a **hardcoded demo private key** (the
   same idea as Android Studio's `debug.keystore` — Android only checks that the
   signature is *valid*, not who signed it, so one shared key installs fine on
   any device/emulator). ~80 lines of precomputed key bytes.
3. **PKCS#7 SignedData DER** — fixed structure for one signer, one digest:
   ~120 lines of DER writer. (This is the part UberAPKSigner's jar does; the
   web editor bundles the same structure.)
4. **Manifest/SF writer** — base64 digests per file + the signature file:
   ~80 lines.

Total ≈ 600 lines of plain C++17, zero runtime deps, deterministic output
(same inputs → same APK → reproducible builds). **Feasible — this is the
recommended path.** v1-only output installs on emulators and real devices alike.

**Fallback (already decided by the user):** if even that is too much, emit an
**unsigned APK** with a loud warning — stock Android refuses it, but
`adb install` with a debug-enabled emulator, custom ROMs, and the web editor's
own "could corrupt" stance all tolerate it. The Java+`apksigner.jar` path stays
as an optional "sign with apksigner.jar" for strict setups.

**Alignment note:** `zipalign` matters for `targetSdk ≥ 30`/v2. For v1-only we
can store `resources.arsc` and `classes*.dex` *uncompressed* (method 0) so the
runtime can mmap them — matching what `zipalign` effectively achieves. Optional
"store uncompressed + 4-byte align" writer mode is a small add-on in the shared
zip writer.

## 4. File/API plan

```
src/platform/zip_archive.{h,cpp}      # shared zip reader/writer (from ruby_cli.cpp)
src/tools/apk_session.{h,cpp}         # apk_import(path) -> Session, apk_export(Session, target),
                                      # apk_sign_v1(apk) native signer, session registry I/O
src/tools/apk_v1_sign.{h,cpp}         # SHA-1/256 + RSA demo key + PKCS#7 + manifest writer
src/ruby/editor/apk_session_panel.{h,cpp}  # Import/Export menu actions + session badge + cleanup ask
tests/apk_roundtrip_test.cpp          # tiny APK fixture: import → touch a scene → export →
                                      # re-extract → compare changed file + unchanged files verbatim
tests/apk_v1_sign_test.cpp            # sign fixture → verify MANIFEST/CERT.SF digests + PKCS#7 parses
```

CLI (`ruby_cli apk …`) is re-plumbed over the same functions so the GUI and CLI
can never drift apart.

## 5. Acceptance criteria (for the TODO)

1. Import a Swordigo APK → Asset Browser + scene editor work on the extracted
   tree; status shows the session.
2. Edit a scene + a texture → Export as APK → the two files differ from the
   source APK, every other entry is byte-identical.
3. Default target = original APK path; deleted-or-moved origin → file dialog.
4. Export offers cleanup; Yes removes `~/.ruby/apk-sessions/<id>/`.
5. Signing: native v1 demo key by default (device-installable), unsigned with
   warning when disabled, `--apksigner PATH` for the java path.
6. `apk_roundtrip_test` + `apk_v1_sign_test` green.

## 6. Links

- Existing backend report: `docs/FILERIFT_NATIVE_COMPATIBILITY.md` (APK extract
  byte-verified; `apk build`/`sign` parity table).
- Web editor comparison: `docs/web_swordigo_editor_research.md` §3.1 (APK/ZIP
  import + repack + resign in-browser; its `_signed.apk` is debug-signed the
  same way §3 proposes natively).
- Master list: `docs/MASTER_TODO_RUBY_GG_PARITY.md` task 4.1.