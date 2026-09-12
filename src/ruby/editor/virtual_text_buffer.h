#pragma once
// ============================================================================
// virtual_text_buffer.h — RAM-backed whole-file text buffer for large files.
//
// Charter rules 2 & 3 (see src/ruby/RUBY_PERF_CHARTER.md):
//   • The whole file lives here as ONE QString (never duplicated).
//   • Line starts are indexed lazily (single O(n) pass, only when needed).
//   • Slicing / write-back work on character ranges bounded by line starts so
//     a paged editor window can be flushed back with no block-count pitfalls.
//   • Only the UI thread mutates; background workers operate on snapshot
//     copies (std::string) so QString thread-affinity is never violated.
// ============================================================================

#include <QString>
#include <vector>
#include <cstdint>

namespace ruby::editor {

class VirtualTextBuffer {
public:
    VirtualTextBuffer() = default;

    // Takes ownership of the whole decoded/plain text (the RAM home of the
    // file). After this call the buffer is the authoritative copy.
    void set_text(QString text);

    // Accessors (all const; index building is cached, not thread-safe to call
    // while another thread mutates — mutate only on the GUI thread).
    const QString& text() const { return m_text; }
    qsizetype char_size() const { return m_text.size(); }

    // Number of logical lines. Empty text => 0 lines.
    qsizetype line_count() const;

    // Position (UTF-16 unit offset) where `line` begins.
    // line == line_count() is valid and returns m_text.size().
    qsizetype line_start(qsizetype line) const;

    // Line index containing the character at `pos` (clamped into range).
    qsizetype line_at(qsizetype pos) const;

    // Text covering lines [first, first+count). If the window reaches the true
    // end of the file the slice stops at the last byte (may lack '\n').
    // Used to materialize exactly the characters that were requested so a
    // later toPlainText() write-back round-trips by character range.
    QString slice_lines(qsizetype first, qsizetype count) const;

    // Write edited window text back: replaces the character range
    // [line_start(first), line_start(first+old_line_count)) with `edited_text`.
    // Invalidates the line index (rebuilt lazily on the next query).
    void replace_line_range(qsizetype first, qsizetype old_line_count,
                            const QString& edited_text);

    // ── Whole-text search (over RAM — instant even for huge files) ──────────
    qsizetype find(const QString& needle, qsizetype from_pos,
                   Qt::CaseSensitivity cs = Qt::CaseSensitive) const;
    qsizetype last_index_of(const QString& needle, qsizetype from_pos,
                            Qt::CaseSensitivity cs = Qt::CaseSensitive) const;

    // Count every occurrence of `needle`. `cancel` (optional) is polled by the
    // caller's worker thread; pass nullptr on the GUI thread for small texts.
    qsizetype count_occurrences(const QString& needle,
                                Qt::CaseSensitivity cs,
                                const volatile bool* cancel = nullptr) const;

    void clear();

private:
    void ensure_index() const;   // mutable cache

    QString m_text;
    mutable std::vector<qsizetype> m_line_starts; // size = line_count()+1
    mutable bool m_indexed = false;
};

} // namespace ruby::editor
