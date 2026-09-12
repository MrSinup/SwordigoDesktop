// ============================================================================
// virtual_text_buffer.cpp — RAM-backed whole-file text buffer (see header).
// ============================================================================

#include "virtual_text_buffer.h"
#include <algorithm>

namespace ruby::editor {

void VirtualTextBuffer::set_text(QString text) {
    m_text = std::move(text);
    m_indexed = false;
    m_line_starts.clear();
}

void VirtualTextBuffer::clear() {
    m_text.clear();
    m_indexed = false;
    m_line_starts.clear();
}

void VirtualTextBuffer::ensure_index() const {
    if (m_indexed) return;
    m_line_starts.clear();
    if (m_text.isEmpty()) {
        m_indexed = true;
        return;
    }
    // starts[0..count] — one entry per line plus a sentinel at text size.
    m_line_starts.reserve(static_cast<size_t>(m_text.count(QLatin1Char('\n'))) + 2);
    m_line_starts.push_back(0);
    qsizetype pos = 0;
    while ((pos = m_text.indexOf(QLatin1Char('\n'), pos)) != -1) {
        ++pos;
        m_line_starts.push_back(pos);
    }
    m_indexed = true;
}

qsizetype VirtualTextBuffer::line_count() const {
    if (m_text.isEmpty()) return 0;
    // Count of '\n' + 1, cheap even without building the full index.
    return m_text.count(QLatin1Char('\n')) + 1;
}

qsizetype VirtualTextBuffer::line_start(qsizetype line) const {
    ensure_index();
    if (line <= 0) return 0;
    const qsizetype n = line_count();
    if (line >= n) return m_text.size();
    // Index has n+1 entries when text non-empty (n == line_count()).
    return (line < static_cast<qsizetype>(m_line_starts.size()))
        ? m_line_starts[static_cast<size_t>(line)]
        : m_text.size();
}

qsizetype VirtualTextBuffer::line_at(qsizetype pos) const {
    ensure_index();
    if (m_text.isEmpty() || pos <= 0) return 0;
    // m_line_starts holds one entry per line (start of each line, the last one
    // pointing right after the final '\n' == size for files that end with a
    // newline). upper_bound gives the first start strictly greater than pos,
    // so the containing line is that index minus one — this also handles a
    // trailing empty line at pos == size correctly.
    auto it = std::upper_bound(m_line_starts.begin(), m_line_starts.end(), pos);
    if (it == m_line_starts.begin()) return 0;
    const qsizetype line = static_cast<qsizetype>(it - m_line_starts.begin()) - 1;
    return std::min(line, line_count() - 1);
}

QString VirtualTextBuffer::slice_lines(qsizetype first, qsizetype count) const {
    ensure_index();
    if (first < 0) first = 0;
    if (count <= 0) return QString();
    const qsizetype n = line_count();
    if (first >= n) return QString();
    const qsizetype end = std::min(first + count, n);
    const qsizetype a = line_start(first);
    const qsizetype b = line_start(end);
    return m_text.mid(a, b - a);
}

void VirtualTextBuffer::replace_line_range(qsizetype first,
                                           qsizetype old_line_count,
                                           const QString& edited_text) {
    ensure_index();
    const qsizetype n = line_count();
    if (first < 0) first = 0;
    if (first >= n && n == 0 && first == 0) {
        // Replacing into an empty file: just take the edited text verbatim.
        m_text = edited_text;
        m_indexed = false;
        return;
    }
    if (first > n) first = n;
    qsizetype old_end_line = std::min(first + std::max<qsizetype>(old_line_count, 0), n);
    const qsizetype a = line_start(first);
    const qsizetype b = line_start(old_end_line);
    if (a < 0 || b < a) return;
    m_text.replace(a, b - a, edited_text);
    m_indexed = false;
    m_line_starts.clear();
}

qsizetype VirtualTextBuffer::find(const QString& needle, qsizetype from_pos,
                                  Qt::CaseSensitivity cs) const {
    if (needle.isEmpty()) return -1;
    return m_text.indexOf(needle, from_pos, cs);
}

qsizetype VirtualTextBuffer::last_index_of(const QString& needle, qsizetype from_pos,
                                           Qt::CaseSensitivity cs) const {
    if (needle.isEmpty()) return -1;
    return m_text.lastIndexOf(needle, from_pos, cs);
}

qsizetype VirtualTextBuffer::count_occurrences(const QString& needle,
                                               Qt::CaseSensitivity cs,
                                               const volatile bool* cancel) const {
    if (needle.isEmpty()) return 0;
    qsizetype count = 0;
    const qsizetype step = std::max<qsizetype>(needle.size(), 1);
    qsizetype from = 0;
    while (true) {
        if (cancel && *cancel) return -1;      // -1 == "aborted"
        const qsizetype pos = m_text.indexOf(needle, from, cs);
        if (pos < 0) break;
        ++count;
        from = pos + step;
    }
    return count;
}

} // namespace ruby::editor
