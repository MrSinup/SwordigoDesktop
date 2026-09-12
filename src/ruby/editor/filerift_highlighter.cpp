// ============================================================================
// filerift_highlighter.cpp — Implementation of FileRift Syntax Highlighter
// ============================================================================

#include "filerift_highlighter.h"
#include <QColor>
#include <QFont>

namespace ruby::filerift {

enum BlockState {
    StateNormal = 0,
    StateInsideLuaChunk = 1
};

FileRiftHighlighter::FileRiftHighlighter(QTextDocument* parent) : QSyntaxHighlighter(parent) {
    // ─── Formats Setup ───────────────────────────────────────────────────────
    // Dark Studio Theme Color Palette
    m_comment_format.setForeground(QColor("#5c6370"));
    m_comment_format.setFontItalic(true);

    m_tag_format.setForeground(QColor("#e5c07b")); // Golden yellow for structural tags: Object, Component
    m_tag_format.setFontWeight(QFont::Bold);

    m_property_format.setForeground(QColor("#61afef")); // Soft cyan for property keys
    m_property_format.setFontWeight(QFont::Medium);

    m_string_format.setForeground(QColor("#98c379")); // Emerald green for strings
    m_number_format.setForeground(QColor("#d19a66")); // Orange for floats & integers
    m_boolean_format.setForeground(QColor("#56b6c2")); // Cyan bold for booleans
    m_boolean_format.setFontWeight(QFont::Bold);

    m_macro_format.setForeground(QColor("#c678dd")); // Purple for $source[...] macros
    m_macro_format.setFontWeight(QFont::Bold);

    m_chunk_delimiter_format.setForeground(QColor("#e06c75")); // Crimson for $ and $end
    m_chunk_delimiter_format.setFontWeight(QFont::Bold);

    // Lua Formats
    m_lua_keyword_format.setForeground(QColor("#c678dd")); // Bright purple for keywords (local, function, if, end)
    m_lua_keyword_format.setFontWeight(QFont::Bold);

    m_lua_builtin_format.setForeground(QColor("#4ec9b0")); // Bright turquoise for Engine Modules & Globals
    m_lua_builtin_format.setFontWeight(QFont::DemiBold);

    m_lua_method_format.setForeground(QColor("#61afef"));  // Vibrant cyan for methods and function calls
    m_lua_self_format.setForeground(QColor("#e5c07b"));    // Golden amber for 'self' parameter
    m_lua_self_format.setFontItalic(true);

    m_lua_comment_format.setForeground(QColor("#5c6370")); // Gray italic
    m_lua_comment_format.setFontItalic(true);
    m_lua_string_format.setForeground(QColor("#98c379")); // Emerald green string
    m_lua_number_format.setForeground(QColor("#d19a66")); // Warm orange number

    // ─── FileRift Regex Rules ────────────────────────────────────────────────
    // Tags: Object {, Component {, Bounds {
    Rule tag_rule;
    tag_rule.pattern = QRegularExpression(QStringLiteral(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(\{))"));
    tag_rule.format = m_tag_format;
    tag_rule.capture_group = 1;
    m_filerift_rules.push_back(tag_rule);

    // Key-Value with colon: Identifier: '...'
    Rule kv_rule;
    kv_rule.pattern = QRegularExpression(QStringLiteral(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(:))"));
    kv_rule.format = m_property_format;
    kv_rule.capture_group = 1;
    m_filerift_rules.push_back(kv_rule);

    // Key-Value without colon: Key "..." or Key 123
    Rule kv_nocolon_rule;
    kv_nocolon_rule.pattern = QRegularExpression(QStringLiteral(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s+(?!\{))"));
    kv_nocolon_rule.format = m_property_format;
    kv_nocolon_rule.capture_group = 1;
    m_filerift_rules.push_back(kv_nocolon_rule);

    // Strings: single and double quoted
    Rule str_single;
    str_single.pattern = QRegularExpression(QStringLiteral(R"('([^'\\]|\\.)*')"));
    str_single.format = m_string_format;
    m_filerift_rules.push_back(str_single);

    Rule str_double;
    str_double.pattern = QRegularExpression(QStringLiteral(R"("[^"\\]*(\\.[^"\\]*)*")"));
    str_double.format = m_string_format;
    m_filerift_rules.push_back(str_double);

    // Numeric constants
    Rule num_float;
    num_float.pattern = QRegularExpression(QStringLiteral(R"(-?\b[0-9]+\.[0-9]+\b)"));
    num_float.format = m_number_format;
    m_filerift_rules.push_back(num_float);

    Rule num_int;
    num_int.pattern = QRegularExpression(QStringLiteral(R"(-?\b[0-9]+\b)"));
    num_int.format = m_number_format;
    m_filerift_rules.push_back(num_int);

    // Booleans
    Rule bool_rule;
    bool_rule.pattern = QRegularExpression(QStringLiteral(R"(\b(true|false)\b)"));
    bool_rule.format = m_boolean_format;
    m_filerift_rules.push_back(bool_rule);

    // Macros: $source[file.lua]
    Rule macro_rule;
    macro_rule.pattern = QRegularExpression(QStringLiteral(R"(\$(\w+)\[([^\]]*)\])"));
    macro_rule.format = m_macro_format;
    m_filerift_rules.push_back(macro_rule);

    // Comments: # ...
    Rule comment_rule;
    comment_rule.pattern = QRegularExpression(QStringLiteral(R"(#.*$)"));
    comment_rule.format = m_comment_format;
    m_filerift_rules.push_back(comment_rule);

    // ─── Lua Rules ──────────────────────────────────────────────────────────
    // 1. Comments
    Rule lua_comment;
    lua_comment.pattern = QRegularExpression(QStringLiteral(R"(--.*$)"));
    lua_comment.format = m_lua_comment_format;
    m_lua_rules.push_back(lua_comment);

    // 2. Strings
    Rule lua_str1;
    lua_str1.pattern = QRegularExpression(QStringLiteral(R"('([^'\\]|\\.)*')"));
    lua_str1.format = m_lua_string_format;
    m_lua_rules.push_back(lua_str1);

    Rule lua_str2;
    lua_str2.pattern = QRegularExpression(QStringLiteral(R"("[^"\\]*(\\.[^"\\]*)*")"));
    lua_str2.format = m_lua_string_format;
    m_lua_rules.push_back(lua_str2);

    // 3. Numbers
    Rule lua_num;
    lua_num.pattern = QRegularExpression(QStringLiteral(R"(-?\b[0-9]+(\.[0-9]+)?\b)"));
    lua_num.format = m_lua_number_format;
    m_lua_rules.push_back(lua_num);

    // 4. Keywords
    const QString lua_keywords[] = {
        QStringLiteral("\\blocal\\b"), QStringLiteral("\\bfunction\\b"),
        QStringLiteral("\\bend\\b"), QStringLiteral("\\bif\\b"),
        QStringLiteral("\\bthen\\b"), QStringLiteral("\\belse\\b"),
        QStringLiteral("\\belseif\\b"), QStringLiteral("\\breturn\\b"),
        QStringLiteral("\\bfor\\b"), QStringLiteral("\\bwhile\\b"),
        QStringLiteral("\\bdo\\b"), QStringLiteral("\\btrue\\b"),
        QStringLiteral("\\bfalse\\b"), QStringLiteral("\\bnil\\b"),
        QStringLiteral("\\bbreak\\b"), QStringLiteral("\\bin\\b"),
        QStringLiteral("\\bnot\\b"), QStringLiteral("\\band\\b"),
        QStringLiteral("\\bor\\b"), QStringLiteral("\\brepeat\\b"),
        QStringLiteral("\\buntil\\b")
    };
    for (const auto& kw : lua_keywords) {
        Rule r; r.pattern = QRegularExpression(kw); r.format = m_lua_keyword_format;
        m_lua_rules.push_back(r);
    }

    // 5. 'self' parameter
    Rule self_rule;
    self_rule.pattern = QRegularExpression(QStringLiteral(R"(\bself\b)"));
    self_rule.format = m_lua_self_format;
    m_lua_rules.push_back(self_rule);

    // 6. Builtin Engine Modules & Standard Lua Tables
    const QString lua_builtins[] = {
        QStringLiteral("\\bScene\\b"), QStringLiteral("\\bCamera\\b"),
        QStringLiteral("\\bEntity\\b"), QStringLiteral("\\bGame\\b"),
        QStringLiteral("\\bSound\\b"), QStringLiteral("\\bMusic\\b"),
        QStringLiteral("\\bPlayer\\b"), QStringLiteral("\\bDoorController\\b"),
        QStringLiteral("\\bTransformController\\b"), QStringLiteral("\\bPhysicsObject\\b"),
        QStringLiteral("\\bProgram\\b"), QStringLiteral("\\bmath\\b"),
        QStringLiteral("\\bstring\\b"), QStringLiteral("\\btable\\b"),
        QStringLiteral("\\bpairs\\b"), QStringLiteral("\\bipairs\\b"),
        QStringLiteral("\\btonumber\\b"), QStringLiteral("\\btostring\\b"),
        QStringLiteral("\\btype\\b"), QStringLiteral("\\bassert\\b"),
        QStringLiteral("\\bpcall\\b"), QStringLiteral("\\bxpcall\\b"),
        QStringLiteral("\\berror\\b"), QStringLiteral("\\bselect\\b")
    };
    for (const auto& bi : lua_builtins) {
        Rule r; r.pattern = QRegularExpression(bi); r.format = m_lua_builtin_format;
        m_lua_rules.push_back(r);
    }

    // 7. Method calls: :methodName(
    Rule method_call_rule;
    method_call_rule.pattern = QRegularExpression(QStringLiteral(R"(:([A-Za-z_][A-Za-z0-9_]*))"));
    method_call_rule.format = m_lua_method_format;
    method_call_rule.capture_group = 1;
    m_lua_rules.push_back(method_call_rule);

    // 8. Function invocations: funcName(
    Rule func_call_rule;
    func_call_rule.pattern = QRegularExpression(QStringLiteral(R"(\b([A-Za-z_][A-Za-z0-9_]*)\s*(?=\())"));
    func_call_rule.format = m_lua_method_format;
    func_call_rule.capture_group = 1;
    m_lua_rules.push_back(func_call_rule);
}

void FileRiftHighlighter::set_diagnostics(const std::vector<Diagnostic>& diags) {
    std::unordered_map<int, std::vector<Diagnostic>> fresh;
    for (const auto& d : diags) {
        fresh[d.line].push_back(d);
    }
    set_diagnostics_map(std::move(fresh));
}

void FileRiftHighlighter::set_diagnostics_map(
    std::unordered_map<int, std::vector<Diagnostic>> map) {
    // Only re-highlight blocks whose squiggle set actually changed. This is
    // the hot path for huge files: rehighlight() would re-run the grammar over
    // every block, which is exactly what the perf charter forbids.
    std::unordered_set<int> dirty;
    for (const auto& kv : m_line_diagnostics) dirty.insert(kv.first);
    for (const auto& kv : map) dirty.insert(kv.first);
    m_line_diagnostics = std::move(map);

    QTextDocument* doc = document();
    if (!doc) return;
    const int max_block = doc->blockCount();
    for (int key : dirty) {
        if (key < 0 || key >= max_block) continue;
        rehighlightBlock(doc->findBlockByNumber(key));
    }
}

void FileRiftHighlighter::clear_diagnostics() {
    set_diagnostics_map({});
}

void FileRiftHighlighter::highlightBlock(const QString& text) {
    int prev_state = previousBlockState();
    if (prev_state == -1) prev_state = StateNormal;

    QString trimmed = text.trimmed();

    // Matches any key: $ or key $ or line ending in $
    static const QRegularExpression chunk_start_re(QStringLiteral(R"(^\s*[\w\.\:]+.*(\$)\s*$)"));
    static const QRegularExpression chunk_end_re(QStringLiteral(R"(^\s*(\$end)\s*$)"));

    if (prev_state == StateInsideLuaChunk) {
        auto match_end = chunk_end_re.match(trimmed);
        if (match_end.hasMatch()) {
            setFormat(0, text.length(), m_chunk_delimiter_format);
            setCurrentBlockState(StateNormal);
            return;
        }

        // Inside Lua chunk -> highlight with Lua grammar
        highlight_lua_line(text);
        setCurrentBlockState(StateInsideLuaChunk);
        return;
    }

    // Normal FileRift Line
    auto match_start = chunk_start_re.match(trimmed);
    if (match_start.hasMatch()) {
        highlight_filerift_line(text, currentBlock().blockNumber());
        int dollar_idx = text.lastIndexOf('$');
        if (dollar_idx != -1) {
            setFormat(dollar_idx, 1, m_chunk_delimiter_format);
        }
        setCurrentBlockState(StateInsideLuaChunk);
        return;
    }

    highlight_filerift_line(text, currentBlock().blockNumber());
    setCurrentBlockState(StateNormal);

    // Apply Diagnostics Squiggles
    int line_num = currentBlock().blockNumber();
    auto dit = m_line_diagnostics.find(line_num);
    if (dit != m_line_diagnostics.end()) {
        for (const auto& d : dit->second) {
            if (d.start_col >= 0 && d.start_col + d.length <= text.length()) {
                QTextCharFormat error_fmt = format(d.start_col);
                error_fmt.setUnderlineStyle(QTextCharFormat::WaveUnderline);
                error_fmt.setUnderlineColor(d.severity == Diagnostic::Error ? QColor("#e06c75") : QColor("#e5c07b"));
                setFormat(d.start_col, d.length, error_fmt);
            }
        }
    }
}

void FileRiftHighlighter::highlight_filerift_line(const QString& text, int /* block_number */) {
    for (const auto& rule : m_filerift_rules) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            auto match = it.next();
            int start = match.capturedStart(rule.capture_group);
            int len = match.capturedLength(rule.capture_group);
            if (start >= 0 && len > 0) {
                setFormat(start, len, rule.format);
            }
        }
    }
}

void FileRiftHighlighter::highlight_lua_line(const QString& text) {
    for (const auto& rule : m_lua_rules) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            auto match = it.next();
            int start = match.capturedStart(rule.capture_group);
            int len = match.capturedLength(rule.capture_group);
            if (start >= 0 && len > 0) {
                setFormat(start, len, rule.format);
            }
        }
    }
}

} // namespace ruby::filerift
