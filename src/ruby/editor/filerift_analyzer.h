#pragma once
// ============================================================================
// filerift_analyzer.h — Native C++ FileRift Semantic Analyzer & Diagnostics
//   Direct port of filerift-vscode/server/src/analyze.js
// ============================================================================

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include "filerift_schema.h"

namespace ruby::filerift {

struct NavNode {
    std::string type;   // "Object", "Component", "ObjectLibrary", "Bounds"
    int line = -1;
    int end_line = -1;
    std::string id;     // Identifier / Name
    std::string cls;    // ClassName for Component
    bool is_template_def = false;
    std::vector<std::shared_ptr<NavNode>> kids;
};

struct FieldLine {
    std::string key;
    std::string value;
};

struct Diagnostic {
    int line = 0;
    int start_col = 0;
    int length = 0;
    enum Severity { Error, Warning, Info } severity = Error;
    std::string message;
    bool is_template_warning = false;
    std::string template_name;
};

struct AnalysisResult {
    std::vector<std::string> scope_cache;       // scope per line
    std::vector<bool> chunk_flag;               // true if line is inside $...$end Lua chunk
    std::vector<bool> chunk_start;              // true if line opens a chunk
    std::vector<bool> chunk_end;                // true if line closes a chunk ($end)
    std::vector<std::shared_ptr<NavNode>> nav_data; // document outline tree
    std::unordered_map<std::string, int> template_index; // template name -> line
    std::vector<std::string> imported_libraries;
    std::unordered_map<int, int> fold_ranges;   // start_line -> end_line
};

class FileRiftAnalyzer {
public:
    static std::string strip_quotes(const std::string& s);
    static std::optional<FieldLine> parse_field_line(const std::string& raw);

    static AnalysisResult analyze_lines(const std::vector<std::string>& lines, const std::string& root_ext);

    static std::vector<Diagnostic> compute_diagnostics(
        const std::vector<std::string>& lines,
        const AnalysisResult& analysis,
        const std::unordered_set<std::string>& workspace_templates = {});

    static std::vector<std::string> get_completions_at(
        const AnalysisResult& analysis,
        int line,
        const std::string& current_word,
        bool inside_class_name = false);
};

} // namespace ruby::filerift
