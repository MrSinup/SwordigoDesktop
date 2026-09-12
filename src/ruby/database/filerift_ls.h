#pragma once
// ============================================================================
// filerift_ls.h — High-Performance FileRift Language Server & Code Intelligence
//   Context-aware autocompletion, hover docs, signature help, and quick info
//   bridging FileRift schema, Swordigo Engine DB, and Lua scripting runtime.
// ============================================================================

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include "swordigo_engine_db.h"
#include "../editor/filerift_analyzer.h"

namespace ruby::filerift {

enum class CompletionKind {
    Component,
    Field,
    EnumValue,
    LuaHook,
    LuaFunction,
    LuaModule,
    Variable,
    Keyword,
    Template
};

struct CompletionItem {
    std::string label;
    std::string insert_text;
    CompletionKind kind = CompletionKind::Field;
    std::string detail;        // e.g. "(int)", "(DamageType)", "Lua Function"
    std::string documentation; // Brief summary
    int priority = 100;        // Higher = shown earlier
};

enum class ContextType {
    Unknown,
    RootScope,             // Top-level block declarations (Scene, Object, Component, etc.)
    InsideComponentClass,  // Immediately after "ClassName :"
    ComponentBody,         // Inside a Component { ... } block
    FieldValue,            // Right side of "field_name :" (could be enum or bool)
    InsideLuaChunk,        // Inside $ ... $end Lua script
    ObjectBody             // Inside Object { ... } or ObjectLibrary { ... }
};

struct CursorContext {
    ContextType type = ContextType::Unknown;
    std::string current_component_class; // e.g. "Damage", "Health", "Model"
    std::string current_field_name;      // e.g. "DamageType", "SpecialDamageType"
    std::string current_token;           // Prefix being typed
    std::string current_scope;           // Scope name from analyzer scope_cache
    bool inside_lua = false;
    std::string lua_module_prefix;       // e.g. "Camera" when typing "Camera."
};

class FileRiftLS {
public:
    static FileRiftLS& instance();

    // Context analysis for cursor position
    CursorContext resolve_context(
        const std::vector<std::string>& lines,
        const AnalysisResult& analysis,
        int line_idx,
        int col_idx,
        const std::string& current_word);

    // Context-aware autocompletions
    std::vector<CompletionItem> get_completions(
        const std::vector<std::string>& lines,
        const AnalysisResult& analysis,
        int line_idx,
        int col_idx,
        const std::string& current_word);

    // JetBrains-style Hover & Quick Documentation (HTML format)
    std::string get_hover_documentation(
        const std::vector<std::string>& lines,
        const AnalysisResult& analysis,
        int line_idx,
        const std::string& word);

private:
    FileRiftLS() = default;

    // Internal helper to find enclosing Component { ClassName : "..." }
    std::string find_enclosing_component_class(
        const std::vector<std::string>& lines,
        int current_line);
};

} // namespace ruby::filerift
