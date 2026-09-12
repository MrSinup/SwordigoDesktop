// ============================================================================
// perf_charter.h — Ruby GG Performance Charter (binding rules)
//
// THE RULES (full text: src/ruby/RUBY_PERF_CHARTER.md):
//   1. MAIN THREAD IS SACRED — no whole-file decode / analysis / highlight /
//      indexing / file I/O on the GUI thread. Background workers post results
//      back via Qt::QueuedConnection, guarded by a generation counter so
//      stale results are dropped.
//   2. RAM BUDGET — aim ≤ 500 MB runtime (~200 MB today). Whole file text is a
//      single RAM-backed QString (COW-shared), lazily indexed by line starts;
//      never duplicate it with toPlainText()+split()+vector<string> scans.
//      Per-session diagnostics live in RAM and die with the session.
//   3. LOAD LIKE VSCODE — paint the first screen in ms; populate progressively
//      in bounded time-slices; above the virtual threshold only a visible
//      window is materialized over the RAM buffer (paged navigation).
//   4. HIGHLIGHT & DIAGNOSTICS DEFERRED — never re-highlight whole docs;
//      semantic analysis runs in a worker; edits invalidate via generation.
//   5. PERSISTENT CACHE — optional cache in ~/.ruby/ruby_gg/cache (hidden app
//      folder, VS Code style), keyed path+size+mtime; always deletable.
//   6. SEARCH OVER RAM TEXT — Ctrl+F scans the RAM text with background
//      occurrence counting (instant even for multi-lakh-line files).
// ============================================================================
#pragma once
