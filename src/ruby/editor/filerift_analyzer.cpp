// ============================================================================
// filerift_analyzer.cpp — Implementation of FileRift Semantic Analyzer
// ============================================================================

#include "filerift_analyzer.h"
#include "../database/swordigo_engine_db.h"
#include <algorithm>
#include <cctype>

namespace ruby::filerift {

static inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string FileRiftAnalyzer::strip_quotes(const std::string& s) {
    if (s.size() >= 2 && ((s.front() == '\'' && s.back() == '\'') || (s.front() == '"' && s.back() == '"'))) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

// Fast string scanner for: ^(\w+)\s*\{
static bool match_block_open(const std::string& line, std::string& out_tag) {
    size_t i = 0;
    while (i < line.size() && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_')) {
        ++i;
    }
    if (i == 0) return false;
    size_t j = i;
    while (j < line.size() && (line[j] == ' ' || line[j] == '\t')) {
        ++j;
    }
    if (j < line.size() && line[j] == '{') {
        out_tag = line.substr(0, i);
        return true;
    }
    return false;
}

// Fast string scanner for: ^\w+\s*:?\s*\$$
static bool match_chunk_open(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_')) {
        ++i;
    }
    if (i == 0) return false;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i < line.size() && line[i] == ':') {
        ++i;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    }
    return (i < line.size() && line[i] == '$' && i + 1 == line.size());
}

std::optional<FieldLine> FileRiftAnalyzer::parse_field_line(const std::string& raw) {
    std::string trimmed = trim(raw);
    if (trimmed.empty() || trimmed == "}" || trimmed.front() == '#') return std::nullopt;

    std::string dummy;
    if (match_block_open(trimmed, dummy)) return std::nullopt;

    size_t colon_idx = trimmed.find(':');
    if (colon_idx != std::string::npos) {
        std::string key = trim(trimmed.substr(0, colon_idx));
        std::string value = trim(trimmed.substr(colon_idx + 1));
        if (!value.empty() && value.back() == ',') {
            value.pop_back();
            value = trim(value);
        }
        if (!key.empty()) return FieldLine{ key, value };
        return std::nullopt;
    }

    // No colon: `Key "value"` or `Key value`
    size_t i = 0;
    while (i < trimmed.size() && (std::isalnum(static_cast<unsigned char>(trimmed[i])) || trimmed[i] == '_')) {
        ++i;
    }
    if (i > 0) {
        std::string key = trimmed.substr(0, i);
        std::string val;
        while (i < trimmed.size() && (trimmed[i] == ' ' || trimmed[i] == '\t')) ++i;
        if (i < trimmed.size()) {
            val = trim(trimmed.substr(i));
            if (!val.empty() && val.back() == ',') {
                val.pop_back();
                val = trim(val);
            }
        }
        return FieldLine{ key, val };
    }
    return std::nullopt;
}

AnalysisResult FileRiftAnalyzer::analyze_lines(const std::vector<std::string>& lines, const std::string& root_ext) {
    AnalysisResult res;
    const size_t n = lines.size();
    res.scope_cache.assign(n, root_ext);
    res.chunk_flag.assign(n, false);
    res.chunk_start.assign(n, false);
    res.chunk_end.assign(n, false);

    std::vector<std::string> scope_stack = { root_ext.empty() ? "scene" : root_ext };
    std::vector<std::string> tag_stack;
    std::vector<std::shared_ptr<NavNode>> nav_stack;
    std::vector<int> open_stack;

    bool in_chunk = false;
    int chunk_open_line = -1;

    auto& schema = FileRiftSchema::instance();

    for (size_t i = 0; i < n; ++i) {
        std::string line = trim(lines[i]);

        if (in_chunk) {
            res.scope_cache[i] = scope_stack.back();
            if (line == "$end") {
                res.chunk_end[i] = true;
                in_chunk = false;
                if (static_cast<int>(i) - chunk_open_line > 1) {
                    res.fold_ranges[chunk_open_line] = static_cast<int>(i);
                }
            } else {
                res.chunk_flag[i] = true;
            }
            continue;
        }

        res.scope_cache[i] = scope_stack.back();

        // Check if opens a $ chunk
        if (match_chunk_open(line)) {
            res.chunk_start[i] = true;
            in_chunk = true;
            chunk_open_line = static_cast<int>(i);
            continue;
        }

        std::string tag;
        if (match_block_open(line, tag)) {
            std::string parent_scope = scope_stack.back();
            std::string nested = schema.get_nested_scope(parent_scope, tag);
            if (nested.empty() || nested == "Unknown") {
                if (schema.has_scope(tag)) {
                    nested = tag;
                }
            }
            scope_stack.push_back(nested.empty() ? "Unknown" : nested);

            auto node = std::make_shared<NavNode>();
            node->type = tag;
            node->line = static_cast<int>(i);

            if (tag == "Object") {
                bool is_template = !tag_stack.empty() && tag_stack.back() == "Template";
                node->is_template_def = is_template;
                res.nav_data.push_back(node);
                nav_stack.push_back(node);
            } else if (tag == "ObjectLibrary" || tag == "Bounds") {
                res.nav_data.push_back(node);
                nav_stack.push_back(node);
            } else if (tag == "Component") {
                if (!nav_stack.empty() && nav_stack.back() && nav_stack.back()->type == "Object") {
                    nav_stack.back()->kids.push_back(node);
                    nav_stack.push_back(node);
                } else {
                    res.nav_data.push_back(node);
                    nav_stack.push_back(node);
                }
            } else {
                nav_stack.push_back(nullptr);
            }

            tag_stack.push_back(tag);
            open_stack.push_back(static_cast<int>(i));
            continue;
        }

        if (line == "}") {
            if (!open_stack.empty()) {
                int start_line = open_stack.back();
                open_stack.pop_back();
                if (static_cast<int>(i) - start_line > 1) {
                    res.fold_ranges[start_line] = static_cast<int>(i);
                }
            }
            if (!nav_stack.empty()) {
                auto closing = nav_stack.back();
                nav_stack.pop_back();
                if (closing) closing->end_line = static_cast<int>(i);
            }
            if (scope_stack.size() > 1) scope_stack.pop_back();
            if (!tag_stack.empty()) tag_stack.pop_back();
            continue;
        }

        auto field = parse_field_line(line);
        if (field) {
            std::string val = strip_quotes(field->value);
            if (field->key == "ImportedLibrary") {
                res.imported_libraries.push_back(val);
            }

            for (int k = static_cast<int>(nav_stack.size()) - 1; k >= 0; --k) {
                auto top = nav_stack[k];
                if (top) {
                    if (top->type == "Object" && field->key == "Identifier") {
                        top->id = val;
                    } else if (top->type == "ObjectLibrary" && field->key == "Name") {
                        top->id = val;
                    } else if (top->type == "Component") {
                        if (field->key == "ClassName") top->cls = val;
                        else if (field->key == "Identifier") top->id = val;
                    }
                    break;
                }
            }
        }
    }

    // Build template index
    for (const auto& node : res.nav_data) {
        if (node && node->is_template_def && !node->id.empty()) {
            res.template_index[node->id] = node->line;
        }
    }

    return res;
}

std::vector<Diagnostic> FileRiftAnalyzer::compute_diagnostics(
    const std::vector<std::string>& lines,
    const AnalysisResult& analysis,
    const std::unordered_set<std::string>& workspace_templates) {

    std::vector<Diagnostic> diagnostics;
    auto& schema = FileRiftSchema::instance();

    for (size_t i = 0; i < lines.size(); ++i) {
        if (analysis.chunk_flag[i] || analysis.chunk_start[i] || analysis.chunk_end[i]) continue;
        const std::string& raw = lines[i];
        std::string trimmed = trim(raw);
        if (trimmed.empty() || trimmed.front() == '#' || trimmed == "}") continue;

        std::string current_scope = (i < analysis.scope_cache.size()) ? analysis.scope_cache[i] : "Unknown";

        std::string block_key;
        if (match_block_open(trimmed, block_key)) {
            if (current_scope != "Unknown" && schema.has_scope(current_scope)) {
                bool is_valid = schema.is_valid_field(current_scope, block_key);
                if (!is_valid) {
                    if (current_scope == "Component") {
                        if ((block_key.size() >= 9 && block_key.compare(block_key.size() - 9, 9, "Component") == 0) ||
                            schema.has_scope(block_key) ||
                            ruby::database::SwordigoEngineDB::instance().find_component(block_key)) {
                            is_valid = true;
                        }
                    } else if (current_scope == "Mesh") {
                        if (block_key == "Indices" || block_key == "Vertices" || block_key == "Normals" ||
                            block_key == "TexCoordSet" || block_key == "Material" || block_key == "BoundingBox" ||
                            block_key == "VertexColors" || block_key == "BoneIndices" || block_key == "BoneWeights") {
                            is_valid = true;
                        }
                    } else if (block_key == "OnCollide" || block_key == "OnBreak" || block_key == "OnKill" ||
                               block_key == "OnHurt" || block_key == "Program" || block_key == "OnLoad") {
                        is_valid = true;
                    }
                }

                if (!is_valid) {
                    size_t pos = raw.find(block_key);
                    Diagnostic d;
                    d.line = static_cast<int>(i);
                    d.start_col = (pos != std::string::npos) ? static_cast<int>(pos) : 0;
                    d.length = static_cast<int>(block_key.size());
                    d.severity = Diagnostic::Error;
                    d.message = "Structural tag \"" + block_key + "\" is undefined under block [" + current_scope + "]";
                    diagnostics.push_back(d);
                }
            }
            continue;
        }

        auto field = parse_field_line(raw);
        if (field) {
            if (field->key == "Comment") continue;
            if (current_scope != "Unknown" && schema.has_scope(current_scope)) {
                bool is_valid = schema.is_valid_field(current_scope, field->key);
                
                if (!is_valid) {
                    static const std::unordered_set<std::string> base_comp_fields = {
                        "ClassName", "Identifier", "Label", "ParentComponentIdentifier",
                        "Active", "Exclusive", "ComponentTag", "ExecuteOnce", "Enabled", "Program"
                    };
                    static const std::unordered_set<std::string> base_obj_fields = {
                        "TemplateName", "Identifier", "Component", "Position", "Depth", "Rotation", "Scaling",
                        "LocalAabb", "Hidden", "OnLoad", "Active", "Exclusive"
                    };
                    static const std::unordered_set<std::string> vertex_channel_fields = {
                        "ValueType", "ValuesPerVertex", "Stride", "DataOffset"
                    };

                    if (current_scope == "Component" || current_scope.find("Component") != std::string::npos) {
                        if (base_comp_fields.count(field->key)) {
                            is_valid = true;
                        } else {
                            for (const auto& comp : ruby::database::SwordigoEngineDB::instance().all_components()) {
                                for (const auto& cf : comp.fields) {
                                    if (cf.name == field->key) {
                                        is_valid = true;
                                        break;
                                    }
                                }
                                if (is_valid) break;
                            }
                        }
                    } else if (current_scope == "SceneObject" || current_scope == "ObjectTemplate") {
                        if (base_obj_fields.count(field->key)) {
                            is_valid = true;
                        }
                    } else if (current_scope == "Square" || current_scope == "VertexChannel" ||
                               current_scope == "Indices" || current_scope == "Vertices" ||
                               current_scope == "Normals" || current_scope == "TexCoordSet") {
                        if (vertex_channel_fields.count(field->key)) {
                            is_valid = true;
                        }
                    }
                }

                if (!is_valid) {
                    size_t pos = raw.find(field->key);
                    Diagnostic d;
                    d.line = static_cast<int>(i);
                    d.start_col = (pos != std::string::npos) ? static_cast<int>(pos) : 0;
                    d.length = static_cast<int>(field->key.size());
                    d.severity = Diagnostic::Error;
                    d.message = "Field \"" + field->key + "\" is undefined under scope structure [" + current_scope + "]";
                    diagnostics.push_back(d);
                    continue;
                }
            }

            // Check if this is a template reference (e.g. Template: "..." or TemplateRef: "...")
            std::string lower_key = field->key;
            std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (lower_key.find("template") != std::string::npos) {
                std::string tname = strip_quotes(field->value);
                if (!tname.empty() && !workspace_templates.empty()) {
                    if (workspace_templates.find(tname) == workspace_templates.end() &&
                        analysis.template_index.find(tname) == analysis.template_index.end()) {
                        size_t pos = raw.find(tname);
                        Diagnostic d;
                        d.line = static_cast<int>(i);
                        d.start_col = (pos != std::string::npos) ? static_cast<int>(pos) : 0;
                        d.length = static_cast<int>(tname.size());
                        d.severity = Diagnostic::Warning;
                        d.is_template_warning = true;
                        d.template_name = tname;
                        d.message = "Template \"" + tname + "\" is not defined in any reachable file";
                        diagnostics.push_back(d);
                    }
                }
            }
        }
    }

    return diagnostics;
}

std::vector<std::string> FileRiftAnalyzer::get_completions_at(
    const AnalysisResult& analysis,
    int line,
    const std::string& current_word,
    bool inside_class_name) {

    auto& schema = FileRiftSchema::instance();
    if (inside_class_name) {
        return schema.get_component_classes();
    }

    std::string scope = "scene";
    if (line >= 0 && line < (int)analysis.scope_cache.size()) {
        scope = analysis.scope_cache[line];
    }
    return schema.get_completions(scope);
}

} // namespace ruby::filerift
