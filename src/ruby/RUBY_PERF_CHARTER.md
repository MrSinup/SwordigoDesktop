# Ruby GG Performance Charter

> These rules are **binding** for all Ruby GG / ruby_gg editor code (Qt Widgets
> frontend under `src/ruby/`). Any feature touching file I/O, text, decode,
> highlight or analysis must follow them. Keep this banner visible in the
> headers of `src/ruby/` translation units (see `perf_charter.h`).

## 1. Main thread is sacred
- **Never** decode, analyze, syntax-highlight, count, index, or do heavy file
  I/O for whole files on the main/GUI thread.
- The UI thread only: paints, event handling, materializing *small* windows of
  text, and marshalling results posted back from background workers.
- Heavy work runs on `std::thread`/background workers; results are delivered
  back to the GUI thread with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`
  guarded by a **generation counter** so stale/late results are dropped.

## 2. RAM budget — aim ≤ 500 MB runtime, currently ~200 MB
- Whole decoded/plain file text is a **single RAM-backed QString** (the buffer),
  shared copy-on-write with the document cache — never duplicated per widget.
- Never build throwaway whole-file copies (`toPlainText()` + `split` + a
  `std::vector<std::string>` line cache) just to scan a file. Use the lazily
  indexed line buffer (line starts, slicing, write-back) instead.
- Per-session diagnostics / analysis state lives in RAM for the session and is
  released when the document closes.

## 3. Load like VSCode — instant paint, background population
- Opening a file must paint the first visible screen in milliseconds.
- Files above the sync threshold are materialized progressively in bounded
  time-slices (never a single blocking `setPlainText` of the whole file).
- Files above the virtual threshold never fully materialize into the
  QTextDocument: only a visible window (± headroom) is materialized over the
  RAM buffer; navigation pages the window.

## 4. Highlighting & diagnostics are deferred/background
- Syntax highlighting of huge documents must not re-run over the whole
  document; only affected blocks are re-highlighted.
- Semantic analysis runs in a worker and posts results back; while pending the
  UI stays live. Edits invalidate in-flight results via the generation guard.

## 5. Persistent cache — `~/.ruby/` (hidden app-data folder, like VS Code)
- Optional persistent cache (decoded analysis/diagnostics/highlight
  configuration, keyed by path + size + mtime) lives under
  `QDir::homePath() + "/.ruby/ruby_gg/cache"`.
- Session-only data is never written there; the cache is best-effort and must
  be safe to delete at any time.

## 6. Search is over RAM text
- Find/Ctrl+F runs against the RAM text with background occurrence counting so
  even multi-lakh-line documents answer instantly without freezing the UI.
