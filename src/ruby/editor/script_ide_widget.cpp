// ============================================================================
// script_ide_widget.cpp — Implementation of the Script IDE & Code Editor
//
// Performance charter (src/ruby/RUBY_PERF_CHARTER.md):
//   - Whole-file decode / analysis / indexing never runs on the GUI thread.
//   - Small files load synchronously; medium files stream into the document in
//     bounded time-slices; huge files are virtualized (visible window only,
//     full text stays in the RAM VirtualTextBuffer).
//   - Semantic analysis runs in background workers guarded by a generation
//     counter; results are cached per-session and persisted under ~/.ruby.
//   - Ctrl+F searches the RAM text with background occurrence counting.
// ============================================================================

#include "script_ide_widget.h"
#include <algorithm>
#include <thread>
#include <chrono>
#include <cstdio>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QFile>
#include <QTextStream>
#include <QFontDatabase>
#include <QAbstractItemView>
#include <QScrollBar>
#include <QFileInfo>
#include <QShortcut>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QCheckBox>
#include <QToolButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QInputDialog>
#include <climits>
#include "tools/filerift.h"
#include "../database/swordigo_engine_db.h"
#include "../database/filerift_ls.h"

namespace ruby::editor {

// ─── Tunables (Charter thresholds) ─────────────────────────────────────────
namespace {
constexpr qsizetype kSyncMaxLines = 6000;                 // sync materialize + small analysis
constexpr qsizetype kSyncMaxChars = 3 * 1024 * 1024;      // 3 MB UTF-16 (~1.5 MB text)
constexpr qsizetype kStreamChunkChars = 4 * 1024 * 1024;  // per time-slice budget
constexpr qsizetype kStreamMaxChars = 64 * 1024 * 1024;   // 64 MB UTF-16 => virtualize
constexpr qsizetype kAnalysisCapLines = 250000;           // above: skip semantic analysis
constexpr qsizetype kLSMaxLines = 50000;                  // above: disable completer/LS
constexpr qsizetype kWindowCapacity = 6000;               // virtual window lines
constexpr int kFindCountSyncMaxChars = 8 * 1024 * 1024;   // count off-thread above this

qsizetype env_lines(const char* var, qsizetype fallback) {
    const QByteArray v = qgetenv(var);
    if (v.isEmpty()) return fallback;
    bool ok = false;
    const qlonglong n = v.toLongLong(&ok);
    return (ok && n > 0) ? static_cast<qsizetype>(n) : fallback;
}

// Verbose worker/load tracing; off unless RUBY_GG_DEBUG=1 is set.
bool gg_debug() {
    static const bool on = qEnvironmentVariableIntValue("RUBY_GG_DEBUG") != 0;
    return on;
}
} // namespace

qsizetype ScriptIDEWidget::virtual_min_lines() {
    return env_lines("RUBY_GG_VIRTUAL_MIN_LINES", 200000);
}

qsizetype ScriptIDEWidget::stream_max_lines() {
    return env_lines("RUBY_GG_STREAM_MAX_LINES", 200000);
}

// ─── Line Number Area ───────────────────────────────────────────────────────
LineNumberArea::LineNumberArea(ScriptIDEWidget* editor) : QWidget(editor), m_editor(editor) {}

QSize LineNumberArea::sizeHint() const {
    return QSize(m_editor->lineNumberAreaWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent* event) {
    m_editor->lineNumberAreaPaintEvent(event);
}

// ─── Script IDE Widget ──────────────────────────────────────────────────────
ScriptIDEWidget::~ScriptIDEWidget() {
    // Charter rule 1/2: never destroy Qt objects a background worker may still
    // touch. Wait (bounded) for decode / analysis / find-count threads to end.
    std::unique_lock<std::mutex> lk(m_worker_mutex);
    m_worker_cv.wait_for(lk, std::chrono::seconds(10),
                         [this]() { return m_worker_count == 0; });
}

void ScriptIDEWidget::spawn_worker(std::function<void()> fn) {
    if (gg_debug()) fprintf(stderr, "[spawn_worker] count++\n");
    {
        std::lock_guard<std::mutex> lk(m_worker_mutex);
        ++m_worker_count;
    }
    std::thread worker([this, fn = std::move(fn)]() {
        if (gg_debug()) fprintf(stderr, "[spawn_worker] thread running\n");
        try {
            fn();
        } catch (...) {
            fprintf(stderr, "[spawn_worker] fn threw\n");
        }
        if (gg_debug()) fprintf(stderr, "[spawn_worker] thread done\n");
        notify_worker_done();
    });
    worker.detach();
}

void ScriptIDEWidget::notify_worker_done() {
    {
        std::lock_guard<std::mutex> lk(m_worker_mutex);
        --m_worker_count;
    }
    m_worker_cv.notify_all();
}

void ScriptIDEWidget::poke_poll_timer() {
    // The poll timer is always running (cheap empty checks); nothing to do.
}

void ScriptIDEWidget::deliver_decode_result() {
    std::shared_ptr<DecodeResult> r;
    {
        std::lock_guard<std::mutex> lk(m_result_mutex);
        r = std::move(m_decode_result);
        m_decode_result.reset();
    }
    if (!r) return;
    if (gg_debug())
        fprintf(stderr, "[deliverDecode] ok=%d gen=%llu cur=%llu\n",
                (int)r->ok, (unsigned long long)r->gen, (unsigned long long)m_gen);
    m_busy_loading = false;               // always release the loading latch
    if (r->gen != m_gen) return;          // superseded before decode finished
    if (!r->ok) return;
    adopt_loaded_text(std::move(r->content), r->path, r->type, !r->type.isEmpty());
}

void ScriptIDEWidget::deliver_analysis_result() {
    std::shared_ptr<AnalysisPack> p;
    {
        std::lock_guard<std::mutex> lk(m_result_mutex);
        p = std::move(m_analysis_pack);
        m_analysis_pack.reset();
    }
    if (!p) return;
    m_analysis_running = false;
    if (p->gen != m_gen) return;          // stale — a newer document replaced this
    m_analysis_result = *p->res;
    m_diagnostics = *p->diags;
    report_diagnostics_to_highlighter();
    emit analysisCompleted(p->errors, p->warnings);
}

void ScriptIDEWidget::deliver_find_result() {
    std::shared_ptr<FindPack> p;
    {
        std::lock_guard<std::mutex> lk(m_result_mutex);
        p = std::move(m_find_pack);
        m_find_pack.reset();
    }
    if (!p) return;
    m_find_counting = false;
    if (p->gen != m_gen) return;
    m_find_last_count = p->count;
    if (m_find_count) m_find_count->setText(QString("0 / %1").arg(p->count));
}

void ScriptIDEWidget::poll_worker_results() {
    if (gg_debug()) fprintf(stderr, "[poll]\n");
    deliver_decode_result();
    deliver_analysis_result();
    deliver_find_result();
}

ScriptIDEWidget::ScriptIDEWidget(QWidget* parent) : QPlainTextEdit(parent) {
    m_line_number_area = new LineNumberArea(this);

    // Monospace code font
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(11);
    setFont(font);

    // Virtualization-friendly layout: no wrap keeps per-line layout O(1).
    setLineWrapMode(QPlainTextEdit::NoWrap);

    // FileRift TextMate-fidelity syntax highlighter
    m_highlighter = new ruby::filerift::FileRiftHighlighter(document());

    // Autocompleter setup
    m_completer = new QCompleter(this);
    m_completer_model = new QStringListModel(this);
    m_completer->setModel(m_completer_model);
    m_completer->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setWrapAround(false);
    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    if (m_completer->popup()) {
        m_completer->popup()->setStyleSheet(
            "QListView { background-color: #21252b; color: #abb2bf; border: 1px solid #181a1f; "
            "selection-background-color: #2c313a; selection-color: #61afef; padding: 4px; font-family: monospace; font-size: 12px; }"
        );
    }
    connect(m_completer, QOverload<const QString&>::of(&QCompleter::activated),
            this, &ScriptIDEWidget::insertCompletion);

    // Debounced semantic analysis timer (150ms)
    m_analysis_timer = new QTimer(this);
    m_analysis_timer->setSingleShot(true);
    m_analysis_timer->setInterval(150);
    connect(m_analysis_timer, &QTimer::timeout, this, &ScriptIDEWidget::start_background_analysis);
    connect(this, &QPlainTextEdit::textChanged, this, &ScriptIDEWidget::onDocumentTextChanged);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &ScriptIDEWidget::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &ScriptIDEWidget::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &ScriptIDEWidget::highlightCurrentLine);

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();

    // Progressive stream loader
    m_stream_timer = new QTimer(this);
    m_stream_timer->setSingleShot(false);
    m_stream_timer->setInterval(0);
    connect(m_stream_timer, &QTimer::timeout, this, &ScriptIDEWidget::stream_more);

    // Virtual-mode window paging (whole-file scrollbar mapping)
    connect(verticalScrollBar(), &QScrollBar::valueChanged,
            this, &ScriptIDEWidget::on_slider_value_changed);
    connect(verticalScrollBar(), &QScrollBar::sliderPressed,
            this, &ScriptIDEWidget::on_slider_pressed);
    connect(verticalScrollBar(), &QScrollBar::sliderReleased,
            this, &ScriptIDEWidget::on_slider_released);

    // JetBrains-style Quick Documentation: Ctrl+Q and F1
    auto* quick_doc_shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q), this);
    connect(quick_doc_shortcut, &QShortcut::activated, this, &ScriptIDEWidget::showQuickDoc);
    auto* f1_shortcut = new QShortcut(QKeySequence(Qt::Key_F1), this);
    connect(f1_shortcut, &QShortcut::activated, this, &ScriptIDEWidget::showQuickDoc);

    // Ctrl+F find bar, Ctrl+G go-to-line
    auto* find_shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this);
    connect(find_shortcut, &QShortcut::activated, this, &ScriptIDEWidget::toggle_find_bar);
    auto* goto_shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_G), this);
    connect(goto_shortcut, &QShortcut::activated, this, [this]() {
        const int cur = textCursor().blockNumber() + 1 + (m_virtual_mode ? static_cast<int>(m_win_start) : 0);
        bool ok = false;
        const int line = QInputDialog::getInt(this, "Go to Line", "Line number (1-based):",
                                              cur, 1, 2000000000, 1, &ok);
        if (ok) jump_to_line(line);
    });
    connect(goto_shortcut, &QShortcut::activatedAmbiguously, this, [this]() {
        const int cur = textCursor().blockNumber() + 1 + (m_virtual_mode ? static_cast<int>(m_win_start) : 0);
        bool ok = false;
        const int line = QInputDialog::getInt(this, "Go to Line", "Line number (1-based):",
                                              cur, 1, 2000000000, 1, &ok);
        if (ok) jump_to_line(line);
    });

    // Find bar UI (hidden until Ctrl+F)
    m_find_bar = new QWidget(this);
    m_find_bar->setObjectName("RubyFindBar");
    m_find_bar->setStyleSheet(
        "#RubyFindBar { background:#21252b; border:1px solid #3e4451; border-radius:6px; }"
        "QLineEdit { background:#181a1f; color:#abb2bf; border:1px solid #3e4451; border-radius:4px; padding:3px 6px; }"
        "QLabel { color:#7d8492; background:transparent; }"
        "QToolButton { background:transparent; color:#abb2bf; border:none; padding:2px 6px; }"
        "QToolButton:hover { background:#2c313a; border-radius:4px; }"
        "QCheckBox { color:#7d8492; background:transparent; }");
    auto* find_layout = new QHBoxLayout(m_find_bar);
    find_layout->setContentsMargins(6, 4, 6, 4);
    find_layout->setSpacing(4);
    auto* find_label = new QLabel("Find:", m_find_bar);
    find_layout->addWidget(find_label);
    m_find_edit = new QLineEdit(m_find_bar);
    m_find_edit->setMinimumWidth(180);
    m_find_edit->setClearButtonEnabled(true);
    find_layout->addWidget(m_find_edit, 1);
    m_find_case = new QCheckBox("Aa", m_find_bar);
    m_find_case->setToolTip("Match case");
    find_layout->addWidget(m_find_case);
    m_find_prev = new QToolButton(m_find_bar);
    m_find_prev->setText("▲");
    m_find_prev->setToolTip("Previous (Shift+Enter)");
    find_layout->addWidget(m_find_prev);
    m_find_next = new QToolButton(m_find_bar);
    m_find_next->setText("▼");
    m_find_next->setToolTip("Next (Enter)");
    find_layout->addWidget(m_find_next);
    m_find_count = new QLabel("", m_find_bar);
    m_find_count->setMinimumWidth(56);
    find_layout->addWidget(m_find_count);
    m_find_close = new QToolButton(m_find_bar);
    m_find_close->setText("✕");
    m_find_close->setToolTip("Close (Esc)");
    find_layout->addWidget(m_find_close);
    m_find_bar->hide();
    layout_find_bar();

    m_find_debounce = new QTimer(this);
    m_find_debounce->setSingleShot(true);
    m_find_debounce->setInterval(220);
    connect(m_find_debounce, &QTimer::timeout, this, &ScriptIDEWidget::schedule_find_count);
    connect(m_find_edit, &QLineEdit::textChanged, this, [this](const QString&) {
        m_find_last_needle.clear();          // force re-jump on next Enter
        if (m_find_debounce) m_find_debounce->start();
    });
    connect(m_find_edit, &QLineEdit::returnPressed, this, [this]() { find_next(false); });
    connect(m_find_next, &QToolButton::clicked, this, [this]() { find_next(false); });
    connect(m_find_prev, &QToolButton::clicked, this, [this]() { find_next(true); });
    connect(m_find_close, &QToolButton::clicked, this, &ScriptIDEWidget::toggle_find_bar);
    connect(m_find_case, &QCheckBox::toggled, this, [this](bool) {
        if (m_find_debounce) m_find_debounce->start();
    });

    // Result hand-off timer: drains worker payloads onto the GUI thread.
    m_poll_timer = new QTimer(this);
    m_poll_timer->setInterval(15);
    connect(m_poll_timer, &QTimer::timeout, this, &ScriptIDEWidget::poll_worker_results);
    m_poll_timer->start();
    if (gg_debug()) fprintf(stderr, "[ctor] poll timer id=%d\n", m_poll_timer->timerId());
}

int ScriptIDEWidget::lineNumberAreaWidth() {
    int digits = 1;
    int max_blocks = std::max(1, blockCount());
    if (m_virtual_mode && m_vbuf) {
        const qsizetype total = m_vbuf->line_count();
        if (total > max_blocks) {
            // keep the gutter sized for the whole file
            qint64 v = static_cast<qint64>(total);
            while (v >= 10) { v /= 10; ++digits; }
            if (digits < 2) digits = 2;
            int space = 16 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
            return space;
        }
    }
    while (max_blocks >= 10) { max_blocks /= 10; ++digits; }
    int space = 16 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    return space;
}

void ScriptIDEWidget::updateLineNumberAreaWidth(int /* newBlockCount */) {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void ScriptIDEWidget::updateLineNumberArea(const QRect& rect, int dy) {
    if (dy) m_line_number_area->scroll(0, dy);
    else m_line_number_area->update(0, rect.y(), m_line_number_area->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void ScriptIDEWidget::resizeEvent(QResizeEvent* e) {
    QPlainTextEdit::resizeEvent(e);
    QRect cr = contentsRect();
    m_line_number_area->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
    layout_find_bar();
}

void ScriptIDEWidget::layout_find_bar() {
    if (!m_find_bar) return;
    const int bar_h = m_find_bar->sizeHint().height();
    m_find_bar->setGeometry(width() - m_find_bar->sizeHint().width() - 14, 8,
                            m_find_bar->sizeHint().width(), bar_h);
}

void ScriptIDEWidget::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> extraSelections;

    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        QColor lineColor = QColor("#21242b");
        selection.format.setBackground(lineColor);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);
    }

    setExtraSelections(extraSelections);
}

void ScriptIDEWidget::lineNumberAreaPaintEvent(QPaintEvent* event) {
    QPainter painter(m_line_number_area);
    painter.fillRect(event->rect(), QColor("#181a1f"));

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    const qsizetype base = m_virtual_mode ? m_win_start : 0;
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const qsizetype abs = base + blockNumber;
            QString number = QString::number(abs + 1);
            painter.setPen(QColor("#5c6370"));
            painter.drawText(0, top, m_line_number_area->width() - 8, fontMetrics().height(),
                             Qt::AlignRight, number);
        }

        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void ScriptIDEWidget::onDocumentTextChanged() {
    // Ignore programmatic re-materialization (window paging / load).
    if (m_materializing) return;
    if (m_busy_loading) return;
    if (m_mode == LoadMode::Stream && !m_stream_done) return;

    if (m_mode == LoadMode::Virtual) {
        m_win_dirty = true;
        m_edited = true;
        if (m_analysis_timer) m_analysis_timer->start(600);
        return;
    }
    if (m_mode == LoadMode::Sync) {
        // Small docs: debounced analysis is cheap enough to re-run on edit.
        if (m_analysis_timer) m_analysis_timer->start(150);
    } else if (m_mode == LoadMode::Stream && m_stream_done) {
        if (m_analysis_timer) m_analysis_timer->start(400);
    }
}

void ScriptIDEWidget::trigger_analysis() {
    start_background_analysis();
}

std::string ScriptIDEWidget::file_ext_or_type() const {
    QString ext = QFileInfo(m_file_path).suffix().toLower();
    if (ext.isEmpty() && !m_filerift_type.isEmpty()) ext = m_filerift_type;
    if (ext.isEmpty()) ext = QStringLiteral("scene");
    return ext.toStdString();
}

QString ScriptIDEWidget::text_for_analysis() {
    if (m_virtual_mode && m_vbuf) {
        flush_window_to_buffer();
        return m_vbuf->text();
    }
    if (m_mode == LoadMode::Stream && m_stream_done) {
        // Document is the live source; extract once (bounded by stream cap).
        return toPlainText();
    }
    return toPlainText();
}

// ─── Small-file synchronous analysis (legacy fast path) ────────────────────
void ScriptIDEWidget::run_filerift_analysis() {
    if (m_busy_loading) return;
    const qsizetype lines = static_cast<qsizetype>(document()->blockCount());
    if (lines > kSyncMaxLines) return;   // routed to background analysis

    const QString text = toPlainText();
    if (text.isEmpty()) {
        m_diagnostics.clear();
        m_analysis_result = {};
        m_lines_cache.clear();
        if (m_highlighter) m_highlighter->set_diagnostics({});
        emit analysisCompleted(0, 0);
        return;
    }

    const QStringList qlines = text.split(QLatin1Char('\n'));
    m_lines_cache.clear();
    m_lines_cache.reserve(static_cast<size_t>(qlines.size()));
    for (const auto& ql : qlines) {
        m_lines_cache.push_back(ql.toStdString());
    }

    const std::string ext = file_ext_or_type();
    m_analysis_result = ruby::filerift::FileRiftAnalyzer::analyze_lines(m_lines_cache, ext);
    m_diagnostics = ruby::filerift::FileRiftAnalyzer::compute_diagnostics(m_lines_cache, m_analysis_result);

    report_diagnostics_to_highlighter();

    int errors = 0, warnings = 0;
    for (const auto& d : m_diagnostics) {
        if (d.severity == ruby::filerift::Diagnostic::Error) ++errors;
        else if (d.severity == ruby::filerift::Diagnostic::Warning) ++warnings;
    }
    emit analysisCompleted(errors, warnings);
}

// ─── Background analysis worker ─────────────────────────────────────────────
namespace {
struct AnalysisCacheEntry {
    QString path;
    qint64 size = 0;
    qint64 mtime = 0;
};

QString cache_dir() {
    return QDir::homePath() + QStringLiteral("/.ruby/ruby_gg/cache/filerift");
}

QString cache_key(const QString& path, qint64 size, qint64 mtime) {
    const QByteArray src = QStringLiteral("%1|%2|%3").arg(path).arg(size).arg(mtime).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(src, QCryptographicHash::Sha1).toHex());
}

QString cache_path_for(const QString& path, qint64 size, qint64 mtime) {
    return cache_dir() + QLatin1Char('/') + cache_key(path, size, mtime) + QStringLiteral(".json");
}

void cache_save(const QString& file_path, qint64 size, qint64 mtime,
                const ruby::filerift::AnalysisResult& res,
                const std::vector<ruby::filerift::Diagnostic>& diags) {
    QDir().mkpath(cache_dir());
    QJsonObject root;
    root["path"] = file_path;
    root["size"] = static_cast<double>(size);
    root["mtime"] = static_cast<double>(mtime);
    int errors = 0, warnings = 0;
    for (const auto& d : diags) {
        if (d.severity == ruby::filerift::Diagnostic::Error) ++errors;
        else if (d.severity == ruby::filerift::Diagnostic::Warning) ++warnings;
    }
    root["errors"] = errors;
    root["warnings"] = warnings;
    root["lua_chunks"] = !res.chunk_start.empty();
    QJsonArray arr;
    for (const auto& d : diags) {
        QJsonObject o;
        o["line"] = d.line;
        o["col"] = d.start_col;
        o["len"] = d.length;
        o["sev"] = static_cast<int>(d.severity);
        o["msg"] = QString::fromStdString(d.message);
        arr.append(o);
    }
    root["diagnostics"] = arr;
    QSaveFile f(cache_path_for(file_path, size, mtime));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

// Returns true + fills diags/counts if a fresh cache exists.
bool cache_load(const QString& file_path, qint64 size, qint64 mtime,
                std::vector<ruby::filerift::Diagnostic>& diags,
                int& errors, int& warnings, bool& lua_chunks) {
    QFile f(cache_path_for(file_path, size, mtime));
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    const QJsonObject root = doc.object();
    if (root.value("path").toString() != file_path) return false;
    if (static_cast<qint64>(root.value("size").toDouble()) != size) return false;
    if (static_cast<qint64>(root.value("mtime").toDouble()) != mtime) return false;
    errors = root.value("errors").toInt();
    warnings = root.value("warnings").toInt();
    lua_chunks = root.value("lua_chunks").toBool();
    diags.clear();
    const QJsonArray arr = root.value("diagnostics").toArray();
    for (const auto& v : arr) {
        const QJsonObject o = v.toObject();
        ruby::filerift::Diagnostic d;
        d.line = o.value("line").toInt();
        d.start_col = o.value("col").toInt();
        d.length = o.value("len").toInt();
        d.severity = static_cast<ruby::filerift::Diagnostic::Severity>(o.value("sev").toInt());
        d.message = o.value("msg").toString().toStdString();
        diags.push_back(std::move(d));
    }
    return true;
}
} // namespace

void ScriptIDEWidget::start_background_analysis() {
    if (m_busy_loading) return;
    if (m_mode == LoadMode::Idle) return;
    if (m_analysis_running) return;
    if (m_virtual_mode || m_mode == LoadMode::Stream) {
        const qsizetype nlines = m_virtual_mode && m_vbuf
            ? m_vbuf->line_count()
            : static_cast<qsizetype>(document()->blockCount());
        if (nlines > kAnalysisCapLines) {
            // Too large for full semantic analysis: report plain (fast) status.
            m_diagnostics.clear();
            if (m_highlighter) m_highlighter->clear_diagnostics();
            emit analysisCompleted(0, 0);
            return;
        }
    }
    // snapshot text on the GUI thread (single copy), then analyze off-thread
    const QString text = text_for_analysis();
    if (text.isEmpty()) {
        m_diagnostics.clear();
        m_analysis_result = {};
        m_lines_cache.clear();
        if (m_highlighter) m_highlighter->set_diagnostics({});
        emit analysisCompleted(0, 0);
        return;
    }

    // Warm the const-read singletons on the GUI thread before workers read them.
    (void)ruby::filerift::FileRiftSchema::instance();
    (void)ruby::database::SwordigoEngineDB::instance();

    const quint64 gen = m_gen;
    const std::string ext = file_ext_or_type();
    const QString fpath = m_file_path;
    const QFileInfo fi(fpath);
    const qint64 fsize = fi.exists() ? fi.size() : 0;
    const qint64 fmtime = fi.exists() ? fi.lastModified().toSecsSinceEpoch() : 0;
    // The file on disk only matches the buffer for pristine docs; only use the
    // cache for unmodified content.
    const bool pristine = (m_mode == LoadMode::Sync && !document()->isModified()) ||
                          (m_virtual_mode && !m_win_dirty && !document()->isModified()) ||
                          (m_mode == LoadMode::Stream && m_stream_done && !document()->isModified());

    auto worker_lambda = [this, text, ext, gen, fpath, fsize, fmtime, pristine]() {
        std::shared_ptr<ruby::filerift::AnalysisResult> res =
            std::make_shared<ruby::filerift::AnalysisResult>();
        std::shared_ptr<std::vector<ruby::filerift::Diagnostic>> diags =
            std::make_shared<std::vector<ruby::filerift::Diagnostic>>();

        int errors = 0, warnings = 0;
        bool lua_chunks = false;
        bool from_cache = false;
        if (pristine && !fpath.isEmpty() && fsize > 0) {
            from_cache = cache_load(fpath, fsize, fmtime, *diags, errors, warnings, lua_chunks);
        }
        if (!from_cache) {
            // Build the line cache once, off the GUI thread.
            std::vector<std::string> lines;
            const QStringList qlines = text.split(QLatin1Char('\n'));
            lines.reserve(static_cast<size_t>(qlines.size()));
            for (const auto& ql : qlines) lines.push_back(ql.toStdString());

            *res = ruby::filerift::FileRiftAnalyzer::analyze_lines(lines, ext);
            *diags = ruby::filerift::FileRiftAnalyzer::compute_diagnostics(lines, *res);
            for (const auto& d : *diags) {
                if (d.severity == ruby::filerift::Diagnostic::Error) ++errors;
                else if (d.severity == ruby::filerift::Diagnostic::Warning) ++warnings;
            }
            if (pristine && !fpath.isEmpty() && fsize > 0)
                cache_save(fpath, fsize, fmtime, *res, *diags);
        } else {
            res->chunk_start.assign(static_cast<size_t>(lua_chunks ? 1 : 0), true);
        }

        auto pack = std::make_shared<AnalysisPack>();
        pack->gen = gen;
        pack->res = res;
        pack->diags = diags;
        pack->errors = errors;
        pack->warnings = warnings;
        {
            std::lock_guard<std::mutex> lk(m_result_mutex);
            m_analysis_pack = std::move(pack);
        }
    };
    m_analysis_running = true;
    spawn_worker(std::move(worker_lambda));
}



void ScriptIDEWidget::cancel_pending_workers() {
    ++m_gen;
    m_analysis_running = false;   // in-flight posts are dropped via the gen guard
}

// ─── Loading pipeline ───────────────────────────────────────────────────────
void ScriptIDEWidget::reset_editor_state() {
    cancel_pending_workers();
    ++m_gen;
    {
        std::lock_guard<std::mutex> lk(m_result_mutex);
        m_decode_result.reset();
        m_analysis_pack.reset();
        m_find_pack.reset();
    }
    m_mode = LoadMode::Idle;
    m_busy_loading = false;
    m_virtual_mode = false;
    m_stream_done = false;
    m_stream_cursor = 0;
    m_win_dirty = false;
    m_win_start = 0;
    m_win_old_lines = 0;
    if (m_stream_timer) m_stream_timer->stop();
    if (m_analysis_timer) m_analysis_timer->stop();
    m_diagnostics.clear();
    m_analysis_result = {};
    m_lines_cache.clear();
    m_pending_text.clear();
    if (m_vbuf) m_vbuf->clear();
    if (m_highlighter) m_highlighter->clear_diagnostics();
}

qsizetype ScriptIDEWidget::estimate_line_count(const QString& text) const {
    return text.isEmpty() ? 0 : text.count(QLatin1Char('\n')) + 1;
}

bool ScriptIDEWidget::load_file(const QString& file_path, const QString& filerift_type) {
    reset_editor_state();
    m_file_path = file_path;
    m_busy_loading = true;
    ++m_gen;
    const quint64 gen = m_gen;

    auto worker_lambda = [this, gen, file_path, filerift_type]() {
        QFile file(file_path);
        bool ok = false;
        QString content;
        QString type = filerift_type;
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = file.readAll();
            const bool looks_binary = bytes.contains('\0');
            if (!filerift_type.isEmpty() && looks_binary) {
                try {
                    const std::string markup = ::filerift::decode_protobuf(
                        std::string(bytes.constData(), static_cast<size_t>(bytes.size())),
                        filerift_type.toStdString());
                    // The native decoder already emits the FileRift banner.
                    QString decoded = QString::fromUtf8(markup.c_str(),
                                                         static_cast<qsizetype>(markup.size()));
                    if (!decoded.startsWith(QLatin1String("## FileRift decoded"))) {
                        content = QStringLiteral("## FileRift decoded Swordigo file type: ") +
                                  filerift_type + QStringLiteral("\n\n") + decoded;
                    } else {
                        content = decoded;
                    }
                    ok = true;
                } catch (const std::exception& e) {
                    fprintf(stderr, "[filerift decode] %s\n", e.what());
                    ok = false;
                } catch (...) {
                    fprintf(stderr, "[filerift decode] unknown exception for %s\n",
                            file_path.toUtf8().constData());
                    ok = false;
                }
            } else {
                content = QString::fromUtf8(bytes);
                type = filerift_type;
                ok = true;
            }
        } else {
            fprintf(stderr, "[filerift decode] cannot open %s\n", file_path.toUtf8().constData());
        }
        auto result = std::make_shared<DecodeResult>();
        result->gen = gen;
        result->path = file_path;
        result->type = type;
        result->content = std::move(content);
        result->ok = ok;
        {
            std::lock_guard<std::mutex> lk(m_result_mutex);
            m_decode_result = std::move(result);
        }
    };
    spawn_worker(std::move(worker_lambda));
    return true;
}

void ScriptIDEWidget::load_buffer(const QString& text, const QString& file_path,
                                  const QString& filerift_type) {
    reset_editor_state();
    m_file_path = file_path;
    adopt_loaded_text(text, file_path, filerift_type, !filerift_type.isEmpty());
}

void ScriptIDEWidget::adopt_loaded_text(QString content, const QString& path,
                                        const QString& filerift_type, bool was_decoded) {
    (void)was_decoded;
    cancel_pending_workers();
    ++m_gen;
    m_file_path = path;
    m_filerift_type = filerift_type;
    m_virtual_mode = false;
    m_mode = LoadMode::Idle;

    const qsizetype chars = content.size();
    const qsizetype lines = estimate_line_count(content);

    const qsizetype vmin = virtual_min_lines();

    if (lines > vmin || chars > kStreamMaxChars) {
        // ── VIRTUAL MODE: full text stays in RAM, only a window is shown ──
        m_mode = LoadMode::Virtual;
        m_virtual_mode = true;
        m_edited = false;
        m_vbuf = std::make_unique<VirtualTextBuffer>();
        m_vbuf->set_text(std::move(content));
        m_block_h = std::max(fontMetrics().height(), 1);
        m_materializing = true;
        document()->setUndoRedoEnabled(false);
        setPlainText(QString());
        document()->setUndoRedoEnabled(true);
        document()->clearUndoRedoStacks();
        m_materializing = false;
        m_busy_loading = false;
        materialize_virtual_window(0, nullptr, false);
        setReadOnly(false);
        emit fileFullyLoaded();
        start_background_analysis();
        emit documentLoaded(m_file_path);
        return;
    }

    if (lines > kSyncMaxLines || chars > kSyncMaxChars) {
        // ── STREAM MODE: paint the head instantly, stream the rest ────────
        m_mode = LoadMode::Stream;
        m_edited = false;
        setReadOnly(true);
        m_busy_loading = true;
        m_pending_text = std::move(content);
        m_stream_cursor = 0;
        m_stream_done = false;
        // Head chunk = roughly one screen; always end on a line boundary.
        const qsizetype head_chars = std::min<qsizetype>(m_pending_text.size(), 256 * 1024);
        const qsizetype nl = m_pending_text.indexOf(QLatin1Char('\n'), head_chars);
        m_stream_cursor = (nl == -1) ? m_pending_text.size() : nl + 1;
        m_materializing = true;
        document()->setUndoRedoEnabled(false);
        setPlainText(m_pending_text.left(m_stream_cursor));
        document()->setUndoRedoEnabled(true);
        document()->clearUndoRedoStacks();
        m_materializing = false;
        document()->setModified(false);
        m_stream_done = m_stream_cursor >= m_pending_text.size();
        if (m_stream_done) {
            setReadOnly(false);
            m_busy_loading = false;
            emit fileFullyLoaded();
            start_background_analysis();
            emit documentLoaded(m_file_path);
        } else {
            m_stream_timer->start();
        }
        return;
    }

    // ── SYNC MODE: small file, direct fast path ───────────────────────────
    m_mode = LoadMode::Sync;
    m_edited = false;
    setReadOnly(false);
    m_materializing = true;
    document()->setUndoRedoEnabled(false);
    setPlainText(content);
    document()->setUndoRedoEnabled(true);
    document()->clearUndoRedoStacks();
    m_materializing = false;
    document()->setModified(false);
    m_busy_loading = false;
    if (m_analysis_timer) m_analysis_timer->stop();
    emit fileFullyLoaded();
    if (lines > 2000) {
        start_background_analysis();
    } else {
        run_filerift_analysis();
    }
    emit documentLoaded(m_file_path);
}

void ScriptIDEWidget::stream_more() {
    if (m_mode != LoadMode::Stream || m_stream_done ||
        m_stream_cursor >= m_pending_text.size()) {
        m_stream_timer->stop();
        return;
    }
    QElapsedTimer tick;
    tick.start();
    document()->setUndoRedoEnabled(false);
    QTextCursor cursor(document());

    while (tick.elapsed() < 6) {
        if (m_stream_cursor >= m_pending_text.size()) {
            m_stream_done = true;
            break;
        }
        qsizetype end = std::min(m_stream_cursor + kStreamChunkChars, m_pending_text.size());
        // extend to end of the current line so we always split on '\n'
        const qsizetype nl = m_pending_text.indexOf(QLatin1Char('\n'), end);
        if (nl == -1) end = m_pending_text.size();
        else end = nl + 1;
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(m_pending_text.mid(m_stream_cursor, end - m_stream_cursor));
        m_stream_cursor = end;
    }
    document()->setUndoRedoEnabled(true);

    if (m_stream_done || m_stream_cursor >= m_pending_text.size()) {
        m_stream_done = true;
        m_stream_timer->stop();
        m_pending_text.clear();
        m_pending_text.squeeze();
        setReadOnly(false);
        document()->setModified(false);
        m_busy_loading = false;
        emit fileFullyLoaded();
        // Kick the background analyzer (debounced so quick tab hops don't spam).
        start_background_analysis();
        emit documentLoaded(m_file_path);
    }
}

void ScriptIDEWidget::finish_stream() {
    if (m_mode == LoadMode::Stream && !m_stream_done) {
        m_stream_timer->stop();
        QTextCursor cursor(document());
        document()->setUndoRedoEnabled(false);
        if (m_stream_cursor < m_pending_text.size()) {
            cursor.movePosition(QTextCursor::End);
            cursor.insertText(m_pending_text.mid(m_stream_cursor));
        }
        m_stream_cursor = m_pending_text.size();
        document()->setUndoRedoEnabled(true);
        m_pending_text.clear();
        m_stream_done = true;
        setReadOnly(false);
        m_busy_loading = false;
        emit fileFullyLoaded();
        emit documentLoaded(m_file_path);
    }
}

// ─── Virtual window paging ──────────────────────────────────────────────────
void ScriptIDEWidget::capture_modified_state() {
    if (document()->isModified()) m_edited = true;
}

void ScriptIDEWidget::flush_window_to_buffer() {
    if (!m_virtual_mode || !m_vbuf || !m_win_dirty) return;
    const QString window_text = toPlainText();
    m_vbuf->replace_line_range(m_win_start, m_win_old_lines, window_text);
    m_win_dirty = false;
    // The window is still materialized; if it is flushed again before the next
    // rematerialization the range above must still cover it — refresh the
    // covered-line count from the buffer so the flush stays idempotent.
    const qsizetype total = m_vbuf->line_count();
    m_win_old_lines = std::min(kWindowCapacity, total - m_win_start);
}

void ScriptIDEWidget::materialize_virtual_window(qsizetype abs_anchor_line,
                                                 qsizetype* scroll_px, bool keep_px) {
    if (!m_vbuf) return;
    capture_modified_state();
    flush_window_to_buffer();

    const qsizetype total = m_vbuf->line_count();
    qsizetype desired_start;
    if (total <= kWindowCapacity) {
        desired_start = 0;
    } else if (keep_px && scroll_px) {
        desired_start = abs_anchor_line;
    } else {
        desired_start = abs_anchor_line;
    }
    m_win_start = std::max<qsizetype>(0, std::min<qsizetype>(desired_start, std::max<qsizetype>(total - kWindowCapacity, 0)));

    const QString slice = m_vbuf->slice_lines(m_win_start, kWindowCapacity);
    m_win_old_lines = std::min(kWindowCapacity, total - m_win_start);

    m_materializing = true;
    document()->setUndoRedoEnabled(false);
    setPlainText(slice);
    document()->setUndoRedoEnabled(true);
    document()->clearUndoRedoStacks();
    m_materializing = false;
    m_win_dirty = false;

    if (scroll_px) {
        const qsizetype px = keep_px ? *scroll_px : 0;
        const int maxv = verticalScrollBar()->maximum();
        verticalScrollBar()->setValue(std::min<qsizetype>(px, maxv));
    } else {
        verticalScrollBar()->setValue(0);
    }

    // Re-apply diagnostic squiggles for the visible window.
    apply_virtual_diagnostics();

    // Unsaved-edit marker survives paging.
    document()->setModified(m_edited);
}

bool ScriptIDEWidget::ensure_editable() {
    if (m_mode == LoadMode::Stream && !m_stream_done) {
        finish_stream();   // one bounded synchronous flush on explicit user action
        return true;
    }
    return true;
}

QString ScriptIDEWidget::full_text() {
    if (m_virtual_mode && m_vbuf) {
        flush_window_to_buffer();
        return m_vbuf->text();
    }
    ensure_editable();
    return toPlainText();
}

// Page the virtual window by one whole window (content stays perfectly
// contiguous — the window simply slides to cover the next/previous chunk).
void ScriptIDEWidget::maybe_check_window_guard() {
    if (!m_virtual_mode || !m_vbuf || m_swap_pending) return;
    if (m_mode != LoadMode::Virtual) return;
    const qsizetype total = m_vbuf->line_count();
    if (total <= kWindowCapacity) return;

    const int v = verticalScrollBar()->value();
    const int maxv = verticalScrollBar()->maximum();
    QTextBlock top = firstVisibleBlock();
    const qsizetype rel_top = top.isValid() ? top.blockNumber() : 0;

    bool at_top_edge = rel_top <= 1 && m_win_start > 0;
    bool at_bottom_edge = (maxv <= 0 || v >= maxv - m_block_h * 2) &&
                          (m_win_start + static_cast<qsizetype>(blockCount())) < total;

    if (at_top_edge) {
        m_swap_pending = true;
        materialize_virtual_window(std::max<qsizetype>(0, m_win_start - kWindowCapacity),
                                   nullptr, false);
        m_swap_pending = false;
        return;
    }
    if (at_bottom_edge) {
        m_swap_pending = true;
        materialize_virtual_window(std::min(total - 1, m_win_start + kWindowCapacity),
                                   nullptr, false);
        m_swap_pending = false;
    }
}

void ScriptIDEWidget::on_slider_value_changed(int value) {
    if (!m_virtual_mode || !m_vbuf) return;
    if (m_swap_pending || m_materializing) return;
    if (m_dragging) return;   // native scroll while dragging; map on release
    const int maxv = verticalScrollBar()->maximum();
    if (maxv <= 0) return;
    const qsizetype total = m_vbuf->line_count();
    if (total <= kWindowCapacity) return;

    // Scroll that reaches the window edge pages the window (content stays
    // perfectly contiguous — the window slides by one full capacity).
    bool hit_bottom = value >= maxv - m_block_h * 2 &&
                      (m_win_start + static_cast<qsizetype>(blockCount())) < total;
    bool hit_top = value <= m_block_h * 2 && m_win_start > 0;
    if (hit_top) {
        m_swap_pending = true;
        materialize_virtual_window(std::max<qsizetype>(0, m_win_start - kWindowCapacity),
                                   nullptr, false);
        m_swap_pending = false;
    } else if (hit_bottom) {
        m_swap_pending = true;
        materialize_virtual_window(std::min(total - 1, m_win_start + kWindowCapacity),
                                   nullptr, false);
        m_swap_pending = false;
    }
}

void ScriptIDEWidget::on_slider_pressed() {
    if (m_virtual_mode && m_vbuf) m_dragging = true;
}

void ScriptIDEWidget::on_slider_released() {
    if (!m_dragging) return;
    m_dragging = false;
    if (!m_virtual_mode || !m_vbuf || m_swap_pending) return;
    const int maxv = verticalScrollBar()->maximum();
    if (maxv <= 0) return;
    const qsizetype total = m_vbuf->line_count();
    if (total <= kWindowCapacity) return;

    // Whole-file slider mapping: the thumb fraction now addresses the whole
    // file (the window scrollbar itself only spans one window).
    const int value = verticalScrollBar()->value();
    const qsizetype vp = std::max<qsizetype>(1, viewport()->height() / std::max(m_block_h, 1));
    const double frac = static_cast<double>(value) / static_cast<double>(maxv);
    qsizetype target = static_cast<qsizetype>(frac * static_cast<double>(total - vp));
    target = std::clamp<qsizetype>(target, 0, std::max<qsizetype>(total - 1, 0));
    m_swap_pending = true;
    materialize_virtual_window(target, nullptr, false);
    m_swap_pending = false;
}

void ScriptIDEWidget::jump_to_line(int one_based_line) {
    if (one_based_line < 1) return;
    if (m_virtual_mode && m_vbuf) {
        const qsizetype total = m_vbuf->line_count();
        const qsizetype abs = std::min<qsizetype>(static_cast<qsizetype>(one_based_line) - 1,
                                                  std::max<qsizetype>(total - 1, 0));
        ensure_editable();
        m_swap_pending = true;
        materialize_virtual_window(abs, nullptr, false);
        m_swap_pending = false;
        QTextBlock block = document()->findBlockByNumber(0);
        if (block.isValid()) {
            QTextCursor c(block);
            setTextCursor(c);
            verticalScrollBar()->setValue(0);
        }
        setFocus();
        return;
    }
    ensure_editable();
    QTextBlock block = document()->findBlockByNumber(one_based_line - 1);
    if (block.isValid()) {
        QTextCursor c(block);
        setTextCursor(c);
        centerCursor();
    }
}

void ScriptIDEWidget::apply_virtual_diagnostics() {
    if (!m_highlighter) return;
    std::unordered_map<int, std::vector<ruby::filerift::Diagnostic>> windowed;
    if (m_virtual_mode) {
        const qsizetype doc_lines = static_cast<qsizetype>(document()->blockCount());
        for (const auto& d : m_diagnostics) {
            const qsizetype rel = static_cast<qsizetype>(d.line) - m_win_start;
            if (rel >= 0 && rel < doc_lines) windowed[static_cast<int>(rel)].push_back(d);
        }
        m_highlighter->set_diagnostics_map(std::move(windowed));
    } else {
        m_highlighter->set_diagnostics(m_diagnostics);
    }
}

void ScriptIDEWidget::report_diagnostics_to_highlighter() {
    apply_virtual_diagnostics();
}

// ─── Load entry points used by the document bar / binary actions ────────────
void ScriptIDEWidget::set_current_file(const QString& file_path) {
    m_file_path = file_path;
}

bool ScriptIDEWidget::save_file(const QString& file_path) {
    QString target = file_path.isEmpty() ? m_file_path : file_path;
    if (target.isEmpty()) return false;
    m_last_save_error.clear();

    // ── Compute the FULL output bytes BEFORE touching the target file. The
    // old code opened (truncating) the file first, so any exception from the
    // FileRift re-encoder destroyed the original — a corrupted scene file.
    QByteArray output;
    QString source_text;
    if (m_virtual_mode && m_vbuf) {
        flush_window_to_buffer();
        source_text = m_vbuf->text();
    } else {
        ensure_editable();
        source_text = toPlainText();
    }

    try {
        if (m_filerift_encode_on_save && !m_filerift_type.isEmpty()) {
            QString markup = source_text;
            const QString prefix = "## FileRift decoded Swordigo file type: " + m_filerift_type;
            // Strip EVERY leading FileRift banner line. A legacy double-banner
            // (or one the user pasted) must never reach the re-encoder as data.
            for (;;) {
                if (!markup.startsWith(prefix)) break;
                const int nl = markup.indexOf('\n');
                if (nl < 0) { markup.clear(); break; }
                markup = markup.mid(nl + 1).trimmed();
            }
            const std::string binary = ::filerift::recode_markup(markup.toStdString(), m_filerift_type.toStdString());
            // Validate the re-encode before committing: a malformed markup can
            // silently produce a structurally broken binary, so decode it back
            // and require a non-empty round trip. The target file stays intact
            // on failure. An empty binary is NEVER acceptable — writing it
            // would truncate the scene into a 0-byte corrupt file.
            if (binary.empty()) {
                m_last_save_error = QStringLiteral("encode produced no binary (re-code failed)");
                return false;
            }
            const std::string roundtrip = ::filerift::decode_protobuf(binary, m_filerift_type.toStdString());
            if (roundtrip.empty()) {
                m_last_save_error = QStringLiteral("encode produced an unparseable binary (round-trip check failed)");
                return false;
            }
            output = QByteArray(binary.data(), static_cast<qint64>(binary.size()));
        } else if (!m_filerift_type.isEmpty()) {
            // Binary-format file (scene/scl/swdm/gmesh) in Raw-Text mode: refuse.
            // Writing the markup as plain UTF-8 turns a .scene into text the
            // engine and the 3D viewport cannot parse — the exact corruption
            // that destroyed user scenes. Raw-Text is only safe for plain
            // scripts (empty m_filerift_type).
            m_last_save_error = QStringLiteral(
                "'Save as Raw Text' is on for a binary-format file (%1) — re-enable "
                "'Encode to Binary on Save' to keep the file loadable.")
                .arg(m_filerift_type);
            return false;
        } else {
            output = source_text.toUtf8();
        }
    } catch (const std::exception& e) {
        m_last_save_error = QStringLiteral("FileRift encode failed: %1").arg(QString::fromUtf8(e.what()));
        return false;   // file untouched
    } catch (...) {
        m_last_save_error = QStringLiteral("FileRift encode failed (unknown error)");
        return false;
    }

    // ── Atomic write: temp file + rename via QSaveFile, so a crash or failed
    // write can never leave a truncated scene behind. ──
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        m_last_save_error = QStringLiteral("cannot open %1 for writing").arg(target);
        return false;
    }
    if (file.write(output) != output.size() || !file.commit()) {
        m_last_save_error = QStringLiteral("write failed for %1").arg(target);
        return false;
    }
    m_file_path = target;
    m_edited = false;
    document()->setModified(false);
    return true;
}

bool ScriptIDEWidget::decode_current_binary(const QString& type) {
    if (m_busy_loading) return false;
    QString schema = type.isEmpty() ? m_filerift_type : type;
    if (schema.isEmpty()) {
        QString ext = QFileInfo(m_file_path).suffix().toLower();
        schema = (ext == "scl") ? "scl" : "scene";
    }

    QByteArray bytes;
    if (!m_file_path.isEmpty()) {
        QFile f(m_file_path);
        if (f.open(QIODevice::ReadOnly)) bytes = f.readAll();
    }
    if (bytes.isEmpty()) {
        bytes = toPlainText().toUtf8();
    }
    if (bytes.isEmpty()) return false;

    try {
        QString content;
        if (bytes.left(64).contains("## FileRift decoded")) {
            // The file is already FileRift markup. Decoding text as protobuf
            // would turn it into garbage — adopt the text as-is (banner kept).
            content = QString::fromUtf8(bytes);
        } else {
            const std::string markup = ::filerift::decode_protobuf(
                std::string(bytes.constData(), static_cast<size_t>(bytes.size())),
                schema.toStdString());
            // decode_protobuf already emits the FileRift banner — never prepend
            // a second one (the double-banner that corrupted scenes).
            content = QString::fromUtf8(markup.c_str(),
                                        static_cast<qsizetype>(markup.size()));
            if (!content.startsWith(QLatin1String("## FileRift decoded"))) {
                content = QStringLiteral("## FileRift decoded Swordigo file type: ") + schema +
                          QStringLiteral("\n\n") + content;
            }
        }
        load_buffer(content, m_file_path, schema);
        return true;
    } catch (...) {
        return false;
    }
}

bool ScriptIDEWidget::recode_current_markup(const QString& type) {
    if (m_busy_loading) return false;
    QString schema = type.isEmpty() ? m_filerift_type : type;
    if (schema.isEmpty()) {
        QString ext = QFileInfo(m_file_path).suffix().toLower();
        schema = (ext == "scl") ? "scl" : "scene";
    }

    try {
        QString markup;
        if (m_virtual_mode && m_vbuf) {
            flush_window_to_buffer();
            markup = m_vbuf->text();
        } else {
            ensure_editable();
            markup = toPlainText();
        }
        const QString prefix = "## FileRift decoded Swordigo file type: " + schema;
        // Strip EVERY leading banner line (see save_file).
        for (;;) {
            if (!markup.startsWith(prefix)) break;
            const int nl = markup.indexOf('\n');
            if (nl < 0) { markup.clear(); break; }
            markup = markup.mid(nl + 1).trimmed();
        }
        std::string bin = ::filerift::recode_markup(markup.toStdString(), schema.toStdString());
        if (!m_file_path.isEmpty()) {
            QSaveFile f(m_file_path);
            if (f.open(QIODevice::WriteOnly) &&
                f.write(bin.data(), static_cast<qint64>(bin.size())) ==
                    static_cast<qint64>(bin.size()) &&
                f.commit()) {
                m_edited = false;
                document()->setModified(false);
                return true;
            }
            m_last_save_error = QStringLiteral("write failed for %1").arg(m_file_path);
        }
    } catch (...) {
        return false;
    }
    return false;
}

bool ScriptIDEWidget::has_lua_chunks() const {
    for (bool flag : m_analysis_result.chunk_start) {
        if (flag) return true;
    }
    return false;
}

// ─── Editor ergonomics: zoom, auto-indent, auto-pairing ────────────────────
int ScriptIDEWidget::font_size() const {
    const QFont f = font();
    return f.pointSize() > 0 ? f.pointSize() : f.pixelSize();
}

void ScriptIDEWidget::zoom_font(int delta) {
    const int base = font_size();
    const int target = std::clamp(base + delta, 8, 40);
    if (target == base) return;
    QFont f = font();
    if (f.pointSize() > 0) f.setPointSize(target);
    else f.setPixelSize(target);
    setFont(f);
    updateLineNumberAreaWidth(0);
    if (m_virtual_mode) m_block_h = fontMetrics().height();
}

void ScriptIDEWidget::wheelEvent(QWheelEvent* event) {
    // Ctrl+wheel zooms like a real IDE; plain wheel keeps scrolling.
    if (event->modifiers() & Qt::ControlModifier) {
        const int steps = event->angleDelta().y() / 120;
        if (steps != 0) { zoom_font(steps); event->accept(); return; }
    }
    QPlainTextEdit::wheelEvent(event);
    maybe_check_window_guard();
}

static QString indent_of(const QString& line) {
    QString out;
    for (const QChar c : line) {
        if (c == ' ') out += QLatin1Char(' ');
        else if (c == '\t') out += QStringLiteral("    ");
        else break;
    }
    return out;
}

QString ScriptIDEWidget::textUnderCursor() const {
    QTextCursor tc = textCursor();
    tc.select(QTextCursor::WordUnderCursor);
    return tc.selectedText();
}

void ScriptIDEWidget::insertCompletion(const QString& completion) {
    if (m_completer->widget() != this) return;
    QTextCursor tc = textCursor();
    QString prefix = m_completer->completionPrefix();
    tc.select(QTextCursor::WordUnderCursor);
    tc.removeSelectedText();
    tc.insertText(completion);
    setTextCursor(tc);
}

void ScriptIDEWidget::updateCompleterWords() {
    if (!m_completer_model) return;
    if (m_mode != LoadMode::Sync) return;             // huge docs: no popup spam
    if (document()->blockCount() > kLSMaxLines) return;
    QTextCursor tc = textCursor();
    int line = tc.blockNumber();
    int col = tc.positionInBlock();

    auto completions = ruby::filerift::FileRiftLS::instance().get_completions(
        m_lines_cache, m_analysis_result, line, col, textUnderCursor().toStdString());

    QStringList word_list;
    word_list.reserve(static_cast<qsizetype>(completions.size()));
    for (const auto& item : completions) {
        word_list.append(QString::fromStdString(item.label));
    }
    m_completer_model->setStringList(word_list);
}

void ScriptIDEWidget::keyPressEvent(QKeyEvent* event) {
    if (m_busy_loading && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
                           event->text().size() == 1 && event->text().at(0).isPrint())) {
        // Typing while a huge file is still streaming: finish the load first.
        finish_stream();
    }

    if (m_virtual_mode && event->key() == Qt::Key_Home && (event->modifiers() & Qt::ControlModifier)) {
        jump_to_line(1);
        return;
    }
    if (m_virtual_mode && event->key() == Qt::Key_End && (event->modifiers() & Qt::ControlModifier)) {
        if (m_vbuf) jump_to_line(static_cast<int>(std::min<qsizetype>(m_vbuf->line_count(), INT_MAX)));
        return;
    }

    if (m_completer && m_completer->popup()->isVisible()) {
        switch (event->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            event->ignore();
            return;
        default:
            break;
        }
    }

    const Qt::KeyboardModifiers mods = event->modifiers();

    // Font zoom: Ctrl + / − / 0 (also Ctrl+=).
    if (mods & Qt::ControlModifier) {
        const int key = event->key();
        if (key == Qt::Key_Equal || key == Qt::Key_Plus) { zoom_font(1); event->accept(); return; }
        if (key == Qt::Key_Minus) { zoom_font(-1); event->accept(); return; }
        if (key == Qt::Key_0) { QFont f = font(); if (f.pointSize()>0) f.setPointSize(11); else f.setPixelSize(14); setFont(f); updateLineNumberAreaWidth(0); event->accept(); return; }
    }

    QTextCursor cursor = textCursor();

    // ── Tab / Shift+Tab: indent or outdent selection ──
    if (event->key() == Qt::Key_Tab) {
        event->accept();
        cursor.beginEditBlock();
        if (cursor.hasSelection()) {
            const int start_block = document()->findBlock(cursor.selectionStart()).blockNumber();
            const int end_block = document()->findBlock(cursor.selectionEnd()).blockNumber();
            for (int b = start_block; b <= end_block; ++b) {
                QTextBlock block = document()->findBlockByNumber(b);
                QTextCursor line_cursor(block);
                line_cursor.setPosition(block.position());
                if (mods & Qt::ShiftModifier) {
                    QTextCursor scan(block);
                    int removed = 0;
                    while (removed < 4 && scan.position() < block.position() + block.length() - 1) {
                        const QChar ch = document()->characterAt(scan.position());
                        if (ch != QLatin1Char(' ')) break;
                        ++removed; scan.movePosition(QTextCursor::NextCharacter);
                    }
                    scan.setPosition(block.position());
                    scan.setPosition(block.position() + removed, QTextCursor::KeepAnchor);
                    scan.removeSelectedText();
                } else {
                    line_cursor.insertText(QStringLiteral("    "));
                }
            }
        } else if (mods & Qt::ShiftModifier) {
            QTextCursor scan(cursor); scan.movePosition(QTextCursor::StartOfBlock);
            int removed = 0;
            while (removed < 4) {
                const QChar ch = document()->characterAt(scan.position());
                if (ch != QLatin1Char(' ')) break;
                ++removed; scan.movePosition(QTextCursor::NextCharacter);
            }
            if (removed) {
                const int line_start = cursor.block().position();
                QTextCursor del(cursor);
                del.setPosition(line_start);
                del.setPosition(line_start + removed, QTextCursor::KeepAnchor);
                del.removeSelectedText();
            }
        } else {
            const int col = cursor.positionInBlock();
            cursor.insertText(QString(4 - (col % 4), QLatin1Char(' ')));
        }
        cursor.endEditBlock();
        return;
    }

    // ── Enter: auto-indent the fresh line ──
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        event->accept();
        const QString line = cursor.block().text();
        QString indent = indent_of(line);
        const QString trimmed = line.trimmed();
        const bool open_ended =
            trimmed.endsWith(QLatin1Char('{')) || trimmed.endsWith(QLatin1Char('(')) ||
            trimmed.endsWith(QLatin1Char('[')) || trimmed.endsWith(QStringLiteral("then")) ||
            trimmed.endsWith(QStringLiteral("do")) || trimmed.endsWith(QStringLiteral("else")) ||
            trimmed.endsWith(QStringLiteral("begin")) || trimmed.endsWith(QStringLiteral(":"));
        const bool closes_block = trimmed.startsWith(QLatin1Char('}')) ||
                                  trimmed.startsWith(QStringLiteral("end"));
        QString pad = indent;
        if (closes_block) pad = pad.size() >= 4 ? pad.left(pad.size() - 4) : QString();
        insertPlainText(QStringLiteral("\n") + pad + (open_ended ? QStringLiteral("    ") : QString()));
        return;
    }

    // ── Auto-pairing delimiters ("auto framing")
    const QString text = event->text();
    if (text.size() == 1 && !(mods & (Qt::ControlModifier | Qt::AltModifier))) {
        const QChar ch = text.at(0);
        const QChar open_of[3] = {QLatin1Char('('), QLatin1Char('['), QLatin1Char('{')};
        const QChar close_of[3] = {QLatin1Char(')'), QLatin1Char(']'), QLatin1Char('}')};
        for (int i = 0; i < 3; ++i) {
            if (ch == open_of[i]) {
                event->accept();
                if (cursor.hasSelection()) {
                    const QString selected = cursor.selectedText();
                    cursor.insertText(QString(open_of[i]) + selected + QString(close_of[i]));
                    int sel_start = cursor.selectionStart();
                    QTextCursor sel(document());
                    sel.setPosition(sel_start + 1);
                    sel.setPosition(sel_start + 1 + selected.size(), QTextCursor::KeepAnchor);
                    setTextCursor(sel);
                } else {
                    const int pos = cursor.position();
                    insertPlainText(QString(open_of[i]) + QString(close_of[i]));
                    QTextCursor moved(document());
                    moved.setPosition(pos + 1);
                    setTextCursor(moved);
                }
                return;
            }
            if (ch == close_of[i]) {
                const int pos = cursor.position();
                if (document()->characterAt(pos) == ch) {
                    event->accept();
                    QTextCursor moved(document());
                    moved.setPosition(pos + 1);
                    setTextCursor(moved);
                    return;
                }
            }
        }
        if (ch == QLatin1Char('\'') || ch == QLatin1Char('"') || ch == QLatin1Char('`')) {
            event->accept();
            const int pos = cursor.position();
            if (cursor.hasSelection()) {
                const QString selected = cursor.selectedText();
                cursor.insertText(QString(ch) + selected + QString(ch));
                int sel_start = cursor.selectionStart();
                QTextCursor sel(document());
                sel.setPosition(sel_start + 1);
                sel.setPosition(sel_start + 1 + selected.size(), QTextCursor::KeepAnchor);
                setTextCursor(sel);
            } else if (document()->characterAt(pos) == ch) {
                QTextCursor moved(document());
                moved.setPosition(pos + 1);
                setTextCursor(moved);
            } else {
                insertPlainText(QString(ch) + QString(ch));
                QTextCursor moved(document());
                moved.setPosition(pos + 1);
                setTextCursor(moved);
            }
            return;
        }
    }

    QPlainTextEdit::keyPressEvent(event);

    // ── Completer trigger after character input ──
    const bool isShortcut = (mods & Qt::ControlModifier) && event->key() == Qt::Key_Space;
    const bool isTyping = !text.isEmpty() && (text[0].isLetterOrNumber() || text[0] == '_' || text[0] == '$');

    if (isShortcut || (isTyping && m_completer)) {
        updateCompleterWords();
        QString prefix = textUnderCursor();
        if (isShortcut || prefix.length() >= 2) {
            m_completer->setCompletionPrefix(prefix);
            m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
            QRect cr = cursorRect();
            cr.setWidth(m_completer->popup()->sizeHintForColumn(0) +
                        m_completer->popup()->verticalScrollBar()->sizeHint().width() + 24);
            m_completer->complete(cr);
        } else {
            m_completer->popup()->hide();
        }
    } else if (m_completer && m_completer->popup()->isVisible() && !isTyping) {
        m_completer->popup()->hide();
    }

    maybe_check_window_guard();
}

bool ScriptIDEWidget::event(QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        auto* help_event = static_cast<QHelpEvent*>(event);
        QTextCursor cursor = cursorForPosition(help_event->pos());
        cursor.select(QTextCursor::WordUnderCursor);
        QString word = cursor.selectedText().trimmed();
        int line_idx = cursor.blockNumber();
        const qsizetype abs_line = (m_virtual_mode ? m_win_start : 0) + line_idx;

        QStringList tooltip_parts;

        // 1. Diagnostics on this line (absolute line numbers stored)
        for (const auto& diag : m_diagnostics) {
            if (static_cast<qsizetype>(diag.line) == abs_line) {
                QString color = (diag.severity == ruby::filerift::Diagnostic::Error) ? "#e06c75" : "#e5c07b";
                QString label = (diag.severity == ruby::filerift::Diagnostic::Error) ? "Error" : "Warning";
                tooltip_parts.append(QString("<span style='color:%1; font-weight:bold;'>[%2]</span> %3")
                                     .arg(color, label, QString::fromStdString(diag.message).toHtmlEscaped()));
            }
        }

        // 2. FileRift Language Server & Engine Knowledge Base Documentation
        if (!word.isEmpty() && m_mode == LoadMode::Sync &&
            static_cast<qsizetype>(line_idx) < static_cast<qsizetype>(m_lines_cache.size())) {
            std::string doc = ruby::filerift::FileRiftLS::instance().get_hover_documentation(
                m_lines_cache, m_analysis_result, line_idx, word.toStdString());
            if (!doc.empty()) {
                tooltip_parts.append(QString::fromStdString(doc));
            }
        }

        if (!tooltip_parts.isEmpty()) {
            QString html = QString("<div style='background-color:#1e2227; color:#abb2bf; padding:10px 14px; border:1px solid #3e4451; border-radius:8px; font-family:sans-serif; font-size:12px; line-height:1.45; max-width:500px;'>%1</div>")
                .arg(tooltip_parts.join("<hr style='border:0; border-top:1px solid #3e4451; margin:8px 0;'>"));
            QToolTip::showText(help_event->globalPos(), html, this);
            return true;
        } else {
            QToolTip::hideText();
        }
    }
    return QPlainTextEdit::event(event);
}

void ScriptIDEWidget::showQuickDoc() {
    QTextCursor tc = textCursor();
    tc.select(QTextCursor::WordUnderCursor);
    QString word = tc.selectedText().trimmed();
    int line_idx = tc.blockNumber();
    if (word.isEmpty()) return;
    if (m_mode != LoadMode::Sync ||
        line_idx < 0 || line_idx >= static_cast<int>(m_lines_cache.size())) return;

    std::string doc = ruby::filerift::FileRiftLS::instance().get_hover_documentation(
        m_lines_cache, m_analysis_result, line_idx, word.toStdString());
    if (doc.empty()) return;

    QRect cr = cursorRect();
    QPoint global_pos = mapToGlobal(cr.bottomRight());
    QString html = QString("<div style='background-color:#1e2227; color:#abb2bf; padding:10px 14px; border:1px solid #3e4451; border-radius:8px; font-family:sans-serif; font-size:12px; line-height:1.45; max-width:500px;'>%1</div>")
        .arg(QString::fromStdString(doc));
    QToolTip::showText(global_pos, html, this, QRect(), 10000);
}

// ─── Find bar (RAM-text search) ─────────────────────────────────────────────
void ScriptIDEWidget::toggle_find_bar() {
    if (!m_find_bar) return;
    if (m_find_bar->isVisible()) {
        m_find_bar->hide();
        setFocus();
        return;
    }
    if (m_busy_loading) finish_stream();
    // pre-fill with the word under the cursor
    QTextCursor tc = textCursor();
    tc.select(QTextCursor::WordUnderCursor);
    const QString sel = tc.selectedText();
    if (!sel.isEmpty() && m_find_edit->text().isEmpty()) m_find_edit->setText(sel);
    m_find_bar->show();
    m_find_bar->raise();
    layout_find_bar();
    m_find_edit->setFocus();
    m_find_edit->selectAll();
}

static QString find_source_text(ScriptIDEWidget* w) {
    // One whole-text copy made on the GUI thread (RAM; COW cheap).
    return w->full_text();
}

void ScriptIDEWidget::schedule_find_count() {
    const QString needle = m_find_edit->text();
    const Qt::CaseSensitivity cs = m_find_case->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    m_find_last_needle = needle;
    m_find_last_cs = cs;
    if (needle.isEmpty()) {
        m_find_last_count = 0;
        m_find_count->setText("");
        return;
    }
    if (m_find_counting) return;   // a worker is already running
    m_find_count->setText("…");
    const qsizetype total_chars = full_text().size();
    if (total_chars <= kFindCountSyncMaxChars) {
        const QString src = find_source_text(this);
        m_find_last_count = src.count(needle, cs);
        m_find_count->setText(QString("0 / %1").arg(m_find_last_count));
        return;
    }
    // huge: count off the GUI thread
    m_find_counting = true;
    const quint64 gen = m_gen;
    const QString src = find_source_text(this);   // single shared copy
    auto worker_lambda = [this, gen, src, needle, cs]() {
        const qsizetype count = src.count(needle, cs);
        auto pack = std::make_shared<FindPack>();
        pack->gen = gen;
        pack->count = count;
        {
            std::lock_guard<std::mutex> lk(m_result_mutex);
            m_find_pack = std::move(pack);
        }
    };
    spawn_worker(std::move(worker_lambda));
}

void ScriptIDEWidget::find_next(bool backwards) {
    if (m_busy_loading) finish_stream();
    const QString needle = m_find_edit->text();
    if (needle.isEmpty()) return;
    const Qt::CaseSensitivity cs = m_find_case->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;

    QString src;
    qsizetype from = -1;
    QTextCursor cur = textCursor();
    if (cur.hasSelection() && m_find_last_needle == needle) {
        from = cur.selectionStart();
        if (backwards) from = cur.selectionStart() - 1;
        else from = cur.selectionEnd();
    } else {
        const int pos = cur.position();
        from = backwards ? pos - 1 : pos;
    }
    if (from < 0) from = 0;

    if (m_virtual_mode && m_vbuf) {
        flush_window_to_buffer();
        src = m_vbuf->text();
    } else {
        src = toPlainText();   // whole doc (full mode); moderate cost
    }
    if (src.isEmpty()) return;

    qsizetype hit = backwards
        ? src.lastIndexOf(needle, from, cs)
        : src.indexOf(needle, from, cs);
    if (hit < 0) {
        hit = backwards
            ? src.lastIndexOf(needle, cs)
            : src.indexOf(needle, 0, cs);
    }
    if (hit < 0) {
        m_find_count->setText(QString("0 / %1").arg(m_find_last_count));
        return;
    }

    // Convert character hit into a line and select it.
    const qsizetype line = m_virtual_mode
        ? m_vbuf->line_at(hit)
        : static_cast<qsizetype>(document()->findBlock(hit).blockNumber());
    jump_to_line(static_cast<int>(line + 1));
    // Re-find inside the (possibly repaged) document: recompute relative pos.
    QTextCursor c = textCursor();
    c.setPosition(0);
    qsizetype rel = hit;
    if (m_virtual_mode) rel = hit - m_vbuf->line_start(line);
    if (!m_virtual_mode) {
        c.setPosition(0);
    }
    (void)rel;
    // move to the match using document search from the window's line start
    if (m_virtual_mode) {
        QTextBlock b = document()->findBlockByNumber(static_cast<int>(line - m_win_start));
        QTextCursor search_cursor(b);
        QTextDocument::FindFlags flags = QTextDocument::FindCaseSensitively;
        if (cs == Qt::CaseInsensitive) flags = {};
        QTextCursor found = document()->find(needle, search_cursor, flags);
        if (!found.isNull()) {
            setTextCursor(found);
            centerCursor();
        }
    } else {
        QTextDocument::FindFlags flags = QTextDocument::FindCaseSensitively;
        if (cs == Qt::CaseInsensitive) flags = {};
        QTextCursor found = document()->find(needle, textCursor(), flags);
        if (found.isNull()) found = document()->find(needle, 0, flags);
        if (!found.isNull()) {
            setTextCursor(found);
            centerCursor();
        }
    }
    m_find_last_needle = needle;
}

} // namespace ruby::editor
