#include "filerift_ls.h"
#include "../editor/filerift_schema.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace ruby::filerift {

static inline std::string trim(const std::string& s) {
    auto wsfront = std::find_if_not(s.begin(), s.end(), [](int c){ return std::isspace(c); });
    auto wsback = std::find_if_not(s.rbegin(), s.rend(), [](int c){ return std::isspace(c); }).base();
    return (wsback <= wsfront ? std::string() : std::string(wsfront, wsback));
}

static inline bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

static inline bool starts_with_icase(const std::string& str, const std::string& prefix) {
    if (prefix.empty()) return true;
    if (str.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(str[i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

FileRiftLS& FileRiftLS::instance() {
    static FileRiftLS s_inst;
    return s_inst;
}

std::string FileRiftLS::find_enclosing_component_class(
    const std::vector<std::string>& lines,
    int current_line)
{
    if (current_line < 0 || current_line >= static_cast<int>(lines.size())) {
        return "";
    }

    int brace_depth = 0;
    for (int i = current_line; i >= 0; --i) {
        const std::string& l = lines[static_cast<size_t>(i)];
        for (auto it = l.rbegin(); it != l.rend(); ++it) {
            if (*it == '}') ++brace_depth;
            else if (*it == '{') --brace_depth;
        }

        if (brace_depth < 0) {
            // Found parent opening block. Check if this block or its immediate children is a Component
            std::string trimmed = trim(l);
            if (starts_with_icase(trimmed, "Component") || trimmed.find("Component") != std::string::npos) {
                // Look forward from line i up to current_line for "ClassName :"
                for (int j = i; j <= current_line && j < static_cast<int>(lines.size()); ++j) {
                    auto opt = FileRiftAnalyzer::parse_field_line(lines[static_cast<size_t>(j)]);
                    if (opt && iequals(opt->key, "ClassName")) {
                        return FileRiftAnalyzer::strip_quotes(opt->value);
                    }
                }
                return "Component";
            }
        }
    }
    return "";
}

CursorContext FileRiftLS::resolve_context(
    const std::vector<std::string>& lines,
    const AnalysisResult& analysis,
    int line_idx,
    int col_idx,
    const std::string& current_word)
{
    CursorContext ctx;
    ctx.current_token = current_word;

    if (line_idx < 0 || line_idx >= static_cast<int>(lines.size())) {
        return ctx;
    }

    // Check if inside Lua chunk ($ ... $end)
    if (line_idx < static_cast<int>(analysis.chunk_flag.size()) && analysis.chunk_flag[static_cast<size_t>(line_idx)]) {
        ctx.inside_lua = true;
        ctx.type = ContextType::InsideLuaChunk;

        // Check if typing after a module prefix, e.g. "Camera." or method call "self:" / "target:"
        const std::string& line = lines[static_cast<size_t>(line_idx)];
        size_t safe_col = std::min(static_cast<size_t>(col_idx), line.size());
        std::string before_cursor = line.substr(0, safe_col);
        size_t dot_pos = before_cursor.rfind('.');
        size_t colon_pos = before_cursor.rfind(':');

        if (colon_pos != std::string::npos && (dot_pos == std::string::npos || colon_pos > dot_pos)) {
            // Method call e.g. self:identifier() or target:position() -> route to SceneObject methods
            ctx.lua_module_prefix = "SceneObject";
        } else if (dot_pos != std::string::npos && dot_pos > 0) {
            size_t token_start = dot_pos;
            while (token_start > 0 && (std::isalnum(before_cursor[token_start - 1]) || before_cursor[token_start - 1] == '_')) {
                --token_start;
            }
            ctx.lua_module_prefix = before_cursor.substr(token_start, dot_pos - token_start);
        }
        return ctx;
    }

    // FileRift markup context
    const std::string& line = lines[static_cast<size_t>(line_idx)];
    std::string trimmed = trim(line);

    // Get current scope from analyzer
    if (line_idx < static_cast<int>(analysis.scope_cache.size())) {
        ctx.current_scope = analysis.scope_cache[static_cast<size_t>(line_idx)];
    }

    // Check if after "ClassName :"
    size_t colon_pos = line.find(':');
    if (colon_pos != std::string::npos) {
        std::string left = trim(line.substr(0, colon_pos));
        if (iequals(left, "ClassName")) {
            ctx.type = ContextType::InsideComponentClass;
            return ctx;
        }

        // Check if cursor is on the right side of ':' (FieldValue context)
        if (static_cast<size_t>(col_idx) > colon_pos) {
            ctx.type = ContextType::FieldValue;
            ctx.current_field_name = left;
            ctx.current_component_class = find_enclosing_component_class(lines, line_idx);
            return ctx;
        }
    }

    // Check enclosing block
    ctx.current_component_class = find_enclosing_component_class(lines, line_idx);
    if (!ctx.current_component_class.empty()) {
        ctx.type = ContextType::ComponentBody;
        return ctx;
    }

    if (ctx.current_scope == "root" || ctx.current_scope.empty()) {
        ctx.type = ContextType::RootScope;
    } else {
        ctx.type = ContextType::ObjectBody;
    }

    return ctx;
}

std::vector<CompletionItem> FileRiftLS::get_completions(
    const std::vector<std::string>& lines,
    const AnalysisResult& analysis,
    int line_idx,
    int col_idx,
    const std::string& current_word)
{
    std::vector<CompletionItem> items;
    CursorContext ctx = resolve_context(lines, analysis, line_idx, col_idx, current_word);
    const auto& db = ruby::database::SwordigoEngineDB::instance();

    // 1. Inside Lua Chunk ($ ... $end)
    if (ctx.inside_lua) {
        if (!ctx.lua_module_prefix.empty()) {
            // Module-scoped functions (e.g. Camera.FocusAtShape) or SceneObject methods (e.g. self:identifier)
            const auto* mod = db.find_lua_module(ctx.lua_module_prefix);
            if (mod) {
                for (const auto& fn : mod->functions) {
                    if (starts_with_icase(fn.name, current_word)) {
                        CompletionItem ci;
                        ci.label = fn.name;
                        ci.insert_text = fn.name + "()";
                        ci.kind = CompletionKind::LuaFunction;
                        ci.detail = fn.signature;
                        ci.documentation = fn.description;
                        ci.priority = 200;
                        items.push_back(ci);
                    }
                }
            }
        } else {
            // Global Lua Modules & Functions
            for (const auto& mod : db.all_lua_modules()) {
                if (mod.module_name == "Global") {
                    for (const auto& fn : mod.functions) {
                        if (starts_with_icase(fn.name, current_word)) {
                            CompletionItem ci;
                            ci.label = fn.name;
                            ci.insert_text = fn.name + "()";
                            ci.kind = CompletionKind::LuaFunction;
                            ci.detail = fn.signature;
                            ci.documentation = fn.description;
                            ci.priority = 195;
                            items.push_back(ci);
                        }
                    }
                    continue;
                }
                if (mod.module_name == "SceneObject") continue;

                if (starts_with_icase(mod.module_name, current_word)) {
                    CompletionItem ci;
                    ci.label = mod.module_name;
                    ci.insert_text = mod.module_name;
                    ci.kind = CompletionKind::LuaModule;
                    ci.detail = "Swordigo Lua Module";
                    ci.documentation = mod.description;
                    ci.priority = 180;
                    items.push_back(ci);
                }
            }

            // Global variables & symbols
            static const std::vector<std::pair<std::string, std::string>> global_vars = {
                {"self", "SceneObject handle for calling object"},
                {"target", "SceneObject handle for colliding/interacting object"},
                {"inAnotherDimension", "boolean: true if player is currently in shadow dimension"}
            };
            for (const auto& gv : global_vars) {
                if (starts_with_icase(gv.first, current_word)) {
                    CompletionItem ci;
                    ci.label = gv.first;
                    ci.insert_text = gv.first;
                    ci.kind = CompletionKind::Variable;
                    ci.detail = "SceneObject / Engine Global";
                    ci.documentation = gv.second;
                    ci.priority = 170;
                    items.push_back(ci);
                }
            }

            // Standard Lua keywords
            static const std::vector<std::string> lua_keywords = {
                "local", "function", "if", "then", "else", "elseif", "end",
                "for", "while", "do", "repeat", "until", "return", "break",
                "true", "false", "nil", "and", "or", "not"
            };
            for (const auto& kw : lua_keywords) {
                if (starts_with_icase(kw, current_word)) {
                    CompletionItem ci;
                    ci.label = kw;
                    ci.insert_text = kw;
                    ci.kind = CompletionKind::Keyword;
                    ci.detail = "keyword";
                    ci.priority = 50;
                    items.push_back(ci);
                }
            }
        }
        return items;
    }

    // 2. ClassName : "..." completions
    if (ctx.type == ContextType::InsideComponentClass) {
        for (const auto& comp : db.all_components()) {
            if (starts_with_icase(comp.class_name, current_word)) {
                CompletionItem ci;
                ci.label = "\"" + comp.class_name + "\"";
                ci.insert_text = "\"" + comp.class_name + "\"";
                ci.kind = CompletionKind::Component;
                ci.detail = "[" + comp.category + "] " + comp.summary;
                ci.documentation = comp.description;
                ci.priority = 250;
                items.push_back(ci);
            }
        }
        return items;
    }

    // 3. Field Value Context (Enum values or booleans)
    if (ctx.type == ContextType::FieldValue) {
        const auto* comp = db.find_component(ctx.current_component_class);
        if (comp) {
            for (const auto& f : comp->fields) {
                if (iequals(f.name, ctx.current_field_name) && !f.enum_name.empty()) {
                    const auto* enm = db.find_enum(f.enum_name);
                    if (enm) {
                        for (const auto& val : enm->values) {
                            if (starts_with_icase(val.name, current_word)) {
                                CompletionItem ci;
                                ci.label = val.name;
                                ci.insert_text = val.name;
                                ci.kind = CompletionKind::EnumValue;
                                ci.detail = f.enum_name + " (" + std::to_string(val.value) + ")";
                                ci.documentation = val.description;
                                ci.priority = 220;
                                items.push_back(ci);
                            }
                        }
                    }
                }
            }
        }
        // Boolean values
        if (starts_with_icase("true", current_word)) {
            CompletionItem ci; ci.label = "true"; ci.insert_text = "true";
            ci.kind = CompletionKind::Keyword; ci.priority = 100;
            items.push_back(ci);
        }
        if (starts_with_icase("false", current_word)) {
            CompletionItem ci; ci.label = "false"; ci.insert_text = "false";
            ci.kind = CompletionKind::Keyword; ci.priority = 100;
            items.push_back(ci);
        }
        if (!items.empty()) return items;
    }

    // 4. Inside Component Body
    if (ctx.type == ContextType::ComponentBody && !ctx.current_component_class.empty()) {
        const auto* comp = db.find_component(ctx.current_component_class);
        if (comp) {
            // Component-specific fields
            for (const auto& f : comp->fields) {
                if (starts_with_icase(f.name, current_word)) {
                    CompletionItem ci;
                    ci.label = f.name;
                    ci.insert_text = f.name + " : " + (f.default_val.empty() ? "" : f.default_val);
                    ci.kind = CompletionKind::Field;
                    ci.detail = "(" + f.type + ")";
                    ci.documentation = f.description;
                    ci.priority = 200;
                    items.push_back(ci);
                }
            }
            // Component Lua Event Hooks
            for (const auto& h : comp->lua_hooks) {
                if (starts_with_icase(h.event_name, current_word)) {
                    CompletionItem ci;
                    ci.label = h.event_name;
                    ci.insert_text = h.event_name + " : $\n    " + h.signature + "\n$end";
                    ci.kind = CompletionKind::LuaHook;
                    ci.detail = "Lua Event Hook";
                    ci.documentation = h.description;
                    ci.priority = 190;
                    items.push_back(ci);
                }
            }
        }
        // Universal component fields
        static const std::vector<std::pair<std::string, std::string>> base_comp_fields = {
            {"ClassName", "Component class identifier"},
            {"Active", "Component activation toggle (bool)"},
            {"Exclusive", "Exclusive processing flag (bool)"},
            {"ComponentTag", "Optional unique search tag (string)"}
        };
        for (const auto& bf : base_comp_fields) {
            if (starts_with_icase(bf.first, current_word)) {
                CompletionItem ci;
                ci.label = bf.first;
                ci.insert_text = bf.first + " : ";
                ci.kind = CompletionKind::Field;
                ci.detail = "Base Field";
                ci.documentation = bf.second;
                ci.priority = 150;
                items.push_back(ci);
            }
        }
        return items;
    }

    // 5. Schema / Scope completions (Object, Scene, Template)
    auto schema_completions = FileRiftAnalyzer::get_completions_at(analysis, line_idx, current_word, false);
    for (const auto& sc : schema_completions) {
        CompletionItem ci;
        ci.label = sc;
        ci.insert_text = sc;
        ci.kind = (sc.find('{') != std::string::npos) ? CompletionKind::Keyword : CompletionKind::Field;
        ci.detail = "FileRift Schema";
        ci.priority = 100;
        items.push_back(ci);
    }

    // Workspace Templates
    for (const auto& kv : analysis.template_index) {
        if (starts_with_icase(kv.first, current_word)) {
            CompletionItem ci;
            ci.label = kv.first;
            ci.insert_text = kv.first;
            ci.kind = CompletionKind::Template;
            ci.detail = "Template Definition (line " + std::to_string(kv.second + 1) + ")";
            ci.priority = 160;
            items.push_back(ci);
        }
    }

    return items;
}

std::string FileRiftLS::get_hover_documentation(
    const std::vector<std::string>& lines,
    const AnalysisResult& analysis,
    int line_idx,
    const std::string& word)
{
    if (word.empty()) return "";

    const auto& db = ruby::database::SwordigoEngineDB::instance();

    // 1. Is it a Component class name?
    const auto* comp = db.find_component(word);
    if (comp) {
        return db.get_component_html_doc(comp->class_name);
    }

    // 2. Is it an Enum type?
    const auto* enm = db.find_enum(word);
    if (enm) {
        return db.get_enum_html_doc(enm->name);
    }

    // 3. Is it a Lua Module?
    const auto* mod = db.find_lua_module(word);
    if (mod) {
        std::ostringstream ss;
        ss << "<div style='font-family:sans-serif; font-size:12px; color:#abb2bf;'>"
           << "<div style='font-weight:bold; font-size:14px; color:#61afef; margin-bottom:4px;'>"
           << "module <b>" << mod->module_name << "</b></div>"
           << "<div style='color:#e5c07b; margin-bottom:8px;'>Swordigo Lua Scripting API</div>"
           << "<p style='margin:0 0 8px 0; color:#dcdfe4;'>" << mod->description << "</p>"
           << "<div style='font-weight:bold; color:#98c379; margin-bottom:4px;'>Exported Functions:</div>"
           << "<ul style='margin:0; padding-left:18px;'>";
        for (const auto& fn : mod->functions) {
            ss << "<li><code style='color:#61afef;'>" << fn.name << "()</code> - " << fn.description << "</li>";
        }
        ss << "</ul></div>";
        return ss.str();
    }

    // 4. Inside Component? Check field or hook documentation
    std::string comp_class = find_enclosing_component_class(lines, line_idx);
    if (!comp_class.empty()) {
        std::string field_doc = db.get_field_html_doc(comp_class, word);
        if (!field_doc.empty()) return field_doc;

        // Check if word is an enum value inside this component
        for (const auto& em : db.all_enums()) {
            for (const auto& v : em.values) {
                if (iequals(v.name, word)) {
                    std::ostringstream ss;
                    ss << "<div style='font-family:sans-serif; font-size:12px; color:#abb2bf;'>"
                       << "<div style='font-weight:bold; font-size:13px; color:#98c379; margin-bottom:2px;'>"
                       << em.name << "::<b>" << v.name << "</b> = " << v.value << "</div>"
                       << "<div style='color:#dcdfe4;'>" << v.description << "</div></div>";
                    return ss.str();
                }
            }
        }
    }

    // 5. Check Lua functions
    for (const auto& m : db.all_lua_modules()) {
        for (const auto& fn : m.functions) {
            if (iequals(fn.name, word)) {
                return db.get_lua_function_html_doc(m.module_name, fn.name);
            }
        }
    }

    // 6. Check Lua Special Identifiers & SceneObject Handles
    if (word == "self" || word == "target") {
        return "<div style='font-family: -apple-system, Segoe UI, sans-serif; padding: 6px; color: #abb2bf; font-size: 13px;'>"
               "<div style='color: #e5c07b; font-weight: bold; font-size: 14px;'><code>" + word + "</code> : SceneObject</div>"
               "<p style='margin: 4px 0 6px 0; color: #dcdfe4;'>Implicit SceneObject handle in script execution (e.g. <code>local self, target = ...;</code>).</p>"
               "<div style='color: #98c379;'>Common methods: <code>:identifier()</code>, <code>:position()</code>, <code>:setPosition(v)</code>, <code>:velocity()</code>, <code>:setVelocity(v)</code>, <code>:rotation()</code>, <code>:scaling()</code>, <code>:depth()</code>, <code>:setHidden(b)</code>, <code>:setAlwaysActive(b)</code>, <code>:clone()</code>, <code>:destroy()</code>, <code>:addComponent(c)</code></div>"
               "</div>";
    }
    if (word == "inAnotherDimension") {
        return "<div style='font-family: -apple-system, Segoe UI, sans-serif; padding: 6px; color: #abb2bf; font-size: 13px;'>"
               "<div style='color: #e5c07b; font-weight: bold; font-size: 14px;'><code>inAnotherDimension</code> : boolean</div>"
               "<p style='margin: 4px 0 0 0; color: #dcdfe4;'>Global engine state flag indicating whether the player is currently in the shadow/dark dimension.</p>"
               "</div>";
    }

    static const std::unordered_map<std::string, std::pair<std::string, std::string>> global_utils_docs = {
        {"Vector3", {"Vector3.New(x, y, z) -> Vector3", "Constructs a 3D coordinate vector with X, Y, Z components."}},
        {"Vector2", {"Vector2.New(x, y) -> Vector2", "Constructs a 2D coordinate vector with X, Y components."}},
        {"Math", {"Math.RandomInt(min, max) -> int", "Returns a pseudo-random integer in the closed interval [min, max]."}},
        {"ShowTextBubble", {"ShowTextBubble(id, position, text) -> SceneObject", "Spawns a floating speech bubble entity over the given world coordinates."}},
        {"ShowTextBubbles", {"ShowTextBubbles(id, position, loop, textList) -> SceneObject", "Spawns a sequential multi-line dialogue bubble sequence."}},
        {"HideTextBubble", {"HideTextBubble(id) -> void", "Dismisses and hides the dialogue bubble matching the given identifier string."}},
        {"DirectionToTargetFromPosition", {"DirectionToTargetFromPosition(targetPos, selfPos) -> number", "Calculates normalized horizontal direction (1.0 = right, -1.0 = left) toward target."}}
    };
    auto guit = global_utils_docs.find(word);
    if (guit != global_utils_docs.end()) {
        std::ostringstream ss;
        ss << "<div style='font-family: -apple-system, Segoe UI, sans-serif; padding: 6px; color: #abb2bf; font-size: 13px;'>"
           << "<div style='color: #4ec9b0; font-weight: bold; font-size: 14px;'><code>" << guit->second.first << "</code></div>"
           << "<p style='margin: 4px 0 0 0; color: #dcdfe4;'>" << guit->second.second << "</p>"
           << "</div>";
        return ss.str();
    }

    // 7. Check Standard Lua Globals & Tables
    static const std::unordered_map<std::string, std::pair<std::string, std::string>> standard_lua_docs = {
        {"ipairs", {"ipairs(table)", "Returns an iterator function for array-indexed tables starting from index 1."}},
        {"pairs", {"pairs(table)", "Returns an iterator function over all key-value pairs of the given table."}},
        {"type", {"type(v)", "Returns the type of value as a string: 'nil', 'number', 'string', 'boolean', 'table', 'function'."}},
        {"tostring", {"tostring(e)", "Converts any Lua argument to a human-readable string representation."}},
        {"tonumber", {"tonumber(e [, base])", "Attempts to parse argument as a number in the optional base (default 10)."}},
        {"pcall", {"pcall(f, ...)", "Protected call: invokes function f with error handling, returning true on success."}},
        {"assert", {"assert(v [, message])", "Issues an error if value v evaluates to false or nil."}},
        {"math", {"math table", "Standard Lua math library providing sin, cos, abs, floor, ceil, random, etc."}},
        {"string", {"string table", "Standard Lua string manipulation functions: format, find, match, sub, len."}},
        {"table", {"table table", "Standard Lua table manipulation functions: insert, remove, sort, concat."}}
    };
    auto sit = standard_lua_docs.find(word);
    if (sit != standard_lua_docs.end()) {
        std::ostringstream ss;
        ss << "<div style='font-family: -apple-system, Segoe UI, sans-serif; padding: 6px; color: #abb2bf; font-size: 13px;'>"
           << "<div style='color: #4ec9b0; font-weight: bold; font-size: 14px;'><code>" << sit->second.first << "</code></div>"
           << "<p style='margin: 4px 0 0 0; color: #dcdfe4;'>" << sit->second.second << "</p>"
           << "</div>";
        return ss.str();
    }

    // 8. Check FileRift Schema
    std::string scope = (line_idx >= 0 && line_idx < static_cast<int>(analysis.scope_cache.size()))
        ? analysis.scope_cache[static_cast<size_t>(line_idx)] : "scene";
    std::string schema_doc = FileRiftSchema::instance().get_hover_doc(scope, word);
    if (!schema_doc.empty()) {
        std::ostringstream ss;
        ss << "<div style='font-family:sans-serif; font-size:12px; color:#abb2bf;'>"
           << "<div style='font-weight:bold; color:#61afef; margin-bottom:4px;'>FileRift Protobuf Property</div>"
           << "<div style='color:#dcdfe4;'>" << schema_doc << "</div></div>";
        return ss.str();
    }

    return "";
}

} // namespace ruby::filerift
