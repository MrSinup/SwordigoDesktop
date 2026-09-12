#pragma once
// ============================================================================
// swordigo_engine_db.h — Comprehensive Swordigo Engine Knowledge Base
//   Authoritative catalog of all 60+ Caver game engine components, fields,
//   enums, Lua hooks, and runtime scripting APIs from decompiled sources.
// ============================================================================

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace ruby::database {

struct EnumValue {
    int value = 0;
    std::string name;
    std::string description;
};

struct EnumMeta {
    std::string name;
    std::string description;
    std::vector<EnumValue> values;
};

struct FieldMeta {
    std::string name;
    std::string type;          // "int", "float", "string", "bool", "Vector2", "Vector3", "Rectangle", "FloatColor", "Program", "enum:<Name>"
    std::string default_val;
    std::string description;
    std::string enum_name;     // non-empty if type is enum
};

struct LuaHookMeta {
    std::string event_name;    // e.g. "OnCollide", "OnHurt", "OnKill", "OnReceiveDamage", "OnCollisionEnd"
    std::string signature;     // e.g. "local self, target = ...;"
    std::string description;
};

struct ComponentMeta {
    std::string class_name;    // e.g. "Model", "Damage", "Health", "DoorController"
    std::string tag_name;      // e.g. "ModelComponent", "DamageComponent", "HealthComponent"
    std::string category;      // "Combat", "Physics", "Rendering", "AI", "Logic", "Animation", "Audio", "World", "FX"
    std::string summary;       // Quick one-liner
    std::string description;   // In-depth mechanics explanation
    std::vector<FieldMeta> fields;
    std::vector<LuaHookMeta> lua_hooks;
    std::string example_scl;
};

struct LuaFunctionMeta {
    std::string name;
    std::string signature;     // e.g. "Camera.FocusAtShape(shape)"
    std::string description;
    std::string return_type;
    std::vector<std::pair<std::string, std::string>> params;
};

struct LuaModuleMeta {
    std::string module_name;   // e.g. "Camera", "DoorController", "Entity", "Sound", "Music", "Player", "Game"
    std::string description;
    std::vector<LuaFunctionMeta> functions;
};

class SwordigoEngineDB {
public:
    static SwordigoEngineDB& instance();

    // Query components
    const ComponentMeta* find_component(const std::string& class_or_tag_name) const;
    const std::vector<ComponentMeta>& all_components() const;
    std::vector<std::string> get_all_component_class_names() const;

    // Query fields & enums
    const EnumMeta* find_enum(const std::string& enum_name) const;
    const std::vector<EnumMeta>& all_enums() const;

    // Query Lua APIs
    const LuaModuleMeta* find_lua_module(const std::string& module_name) const;
    const LuaFunctionMeta* find_lua_function(const std::string& module_name, const std::string& func_name) const;
    const std::vector<LuaModuleMeta>& all_lua_modules() const;

    // Rich JetBrains-style HTML Quick Documentation
    std::string get_component_html_doc(const std::string& class_name) const;
    std::string get_field_html_doc(const std::string& class_name, const std::string& field_name) const;
    std::string get_enum_html_doc(const std::string& enum_name) const;
    std::string get_lua_function_html_doc(const std::string& module_name, const std::string& func_name) const;

private:
    SwordigoEngineDB();
    void init_enums();
    void init_components();
    void init_lua_apis();

    std::vector<EnumMeta> m_enums;
    std::unordered_map<std::string, size_t> m_enum_map;

    std::vector<ComponentMeta> m_components;
    std::unordered_map<std::string, size_t> m_class_map;
    std::unordered_map<std::string, size_t> m_tag_map;

    std::vector<LuaModuleMeta> m_lua_modules;
    std::unordered_map<std::string, size_t> m_module_map;
};

} // namespace ruby::database
