#pragma once
// ============================================================================
// filerift_highlighter.h — Native C++ TextMate-Fidelity FileRift Syntax Highlighter
//   Implements exact grammar from filerift-vscode/syntaxes/filerift.tmLanguage.json
//   including embedded Lua chunks ($...$end), macros, structural tags, and error lens.
// ============================================================================

#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <vector>
#include <string>
#include "filerift_analyzer.h"

namespace ruby::filerift {

class FileRiftHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit FileRiftHighlighter(QTextDocument* parent = nullptr);
    ~FileRiftHighlighter() override = default;

    // Full-document diagnostics (doc block numbers == absolute line numbers).
    // Only the affected blocks are re-highlighted — never the whole document.
    void set_diagnostics(const std::vector<Diagnostic>& diags);

    // Diagnostics keyed directly by document block number (used by the paged
    // virtual editor, where the widget pre-maps absolute -> window-relative).
    void set_diagnostics_map(std::unordered_map<int, std::vector<Diagnostic>> map);

    // Remove all squiggle formatting without a full-document re-highlight.
    void clear_diagnostics();

protected:
    void highlightBlock(const QString& text) override;

private:
    void highlight_lua_line(const QString& text);
    void highlight_filerift_line(const QString& text, int block_number);

    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
        int capture_group = 0;
    };

    std::vector<Rule> m_filerift_rules;
    std::vector<Rule> m_lua_rules;

    QTextCharFormat m_comment_format;
    QTextCharFormat m_tag_format;
    QTextCharFormat m_property_format;
    QTextCharFormat m_string_format;
    QTextCharFormat m_number_format;
    QTextCharFormat m_boolean_format;
    QTextCharFormat m_macro_format;
    QTextCharFormat m_chunk_delimiter_format;

    // Lua formats
    QTextCharFormat m_lua_keyword_format;
    QTextCharFormat m_lua_builtin_format;
    QTextCharFormat m_lua_method_format;
    QTextCharFormat m_lua_self_format;
    QTextCharFormat m_lua_comment_format;
    QTextCharFormat m_lua_string_format;
    QTextCharFormat m_lua_number_format;

    // Diagnostics squiggles by line
    std::unordered_map<int, std::vector<Diagnostic>> m_line_diagnostics;
};

} // namespace ruby::filerift
