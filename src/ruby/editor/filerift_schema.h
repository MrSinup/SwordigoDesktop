#pragma once
// ============================================================================
// filerift_schema.h — Native C++ Schema & Block Formats for FileRift
//   Direct port of filerift-vscode's blockFormatsData.js into C++
// ============================================================================

#include <string>
#include <vector>
#include <unordered_map>

namespace ruby::filerift {

struct FieldDef {
    std::string name;
    std::string tag_hex;
    std::string description;
    std::string nested_scope;
};

struct ScopeDef {
    std::string name;
    std::unordered_map<std::string, FieldDef> fields;
    std::vector<std::string> field_names_ordered;
};

class FileRiftSchema {
public:
    static FileRiftSchema& instance();

    bool has_scope(const std::string& scope_name) const;
    const ScopeDef* get_scope(const std::string& scope_name) const;

    bool is_valid_field(const std::string& scope_name, const std::string& field_name) const;
    std::string get_nested_scope(const std::string& parent_scope, const std::string& tag_name) const;

    std::vector<std::string> get_completions(const std::string& scope_name) const;
    const std::vector<std::string>& get_component_classes() const;

    std::string get_hover_doc(const std::string& scope_name, const std::string& field_name) const;

private:
    FileRiftSchema();
    void init();

    std::unordered_map<std::string, ScopeDef> m_scopes;
    std::vector<std::string> m_component_classes;
};

} // namespace ruby::filerift
