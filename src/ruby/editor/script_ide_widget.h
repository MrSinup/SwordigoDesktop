#pragma once
// ============================================================================
// script_ide_widget.h — Virtualized Script IDE & Code Editor
//   High-performance code editing with line numbers, FileRift TextMate-fidelity
//   syntax highlighting, embedded Lua chunks ($...$end), semantic diagnostics,
//   hover documentation, autocompletion, and binary Protobuf encode/decode.
//
//   PERFORMANCE CHARTER (see src/ruby/RUBY_PERF_CHARTER.md):
//     • Main thread never decodes / analyzes / indexes whole files.
//     • Files load like VS Code: first screen paints instantly, the rest is
//       populated in bounded background time-slices.
//     • Multi-lakh-line files are VIRTUALIZED: the full text lives once in RAM
//       (VirtualTextBuffer) and only a visible window is materialized in the
//       QTextDocument; scrolling pages the window. Find scans the RAM text.
//     • Semantic analysis runs in a background worker (generation-guarded) and
//       results are cached per-session + persisted under ~/.ruby.
// ============================================================================

#include <QWidget>
#include <QPlainTextEdit>
#include <QCompleter>
#include <QStringListModel>
#include <QTimer>
#include <QToolTip>
#include <QHelpEvent>
#include <QHash>
#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include "perf_charter.h"
#include "virtual_text_buffer.h"
#include "filerift_highlighter.h"
#include "filerift_analyzer.h"
#include "filerift_schema.h"

class QLineEdit;
class QCheckBox;
class QToolButton;
class QLabel;

namespace ruby::editor {

// ─── Line Number Area Widget ───────────────────────────────────────────────
class ScriptIDEWidget;

class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(ScriptIDEWidget* editor);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    ScriptIDEWidget* m_editor = nullptr;
};

// ─── High-Performance Code Editor Widget ────────────────────────────────────
class ScriptIDEWidget : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit ScriptIDEWidget(QWidget* parent = nullptr);
    ~ScriptIDEWidget() override;

    void lineNumberAreaPaintEvent(QPaintEvent* event);
    int lineNumberAreaWidth();

    // When a FileRift schema is supplied, binary protobuf is presented as
    // editable markup and recompiled to the original binary format on save.
    bool load_file(const QString& file_path, const QString& filerift_type = QString());
    bool save_file(const QString& file_path = QString());
    // Human-readable reason for the most recent failed save (empty on success).
    const QString& last_save_error() const { return m_last_save_error; }

    // Rehydrate an in-memory buffer without touching disk (used by the
    // IDE-style document bar so unsaved edits survive tab hops).
    void load_buffer(const QString& text, const QString& file_path,
                     const QString& filerift_type = QString());
    // Re-point the editor at an already-loaded file (used before saving).
    void set_current_file(const QString& file_path);

    const QString& current_file_path() const { return m_file_path; }
    const QString& current_filerift_type() const { return m_filerift_type; }

    void set_filerift_encode_on_save(bool encode) { m_filerift_encode_on_save = encode; }
    bool filerift_encode_on_save() const { return m_filerift_encode_on_save; }

    // Editor ergonomics: zoom the font (Ctrl+=/-/0 or Ctrl+wheel).
    void zoom_font(int delta);
    int font_size() const;

    // FileRift Analysis & Diagnostics
    void trigger_analysis();
    const ruby::filerift::AnalysisResult& analysis_result() const { return m_analysis_result; }
    const std::vector<ruby::filerift::Diagnostic>& diagnostics() const { return m_diagnostics; }

    // Binary / Markup actions
    bool decode_current_binary(const QString& type = QString());
    bool recode_current_markup(const QString& type = QString());

    // Embedded Lua chunks
    bool has_lua_chunks() const;

    // ── Charter API ─────────────────────────────────────────────────────────
    // Full file text exactly as the buffer/save layer needs it (the RAM
    // canonical text in virtual mode — flushing any pending window edits — or
    // the whole document text otherwise). Never returns a partial window.
    QString full_text();
    // True while a big file is still being populated / decoded off-thread.
    bool load_in_progress() const { return m_busy_loading; }
    bool is_virtual_mode() const { return m_virtual_mode; }

    // Jump to absolute line (1-based); used by Go-To-Line and Find.
    void jump_to_line(int one_based_line);
    void toggle_find_bar();

    // Mode decision (kept small + overridable so tests/UX can tune).
    static qsizetype virtual_min_lines();
    static qsizetype stream_max_lines();

signals:
    void analysisCompleted(int error_count, int warning_count);
    void fileFullyLoaded();          // materialization finished (stream/virtual)
    void documentLoaded(const QString& path);   // content adopted (any load mode)

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    bool event(QEvent* event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void highlightCurrentLine();
    void updateLineNumberArea(const QRect& rect, int dy);
    void onDocumentTextChanged();
    void run_filerift_analysis();
    void insertCompletion(const QString& completion);
    void showQuickDoc();

    // Large-file pipeline
    void stream_more();
    void start_background_analysis();
    void maybe_check_window_guard();
    void on_slider_value_changed(int value);
    void on_slider_pressed();
    void on_slider_released();
    void poll_worker_results();

private:
    QString textUnderCursor() const;
    void updateCompleterWords();

    // ── loading pipeline helpers ────────────────────────────────────────────
    void reset_editor_state();
    void adopt_loaded_text(QString content, const QString& path,
                           const QString& filerift_type, bool was_decoded);
    qsizetype estimate_line_count(const QString& text) const;
    void finish_stream();
    bool ensure_editable();
    void flush_window_to_buffer();
    void materialize_virtual_window(qsizetype abs_anchor_line,
                                    qsizetype* scroll_px, bool keep_px);
    void apply_virtual_diagnostics();
    void cancel_pending_workers();

    // analysis/text used by tooltips & status
    QString text_for_analysis();                 // full text snapshot (GUI-safe)
    std::string file_ext_or_type() const;
    void report_diagnostics_to_highlighter();
    void capture_modified_state();               // merge doc modified -> m_edited

    // Run heavy work off the GUI thread; the destructor waits for stragglers.
    void spawn_worker(std::function<void()> fn);
    void notify_worker_done();
    void deliver_decode_result();
    void deliver_analysis_result();
    void deliver_find_result();
    void poke_poll_timer();

    // ── find bar ────────────────────────────────────────────────────────────
    void layout_find_bar();
    void find_next(bool backwards);
    void schedule_find_count();

    // state
    QWidget* m_line_number_area = nullptr;
    ruby::filerift::FileRiftHighlighter* m_highlighter = nullptr;
    QTimer* m_analysis_timer = nullptr;
    QTimer* m_stream_timer = nullptr;
    QCompleter* m_completer = nullptr;
    QStringListModel* m_completer_model = nullptr;

    ruby::filerift::AnalysisResult m_analysis_result;
    std::vector<ruby::filerift::Diagnostic> m_diagnostics;
    std::vector<std::string> m_lines_cache;   // bounded: only for analysable docs

    QString m_file_path;
    QString m_filerift_type;
    bool m_filerift_encode_on_save = true;

    // ── charter / large-file state ──────────────────────────────────────────
    enum class LoadMode { Idle, Sync, Stream, Virtual } m_mode = LoadMode::Idle;
    quint64 m_gen = 0;                 // generation: invalidates stale workers
    bool m_busy_loading = false;       // decode/stream in flight
    bool m_analysis_running = false;   // one background analysis at a time

    std::unique_ptr<VirtualTextBuffer> m_vbuf;   // RAM home (virtual mode)
    qsizetype m_win_start = 0;                   // absolute line of doc[0]
    qsizetype m_win_old_lines = 0;               // buffer lines covered by window
    bool m_win_dirty = false;                    // window edited since materialization
    bool m_virtual_mode = false;
    bool m_materializing = false;                // setPlainText in progress (own code)
    bool m_edited = false;                       // unsaved edits (survive window pages)
    QString m_last_save_error;                   // why the last save_file() failed
    int m_block_h = 0;                           // px per block (monospaced)
    bool m_swap_pending = false;
    bool m_dragging = false;

    // streaming progress
    QString m_pending_text;                      // remaining text to stream in
    qsizetype m_stream_cursor = 0;               // char offset into pending
    bool m_stream_done = false;

    // background worker bookkeeping (decode / analysis / find-count)
    std::mutex m_worker_mutex;
    std::condition_variable m_worker_cv;
    int m_worker_count = 0;

    // Worker -> GUI result hand-off. Cross-thread queued meta-calls proved
    // unreliable under some platforms, so results are parked under a mutex and
    // drained by a GUI timer (charter: heavy work off the main thread, cheap
    // dispatch on it).
    struct DecodeResult {
        quint64 gen = 0;
        QString path, type, content;
        bool ok = false;
    };
    struct AnalysisPack {
        quint64 gen = 0;
        std::shared_ptr<ruby::filerift::AnalysisResult> res;
        std::shared_ptr<std::vector<ruby::filerift::Diagnostic>> diags;
        int errors = 0, warnings = 0;
    };
    struct FindPack {
        quint64 gen = 0;
        qsizetype count = 0;
    };
    std::mutex m_result_mutex;
    std::shared_ptr<DecodeResult> m_decode_result;
    std::shared_ptr<AnalysisPack> m_analysis_pack;
    std::shared_ptr<FindPack> m_find_pack;
    QTimer* m_poll_timer = nullptr;

    // find state
    QWidget* m_find_bar = nullptr;
    QLineEdit* m_find_edit = nullptr;
    QLabel* m_find_count = nullptr;
    QCheckBox* m_find_case = nullptr;
    QToolButton* m_find_prev = nullptr;
    QToolButton* m_find_next = nullptr;
    QToolButton* m_find_close = nullptr;
    QTimer* m_find_debounce = nullptr;
    qsizetype m_find_last_count = 0;
    QString m_find_last_needle;
    Qt::CaseSensitivity m_find_last_cs = Qt::CaseSensitive;
    bool m_find_counting = false;
};

} // namespace ruby::editor
