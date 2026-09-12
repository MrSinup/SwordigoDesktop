// ============================================================================
// filerift_schema.cpp — Implementation of FileRift Schema Database
// ============================================================================

#include "filerift_schema.h"

namespace ruby::filerift {

FileRiftSchema& FileRiftSchema::instance() {
    static FileRiftSchema s_instance;
    return s_instance;
}

FileRiftSchema::FileRiftSchema() {
    init();
}

bool FileRiftSchema::has_scope(const std::string& scope_name) const {
    return m_scopes.find(scope_name) != m_scopes.end();
}

const ScopeDef* FileRiftSchema::get_scope(const std::string& scope_name) const {
    auto it = m_scopes.find(scope_name);
    return (it != m_scopes.end()) ? &it->second : nullptr;
}

bool FileRiftSchema::is_valid_field(const std::string& scope_name, const std::string& field_name) const {
    auto it = m_scopes.find(scope_name);
    if (it == m_scopes.end()) return true; // unknown scope -> do not produce false positive
    return it->second.fields.find(field_name) != it->second.fields.end();
}

std::string FileRiftSchema::get_nested_scope(const std::string& parent_scope, const std::string& tag_name) const {
    auto it = m_scopes.find(parent_scope);
    if (it == m_scopes.end()) return "Unknown";
    auto fit = it->second.fields.find(tag_name);
    if (fit == it->second.fields.end()) return "Unknown";
    return fit->second.nested_scope.empty() ? "Unknown" : fit->second.nested_scope;
}

std::vector<std::string> FileRiftSchema::get_completions(const std::string& scope_name) const {
    auto it = m_scopes.find(scope_name);
    if (it != m_scopes.end()) {
        return it->second.field_names_ordered;
    }
    return {};
}

const std::vector<std::string>& FileRiftSchema::get_component_classes() const {
    return m_component_classes;
}

std::string FileRiftSchema::get_hover_doc(const std::string& scope_name, const std::string& field_name) const {
    auto it = m_scopes.find(scope_name);
    if (it == m_scopes.end()) return "";
    auto fit = it->second.fields.find(field_name);
    if (fit == it->second.fields.end()) return "";
    
    std::string doc = "**" + field_name + "**";
    if (!fit->second.tag_hex.empty()) {
        doc += " `(Tag: 0x" + fit->second.tag_hex + ")`";
    }
    if (!fit->second.nested_scope.empty()) {
        doc += " -> Block `[" + fit->second.nested_scope + "]`";
    }
    if (!fit->second.description.empty()) {
        doc += "\n\n" + fit->second.description;
    }
    return doc;
}

void FileRiftSchema::init() {
    {
        ScopeDef& sc = m_scopes["fr"];
        sc.name = "fr";
        sc.fields["Name"] = { "Name", "0a", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["Template"] = { "Template", "12", "", "ObjectTemplate" };
        sc.field_names_ordered.push_back("Template");
        sc.fields["ImportedLibrary"] = { "ImportedLibrary", "1a", "", "" };
        sc.field_names_ordered.push_back("ImportedLibrary");
        sc.fields["Texture"] = { "Texture", "12", "", "Texture" };
        sc.field_names_ordered.push_back("Texture");
        sc.fields["Program"] = { "Program", "2a", "", "Program" };
        sc.field_names_ordered.push_back("Program");
        sc.fields["Object"] = { "Object", "0a", "", "SceneObject" };
        sc.field_names_ordered.push_back("Object");
        sc.fields["ObjectLibrary"] = { "ObjectLibrary", "12", "", "ObjectLibrary" };
        sc.field_names_ordered.push_back("ObjectLibrary");
        sc.fields["Bounds"] = { "Bounds", "1a", "", "SceneBounds" };
        sc.field_names_ordered.push_back("Bounds");
        sc.fields["Group"] = { "Group", "22", "", "SceneObjectGroup" };
        sc.field_names_ordered.push_back("Group");
        sc.fields["OnLoad"] = { "OnLoad", "2a", "", "Program" };
        sc.field_names_ordered.push_back("OnLoad");
        sc.fields["Item"] = { "Item", "0a", "", "Item" };
        sc.field_names_ordered.push_back("Item");
        sc.fields["Skill"] = { "Skill", "12", "", "Skill" };
        sc.field_names_ordered.push_back("Skill");
        sc.fields["Quest"] = { "Quest", "1a", "", "Quest" };
        sc.field_names_ordered.push_back("Quest");
        sc.fields["EntityClass"] = { "EntityClass", "22", "", "EntityClass" };
        sc.field_names_ordered.push_back("EntityClass");
        sc.fields["GuideTarget"] = { "GuideTarget", "2a", "", "GuideTarget" };
        sc.field_names_ordered.push_back("GuideTarget");
        sc.fields["Playlist"] = { "Playlist", "0a", "", "MusicPlaylist" };
        sc.field_names_ordered.push_back("Playlist");
        sc.fields["MusicEnabled"] = { "MusicEnabled", "10", "", "" };
        sc.field_names_ordered.push_back("MusicEnabled");
        sc.fields["SoundEnabled"] = { "SoundEnabled", "18", "", "" };
        sc.field_names_ordered.push_back("SoundEnabled");
        sc.fields["MusicVolume"] = { "MusicVolume", "25", "", "" };
        sc.field_names_ordered.push_back("MusicVolume");
        sc.fields["SoundVolume"] = { "SoundVolume", "2d", "", "" };
        sc.field_names_ordered.push_back("SoundVolume");
        sc.fields["PhoneControlsLayout"] = { "PhoneControlsLayout", "32", "", "GUIViewLayout" };
        sc.field_names_ordered.push_back("PhoneControlsLayout");
        sc.fields["PadControlsLayout"] = { "PadControlsLayout", "3a", "", "GUIViewLayout" };
        sc.field_names_ordered.push_back("PadControlsLayout");
        sc.fields["GuideUnlocked"] = { "GuideUnlocked", "40", "", "" };
        sc.field_names_ordered.push_back("GuideUnlocked");
        sc.fields["CoinDoublerUnlocked"] = { "CoinDoublerUnlocked", "48", "", "" };
        sc.field_names_ordered.push_back("CoinDoublerUnlocked");
        sc.fields["NoAdsUnlocked"] = { "NoAdsUnlocked", "50", "", "" };
        sc.field_names_ordered.push_back("NoAdsUnlocked");
        sc.fields["ExperienceLevel"] = { "ExperienceLevel", "10", "", "" };
        sc.field_names_ordered.push_back("ExperienceLevel");
        sc.fields["TimePlayed"] = { "TimePlayed", "19", "", "" };
        sc.field_names_ordered.push_back("TimePlayed");
        sc.fields["GameState"] = { "GameState", "22", "", "GameState" };
        sc.field_names_ordered.push_back("GameState");
        sc.fields["EquippedWeaponName"] = { "EquippedWeaponName", "2a", "", "" };
        sc.field_names_ordered.push_back("EquippedWeaponName");
        sc.fields["EquippedArmorName"] = { "EquippedArmorName", "32", "", "" };
        sc.field_names_ordered.push_back("EquippedArmorName");
        sc.fields["WeaponTrinketName"] = { "WeaponTrinketName", "3a", "", "" };
        sc.field_names_ordered.push_back("WeaponTrinketName");
        sc.fields["ArmorTrinketName"] = { "ArmorTrinketName", "42", "", "" };
        sc.field_names_ordered.push_back("ArmorTrinketName");
        sc.fields["CurrentLevelTitle"] = { "CurrentLevelTitle", "4a", "", "" };
        sc.field_names_ordered.push_back("CurrentLevelTitle");
        sc.fields["LastPlayedTime"] = { "LastPlayedTime", "52", "", "DateTime" };
        sc.field_names_ordered.push_back("LastPlayedTime");
        sc.fields["PercentCompleted"] = { "PercentCompleted", "5d", "", "" };
        sc.field_names_ordered.push_back("PercentCompleted");
        sc.fields["Counter"] = { "Counter", "62", "", "PlayerProfile_Counter" };
        sc.field_names_ordered.push_back("Counter");
        sc.fields["CheatEnabled"] = { "CheatEnabled", "68", "", "" };
        sc.field_names_ordered.push_back("CheatEnabled");
        sc.fields["Identifier"] = { "Identifier", "72", "", "" };
        sc.field_names_ordered.push_back("Identifier");
        sc.fields["CharacterState"] = { "CharacterState", "0a", "", "CharacterState" };
        sc.field_names_ordered.push_back("CharacterState");
        sc.fields["LevelState"] = { "LevelState", "12", "", "LevelState" };
        sc.field_names_ordered.push_back("LevelState");
        sc.fields["CurrentLevel"] = { "CurrentLevel", "1a", "", "" };
        sc.field_names_ordered.push_back("CurrentLevel");
        sc.fields["CurrentSpawnPoint"] = { "CurrentSpawnPoint", "22", "", "" };
        sc.field_names_ordered.push_back("CurrentSpawnPoint");
        sc.fields["CurrentMapNodeName"] = { "CurrentMapNodeName", "2a", "", "" };
        sc.field_names_ordered.push_back("CurrentMapNodeName");
        sc.fields["QuestState"] = { "QuestState", "3a", "", "QuestState" };
        sc.field_names_ordered.push_back("QuestState");
        sc.fields["Properties"] = { "Properties", "42", "", "StateProperties" };
        sc.field_names_ordered.push_back("Properties");
        sc.fields["SelectedMenuTab"] = { "SelectedMenuTab", "4a", "", "" };
        sc.field_names_ordered.push_back("SelectedMenuTab");
        sc.fields["CarriedObjectTemplate"] = { "CarriedObjectTemplate", "52", "", "" };
        sc.field_names_ordered.push_back("CarriedObjectTemplate");
        sc.fields["CarriedObjectIdentifier"] = { "CarriedObjectIdentifier", "5a", "", "" };
        sc.field_names_ordered.push_back("CarriedObjectIdentifier");
        sc.fields["QuestText"] = { "QuestText", "62", "", "QuestText" };
        sc.field_names_ordered.push_back("QuestText");
        sc.fields["PreviousPortalLevel"] = { "PreviousPortalLevel", "6a", "", "" };
        sc.field_names_ordered.push_back("PreviousPortalLevel");
        sc.fields["MenuButtonFlashing"] = { "MenuButtonFlashing", "70", "", "" };
        sc.field_names_ordered.push_back("MenuButtonFlashing");
        sc.fields["SkillToggleButtonFlashing"] = { "SkillToggleButtonFlashing", "78", "", "" };
        sc.field_names_ordered.push_back("SkillToggleButtonFlashing");
        sc.fields["FlashingItemName"] = { "FlashingItemName", "82", "", "" };
        sc.field_names_ordered.push_back("FlashingItemName");
        sc.fields["FlashingSkillName"] = { "FlashingSkillName", "8a", "", "" };
        sc.field_names_ordered.push_back("FlashingSkillName");
        sc.fields["GuideEnabled"] = { "GuideEnabled", "90", "", "" };
        sc.field_names_ordered.push_back("GuideEnabled");
        sc.fields["GuideToggled"] = { "GuideToggled", "98", "", "" };
        sc.field_names_ordered.push_back("GuideToggled");
        sc.fields["CoinDoublerEnabled"] = { "CoinDoublerEnabled", "a0", "", "" };
        sc.field_names_ordered.push_back("CoinDoublerEnabled");
        sc.fields["CoinDoublerToggled"] = { "CoinDoublerToggled", "a8", "", "" };
        sc.field_names_ordered.push_back("CoinDoublerToggled");
        sc.fields["Zone"] = { "Zone", "12", "", "MapZone" };
        sc.field_names_ordered.push_back("Zone");
        sc.fields["Effect"] = { "Effect", "0a", "", "SoundEffect" };
        sc.field_names_ordered.push_back("Effect");
        sc.fields["Glyph"] = { "Glyph", "1a", "", "Font_Glyph" };
        sc.field_names_ordered.push_back("Glyph");
        sc.fields["Kerning"] = { "Kerning", "22", "", "" };
        sc.field_names_ordered.push_back("Kerning");
        sc.fields["Height"] = { "Height", "28", "", "" };
        sc.field_names_ordered.push_back("Height");
        sc.fields["BoundingBox"] = { "BoundingBox", "32", "", "Rectangle" };
        sc.field_names_ordered.push_back("BoundingBox");
        sc.fields["PixelFormat"] = { "PixelFormat", "10", "", "" };
        sc.field_names_ordered.push_back("PixelFormat");
        sc.fields["Subtexture"] = { "Subtexture", "1a", "", "Texture_Subtexture" };
        sc.field_names_ordered.push_back("Subtexture");
        sc.fields["ImageType"] = { "ImageType", "20", "", "" };
        sc.field_names_ordered.push_back("ImageType");
        sc.fields["ConversionInfo"] = { "ConversionInfo", "2a", "", "Texture_ConversionInfo" };
        sc.field_names_ordered.push_back("ConversionInfo");
    }
    {
        ScopeDef& sc = m_scopes["scl"];
        sc.name = "scl";
        sc.fields["Name"] = { "Name", "0a", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["Template"] = { "Template", "12", "", "ObjectTemplate" };
        sc.field_names_ordered.push_back("Template");
        sc.fields["ImportedLibrary"] = { "ImportedLibrary", "1a", "", "" };
        sc.field_names_ordered.push_back("ImportedLibrary");
        sc.fields["Texture"] = { "Texture", "22", "", "Texture" };
        sc.field_names_ordered.push_back("Texture");
        sc.fields["Program"] = { "Program", "2a", "", "Program" };
        sc.field_names_ordered.push_back("Program");
        sc.fields["Object"] = { "Object", "0a", "", "SceneObject" };
        sc.field_names_ordered.push_back("Object");
        sc.fields["ObjectLibrary"] = { "ObjectLibrary", "12", "", "ObjectLibrary" };
        sc.field_names_ordered.push_back("ObjectLibrary");
        sc.fields["Bounds"] = { "Bounds", "1a", "", "SceneBounds" };
        sc.field_names_ordered.push_back("Bounds");
        sc.fields["Group"] = { "Group", "22", "", "SceneObjectGroup" };
        sc.field_names_ordered.push_back("Group");
        sc.fields["OnLoad"] = { "OnLoad", "2a", "", "Program" };
        sc.field_names_ordered.push_back("OnLoad");
    }
    {
        ScopeDef& sc = m_scopes["scene"];
        sc.name = "scene";
        sc.fields["Object"] = { "Object", "0a", "", "SceneObject" };
        sc.field_names_ordered.push_back("Object");
        sc.fields["ObjectLibrary"] = { "ObjectLibrary", "12", "", "ObjectLibrary" };
        sc.field_names_ordered.push_back("ObjectLibrary");
        sc.fields["Bounds"] = { "Bounds", "1a", "", "SceneBounds" };
        sc.field_names_ordered.push_back("Bounds");
        sc.fields["Group"] = { "Group", "22", "", "SceneObjectGroup" };
        sc.field_names_ordered.push_back("Group");
        sc.fields["OnLoad"] = { "OnLoad", "2a", "", "Program" };
        sc.field_names_ordered.push_back("OnLoad");
    }
    {
        ScopeDef& sc = m_scopes["gdata"];
        sc.name = "gdata";
        sc.fields["Item"] = { "Item", "0a", "", "Item" };
        sc.field_names_ordered.push_back("Item");
        sc.fields["Skill"] = { "Skill", "12", "", "Skill" };
        sc.field_names_ordered.push_back("Skill");
        sc.fields["Quest"] = { "Quest", "1a", "", "Quest" };
        sc.field_names_ordered.push_back("Quest");
        sc.fields["EntityClass"] = { "EntityClass", "22", "", "EntityClass" };
        sc.field_names_ordered.push_back("EntityClass");
        sc.fields["GuideTarget"] = { "GuideTarget", "2a", "", "GuideTarget" };
        sc.field_names_ordered.push_back("GuideTarget");
    }
    {
        ScopeDef& sc = m_scopes["gopt"];
        sc.name = "gopt";
        sc.fields["Playlist"] = { "Playlist", "0a", "", "MusicPlaylist" };
        sc.field_names_ordered.push_back("Playlist");
        sc.fields["MusicEnabled"] = { "MusicEnabled", "10", "", "" };
        sc.field_names_ordered.push_back("MusicEnabled");
        sc.fields["SoundEnabled"] = { "SoundEnabled", "18", "", "" };
        sc.field_names_ordered.push_back("SoundEnabled");
        sc.fields["MusicVolume"] = { "MusicVolume", "25", "", "" };
        sc.field_names_ordered.push_back("MusicVolume");
        sc.fields["SoundVolume"] = { "SoundVolume", "2d", "", "" };
        sc.field_names_ordered.push_back("SoundVolume");
        sc.fields["PhoneControlsLayout"] = { "PhoneControlsLayout", "32", "", "GUIViewLayout" };
        sc.field_names_ordered.push_back("PhoneControlsLayout");
        sc.fields["PadControlsLayout"] = { "PadControlsLayout", "3a", "", "GUIViewLayout" };
        sc.field_names_ordered.push_back("PadControlsLayout");
        sc.fields["GuideUnlocked"] = { "GuideUnlocked", "40", "", "" };
        sc.field_names_ordered.push_back("GuideUnlocked");
        sc.fields["CoinDoublerUnlocked"] = { "CoinDoublerUnlocked", "48", "", "" };
        sc.field_names_ordered.push_back("CoinDoublerUnlocked");
        sc.fields["NoAdsUnlocked"] = { "NoAdsUnlocked", "50", "", "" };
        sc.field_names_ordered.push_back("NoAdsUnlocked");
    }
    {
        ScopeDef& sc = m_scopes["gplayer"];
        sc.name = "gplayer";
        sc.fields["Name"] = { "Name", "0a", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["ExperienceLevel"] = { "ExperienceLevel", "10", "", "" };
        sc.field_names_ordered.push_back("ExperienceLevel");
        sc.fields["TimePlayed"] = { "TimePlayed", "19", "", "" };
        sc.field_names_ordered.push_back("TimePlayed");
        sc.fields["GameState"] = { "GameState", "22", "", "GameState" };
        sc.field_names_ordered.push_back("GameState");
        sc.fields["EquippedWeaponName"] = { "EquippedWeaponName", "2a", "", "" };
        sc.field_names_ordered.push_back("EquippedWeaponName");
        sc.fields["EquippedArmorName"] = { "EquippedArmorName", "32", "", "" };
        sc.field_names_ordered.push_back("EquippedArmorName");
        sc.fields["WeaponTrinketName"] = { "WeaponTrinketName", "3a", "", "" };
        sc.field_names_ordered.push_back("WeaponTrinketName");
        sc.fields["ArmorTrinketName"] = { "ArmorTrinketName", "42", "", "" };
        sc.field_names_ordered.push_back("ArmorTrinketName");
        sc.fields["CurrentLevelTitle"] = { "CurrentLevelTitle", "4a", "", "" };
        sc.field_names_ordered.push_back("CurrentLevelTitle");
        sc.fields["LastPlayedTime"] = { "LastPlayedTime", "52", "", "DateTime" };
        sc.field_names_ordered.push_back("LastPlayedTime");
        sc.fields["PercentCompleted"] = { "PercentCompleted", "5d", "", "" };
        sc.field_names_ordered.push_back("PercentCompleted");
        sc.fields["Counter"] = { "Counter", "62", "", "PlayerProfile_Counter" };
        sc.field_names_ordered.push_back("Counter");
        sc.fields["CheatEnabled"] = { "CheatEnabled", "68", "", "" };
        sc.field_names_ordered.push_back("CheatEnabled");
        sc.fields["Identifier"] = { "Identifier", "72", "", "" };
        sc.field_names_ordered.push_back("Identifier");
    }
    {
        ScopeDef& sc = m_scopes["gstate"];
        sc.name = "gstate";
        sc.fields["CharacterState"] = { "CharacterState", "0a", "", "CharacterState" };
        sc.field_names_ordered.push_back("CharacterState");
        sc.fields["LevelState"] = { "LevelState", "12", "", "LevelState" };
        sc.field_names_ordered.push_back("LevelState");
        sc.fields["CurrentLevel"] = { "CurrentLevel", "1a", "", "" };
        sc.field_names_ordered.push_back("CurrentLevel");
        sc.fields["CurrentSpawnPoint"] = { "CurrentSpawnPoint", "22", "", "" };
        sc.field_names_ordered.push_back("CurrentSpawnPoint");
        sc.fields["CurrentMapNodeName"] = { "CurrentMapNodeName", "2a", "", "" };
        sc.field_names_ordered.push_back("CurrentMapNodeName");
        sc.fields["QuestState"] = { "QuestState", "3a", "", "QuestState" };
        sc.field_names_ordered.push_back("QuestState");
        sc.fields["Properties"] = { "Properties", "42", "", "StateProperties" };
        sc.field_names_ordered.push_back("Properties");
        sc.fields["SelectedMenuTab"] = { "SelectedMenuTab", "4a", "", "" };
        sc.field_names_ordered.push_back("SelectedMenuTab");
        sc.fields["CarriedObjectTemplate"] = { "CarriedObjectTemplate", "52", "", "" };
        sc.field_names_ordered.push_back("CarriedObjectTemplate");
        sc.fields["CarriedObjectIdentifier"] = { "CarriedObjectIdentifier", "5a", "", "" };
        sc.field_names_ordered.push_back("CarriedObjectIdentifier");
        sc.fields["QuestText"] = { "QuestText", "62", "", "QuestText" };
        sc.field_names_ordered.push_back("QuestText");
        sc.fields["PreviousPortalLevel"] = { "PreviousPortalLevel", "6a", "", "" };
        sc.field_names_ordered.push_back("PreviousPortalLevel");
        sc.fields["MenuButtonFlashing"] = { "MenuButtonFlashing", "70", "", "" };
        sc.field_names_ordered.push_back("MenuButtonFlashing");
        sc.fields["SkillToggleButtonFlashing"] = { "SkillToggleButtonFlashing", "78", "", "" };
        sc.field_names_ordered.push_back("SkillToggleButtonFlashing");
        sc.fields["FlashingItemName"] = { "FlashingItemName", "82", "", "" };
        sc.field_names_ordered.push_back("FlashingItemName");
        sc.fields["FlashingSkillName"] = { "FlashingSkillName", "8a", "", "" };
        sc.field_names_ordered.push_back("FlashingSkillName");
        sc.fields["GuideEnabled"] = { "GuideEnabled", "90", "", "" };
        sc.field_names_ordered.push_back("GuideEnabled");
        sc.fields["GuideToggled"] = { "GuideToggled", "98", "", "" };
        sc.field_names_ordered.push_back("GuideToggled");
        sc.fields["CoinDoublerEnabled"] = { "CoinDoublerEnabled", "a0", "", "" };
        sc.field_names_ordered.push_back("CoinDoublerEnabled");
        sc.fields["CoinDoublerToggled"] = { "CoinDoublerToggled", "a8", "", "" };
        sc.field_names_ordered.push_back("CoinDoublerToggled");
    }
    {
        ScopeDef& sc = m_scopes["scmap"];
        sc.name = "scmap";
        sc.fields["Zone"] = { "Zone", "12", "", "MapZone" };
        sc.field_names_ordered.push_back("Zone");
    }
    {
        ScopeDef& sc = m_scopes["sounds"];
        sc.name = "sounds";
        sc.fields["Effect"] = { "Effect", "0a", "", "SoundEffect" };
        sc.field_names_ordered.push_back("Effect");
    }
    {
        ScopeDef& sc = m_scopes["fnt"];
        sc.name = "fnt";
        sc.fields["Name"] = { "Name", "0a", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["Texture"] = { "Texture", "12", "", "Texture" };
        sc.field_names_ordered.push_back("Texture");
        sc.fields["Glyph"] = { "Glyph", "1a", "", "Font_Glyph" };
        sc.field_names_ordered.push_back("Glyph");
        sc.fields["Kerning"] = { "Kerning", "22", "", "" };
        sc.field_names_ordered.push_back("Kerning");
        sc.fields["Height"] = { "Height", "28", "", "" };
        sc.field_names_ordered.push_back("Height");
        sc.fields["BoundingBox"] = { "BoundingBox", "32", "", "Rectangle" };
        sc.field_names_ordered.push_back("BoundingBox");
    }
    {
        ScopeDef& sc = m_scopes["atlas"];
        sc.name = "atlas";
        sc.fields["Name"] = { "Name", "0a", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["PixelFormat"] = { "PixelFormat", "10", "", "" };
        sc.field_names_ordered.push_back("PixelFormat");
        sc.fields["Subtexture"] = { "Subtexture", "1a", "", "Texture_Subtexture" };
        sc.field_names_ordered.push_back("Subtexture");
        sc.fields["ImageType"] = { "ImageType", "20", "", "" };
        sc.field_names_ordered.push_back("ImageType");
        sc.fields["ConversionInfo"] = { "ConversionInfo", "2a", "", "Texture_ConversionInfo" };
        sc.field_names_ordered.push_back("ConversionInfo");
    }
    {
        ScopeDef& sc = m_scopes["Scene"];
        sc.name = "Scene";
        sc.fields["Object"] = { "Object", "0a", "", "SceneObject" };
        sc.field_names_ordered.push_back("Object");
        sc.fields["ObjectLibrary"] = { "ObjectLibrary", "12", "", "ObjectLibrary" };
        sc.field_names_ordered.push_back("ObjectLibrary");
        sc.fields["Bounds"] = { "Bounds", "1a", "", "SceneBounds" };
        sc.field_names_ordered.push_back("Bounds");
        sc.fields["Group"] = { "Group", "22", "", "SceneObjectGroup" };
        sc.field_names_ordered.push_back("Group");
        sc.fields["OnLoad"] = { "OnLoad", "2a", "", "Program" };
        sc.field_names_ordered.push_back("OnLoad");
    }
    {
        ScopeDef& sc = m_scopes["SceneObject"];
        sc.name = "SceneObject";
        sc.fields["TemplateName"] = { "TemplateName", "0a", "[optional] reference to a template to initialize the object from", "" };
        sc.field_names_ordered.push_back("TemplateName");
        sc.fields["Identifier"] = { "Identifier", "12", "used to refer to this object", "" };
        sc.field_names_ordered.push_back("Identifier");
        sc.fields["Component"] = { "Component", "1a", "", "Component" };
        sc.field_names_ordered.push_back("Component");
        sc.fields["Position"] = { "Position", "22", "", "Vector2" };
        sc.field_names_ordered.push_back("Position");
        sc.fields["Depth"] = { "Depth", "2d", "", "" };
        sc.field_names_ordered.push_back("Depth");
        sc.fields["Rotation"] = { "Rotation", "35", "", "" };
        sc.field_names_ordered.push_back("Rotation");
        sc.fields["Scaling"] = { "Scaling", "3d", "", "" };
        sc.field_names_ordered.push_back("Scaling");
        sc.fields["LocalAabb"] = { "LocalAabb", "42", "", "Rectangle" };
        sc.field_names_ordered.push_back("LocalAabb");
        sc.fields["Hidden"] = { "Hidden", "48", "", "" };
        sc.field_names_ordered.push_back("Hidden");
        sc.fields["OnLoad"] = { "OnLoad", "52", "", "Program" };
        sc.field_names_ordered.push_back("OnLoad");
    }
    {
        ScopeDef& sc = m_scopes["ObjectLibrary"];
        sc.name = "ObjectLibrary";
        sc.fields["Name"] = { "Name", "0a", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["Template"] = { "Template", "12", "", "ObjectTemplate" };
        sc.field_names_ordered.push_back("Template");
        sc.fields["ImportedLibrary"] = { "ImportedLibrary", "1a", "", "" };
        sc.field_names_ordered.push_back("ImportedLibrary");
        sc.fields["Texture"] = { "Texture", "22", "", "Texture" };
        sc.field_names_ordered.push_back("Texture");
        sc.fields["Program"] = { "Program", "2a", "", "Program" };
        sc.field_names_ordered.push_back("Program");
    }
    {
        ScopeDef& sc = m_scopes["ObjectTemplate"];
        sc.name = "ObjectTemplate";
        sc.fields["Object"] = { "Object", "0a", "", "SceneObject" };
        sc.field_names_ordered.push_back("Object");
        sc.fields["Scaling"] = { "Scaling", "15", "", "" };
        sc.field_names_ordered.push_back("Scaling");
        sc.fields["Identifier"] = { "Identifier", "12", "", "" };
        sc.field_names_ordered.push_back("Identifier");
        sc.fields["Component"] = { "Component", "1a", "", "Component" };
        sc.field_names_ordered.push_back("Component");
        sc.fields["Position"] = { "Position", "22", "", "Vector2" };
        sc.field_names_ordered.push_back("Position");
        sc.fields["Depth"] = { "Depth", "2d", "", "" };
        sc.field_names_ordered.push_back("Depth");
        sc.fields["Rotation"] = { "Rotation", "35", "", "" };
        sc.field_names_ordered.push_back("Rotation");
    }
    {
        ScopeDef& sc = m_scopes["SceneBounds"];
        sc.name = "SceneBounds";
        sc.fields["Top"] = { "Top", "0d", "level max height", "" };
        sc.field_names_ordered.push_back("Top");
        sc.fields["Left"] = { "Left", "15", "", "" };
        sc.field_names_ordered.push_back("Left");
        sc.fields["Right"] = { "Right", "1d", "", "" };
        sc.field_names_ordered.push_back("Right");
        sc.fields["Bottom"] = { "Bottom", "25", "", "" };
        sc.field_names_ordered.push_back("Bottom");
    }
    {
        ScopeDef& sc = m_scopes["SceneObjectGroup"];
        sc.name = "SceneObjectGroup";
        sc.fields["Identifier"] = { "Identifier", "0a", "", "" };
        sc.field_names_ordered.push_back("Identifier");
        sc.fields["ObjectIdentifier"] = { "ObjectIdentifier", "12", "", "" };
        sc.field_names_ordered.push_back("ObjectIdentifier");
        sc.fields["Hidden"] = { "Hidden", "18", "", "" };
        sc.field_names_ordered.push_back("Hidden");
        sc.fields["OnLoad"] = { "OnLoad", "22", "", "Program" };
        sc.field_names_ordered.push_back("OnLoad");
        sc.fields["CanBecomeActive"] = { "CanBecomeActive", "28", "", "" };
        sc.field_names_ordered.push_back("CanBecomeActive");
        sc.fields["Locked"] = { "Locked", "30", "", "" };
        sc.field_names_ordered.push_back("Locked");
    }
    {
        ScopeDef& sc = m_scopes["GameData"];
        sc.name = "GameData";
        sc.fields["Item"] = { "Item", "0a", "", "Item" };
        sc.field_names_ordered.push_back("Item");
        sc.fields["Skill"] = { "Skill", "12", "", "Skill" };
        sc.field_names_ordered.push_back("Skill");
        sc.fields["Quest"] = { "Quest", "1a", "", "Quest" };
        sc.field_names_ordered.push_back("Quest");
        sc.fields["EntityClass"] = { "EntityClass", "22", "", "EntityClass" };
        sc.field_names_ordered.push_back("EntityClass");
        sc.fields["GuideTarget"] = { "GuideTarget", "2a", "", "GuideTarget" };
        sc.field_names_ordered.push_back("GuideTarget");
    }
    {
        ScopeDef& sc = m_scopes["Item"];
        sc.name = "Item";
        sc.fields["Type"] = { "Type", "08", "1:CONSUMABLE 2:WEAPON 3:ARMOR 5:QUEST", "" };
        sc.field_names_ordered.push_back("Type");
        sc.fields["Name"] = { "Name", "12", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["Title"] = { "Title", "1a", "", "" };
        sc.field_names_ordered.push_back("Title");
        sc.fields["ShortDescription"] = { "ShortDescription", "22", "", "" };
        sc.field_names_ordered.push_back("ShortDescription");
        sc.fields["Description"] = { "Description", "2a", "", "" };
        sc.field_names_ordered.push_back("Description");
        sc.fields["Unique"] = { "Unique", "30", "", "" };
        sc.field_names_ordered.push_back("Unique");
        sc.fields["MinDamage"] = { "MinDamage", "38", "", "" };
        sc.field_names_ordered.push_back("MinDamage");
        sc.fields["MaxDamage"] = { "MaxDamage", "40", "", "" };
        sc.field_names_ordered.push_back("MaxDamage");
        sc.fields["Level"] = { "Level", "48", "", "" };
        sc.field_names_ordered.push_back("Level");
    }
    {
        ScopeDef& sc = m_scopes["Skill"];
        sc.name = "Skill";
        sc.fields["Name"] = { "Name", "0a", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["Title"] = { "Title", "12", "", "" };
        sc.field_names_ordered.push_back("Title");
        sc.fields["Description"] = { "Description", "1a", "", "" };
        sc.field_names_ordered.push_back("Description");
        sc.fields["ManaCost"] = { "ManaCost", "20", "", "" };
        sc.field_names_ordered.push_back("ManaCost");
        sc.fields["MinDamage"] = { "MinDamage", "28", "", "" };
        sc.field_names_ordered.push_back("MinDamage");
        sc.fields["MaxDamage"] = { "MaxDamage", "30", "", "" };
        sc.field_names_ordered.push_back("MaxDamage");
    }
    {
        ScopeDef& sc = m_scopes["Quest"];
        sc.name = "Quest";
        sc.fields["Name"] = { "Name", "0a", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["Title"] = { "Title", "12", "", "" };
        sc.field_names_ordered.push_back("Title");
        sc.fields["FollowUpQuest"] = { "FollowUpQuest", "1a", "", "" };
        sc.field_names_ordered.push_back("FollowUpQuest");
        sc.fields["MapLocation"] = { "MapLocation", "22", "", "" };
        sc.field_names_ordered.push_back("MapLocation");
    }
    {
        ScopeDef& sc = m_scopes["EntityClass"];
        sc.name = "EntityClass";
        sc.fields["Name"] = { "Name", "0a", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["Title"] = { "Title", "12", "", "" };
        sc.field_names_ordered.push_back("Title");
        sc.fields["LevelHidden"] = { "LevelHidden", "18", "", "" };
        sc.field_names_ordered.push_back("LevelHidden");
        sc.fields["Freezable"] = { "Freezable", "20", "", "" };
        sc.field_names_ordered.push_back("Freezable");
        sc.fields["Stunnable"] = { "Stunnable", "28", "", "" };
        sc.field_names_ordered.push_back("Stunnable");
        sc.fields["Grabbable"] = { "Grabbable", "30", "", "" };
        sc.field_names_ordered.push_back("Grabbable");
        sc.fields["MagicResistance"] = { "MagicResistance", "3d", "", "" };
        sc.field_names_ordered.push_back("MagicResistance");
        sc.fields["PhysicalResistance"] = { "PhysicalResistance", "45", "", "" };
        sc.field_names_ordered.push_back("PhysicalResistance");
    }
    {
        ScopeDef& sc = m_scopes["GuideTarget"];
        sc.name = "GuideTarget";
        sc.fields["Type"] = { "Type", "08", "1:QUESTGET 2:QUEST 3:SPELL 4:KEY", "" };
        sc.field_names_ordered.push_back("Type");
        sc.fields["Name"] = { "Name", "12", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["LevelName"] = { "LevelName", "1a", "", "" };
        sc.field_names_ordered.push_back("LevelName");
        sc.fields["ObjectIdentifier"] = { "ObjectIdentifier", "22", "", "" };
        sc.field_names_ordered.push_back("ObjectIdentifier");
        sc.fields["CarryObjectIdentifier"] = { "CarryObjectIdentifier", "2a", "", "" };
        sc.field_names_ordered.push_back("CarryObjectIdentifier");
        sc.fields["ShowOnlyAfterSceneLoad"] = { "ShowOnlyAfterSceneLoad", "30", "", "" };
        sc.field_names_ordered.push_back("ShowOnlyAfterSceneLoad");
        sc.fields["PortalHint"] = { "PortalHint", "3a", "", "GuideTarget_LevelObject" };
        sc.field_names_ordered.push_back("PortalHint");
    }
    {
        ScopeDef& sc = m_scopes["GuideTarget_LevelObject"];
        sc.name = "GuideTarget_LevelObject";
        sc.fields["LevelName"] = { "LevelName", "0a", "", "" };
        sc.field_names_ordered.push_back("LevelName");
        sc.fields["ObjectIdentifier"] = { "ObjectIdentifier", "12", "", "" };
        sc.field_names_ordered.push_back("ObjectIdentifier");
    }
    {
        ScopeDef& sc = m_scopes["GameOptions"];
        sc.name = "GameOptions";
        sc.fields["Playlist"] = { "Playlist", "0a", "", "MusicPlaylist" };
        sc.field_names_ordered.push_back("Playlist");
        sc.fields["MusicEnabled"] = { "MusicEnabled", "10", "", "" };
        sc.field_names_ordered.push_back("MusicEnabled");
        sc.fields["SoundEnabled"] = { "SoundEnabled", "unk", "", "" };
        sc.field_names_ordered.push_back("SoundEnabled");
        sc.fields["MusicVolume"] = { "MusicVolume", "unk", "", "" };
        sc.field_names_ordered.push_back("MusicVolume");
        sc.fields["SoundVolume"] = { "SoundVolume", "unk", "", "" };
        sc.field_names_ordered.push_back("SoundVolume");
        sc.fields["PhoneControlsLayout"] = { "PhoneControlsLayout", "32", "", "GUIViewLayout" };
        sc.field_names_ordered.push_back("PhoneControlsLayout");
        sc.fields["PadControlsLayout"] = { "PadControlsLayout", "3a", "", "GUIViewLayout" };
        sc.field_names_ordered.push_back("PadControlsLayout");
        sc.fields["GuideUnlocked"] = { "GuideUnlocked", "unk", "", "" };
        sc.field_names_ordered.push_back("GuideUnlocked");
        sc.fields["CoinDoublerUnlocked"] = { "CoinDoublerUnlocked", "unk", "", "" };
        sc.field_names_ordered.push_back("CoinDoublerUnlocked");
        sc.fields["NoAdsUnlocked"] = { "NoAdsUnlocked", "unk", "", "" };
        sc.field_names_ordered.push_back("NoAdsUnlocked");
    }
    {
        ScopeDef& sc = m_scopes["MusicPlaylist"];
        sc.name = "MusicPlaylist";
        sc.fields["Name"] = { "Name", "0a", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["Track"] = { "Track", "12", "", "MusicTrack" };
        sc.field_names_ordered.push_back("Track");
    }
    {
        ScopeDef& sc = m_scopes["MusicTrack"];
        sc.name = "MusicTrack";
        sc.fields["ResourceName"] = { "ResourceName", "0a", "", "" };
        sc.field_names_ordered.push_back("ResourceName");
        sc.fields["Volume"] = { "Volume", "15", "", "" };
        sc.field_names_ordered.push_back("Volume");
    }
    {
        ScopeDef& sc = m_scopes["GUIViewLayout"];
        sc.name = "GUIViewLayout";
        sc.fields["Identifier"] = { "Identifier", "0a", "", "" };
        sc.field_names_ordered.push_back("Identifier");
        sc.fields["Subview"] = { "Subview", "12", "", "GUIViewLayout" };
        sc.field_names_ordered.push_back("Subview");
        sc.fields["Margins"] = { "Margins", "1a", "", "GUIMargins" };
        sc.field_names_ordered.push_back("Margins");
    }
    {
        ScopeDef& sc = m_scopes["GUIMargins"];
        sc.name = "GUIMargins";
        sc.fields["Left"] = { "Left", "0d", "", "" };
        sc.field_names_ordered.push_back("Left");
        sc.fields["Right"] = { "Right", "15", "", "" };
        sc.field_names_ordered.push_back("Right");
        sc.fields["Bottom"] = { "Bottom", "1d", "", "" };
        sc.field_names_ordered.push_back("Bottom");
        sc.fields["Top"] = { "Top", "25", "", "" };
        sc.field_names_ordered.push_back("Top");
    }
    {
        ScopeDef& sc = m_scopes["PlayerProfile"];
        sc.name = "PlayerProfile";
        sc.fields["Name"] = { "Name", "0a", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["ExperienceLevel"] = { "ExperienceLevel", "10", "", "" };
        sc.field_names_ordered.push_back("ExperienceLevel");
        sc.fields["TimePlayed"] = { "TimePlayed", "19", "", "" };
        sc.field_names_ordered.push_back("TimePlayed");
        sc.fields["GameState"] = { "GameState", "22", "", "GameState" };
        sc.field_names_ordered.push_back("GameState");
        sc.fields["EquippedWeaponName"] = { "EquippedWeaponName", "2a", "", "" };
        sc.field_names_ordered.push_back("EquippedWeaponName");
        sc.fields["EquippedArmorName"] = { "EquippedArmorName", "32", "", "" };
        sc.field_names_ordered.push_back("EquippedArmorName");
        sc.fields["WeaponTrinketName"] = { "WeaponTrinketName", "3a", "", "" };
        sc.field_names_ordered.push_back("WeaponTrinketName");
        sc.fields["ArmorTrinketName"] = { "ArmorTrinketName", "42", "", "" };
        sc.field_names_ordered.push_back("ArmorTrinketName");
        sc.fields["CurrentLevelTitle"] = { "CurrentLevelTitle", "4a", "", "" };
        sc.field_names_ordered.push_back("CurrentLevelTitle");
        sc.fields["LastPlayedTime"] = { "LastPlayedTime", "52", "", "DateTime" };
        sc.field_names_ordered.push_back("LastPlayedTime");
        sc.fields["PercentCompleted"] = { "PercentCompleted", "5d", "", "" };
        sc.field_names_ordered.push_back("PercentCompleted");
        sc.fields["Counter"] = { "Counter", "62", "", "PlayerProfile_Counter" };
        sc.field_names_ordered.push_back("Counter");
        sc.fields["CheatEnabled"] = { "CheatEnabled", "68", "", "" };
        sc.field_names_ordered.push_back("CheatEnabled");
        sc.fields["Identifier"] = { "Identifier", "72", "", "" };
        sc.field_names_ordered.push_back("Identifier");
    }
    {
        ScopeDef& sc = m_scopes["GameState"];
        sc.name = "GameState";
        sc.fields["CharacterState"] = { "CharacterState", "0a", "", "CharacterState" };
        sc.field_names_ordered.push_back("CharacterState");
        sc.fields["LevelState"] = { "LevelState", "12", "", "LevelState" };
        sc.field_names_ordered.push_back("LevelState");
        sc.fields["CurrentLevel"] = { "CurrentLevel", "1a", "", "" };
        sc.field_names_ordered.push_back("CurrentLevel");
        sc.fields["CurrentSpawnPoint"] = { "CurrentSpawnPoint", "22", "", "" };
        sc.field_names_ordered.push_back("CurrentSpawnPoint");
        sc.fields["CurrentMapNodeName"] = { "CurrentMapNodeName", "2a", "", "" };
        sc.field_names_ordered.push_back("CurrentMapNodeName");
        sc.fields["QuestState"] = { "QuestState", "3a", "", "QuestState" };
        sc.field_names_ordered.push_back("QuestState");
        sc.fields["Properties"] = { "Properties", "42", "", "StateProperties" };
        sc.field_names_ordered.push_back("Properties");
        sc.fields["SelectedMenuTab"] = { "SelectedMenuTab", "4a", "", "" };
        sc.field_names_ordered.push_back("SelectedMenuTab");
        sc.fields["CarriedObjectTemplate"] = { "CarriedObjectTemplate", "52", "", "" };
        sc.field_names_ordered.push_back("CarriedObjectTemplate");
        sc.fields["CarriedObjectIdentifier"] = { "CarriedObjectIdentifier", "5a", "", "" };
        sc.field_names_ordered.push_back("CarriedObjectIdentifier");
        sc.fields["QuestText"] = { "QuestText", "62", "", "QuestText" };
        sc.field_names_ordered.push_back("QuestText");
        sc.fields["PreviousPortalLevel"] = { "PreviousPortalLevel", "6a", "", "" };
        sc.field_names_ordered.push_back("PreviousPortalLevel");
        sc.fields["MenuButtonFlashing"] = { "MenuButtonFlashing", "70", "", "" };
        sc.field_names_ordered.push_back("MenuButtonFlashing");
        sc.fields["SkillToggleButtonFlashing"] = { "SkillToggleButtonFlashing", "78", "", "" };
        sc.field_names_ordered.push_back("SkillToggleButtonFlashing");
        sc.fields["FlashingItemName"] = { "FlashingItemName", "82", "", "" };
        sc.field_names_ordered.push_back("FlashingItemName");
        sc.fields["FlashingSkillName"] = { "FlashingSkillName", "8a", "", "" };
        sc.field_names_ordered.push_back("FlashingSkillName");
        sc.fields["GuideEnabled"] = { "GuideEnabled", "90", "", "" };
        sc.field_names_ordered.push_back("GuideEnabled");
        sc.fields["GuideToggled"] = { "GuideToggled", "98", "", "" };
        sc.field_names_ordered.push_back("GuideToggled");
        sc.fields["CoinDoublerEnabled"] = { "CoinDoublerEnabled", "a0", "", "" };
        sc.field_names_ordered.push_back("CoinDoublerEnabled");
        sc.fields["CoinDoublerToggled"] = { "CoinDoublerToggled", "a8", "", "" };
        sc.field_names_ordered.push_back("CoinDoublerToggled");
    }
    {
        ScopeDef& sc = m_scopes["CharacterState"];
        sc.name = "CharacterState";
        sc.fields["CurrentHealth"] = { "CurrentHealth", "10", "", "" };
        sc.field_names_ordered.push_back("CurrentHealth");
        sc.fields["CurrentMana"] = { "CurrentMana", "20", "", "" };
        sc.field_names_ordered.push_back("CurrentMana");
        sc.fields["CurrentCoins"] = { "CurrentCoins", "28", "", "" };
        sc.field_names_ordered.push_back("CurrentCoins");
        sc.fields["ExperiencePoints"] = { "ExperiencePoints", "30", "", "" };
        sc.field_names_ordered.push_back("ExperiencePoints");
        sc.fields["ExperienceLevel"] = { "ExperienceLevel", "38", "", "" };
        sc.field_names_ordered.push_back("ExperienceLevel");
        sc.fields["Item"] = { "Item", "5a", "", "CharacterState_ItemState" };
        sc.field_names_ordered.push_back("Item");
        sc.fields["EquippedWeapon"] = { "EquippedWeapon", "62", "", "" };
        sc.field_names_ordered.push_back("EquippedWeapon");
        sc.fields["EquippedArmor"] = { "EquippedArmor", "6a", "", "" };
        sc.field_names_ordered.push_back("EquippedArmor");
        sc.fields["Skill"] = { "Skill", "7a", "", "" };
        sc.field_names_ordered.push_back("Skill");
        sc.fields["CurrentSkill"] = { "CurrentSkill", "82", "", "" };
        sc.field_names_ordered.push_back("CurrentSkill");
        sc.fields["WeaponTrinket"] = { "WeaponTrinket", "8a", "", "" };
        sc.field_names_ordered.push_back("WeaponTrinket");
        sc.fields["ArmorTrinket"] = { "ArmorTrinket", "92", "", "" };
        sc.field_names_ordered.push_back("ArmorTrinket");
        sc.fields["SkillTrinket"] = { "SkillTrinket", "9a", "", "" };
        sc.field_names_ordered.push_back("SkillTrinket");
        sc.fields["HealthAttribute"] = { "HealthAttribute", "a0", "", "" };
        sc.field_names_ordered.push_back("HealthAttribute");
        sc.fields["AttackAttribute"] = { "AttackAttribute", "a8", "", "" };
        sc.field_names_ordered.push_back("AttackAttribute");
        sc.fields["MagicAttribute"] = { "MagicAttribute", "b0", "", "" };
        sc.field_names_ordered.push_back("MagicAttribute");
    }
    {
        ScopeDef& sc = m_scopes["CharacterState_ItemState"];
        sc.name = "CharacterState_ItemState";
        sc.fields["Name"] = { "Name", "0a", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["Count"] = { "Count", "10", "", "" };
        sc.field_names_ordered.push_back("Count");
    }
    {
        ScopeDef& sc = m_scopes["LevelState"];
        sc.name = "LevelState";
        sc.fields["LevelName"] = { "LevelName", "0a", "", "" };
        sc.field_names_ordered.push_back("LevelName");
        sc.fields["Visited"] = { "Visited", "10", "", "" };
        sc.field_names_ordered.push_back("Visited");
        sc.fields["Properties"] = { "Properties", "1a", "", "StateProperties" };
        sc.field_names_ordered.push_back("Properties");
        sc.fields["NumTreasures"] = { "NumTreasures", "20", "", "" };
        sc.field_names_ordered.push_back("NumTreasures");
        sc.fields["TreasuresFound"] = { "TreasuresFound", "28", "", "" };
        sc.field_names_ordered.push_back("TreasuresFound");
    }
    {
        ScopeDef& sc = m_scopes["StateProperties"];
        sc.name = "StateProperties";
        sc.fields["Flag"] = { "Flag", "0a", "", "" };
        sc.field_names_ordered.push_back("Flag");
    }
    {
        ScopeDef& sc = m_scopes["QuestState"];
        sc.name = "QuestState";
        sc.fields["QuestName"] = { "QuestName", "0a", "", "" };
        sc.field_names_ordered.push_back("QuestName");
        sc.fields["Completed"] = { "Completed", "10", "", "" };
        sc.field_names_ordered.push_back("Completed");
    }
    {
        ScopeDef& sc = m_scopes["QuestText"];
        sc.name = "QuestText";
        sc.fields["QuestName"] = { "QuestName", "0a", "", "" };
        sc.field_names_ordered.push_back("QuestName");
        sc.fields["Line"] = { "Line", "12", "", "" };
        sc.field_names_ordered.push_back("Line");
    }
    {
        ScopeDef& sc = m_scopes["PlayerProfile_Counter"];
        sc.name = "PlayerProfile_Counter";
        sc.fields["Name"] = { "Name", "0a", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["Value"] = { "Value", "10", "", "" };
        sc.field_names_ordered.push_back("Value");
    }
    {
        ScopeDef& sc = m_scopes["Map"];
        sc.name = "Map";
        sc.fields["Zone"] = { "Zone", "12", "", "MapZone" };
        sc.field_names_ordered.push_back("Zone");
    }
    {
        ScopeDef& sc = m_scopes["MapZone"];
        sc.name = "MapZone";
        sc.fields["Name"] = { "Name", "0a", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["Title"] = { "Title", "12", "", "" };
        sc.field_names_ordered.push_back("Title");
        sc.fields["Node"] = { "Node", "1a", "", "MapNode" };
        sc.field_names_ordered.push_back("Node");
        sc.fields["ExperienceLevel"] = { "ExperienceLevel", "20", "", "" };
        sc.field_names_ordered.push_back("ExperienceLevel");
        sc.fields["Music"] = { "Music", "2a", "", "" };
        sc.field_names_ordered.push_back("Music");
    }
    {
        ScopeDef& sc = m_scopes["MapNode"];
        sc.name = "MapNode";
        sc.fields["Position"] = { "Position", "unk", "", "" };
        sc.field_names_ordered.push_back("Position");
        sc.fields["LevelName"] = { "LevelName", "12", "", "" };
        sc.field_names_ordered.push_back("LevelName");
        sc.fields["Portal"] = { "Portal", "1a", "", "MapNode_Portal" };
        sc.field_names_ordered.push_back("Portal");
        sc.fields["Type"] = { "Type", "20", "0:DEFAULT 1:TOWN 2:WAYPOINT 3:BOSS", "" };
        sc.field_names_ordered.push_back("Type");
        sc.fields["Hidden"] = { "Hidden", "28", "", "" };
        sc.field_names_ordered.push_back("Hidden");
        sc.fields["ExperienceLevel"] = { "ExperienceLevel", "30", "", "" };
        sc.field_names_ordered.push_back("ExperienceLevel");
        sc.fields["Music"] = { "Music", "3a", "", "" };
        sc.field_names_ordered.push_back("Music");
        sc.fields["HasPortal"] = { "HasPortal", "40", "", "" };
        sc.field_names_ordered.push_back("HasPortal");
        sc.fields["NumTreasures"] = { "NumTreasures", "48", "", "" };
        sc.field_names_ordered.push_back("NumTreasures");
        sc.fields["Title"] = { "Title", "52", "", "" };
        sc.field_names_ordered.push_back("Title");
        sc.fields["IgnoreInStatistics"] = { "IgnoreInStatistics", "58", "", "" };
        sc.field_names_ordered.push_back("IgnoreInStatistics");
    }
    {
        ScopeDef& sc = m_scopes["MapNode_Portal"];
        sc.name = "MapNode_Portal";
        sc.fields["DestinationName"] = { "DestinationName", "0a", "", "" };
        sc.field_names_ordered.push_back("DestinationName");
        sc.fields["Direction"] = { "Direction", "10", "1:E 2:NE 3:N 4:NW 5:W 6:SW 7:S 8:SE", "" };
        sc.field_names_ordered.push_back("Direction");
        sc.fields["PassDirection"] = { "PassDirection", "18", "1:E 2:NE 3:N 4:NW 5:W 6:SW 7:S 8:SE", "" };
        sc.field_names_ordered.push_back("PassDirection");
        sc.fields["IgnoreInNodePositioning"] = { "IgnoreInNodePositioning", "20", "", "" };
        sc.field_names_ordered.push_back("IgnoreInNodePositioning");
    }
    {
        ScopeDef& sc = m_scopes["SoundLibrary"];
        sc.name = "SoundLibrary";
        sc.fields["Effect"] = { "Effect", "0a", "", "SoundEffect" };
        sc.field_names_ordered.push_back("Effect");
    }
    {
        ScopeDef& sc = m_scopes["SoundEffect"];
        sc.name = "SoundEffect";
        sc.fields["Name"] = { "Name", "0a", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["ResourceName"] = { "ResourceName", "12", "", "" };
        sc.field_names_ordered.push_back("ResourceName");
        sc.fields["Volume"] = { "Volume", "1d", "", "" };
        sc.field_names_ordered.push_back("Volume");
        sc.fields["MinPlayInterval"] = { "MinPlayInterval", "25", "", "" };
        sc.field_names_ordered.push_back("MinPlayInterval");
    }
    {
        ScopeDef& sc = m_scopes["Font"];
        sc.name = "Font";
        sc.fields["Name"] = { "Name", "0a", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["Texture"] = { "Texture", "12", "", "Texture" };
        sc.field_names_ordered.push_back("Texture");
        sc.fields["Glyph"] = { "Glyph", "1a", "", "Font_Glyph" };
        sc.field_names_ordered.push_back("Glyph");
        sc.fields["Kerning"] = { "Kerning", "22", "", "" };
        sc.field_names_ordered.push_back("Kerning");
        sc.fields["Height"] = { "Height", "28", "", "" };
        sc.field_names_ordered.push_back("Height");
        sc.fields["BoundingBox"] = { "BoundingBox", "32", "", "Rectangle" };
        sc.field_names_ordered.push_back("BoundingBox");
    }
    {
        ScopeDef& sc = m_scopes["Texture"];
        sc.name = "Texture";
        sc.fields["Name"] = { "Name", "0a", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["PixelFormat"] = { "PixelFormat", "10", "", "" };
        sc.field_names_ordered.push_back("PixelFormat");
        sc.fields["Subtexture"] = { "Subtexture", "1a", "", "Texture_Subtexture" };
        sc.field_names_ordered.push_back("Subtexture");
        sc.fields["ImageType"] = { "ImageType", "20", "", "" };
        sc.field_names_ordered.push_back("ImageType");
        sc.fields["ConversionInfo"] = { "ConversionInfo", "2a", "", "Texture_ConversionInfo" };
        sc.field_names_ordered.push_back("ConversionInfo");
    }
    {
        ScopeDef& sc = m_scopes["Font_Glyph"];
        sc.name = "Font_Glyph";
        sc.fields["CharCode"] = { "CharCode", "08", "", "" };
        sc.field_names_ordered.push_back("CharCode");
        sc.fields["DrawBounds"] = { "DrawBounds", "12", "", "Rectangle" };
        sc.field_names_ordered.push_back("DrawBounds");
        sc.fields["HorizAdvance"] = { "HorizAdvance", "18", "", "" };
        sc.field_names_ordered.push_back("HorizAdvance");
        sc.fields["TextureBounds"] = { "TextureBounds", "22", "", "Rectangle" };
        sc.field_names_ordered.push_back("TextureBounds");
    }
    {
        ScopeDef& sc = m_scopes["Texture_Subtexture"];
        sc.name = "Texture_Subtexture";
        sc.fields["Name"] = { "Name", "0a", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["Bounds"] = { "Bounds", "12", "", "Rectangle" };
        sc.field_names_ordered.push_back("Bounds");
        sc.fields["Resolution"] = { "Resolution", "1d", "", "" };
        sc.field_names_ordered.push_back("Resolution");
    }
    {
        ScopeDef& sc = m_scopes["Texture_ConversionInfo"];
        sc.name = "Texture_ConversionInfo";
        sc.fields["Width"] = { "Width", "unk", "", "" };
        sc.field_names_ordered.push_back("Width");
        sc.fields["Height"] = { "Height", "unk", "", "" };
        sc.field_names_ordered.push_back("Height");
        sc.fields["ImageType"] = { "ImageType", "unk", "", "" };
        sc.field_names_ordered.push_back("ImageType");
        sc.fields["PixelFormat"] = { "PixelFormat", "unk", "", "" };
        sc.field_names_ordered.push_back("PixelFormat");
    }
    {
        ScopeDef& sc = m_scopes["Program"];
        sc.name = "Program";
        sc.fields["String"] = { "String", "0a", "", "" };
        sc.field_names_ordered.push_back("String");
        sc.fields["Bytes"] = { "Bytes", "12", "", "" };
        sc.field_names_ordered.push_back("Bytes");
        sc.fields["Name"] = { "Name", "1a", "", "" };
        sc.field_names_ordered.push_back("Name");
    }
    {
        ScopeDef& sc = m_scopes["Vector2"];
        sc.name = "Vector2";
        sc.fields["X"] = { "X", "0d", "", "" };
        sc.field_names_ordered.push_back("X");
        sc.fields["Y"] = { "Y", "15", "", "" };
        sc.field_names_ordered.push_back("Y");
    }
    {
        ScopeDef& sc = m_scopes["Vector3"];
        sc.name = "Vector3";
        sc.fields["X"] = { "X", "0d", "", "" };
        sc.field_names_ordered.push_back("X");
        sc.fields["Y"] = { "Y", "15", "", "" };
        sc.field_names_ordered.push_back("Y");
        sc.fields["Z"] = { "Z", "1d", "", "" };
        sc.field_names_ordered.push_back("Z");
    }
    {
        ScopeDef& sc = m_scopes["Circle"];
        sc.name = "Circle";
        sc.fields["Center"] = { "Center", "0a", "", "Vector2" };
        sc.field_names_ordered.push_back("Center");
        sc.fields["Radius"] = { "Radius", "15", "", "" };
        sc.field_names_ordered.push_back("Radius");
    }
    {
        ScopeDef& sc = m_scopes["Rectangle"];
        sc.name = "Rectangle";
        sc.fields["X"] = { "X", "0d", "", "" };
        sc.field_names_ordered.push_back("X");
        sc.fields["Y"] = { "Y", "15", "", "" };
        sc.field_names_ordered.push_back("Y");
        sc.fields["Width"] = { "Width", "1d", "", "" };
        sc.field_names_ordered.push_back("Width");
        sc.fields["Height"] = { "Height", "25", "", "" };
        sc.field_names_ordered.push_back("Height");
    }
    {
        ScopeDef& sc = m_scopes["VertexChannel"];
        sc.name = "VertexChannel";
        sc.fields["ValueType"] = { "ValueType", "08", "GL data type constant", "" };
        sc.field_names_ordered.push_back("ValueType");
        sc.fields["ValuesPerVertex"] = { "ValuesPerVertex", "10", "Component count", "" };
        sc.field_names_ordered.push_back("ValuesPerVertex");
        sc.fields["Stride"] = { "Stride", "18", "Byte stride between elements", "" };
        sc.field_names_ordered.push_back("Stride");
        sc.fields["DataOffset"] = { "DataOffset", "20", "Byte offset in buffer", "" };
        sc.field_names_ordered.push_back("DataOffset");
    }
    {
        ScopeDef& sc = m_scopes["Square"];
        sc.name = "Square";
        sc.fields["X"] = { "X", "08", "", "" };
        sc.field_names_ordered.push_back("X");
        sc.fields["Y"] = { "Y", "10", "", "" };
        sc.field_names_ordered.push_back("Y");
        sc.fields["Width"] = { "Width", "18", "", "" };
        sc.field_names_ordered.push_back("Width");
        sc.fields["Height"] = { "Height", "20", "", "" };
        sc.field_names_ordered.push_back("Height");
        sc.fields["ValueType"] = { "ValueType", "08", "", "" };
        sc.field_names_ordered.push_back("ValueType");
        sc.fields["ValuesPerVertex"] = { "ValuesPerVertex", "10", "", "" };
        sc.field_names_ordered.push_back("ValuesPerVertex");
        sc.fields["Stride"] = { "Stride", "18", "", "" };
        sc.field_names_ordered.push_back("Stride");
        sc.fields["DataOffset"] = { "DataOffset", "20", "", "" };
        sc.field_names_ordered.push_back("DataOffset");
    }
    {
        ScopeDef& sc = m_scopes["Box"];
        sc.name = "Box";
        sc.fields["X"] = { "X", "0d", "", "" };
        sc.field_names_ordered.push_back("X");
        sc.fields["Y"] = { "Y", "15", "", "" };
        sc.field_names_ordered.push_back("Y");
        sc.fields["Z"] = { "Z", "1d", "", "" };
        sc.field_names_ordered.push_back("Z");
        sc.fields["Width"] = { "Width", "25", "", "" };
        sc.field_names_ordered.push_back("Width");
        sc.fields["Height"] = { "Height", "2d", "", "" };
        sc.field_names_ordered.push_back("Height");
        sc.fields["Depth"] = { "Depth", "35", "", "" };
        sc.field_names_ordered.push_back("Depth");
    }
    {
        ScopeDef& sc = m_scopes["Polygon"];
        sc.name = "Polygon";
        sc.fields["Vertex"] = { "Vertex", "0a", "", "Vector2" };
        sc.field_names_ordered.push_back("Vertex");
        sc.fields["Convex"] = { "Convex", "10", "", "" };
        sc.field_names_ordered.push_back("Convex");
        sc.fields["Closed"] = { "Closed", "18", "", "" };
        sc.field_names_ordered.push_back("Closed");
    }
    {
        ScopeDef& sc = m_scopes["FloatColor"];
        sc.name = "FloatColor";
        sc.fields["R"] = { "R", "0d", "", "" };
        sc.field_names_ordered.push_back("R");
        sc.fields["G"] = { "G", "15", "", "" };
        sc.field_names_ordered.push_back("G");
        sc.fields["B"] = { "B", "1d", "", "" };
        sc.field_names_ordered.push_back("B");
        sc.fields["A"] = { "A", "25", "", "" };
        sc.field_names_ordered.push_back("A");
    }
    {
        ScopeDef& sc = m_scopes["Mesh"];
        sc.name = "Mesh";
        sc.fields["NumVertices"] = { "NumVertices", "08", "", "" };
        sc.field_names_ordered.push_back("NumVertices");
        sc.fields["NumFaces"] = { "NumFaces", "10", "", "" };
        sc.field_names_ordered.push_back("NumFaces");
        sc.fields["Indices"] = { "Indices", "1a", "", "VertexChannel" };
        sc.field_names_ordered.push_back("Indices");
        sc.fields["Vertices"] = { "Vertices", "22", "", "VertexChannel" };
        sc.field_names_ordered.push_back("Vertices");
        sc.fields["Normals"] = { "Normals", "2a", "", "VertexChannel" };
        sc.field_names_ordered.push_back("Normals");
        sc.fields["TexCoordSet"] = { "TexCoordSet", "32", "", "VertexChannel" };
        sc.field_names_ordered.push_back("TexCoordSet");
        sc.fields["VertexColors"] = { "VertexColors", "unk", "", "VertexChannel" };
        sc.field_names_ordered.push_back("VertexColors");
        sc.fields["BoneIndices"] = { "BoneIndices", "unk", "", "VertexChannel" };
        sc.field_names_ordered.push_back("BoneIndices");
        sc.fields["BoneWeights"] = { "BoneWeights", "unk", "", "VertexChannel" };
        sc.field_names_ordered.push_back("BoneWeights");
        sc.fields["Material"] = { "Material", "52", "", "MeshMaterial" };
        sc.field_names_ordered.push_back("Material");
        sc.fields["BoundingBox"] = { "BoundingBox", "5a", "", "Box" };
        sc.field_names_ordered.push_back("BoundingBox");
        sc.fields["VertexData"] = { "VertexData", "192", "", "" };
        sc.field_names_ordered.push_back("VertexData");
        sc.fields["IndexData"] = { "IndexData", "19a", "", "" };
        sc.field_names_ordered.push_back("IndexData");
    }
    {
        ScopeDef& sc = m_scopes["MeshMaterial"];
        sc.name = "MeshMaterial";
        sc.fields["AmbientColor"] = { "AmbientColor", "0a", "", "FloatColor" };
        sc.field_names_ordered.push_back("AmbientColor");
        sc.fields["DiffuseColor"] = { "DiffuseColor", "12", "", "FloatColor" };
        sc.field_names_ordered.push_back("DiffuseColor");
        sc.fields["SpecularColor"] = { "SpecularColor", "1a", "", "FloatColor" };
        sc.field_names_ordered.push_back("SpecularColor");
        sc.fields["Shininess"] = { "Shininess", "25", "", "" };
        sc.field_names_ordered.push_back("Shininess");
        sc.fields["Texture"] = { "Texture", "2a", "", "Texture" };
        sc.field_names_ordered.push_back("Texture");
    }
    {
        ScopeDef& sc = m_scopes["DateTime"];
        sc.name = "DateTime";
        sc.fields["SecondsSinceReferenceDate"] = { "SecondsSinceReferenceDate", "09", "", "" };
        sc.field_names_ordered.push_back("SecondsSinceReferenceDate");
    }
    {
        ScopeDef& sc = m_scopes["Component"];
        sc.name = "Component";
        sc.fields["ClassName"] = { "ClassName", "0a", "", "" };
        sc.field_names_ordered.push_back("ClassName");
        sc.fields["Identifier"] = { "Identifier", "10", "", "" };
        sc.field_names_ordered.push_back("Identifier");
        sc.fields["Active"] = { "Active", "18", "", "" };
        sc.field_names_ordered.push_back("Active");
        sc.fields["Exclusive"] = { "Exclusive", "20", "", "" };
        sc.field_names_ordered.push_back("Exclusive");
        sc.fields["Label"] = { "Label", "1a", "", "" };
        sc.field_names_ordered.push_back("Label");
        sc.fields["ParentComponentIdentifier"] = { "ParentComponentIdentifier", "20", "", "" };
        sc.field_names_ordered.push_back("ParentComponentIdentifier");
        sc.fields["SpriteComponent"] = { "SpriteComponent", "322", "", "SpriteComponent" };
        sc.field_names_ordered.push_back("SpriteComponent");
        sc.fields["ModelComponent"] = { "ModelComponent", "32a", "", "ModelComponent" };
        sc.field_names_ordered.push_back("ModelComponent");
        sc.fields["KeyframeAnimationComponent"] = { "KeyframeAnimationComponent", "332", "", "KeyframeAnimationComponent" };
        sc.field_names_ordered.push_back("KeyframeAnimationComponent");
        sc.fields["BlendAnimationComponent"] = { "BlendAnimationComponent", "33a", "", "BlendAnimationComponent" };
        sc.field_names_ordered.push_back("BlendAnimationComponent");
        sc.fields["ModelTransformControllerComponent"] = { "ModelTransformControllerComponent", "342", "", "ModelTransformControllerComponent" };
        sc.field_names_ordered.push_back("ModelTransformControllerComponent");
        sc.fields["TransformControllerComponent"] = { "TransformControllerComponent", "34a", "", "TransformControllerComponent" };
        sc.field_names_ordered.push_back("TransformControllerComponent");
        sc.fields["GroundPolygonComponent"] = { "GroundPolygonComponent", "372", "", "GroundPolygonComponent" };
        sc.field_names_ordered.push_back("GroundPolygonComponent");
        sc.fields["GroundMeshComponent"] = { "GroundMeshComponent", "37a", "", "GroundMeshComponent" };
        sc.field_names_ordered.push_back("GroundMeshComponent");
        sc.fields["GroundMeshGeneratorComponent"] = { "GroundMeshGeneratorComponent", "382", "", "GroundMeshGeneratorComponent" };
        sc.field_names_ordered.push_back("GroundMeshGeneratorComponent");
        sc.fields["TextureMappingComponent"] = { "TextureMappingComponent", "38a", "", "TextureMappingComponent" };
        sc.field_names_ordered.push_back("TextureMappingComponent");
        sc.fields["WaterMeshComponent"] = { "WaterMeshComponent", "392", "", "WaterMeshComponent" };
        sc.field_names_ordered.push_back("WaterMeshComponent");
        sc.fields["ShapeComponent"] = { "ShapeComponent", "3c2", "", "ShapeComponent" };
        sc.field_names_ordered.push_back("ShapeComponent");
        sc.fields["CollisionShapeComponent"] = { "CollisionShapeComponent", "3ca", "", "CollisionShapeComponent" };
        sc.field_names_ordered.push_back("CollisionShapeComponent");
        sc.fields["DamageComponent"] = { "DamageComponent", "3d2", "", "DamageComponent" };
        sc.field_names_ordered.push_back("DamageComponent");
        sc.fields["HealthComponent"] = { "HealthComponent", "3da", "", "HealthComponent" };
        sc.field_names_ordered.push_back("HealthComponent");
        sc.fields["BoneControlledCollisionShapeComponent"] = { "BoneControlledCollisionShapeComponent", "3e2", "", "BoneControlledCollisionShapeComponent" };
        sc.field_names_ordered.push_back("BoneControlledCollisionShapeComponent");
        sc.fields["ObjectLinkControllerComponent"] = { "ObjectLinkControllerComponent", "3ea", "", "ObjectLinkControllerComponent" };
        sc.field_names_ordered.push_back("ObjectLinkControllerComponent");
        sc.fields["LightComponent"] = { "LightComponent", "412", "", "LightComponent" };
        sc.field_names_ordered.push_back("LightComponent");
        sc.fields["ShadowComponent"] = { "ShadowComponent", "41a", "", "ShadowComponent" };
        sc.field_names_ordered.push_back("ShadowComponent");
        sc.fields["SoundEffectComponent"] = { "SoundEffectComponent", "462", "", "SoundEffectComponent" };
        sc.field_names_ordered.push_back("SoundEffectComponent");
        sc.fields["AnimationControllerComponent"] = { "AnimationControllerComponent", "4aa", "", "AnimationControllerComponent" };
        sc.field_names_ordered.push_back("AnimationControllerComponent");
        sc.fields["CharAnimControllerComponent"] = { "CharAnimControllerComponent", "4b2", "", "CharAnimControllerComponent" };
        sc.field_names_ordered.push_back("CharAnimControllerComponent");
        sc.fields["CharControllerComponent"] = { "CharControllerComponent", "4ba", "", "CharControllerComponent" };
        sc.field_names_ordered.push_back("CharControllerComponent");
        sc.fields["EntityComponent"] = { "EntityComponent", "4c2", "", "EntityComponent" };
        sc.field_names_ordered.push_back("EntityComponent");
        sc.fields["BushControllerComponent"] = { "BushControllerComponent", "4ca", "", "BushControllerComponent" };
        sc.field_names_ordered.push_back("BushControllerComponent");
        sc.fields["ElevatorControllerComponent"] = { "ElevatorControllerComponent", "4d2", "", "ElevatorControllerComponent" };
        sc.field_names_ordered.push_back("ElevatorControllerComponent");
        sc.fields["PressureTriggerComponent"] = { "PressureTriggerComponent", "4da", "", "PressureTriggerComponent" };
        sc.field_names_ordered.push_back("PressureTriggerComponent");
        sc.fields["DoorControllerComponent"] = { "DoorControllerComponent", "4e2", "", "DoorControllerComponent" };
        sc.field_names_ordered.push_back("DoorControllerComponent");
        sc.fields["ProgramComponent"] = { "ProgramComponent", "4ea", "", "ProgramComponent" };
        sc.field_names_ordered.push_back("ProgramComponent");
        sc.fields["MonsterEntityComponent"] = { "MonsterEntityComponent", "4f2", "", "MonsterEntityComponent" };
        sc.field_names_ordered.push_back("MonsterEntityComponent");
        sc.fields["PhysicsObjectComponent"] = { "PhysicsObjectComponent", "4fa", "", "PhysicsObjectComponent" };
        sc.field_names_ordered.push_back("PhysicsObjectComponent");
        sc.fields["BreakableObjectComponent"] = { "BreakableObjectComponent", "502", "", "BreakableObjectComponent" };
        sc.field_names_ordered.push_back("BreakableObjectComponent");
        sc.fields["EntityControllerComponent"] = { "EntityControllerComponent", "50a", "", "EntityControllerComponent" };
        sc.field_names_ordered.push_back("EntityControllerComponent");
        sc.fields["EntityActionComponent"] = { "EntityActionComponent", "512", "", "EntityActionComponent" };
        sc.field_names_ordered.push_back("EntityActionComponent");
        sc.fields["PhysicsPlatformComponent"] = { "PhysicsPlatformComponent", "51a", "", "PhysicsPlatformComponent" };
        sc.field_names_ordered.push_back("PhysicsPlatformComponent");
        sc.fields["EntityInfoComponent"] = { "EntityInfoComponent", "522", "", "EntityInfoComponent" };
        sc.field_names_ordered.push_back("EntityInfoComponent");
        sc.fields["HeroEntityComponent"] = { "HeroEntityComponent", "52a", "", "HeroEntityComponent" };
        sc.field_names_ordered.push_back("HeroEntityComponent");
        sc.fields["BackgroundComponent"] = { "BackgroundComponent", "642", "", "BackgroundComponent" };
        sc.field_names_ordered.push_back("BackgroundComponent");
        sc.fields["PropertiesComponent"] = { "PropertiesComponent", "692", "", "PropertiesComponent" };
        sc.field_names_ordered.push_back("PropertiesComponent");
        sc.fields["ParticleEmitterComponent"] = { "ParticleEmitterComponent", "7d2", "", "ParticleEmitterComponent" };
        sc.field_names_ordered.push_back("ParticleEmitterComponent");
        sc.fields["ParticleComponent"] = { "ParticleComponent", "7da", "", "ParticleComponent" };
        sc.field_names_ordered.push_back("ParticleComponent");
        sc.fields["FireEmitterComponent"] = { "FireEmitterComponent", "7ea", "", "FireEmitterComponent" };
        sc.field_names_ordered.push_back("FireEmitterComponent");
        sc.fields["SimpleGlowComponent"] = { "SimpleGlowComponent", "7f2", "", "SimpleGlowComponent" };
        sc.field_names_ordered.push_back("SimpleGlowComponent");
        sc.fields["ParticleObjectComponent"] = { "ParticleObjectComponent", "7fa", "", "ParticleObjectComponent" };
        sc.field_names_ordered.push_back("ParticleObjectComponent");
        sc.fields["OrbitControllerComponent"] = { "OrbitControllerComponent", "802", "", "OrbitControllerComponent" };
        sc.field_names_ordered.push_back("OrbitControllerComponent");
        sc.fields["ParticleFieldComponent"] = { "ParticleFieldComponent", "80a", "", "ParticleFieldComponent" };
        sc.field_names_ordered.push_back("ParticleFieldComponent");
        sc.fields["MonsterControllerComponent"] = { "MonsterControllerComponent", "972", "", "MonsterControllerComponent" };
        sc.field_names_ordered.push_back("MonsterControllerComponent");
        sc.fields["WalkingMonsterControllerComponent"] = { "WalkingMonsterControllerComponent", "97a", "", "WalkingMonsterControllerComponent" };
        sc.field_names_ordered.push_back("WalkingMonsterControllerComponent");
        sc.fields["ChargingMonsterControllerComponent"] = { "ChargingMonsterControllerComponent", "982", "", "ChargingMonsterControllerComponent" };
        sc.field_names_ordered.push_back("ChargingMonsterControllerComponent");
        sc.fields["SnappingMonsterControllerComponent"] = { "SnappingMonsterControllerComponent", "98a", "", "SnappingMonsterControllerComponent" };
        sc.field_names_ordered.push_back("SnappingMonsterControllerComponent");
        sc.fields["AttackComponent"] = { "AttackComponent", "992", "", "AttackComponent" };
        sc.field_names_ordered.push_back("AttackComponent");
        sc.fields["LeapingMonsterControllerComponent"] = { "LeapingMonsterControllerComponent", "99a", "", "LeapingMonsterControllerComponent" };
        sc.field_names_ordered.push_back("LeapingMonsterControllerComponent");
        sc.fields["SkellyMonsterControllerComponent"] = { "SkellyMonsterControllerComponent", "9a2", "", "SkellyMonsterControllerComponent" };
        sc.field_names_ordered.push_back("SkellyMonsterControllerComponent");
        sc.fields["StaticMonsterControllerComponent"] = { "StaticMonsterControllerComponent", "9aa", "", "StaticMonsterControllerComponent" };
        sc.field_names_ordered.push_back("StaticMonsterControllerComponent");
        sc.fields["ShootingMonsterControllerComponent"] = { "ShootingMonsterControllerComponent", "9b2", "", "ShootingMonsterControllerComponent" };
        sc.field_names_ordered.push_back("ShootingMonsterControllerComponent");
        sc.fields["BatMonsterControllerComponent"] = { "BatMonsterControllerComponent", "9ba", "", "BatMonsterControllerComponent" };
        sc.field_names_ordered.push_back("BatMonsterControllerComponent");
        sc.fields["BouncingMonsterControllerComponent"] = { "BouncingMonsterControllerComponent", "9c2", "", "BouncingMonsterControllerComponent" };
        sc.field_names_ordered.push_back("BouncingMonsterControllerComponent");
        sc.fields["MonsterDeathControllerComponent"] = { "MonsterDeathControllerComponent", "9ca", "", "MonsterDeathControllerComponent" };
        sc.field_names_ordered.push_back("MonsterDeathControllerComponent");
        sc.fields["GenericMonsterControllerComponent"] = { "GenericMonsterControllerComponent", "9d2", "", "GenericMonsterControllerComponent" };
        sc.field_names_ordered.push_back("GenericMonsterControllerComponent");
        sc.fields["SwingableWeaponComponent"] = { "SwingableWeaponComponent", "c82", "", "SwingableWeaponComponent" };
        sc.field_names_ordered.push_back("SwingableWeaponComponent");
        sc.fields["SwingableWeaponControllerComponent"] = { "SwingableWeaponControllerComponent", "c8a", "", "SwingableWeaponControllerComponent" };
        sc.field_names_ordered.push_back("SwingableWeaponControllerComponent");
        sc.fields["SwingComponent"] = { "SwingComponent", "c92", "", "SwingComponent" };
        sc.field_names_ordered.push_back("SwingComponent");
        sc.fields["WeaponGlowComponent"] = { "WeaponGlowComponent", "c9a", "", "WeaponGlowComponent" };
        sc.field_names_ordered.push_back("WeaponGlowComponent");
        sc.fields["WeaponTrailComponent"] = { "WeaponTrailComponent", "ca2", "", "WeaponTrailComponent" };
        sc.field_names_ordered.push_back("WeaponTrailComponent");
        sc.fields["PortalComponent"] = { "PortalComponent", "fa2", "", "PortalComponent" };
        sc.field_names_ordered.push_back("PortalComponent");
        sc.fields["SpawnPointComponent"] = { "SpawnPointComponent", "faa", "", "SpawnPointComponent" };
        sc.field_names_ordered.push_back("SpawnPointComponent");
        sc.fields["CollectableItemComponent"] = { "CollectableItemComponent", "fb2", "", "CollectableItemComponent" };
        sc.field_names_ordered.push_back("CollectableItemComponent");
        sc.fields["TouchableComponent"] = { "TouchableComponent", "fc2", "", "TouchableComponent" };
        sc.field_names_ordered.push_back("TouchableComponent");
        sc.fields["ItemDropComponent"] = { "ItemDropComponent", "fca", "", "ItemDropComponent" };
        sc.field_names_ordered.push_back("ItemDropComponent");
        sc.fields["OverlayTextComponent"] = { "OverlayTextComponent", "fd2", "", "OverlayTextComponent" };
        sc.field_names_ordered.push_back("OverlayTextComponent");
        sc.fields["PortalEffectComponent"] = { "PortalEffectComponent", "fda", "", "PortalEffectComponent" };
        sc.field_names_ordered.push_back("PortalEffectComponent");
        sc.fields["MagicBoltComponent"] = { "MagicBoltComponent", "1132", "", "MagicBoltComponent" };
        sc.field_names_ordered.push_back("MagicBoltComponent");
        sc.fields["MagicExplosionComponent"] = { "MagicExplosionComponent", "113a", "", "MagicExplosionComponent" };
        sc.field_names_ordered.push_back("MagicExplosionComponent");
        sc.fields["SkillComponent"] = { "SkillComponent", "1142", "", "SkillComponent" };
        sc.field_names_ordered.push_back("SkillComponent");
        sc.fields["MagicSpellCastComponent"] = { "MagicSpellCastComponent", "114a", "", "MagicSpellCastComponent" };
        sc.field_names_ordered.push_back("MagicSpellCastComponent");
        sc.fields["FireBreathComponent"] = { "FireBreathComponent", "1152", "", "FireBreathComponent" };
        sc.field_names_ordered.push_back("FireBreathComponent");
        sc.fields["ProjectileControllerComponent"] = { "ProjectileControllerComponent", "115a", "", "ProjectileControllerComponent" };
        sc.field_names_ordered.push_back("ProjectileControllerComponent");
        sc.fields["MagicBombComponent"] = { "MagicBombComponent", "1162", "", "MagicBombComponent" };
        sc.field_names_ordered.push_back("MagicBombComponent");
        sc.fields["MagicHookshotComponent"] = { "MagicHookshotComponent", "116a", "", "MagicHookshotComponent" };
        sc.field_names_ordered.push_back("MagicHookshotComponent");
        sc.fields["SpellComponent"] = { "SpellComponent", "1172", "", "SpellComponent" };
        sc.field_names_ordered.push_back("SpellComponent");
        sc.fields["DimensionObjectComponent"] = { "DimensionObjectComponent", "117a", "", "DimensionObjectComponent" };
        sc.field_names_ordered.push_back("DimensionObjectComponent");
        sc.fields["DimensionSpellComponent"] = { "DimensionSpellComponent", "1182", "", "DimensionSpellComponent" };
        sc.field_names_ordered.push_back("DimensionSpellComponent");
    }
    {
        ScopeDef& sc = m_scopes["SpriteComponent"];
        sc.name = "SpriteComponent";
        sc.fields["TextureName"] = { "TextureName", "0a", "", "" };
        sc.field_names_ordered.push_back("TextureName");
    }
    {
        ScopeDef& sc = m_scopes["ModelComponent"];
        sc.name = "ModelComponent";
        sc.fields["Name"] = { "Name", "0a", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["YRotation"] = { "YRotation", "15", "", "" };
        sc.field_names_ordered.push_back("YRotation");
        sc.fields["EmissionFactor"] = { "EmissionFactor", "1d", "", "" };
        sc.field_names_ordered.push_back("EmissionFactor");
        sc.fields["XRotation"] = { "XRotation", "25", "", "" };
        sc.field_names_ordered.push_back("XRotation");
        sc.fields["ShatterColor"] = { "ShatterColor", "2a", "", "FloatColor" };
        sc.field_names_ordered.push_back("ShatterColor");
        sc.fields["Origin"] = { "Origin", "32", "", "Vector3" };
        sc.field_names_ordered.push_back("Origin");
        sc.fields["Transparent"] = { "Transparent", "38", "", "" };
        sc.field_names_ordered.push_back("Transparent");
        sc.fields["DiffuseColor"] = { "DiffuseColor", "42", "", "FloatColor" };
        sc.field_names_ordered.push_back("DiffuseColor");
    }
    {
        ScopeDef& sc = m_scopes["KeyframeAnimationComponent"];
        sc.name = "KeyframeAnimationComponent";
        sc.fields["ModelId"] = { "ModelId", "08", "", "" };
        sc.field_names_ordered.push_back("ModelId");
        sc.fields["Name"] = { "Name", "12", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["Repeating"] = { "Repeating", "18", "", "" };
        sc.field_names_ordered.push_back("Repeating");
        sc.fields["SpeedMultiplier"] = { "SpeedMultiplier", "25", "", "" };
        sc.field_names_ordered.push_back("SpeedMultiplier");
        sc.fields["Running"] = { "Running", "28", "", "" };
        sc.field_names_ordered.push_back("Running");
    }
    {
        ScopeDef& sc = m_scopes["BlendAnimationComponent"];
        sc.name = "BlendAnimationComponent";
        sc.fields["Animation1Id"] = { "Animation1Id", "08", "", "" };
        sc.field_names_ordered.push_back("Animation1Id");
        sc.fields["Animation2Id"] = { "Animation2Id", "10", "", "" };
        sc.field_names_ordered.push_back("Animation2Id");
        sc.fields["BlendTime"] = { "BlendTime", "1d", "", "" };
        sc.field_names_ordered.push_back("BlendTime");
        sc.fields["ReverseBlendTime"] = { "ReverseBlendTime", "25", "", "" };
        sc.field_names_ordered.push_back("ReverseBlendTime");
    }
    {
        ScopeDef& sc = m_scopes["ModelTransformControllerComponent"];
        sc.name = "ModelTransformControllerComponent";
        sc.fields["ModelId"] = { "ModelId", "08", "", "" };
        sc.field_names_ordered.push_back("ModelId");
        sc.fields["Origin"] = { "Origin", "12", "", "Vector3" };
        sc.field_names_ordered.push_back("Origin");
        sc.fields["RotationAxis"] = { "RotationAxis", "1a", "", "Vector3" };
        sc.field_names_ordered.push_back("RotationAxis");
        sc.fields["RotationAngle"] = { "RotationAngle", "25", "", "" };
        sc.field_names_ordered.push_back("RotationAngle");
        sc.fields["RotationSpeed"] = { "RotationSpeed", "2d", "", "" };
        sc.field_names_ordered.push_back("RotationSpeed");
    }
    {
        ScopeDef& sc = m_scopes["TransformControllerComponent"];
        sc.name = "TransformControllerComponent";
        sc.fields["Origin"] = { "Origin", "12", "", "Vector3" };
        sc.field_names_ordered.push_back("Origin");
        sc.fields["RotationSpeed"] = { "RotationSpeed", "2d", "", "" };
        sc.field_names_ordered.push_back("RotationSpeed");
        sc.fields["SpeedMultiplier"] = { "SpeedMultiplier", "35", "", "" };
        sc.field_names_ordered.push_back("SpeedMultiplier");
    }
    {
        ScopeDef& sc = m_scopes["GroundPolygonComponent"];
        sc.name = "GroundPolygonComponent";
        sc.fields["Vertex"] = { "Vertex", "unk", "", "" };
        sc.field_names_ordered.push_back("Vertex");
        sc.fields["Polygon"] = { "Polygon", "12", "", "Polygon" };
        sc.field_names_ordered.push_back("Polygon");
        sc.fields["Collides"] = { "Collides", "18", "", "" };
        sc.field_names_ordered.push_back("Collides");
        sc.fields["MinDepth"] = { "MinDepth", "25", "", "" };
        sc.field_names_ordered.push_back("MinDepth");
        sc.fields["MaxDepth"] = { "MaxDepth", "2d", "", "" };
        sc.field_names_ordered.push_back("MaxDepth");
        sc.fields["OnCollide"] = { "OnCollide", "32", "", "Program" };
        sc.field_names_ordered.push_back("OnCollide");
        sc.fields["Friction"] = { "Friction", "3d", "", "" };
        sc.field_names_ordered.push_back("Friction");
        sc.fields["UnsafeGround"] = { "UnsafeGround", "40", "", "" };
        sc.field_names_ordered.push_back("UnsafeGround");
    }
    {
        ScopeDef& sc = m_scopes["GroundMeshComponent"];
        sc.name = "GroundMeshComponent";
        sc.fields["VertexData"] = { "VertexData", "unk", "", "" };
        sc.field_names_ordered.push_back("VertexData");
        sc.fields["Indices"] = { "Indices", "unk", "", "" };
        sc.field_names_ordered.push_back("Indices");
        sc.fields["Mesh"] = { "Mesh", "unk", "", "" };
        sc.field_names_ordered.push_back("Mesh");
        sc.fields["LocalAabb"] = { "LocalAabb", "3a", "", "Rectangle" };
        sc.field_names_ordered.push_back("LocalAabb");
        sc.fields["SurfaceMesh"] = { "SurfaceMesh", "42", "", "Mesh" };
        sc.field_names_ordered.push_back("SurfaceMesh");
        sc.fields["FrontMesh"] = { "FrontMesh", "4a", "", "Mesh" };
        sc.field_names_ordered.push_back("FrontMesh");
        sc.fields["Color"] = { "Color", "52", "", "FloatColor" };
        sc.field_names_ordered.push_back("Color");
        sc.fields["Transparent"] = { "Transparent", "58", "", "" };
        sc.field_names_ordered.push_back("Transparent");
    }
    {
        ScopeDef& sc = m_scopes["GroundMeshGeneratorComponent"];
        sc.name = "GroundMeshGeneratorComponent";
        sc.fields["GroundPolygonId"] = { "GroundPolygonId", "08", "", "" };
        sc.field_names_ordered.push_back("GroundPolygonId");
        sc.fields["TargetMeshId"] = { "TargetMeshId", "10", "", "" };
        sc.field_names_ordered.push_back("TargetMeshId");
        sc.fields["FrontTextureMappingId"] = { "FrontTextureMappingId", "18", "", "" };
        sc.field_names_ordered.push_back("FrontTextureMappingId");
        sc.fields["SurfaceTextureMappingId"] = { "SurfaceTextureMappingId", "20", "", "" };
        sc.field_names_ordered.push_back("SurfaceTextureMappingId");
        sc.fields["RandomSeed"] = { "RandomSeed", "28", "", "" };
        sc.field_names_ordered.push_back("RandomSeed");
        sc.fields["HorizNoise"] = { "HorizNoise", "35", "", "" };
        sc.field_names_ordered.push_back("HorizNoise");
        sc.fields["MeshType"] = { "MeshType", "38", "0:PLAIN 1:ROUNDED_HAT", "" };
        sc.field_names_ordered.push_back("MeshType");
        sc.fields["SurfaceWidth"] = { "SurfaceWidth", "45", "", "" };
        sc.field_names_ordered.push_back("SurfaceWidth");
        sc.fields["HatHeight"] = { "HatHeight", "4d", "", "" };
        sc.field_names_ordered.push_back("HatHeight");
        sc.fields["HatWidthOffset1"] = { "HatWidthOffset1", "55", "", "" };
        sc.field_names_ordered.push_back("HatWidthOffset1");
        sc.fields["HatWidthOffset2"] = { "HatWidthOffset2", "5d", "", "" };
        sc.field_names_ordered.push_back("HatWidthOffset2");
        sc.fields["unk"] = { "unk", "73f2", "", "" };
        sc.field_names_ordered.push_back("unk");
    }
    {
        ScopeDef& sc = m_scopes["TextureMappingComponent"];
        sc.name = "TextureMappingComponent";
        sc.fields["TextureName"] = { "TextureName", "0a", "", "" };
        sc.field_names_ordered.push_back("TextureName");
        sc.fields["Scale"] = { "Scale", "15", "", "" };
        sc.field_names_ordered.push_back("Scale");
        sc.fields["Offset"] = { "Offset", "1a", "", "Vector2" };
        sc.field_names_ordered.push_back("Offset");
    }
    {
        ScopeDef& sc = m_scopes["WaterMeshComponent"];
        sc.name = "WaterMeshComponent";
        sc.fields["BoundsShapeId"] = { "BoundsShapeId", "08", "", "" };
        sc.field_names_ordered.push_back("BoundsShapeId");
        sc.fields["TextureMappingId"] = { "TextureMappingId", "10", "", "" };
        sc.field_names_ordered.push_back("TextureMappingId");
        sc.fields["FrontColor"] = { "FrontColor", "1a", "", "FloatColor" };
        sc.field_names_ordered.push_back("FrontColor");
        sc.fields["SurfaceColor"] = { "SurfaceColor", "22", "", "FloatColor" };
        sc.field_names_ordered.push_back("SurfaceColor");
    }
    {
        ScopeDef& sc = m_scopes["ShapeComponent"];
        sc.name = "ShapeComponent";
        sc.fields["Rectangle"] = { "Rectangle", "0a", "", "Rectangle" };
        sc.field_names_ordered.push_back("Rectangle");
        sc.fields["Circle"] = { "Circle", "12", "", "Circle" };
        sc.field_names_ordered.push_back("Circle");
        sc.fields["Polygon"] = { "Polygon", "1a", "", "Polygon" };
        sc.field_names_ordered.push_back("Polygon");
    }
    {
        ScopeDef& sc = m_scopes["CollisionShapeComponent"];
        sc.name = "CollisionShapeComponent";
        sc.fields["IsGround"] = { "IsGround", "10", "", "" };
        sc.field_names_ordered.push_back("IsGround");
        sc.fields["Collides"] = { "Collides", "18", "", "" };
        sc.field_names_ordered.push_back("Collides");
        sc.fields["ReceivesDamage"] = { "ReceivesDamage", "20", "", "" };
        sc.field_names_ordered.push_back("ReceivesDamage");
        sc.fields["InflictsDamage"] = { "InflictsDamage", "28", "", "" };
        sc.field_names_ordered.push_back("InflictsDamage");
        sc.fields["MinDepth"] = { "MinDepth", "35", "", "" };
        sc.field_names_ordered.push_back("MinDepth");
        sc.fields["MaxDepth"] = { "MaxDepth", "3d", "", "" };
        sc.field_names_ordered.push_back("MaxDepth");
        sc.fields["SpecialType"] = { "SpecialType", "40", "0:NONE 1:PICKUP 2:PORTAL 3:COLLECTABLE\n4:USE 5:BLOCKS_DAMAGE 6:GRABBABLE 7:PUSHABLE", "" };
        sc.field_names_ordered.push_back("SpecialType");
        sc.fields["OnCollide"] = { "OnCollide", "4a", "", "Program" };
        sc.field_names_ordered.push_back("OnCollide");
        sc.fields["OnCollisionEnd"] = { "OnCollisionEnd", "52", "", "Program" };
        sc.field_names_ordered.push_back("OnCollisionEnd");
        sc.fields["Enabled"] = { "Enabled", "58", "", "" };
        sc.field_names_ordered.push_back("Enabled");
        sc.fields["OnReceiveDamage"] = { "OnReceiveDamage", "62", "", "Program" };
        sc.field_names_ordered.push_back("OnReceiveDamage");
        sc.fields["Friction"] = { "Friction", "6d", "", "" };
        sc.field_names_ordered.push_back("Friction");
        sc.fields["UnsafeGround"] = { "UnsafeGround", "70", "", "" };
        sc.field_names_ordered.push_back("UnsafeGround");
    }
    {
        ScopeDef& sc = m_scopes["DamageComponent"];
        sc.name = "DamageComponent";
        sc.fields["MinDamage"] = { "MinDamage", "08", "", "" };
        sc.field_names_ordered.push_back("MinDamage");
        sc.fields["DamageType"] = { "DamageType", "10", "", "" };
        sc.field_names_ordered.push_back("DamageType");
        sc.fields["SpecialDamageType"] = { "SpecialDamageType", "18", "", "" };
        sc.field_names_ordered.push_back("SpecialDamageType");
        sc.fields["StandAlone"] = { "StandAlone", "20", "", "" };
        sc.field_names_ordered.push_back("StandAlone");
        sc.fields["MaxDamage"] = { "MaxDamage", "28", "", "" };
        sc.field_names_ordered.push_back("MaxDamage");
        sc.fields["PhysicalDamageFactor"] = { "PhysicalDamageFactor", "35", "", "" };
        sc.field_names_ordered.push_back("PhysicalDamageFactor");
        sc.fields["MagicDamageFactor"] = { "MagicDamageFactor", "3d", "", "" };
        sc.field_names_ordered.push_back("MagicDamageFactor");
        sc.fields["IgnoreTargetImmunity"] = { "IgnoreTargetImmunity", "40", "", "" };
        sc.field_names_ordered.push_back("IgnoreTargetImmunity");
        sc.fields["CanBeBlocked"] = { "CanBeBlocked", "48", "", "" };
        sc.field_names_ordered.push_back("CanBeBlocked");
    }
    {
        ScopeDef& sc = m_scopes["HealthComponent"];
        sc.name = "HealthComponent";
        sc.fields["MaxHealth"] = { "MaxHealth", "08", "", "" };
        sc.field_names_ordered.push_back("MaxHealth");
        sc.fields["HEALTHTYPE"] = { "HEALTHTYPE", "10", "0:ENEMY 1:FRIENDLY", "" };
        sc.field_names_ordered.push_back("HEALTHTYPE");
        sc.fields["BarOffset"] = { "BarOffset", "1a", "", "Vector3" };
        sc.field_names_ordered.push_back("BarOffset");
    }
    {
        ScopeDef& sc = m_scopes["BoneControlledCollisionShapeComponent"];
        sc.name = "BoneControlledCollisionShapeComponent";
        sc.fields["ControllingModelId"] = { "ControllingModelId", "08", "", "" };
        sc.field_names_ordered.push_back("ControllingModelId");
        sc.fields["ControllingBoneName"] = { "ControllingBoneName", "12", "", "" };
        sc.field_names_ordered.push_back("ControllingBoneName");
    }
    {
        ScopeDef& sc = m_scopes["ObjectLinkControllerComponent"];
        sc.name = "ObjectLinkControllerComponent";
        sc.fields["TargetObjectIdentifier"] = { "TargetObjectIdentifier", "0a", "", "" };
        sc.field_names_ordered.push_back("TargetObjectIdentifier");
        sc.fields["TargetBoneIdentifier"] = { "TargetBoneIdentifier", "12", "", "" };
        sc.field_names_ordered.push_back("TargetBoneIdentifier");
        sc.fields["LocalOffset"] = { "LocalOffset", "1a", "", "Vector3" };
        sc.field_names_ordered.push_back("LocalOffset");
        sc.fields["WorldOffset"] = { "WorldOffset", "22", "", "Vector3" };
        sc.field_names_ordered.push_back("WorldOffset");
        sc.fields["LocalRotation"] = { "LocalRotation", "2a", "", "Vector3" };
        sc.field_names_ordered.push_back("LocalRotation");
    }
    {
        ScopeDef& sc = m_scopes["LightComponent"];
        sc.name = "LightComponent";
        sc.fields["Type"] = { "Type", "08", "0:UNKNOWN 1:AMBIENT 2:DIRECTIONAL 3:POINT 4:OVERLAY", "" };
        sc.field_names_ordered.push_back("Type");
        sc.fields["Intensity"] = { "Intensity", "15", "", "" };
        sc.field_names_ordered.push_back("Intensity");
        sc.fields["Color"] = { "Color", "1a", "", "FloatColor" };
        sc.field_names_ordered.push_back("Color");
        sc.fields["LinearAttenuation"] = { "LinearAttenuation", "25", "", "" };
        sc.field_names_ordered.push_back("LinearAttenuation");
        sc.fields["QuadraticAttenuation"] = { "QuadraticAttenuation", "2d", "", "" };
        sc.field_names_ordered.push_back("QuadraticAttenuation");
        sc.fields["Offset"] = { "Offset", "32", "", "Vector3" };
        sc.field_names_ordered.push_back("Offset");
        sc.fields["Radius"] = { "Radius", "3d", "", "" };
        sc.field_names_ordered.push_back("Radius");
    }
    {
        ScopeDef& sc = m_scopes["ShadowComponent"];
        sc.name = "ShadowComponent";
        sc.fields["WidthRadius"] = { "WidthRadius", "0d", "", "" };
        sc.field_names_ordered.push_back("WidthRadius");
        sc.fields["DepthRadius"] = { "DepthRadius", "15", "", "" };
        sc.field_names_ordered.push_back("DepthRadius");
        sc.fields["Offset"] = { "Offset", "1a", "", "Vector3" };
        sc.field_names_ordered.push_back("Offset");
    }
    {
        ScopeDef& sc = m_scopes["SoundEffectComponent"];
        sc.name = "SoundEffectComponent";
        sc.fields["Name"] = { "Name", "0a", "", "" };
        sc.field_names_ordered.push_back("Name");
        sc.fields["Delay"] = { "Delay", "15", "", "" };
        sc.field_names_ordered.push_back("Delay");
        sc.fields["Volume"] = { "Volume", "1d", "", "" };
        sc.field_names_ordered.push_back("Volume");
    }
    {
        ScopeDef& sc = m_scopes["AnimationControllerComponent"];
        sc.name = "AnimationControllerComponent";
        sc.fields["ModelId"] = { "ModelId", "08", "", "" };
        sc.field_names_ordered.push_back("ModelId");
        sc.fields["DefaultAnimationId"] = { "DefaultAnimationId", "10", "", "" };
        sc.field_names_ordered.push_back("DefaultAnimationId");
        sc.fields["SelfUpdate"] = { "SelfUpdate", "18", "", "" };
        sc.field_names_ordered.push_back("SelfUpdate");
    }
    {
        ScopeDef& sc = m_scopes["CharAnimControllerComponent"];
        sc.name = "CharAnimControllerComponent";
        sc.fields["StandAnimationId"] = { "StandAnimationId", "20", "", "" };
        sc.field_names_ordered.push_back("StandAnimationId");
        sc.fields["WalkAnimationId"] = { "WalkAnimationId", "28", "", "" };
        sc.field_names_ordered.push_back("WalkAnimationId");
        sc.fields["JumpAnimationId"] = { "JumpAnimationId", "30", "", "" };
        sc.field_names_ordered.push_back("JumpAnimationId");
        sc.fields["FallAnimationId"] = { "FallAnimationId", "38", "", "" };
        sc.field_names_ordered.push_back("FallAnimationId");
        sc.fields["CastAnimationId"] = { "CastAnimationId", "40", "", "" };
        sc.field_names_ordered.push_back("CastAnimationId");
        sc.fields["AirJumpAnimationId"] = { "AirJumpAnimationId", "48", "", "" };
        sc.field_names_ordered.push_back("AirJumpAnimationId");
    }
    {
        ScopeDef& sc = m_scopes["CharControllerComponent"];
        sc.name = "CharControllerComponent";
        sc.fields["DefaultAnimationControllerId"] = { "DefaultAnimationControllerId", "08", "", "" };
        sc.field_names_ordered.push_back("DefaultAnimationControllerId");
        sc.fields["RightWeaponControllerId"] = { "RightWeaponControllerId", "10", "", "" };
        sc.field_names_ordered.push_back("RightWeaponControllerId");
        sc.fields["NormalRunSpeed"] = { "NormalRunSpeed", "1d", "", "" };
        sc.field_names_ordered.push_back("NormalRunSpeed");
        sc.fields["JumpSpeed"] = { "JumpSpeed", "25", "", "" };
        sc.field_names_ordered.push_back("JumpSpeed");
        sc.fields["NormalMaxJumpTime"] = { "NormalMaxJumpTime", "2d", "", "" };
        sc.field_names_ordered.push_back("NormalMaxJumpTime");
        sc.fields["LeftWeaponControllerId"] = { "LeftWeaponControllerId", "30", "", "" };
        sc.field_names_ordered.push_back("LeftWeaponControllerId");
        sc.fields["EntityId"] = { "EntityId", "38", "", "" };
        sc.field_names_ordered.push_back("EntityId");
        sc.fields["SwingComponentId"] = { "SwingComponentId", "40", "", "" };
        sc.field_names_ordered.push_back("SwingComponentId");
        sc.fields["LiftAnimationControllerId"] = { "LiftAnimationControllerId", "48", "", "" };
        sc.field_names_ordered.push_back("LiftAnimationControllerId");
        sc.fields["LiftAnimationId"] = { "LiftAnimationId", "50", "", "" };
        sc.field_names_ordered.push_back("LiftAnimationId");
        sc.fields["DropAnimationId"] = { "DropAnimationId", "58", "", "" };
        sc.field_names_ordered.push_back("DropAnimationId");
        sc.fields["ThrowAnimationId"] = { "ThrowAnimationId", "60", "", "" };
        sc.field_names_ordered.push_back("ThrowAnimationId");
        sc.fields["HurtAnimationId"] = { "HurtAnimationId", "68", "", "" };
        sc.field_names_ordered.push_back("HurtAnimationId");
        sc.fields["DieAnimationId"] = { "DieAnimationId", "70", "", "" };
        sc.field_names_ordered.push_back("DieAnimationId");
        sc.fields["PushAnimationId"] = { "PushAnimationId", "78", "", "" };
        sc.field_names_ordered.push_back("PushAnimationId");
        sc.fields["FastRunSpeed"] = { "FastRunSpeed", "85", "", "" };
        sc.field_names_ordered.push_back("FastRunSpeed");
        sc.fields["FastMaxJumpTime"] = { "FastMaxJumpTime", "8d", "", "" };
        sc.field_names_ordered.push_back("FastMaxJumpTime");
        sc.fields["JumpSoundId"] = { "JumpSoundId", "90", "", "" };
        sc.field_names_ordered.push_back("JumpSoundId");
        sc.fields["AirJumpSoundId"] = { "AirJumpSoundId", "98", "", "" };
        sc.field_names_ordered.push_back("AirJumpSoundId");
        sc.fields["JumpLandSoundId"] = { "JumpLandSoundId", "a0", "", "" };
        sc.field_names_ordered.push_back("JumpLandSoundId");
    }
    {
        ScopeDef& sc = m_scopes["EntityComponent"];
        sc.name = "EntityComponent";
        sc.fields["FacingDirection"] = { "FacingDirection", "08", "", "" };
        sc.field_names_ordered.push_back("FacingDirection");
        sc.fields["PhysicsEnabled"] = { "PhysicsEnabled", "10", "", "" };
        sc.field_names_ordered.push_back("PhysicsEnabled");
    }
    {
        ScopeDef& sc = m_scopes["BushControllerComponent"];
        sc.name = "BushControllerComponent";
        sc.fields["WobbleAnimationId"] = { "WobbleAnimationId", "08", "", "" };
        sc.field_names_ordered.push_back("WobbleAnimationId");
        sc.fields["WobbleSoundId"] = { "WobbleSoundId", "10", "", "" };
        sc.field_names_ordered.push_back("WobbleSoundId");
        sc.fields["CutSoundId"] = { "CutSoundId", "18", "", "" };
        sc.field_names_ordered.push_back("CutSoundId");
    }
    {
        ScopeDef& sc = m_scopes["ElevatorControllerComponent"];
        sc.name = "ElevatorControllerComponent";
        sc.fields["ElevationShapeId"] = { "ElevationShapeId", "08", "", "" };
        sc.field_names_ordered.push_back("ElevationShapeId");
        sc.fields["Mode"] = { "Mode", "10", "1:INACTIVE 2:WAIT 3:CONTINUOUS", "" };
        sc.field_names_ordered.push_back("Mode");
    }
    {
        ScopeDef& sc = m_scopes["PressureTriggerComponent"];
        sc.name = "PressureTriggerComponent";
        sc.fields["MaxHeightOffset"] = { "MaxHeightOffset", "0d", "", "" };
        sc.field_names_ordered.push_back("MaxHeightOffset");
        sc.fields["OnPress"] = { "OnPress", "12", "", "Program" };
        sc.field_names_ordered.push_back("OnPress");
        sc.fields["OnRelease"] = { "OnRelease", "1a", "", "Program" };
        sc.field_names_ordered.push_back("OnRelease");
        sc.fields["StayPressed"] = { "StayPressed", "20", "", "" };
        sc.field_names_ordered.push_back("StayPressed");
    }
    {
        ScopeDef& sc = m_scopes["DoorControllerComponent"];
        sc.name = "DoorControllerComponent";
        sc.fields["AnimationControllerId"] = { "AnimationControllerId", "08", "", "" };
        sc.field_names_ordered.push_back("AnimationControllerId");
        sc.fields["AnimationId"] = { "AnimationId", "10", "", "" };
        sc.field_names_ordered.push_back("AnimationId");
        sc.fields["Open"] = { "Open", "20", "", "" };
        sc.field_names_ordered.push_back("Open");
        sc.fields["CloseSoundId"] = { "CloseSoundId", "28", "", "" };
        sc.field_names_ordered.push_back("CloseSoundId");
        sc.fields["OpenSoundId"] = { "OpenSoundId", "30", "", "" };
        sc.field_names_ordered.push_back("OpenSoundId");
    }
    {
        ScopeDef& sc = m_scopes["ProgramComponent"];
        sc.name = "ProgramComponent";
        sc.fields["ExecuteOnce"] = { "ExecuteOnce", "08", "", "" };
        sc.field_names_ordered.push_back("ExecuteOnce");
        sc.fields["Program"] = { "Program", "12", "", "Program" };
        sc.field_names_ordered.push_back("Program");
        sc.fields["Enabled"] = { "Enabled", "18", "", "" };
        sc.field_names_ordered.push_back("Enabled");
        sc.fields["Trigger"] = { "Trigger", "20", "0:NONE 1:ACTIVATE 2:USE 3:OK\n4:CANCEL 5:DESTROY 6:RECEIVE_DAMAGE 7:BLOCK_DAMAGE\n8:INFLICT_DAMAGE 9:DEACTIVATE 10:LOAD", "" };
        sc.field_names_ordered.push_back("Trigger");
    }
    {
        ScopeDef& sc = m_scopes["MonsterEntityComponent"];
        sc.name = "MonsterEntityComponent";
        sc.fields["OnKill"] = { "OnKill", "0a", "", "Program" };
        sc.field_names_ordered.push_back("OnKill");
        sc.fields["OnHurt"] = { "OnHurt", "12", "", "Program" };
        sc.field_names_ordered.push_back("OnHurt");
        sc.fields["GivesExperience"] = { "GivesExperience", "18", "", "" };
        sc.field_names_ordered.push_back("GivesExperience");
        sc.fields["DefaultDeathAnimation"] = { "DefaultDeathAnimation", "20", "", "" };
        sc.field_names_ordered.push_back("DefaultDeathAnimation");
    }
    {
        ScopeDef& sc = m_scopes["PhysicsObjectComponent"];
        sc.name = "PhysicsObjectComponent";
        sc.fields["PhysicsEnabled"] = { "PhysicsEnabled", "08", "", "" };
        sc.field_names_ordered.push_back("PhysicsEnabled");
        sc.fields["GravityDirection"] = { "GravityDirection", "12", "", "Vector2" };
        sc.field_names_ordered.push_back("GravityDirection");
        sc.fields["GravityMagnitude"] = { "GravityMagnitude", "1d", "", "" };
        sc.field_names_ordered.push_back("GravityMagnitude");
        sc.fields["GroundDeceleration"] = { "GroundDeceleration", "25", "", "" };
        sc.field_names_ordered.push_back("GroundDeceleration");
        sc.fields["AirDeceleration"] = { "AirDeceleration", "2d", "", "" };
        sc.field_names_ordered.push_back("AirDeceleration");
        sc.fields["MaxSpeed"] = { "MaxSpeed", "35", "", "" };
        sc.field_names_ordered.push_back("MaxSpeed");
        sc.fields["AllowRotation"] = { "AllowRotation", "38", "", "" };
        sc.field_names_ordered.push_back("AllowRotation");
        sc.fields["Elasticity"] = { "Elasticity", "45", "", "" };
        sc.field_names_ordered.push_back("Elasticity");
    }
    {
        ScopeDef& sc = m_scopes["BreakableObjectComponent"];
        sc.name = "BreakableObjectComponent";
        sc.fields["BreaksOnImpact"] = { "BreaksOnImpact", "08", "", "" };
        sc.field_names_ordered.push_back("BreaksOnImpact");
        sc.fields["NumHitsToBreak"] = { "NumHitsToBreak", "10", "", "" };
        sc.field_names_ordered.push_back("NumHitsToBreak");
        sc.fields["RequiredDamageType"] = { "RequiredDamageType", "18", "", "" };
        sc.field_names_ordered.push_back("RequiredDamageType");
        sc.fields["OnBreak"] = { "OnBreak", "22", "", "Program" };
        sc.field_names_ordered.push_back("OnBreak");
    }
    {
        ScopeDef& sc = m_scopes["EntityControllerComponent"];
        sc.name = "EntityControllerComponent";
        sc.fields["EntityId"] = { "EntityId", "08", "", "" };
        sc.field_names_ordered.push_back("EntityId");
        sc.fields["AnimationControllerId"] = { "AnimationControllerId", "10", "", "" };
        sc.field_names_ordered.push_back("AnimationControllerId");
        sc.fields["DefaultMoveAnimationId"] = { "DefaultMoveAnimationId", "18", "", "" };
        sc.field_names_ordered.push_back("DefaultMoveAnimationId");
        sc.fields["RoamAreaId"] = { "RoamAreaId", "20", "", "" };
        sc.field_names_ordered.push_back("RoamAreaId");
        sc.fields["DefaultMoveSpeed"] = { "DefaultMoveSpeed", "2d", "", "" };
        sc.field_names_ordered.push_back("DefaultMoveSpeed");
        sc.fields["DefaultAcceleration"] = { "DefaultAcceleration", "35", "", "" };
        sc.field_names_ordered.push_back("DefaultAcceleration");
        sc.fields["TargetingDistance"] = { "TargetingDistance", "3d", "", "" };
        sc.field_names_ordered.push_back("TargetingDistance");
        sc.fields["MovementBehavior"] = { "MovementBehavior", "40", "1:NONE 2:ROAM 3:FOLLOW 4:FIGHT", "" };
        sc.field_names_ordered.push_back("MovementBehavior");
    }
    {
        ScopeDef& sc = m_scopes["EntityActionComponent"];
        sc.name = "EntityActionComponent";
        sc.fields["OnActivate"] = { "OnActivate", "0a", "", "Program" };
        sc.field_names_ordered.push_back("OnActivate");
    }
    {
        ScopeDef& sc = m_scopes["PhysicsPlatformComponent"];
        sc.name = "PhysicsPlatformComponent";
        sc.fields["Mass"] = { "Mass", "0d", "", "" };
        sc.field_names_ordered.push_back("Mass");
        sc.fields["SpringForce"] = { "SpringForce", "15", "", "" };
        sc.field_names_ordered.push_back("SpringForce");
        sc.fields["DecelerationForce"] = { "DecelerationForce", "1d", "", "" };
        sc.field_names_ordered.push_back("DecelerationForce");
        sc.fields["MinSpeed"] = { "MinSpeed", "25", "", "" };
        sc.field_names_ordered.push_back("MinSpeed");
    }
    {
        ScopeDef& sc = m_scopes["EntityInfoComponent"];
        sc.name = "EntityInfoComponent";
        sc.fields["EntityClass"] = { "EntityClass", "0a", "", "" };
        sc.field_names_ordered.push_back("EntityClass");
    }
    {
        ScopeDef& sc = m_scopes["HeroEntityComponent"];
        sc.name = "HeroEntityComponent";
        sc.fields["OnItemGet"] = { "OnItemGet", "0a", "", "Program" };
        sc.field_names_ordered.push_back("OnItemGet");
    }
    {
        ScopeDef& sc = m_scopes["BackgroundComponent"];
        sc.name = "BackgroundComponent";
        sc.fields["TextureName"] = { "TextureName", "0a", "", "" };
        sc.field_names_ordered.push_back("TextureName");
    }
    {
        ScopeDef& sc = m_scopes["PropertiesComponent"];
        sc.name = "PropertiesComponent";
        sc.fields["OnLoad"] = { "OnLoad", "0a", "", "Program" };
        sc.field_names_ordered.push_back("OnLoad");
    }
    {
        ScopeDef& sc = m_scopes["ParticleEmitter"];
        sc.name = "ParticleEmitter";
        sc.fields["Type"] = { "Type", "08", "0:NONE 1:BLAST 2:SPARK 3:TRAIL\n4:WHOOSH 5:FOUNTAIN", "" };
        sc.field_names_ordered.push_back("Type");
        sc.fields["BaseColor"] = { "BaseColor", "12", "", "FloatColor" };
        sc.field_names_ordered.push_back("BaseColor");
        sc.fields["Parameter"] = { "Parameter", "1d", "", "" };
        sc.field_names_ordered.push_back("Parameter");
        sc.fields["HueVariance"] = { "HueVariance", "25", "", "" };
        sc.field_names_ordered.push_back("HueVariance");
        sc.fields["SaturationVariance"] = { "SaturationVariance", "2d", "", "" };
        sc.field_names_ordered.push_back("SaturationVariance");
        sc.fields["LightnessVariance"] = { "LightnessVariance", "35", "", "" };
        sc.field_names_ordered.push_back("LightnessVariance");
        sc.fields["OriginOffset"] = { "OriginOffset", "3a", "", "Vector3" };
        sc.field_names_ordered.push_back("OriginOffset");
    }
    {
        ScopeDef& sc = m_scopes["ParticleEmitterComponent"];
        sc.name = "ParticleEmitterComponent";
        sc.fields["ParticleId"] = { "ParticleId", "10", "", "" };
        sc.field_names_ordered.push_back("ParticleId");
        sc.fields["ModelBindingId"] = { "ModelBindingId", "18", "", "" };
        sc.field_names_ordered.push_back("ModelBindingId");
        sc.fields["MaxParticles"] = { "MaxParticles", "20", "", "" };
        sc.field_names_ordered.push_back("MaxParticles");
        sc.fields["ParentEmitterId"] = { "ParentEmitterId", "28", "", "" };
        sc.field_names_ordered.push_back("ParentEmitterId");
        sc.fields["DestroyWhenFinished"] = { "DestroyWhenFinished", "30", "", "" };
        sc.field_names_ordered.push_back("DestroyWhenFinished");
        sc.fields["Emitter"] = { "Emitter", "3a", "", "ParticleEmitter" };
        sc.field_names_ordered.push_back("Emitter");
        sc.fields["LocalSystem"] = { "LocalSystem", "40", "", "" };
        sc.field_names_ordered.push_back("LocalSystem");
        sc.fields["Gravity"] = { "Gravity", "4a", "", "Vector3" };
        sc.field_names_ordered.push_back("Gravity");
        sc.fields["Rotation"] = { "Rotation", "52", "", "Vector3" };
        sc.field_names_ordered.push_back("Rotation");
    }
    {
        ScopeDef& sc = m_scopes["ParticleComponent"];
        sc.name = "ParticleComponent";
        sc.fields["TextureName"] = { "TextureName", "0a", "", "" };
        sc.field_names_ordered.push_back("TextureName");
        sc.fields["Size"] = { "Size", "15", "", "" };
        sc.field_names_ordered.push_back("Size");
    }
    {
        ScopeDef& sc = m_scopes["FireEmitterComponent"];
        sc.name = "FireEmitterComponent";
        sc.fields["ParticleEmitterId"] = { "ParticleEmitterId", "08", "", "" };
        sc.field_names_ordered.push_back("ParticleEmitterId");
        sc.fields["Origin"] = { "Origin", "12", "", "Vector3" };
        sc.field_names_ordered.push_back("Origin");
        sc.fields["LightId"] = { "LightId", "18", "", "" };
        sc.field_names_ordered.push_back("LightId");
        sc.fields["Color"] = { "Color", "2a", "", "FloatColor" };
        sc.field_names_ordered.push_back("Color");
        sc.fields["ParticleInterval"] = { "ParticleInterval", "35", "", "" };
        sc.field_names_ordered.push_back("ParticleInterval");
        sc.fields["ParticleMaxAge"] = { "ParticleMaxAge", "3d", "", "" };
        sc.field_names_ordered.push_back("ParticleMaxAge");
        sc.fields["ParticleSpread"] = { "ParticleSpread", "42", "", "Vector3" };
        sc.field_names_ordered.push_back("ParticleSpread");
        sc.fields["Origin3"] = { "Origin3", "4a", "", "Vector3" };
        sc.field_names_ordered.push_back("Origin3");
    }
    {
        ScopeDef& sc = m_scopes["SimpleGlowComponent"];
        sc.name = "SimpleGlowComponent";
        sc.fields["Color"] = { "Color", "0a", "", "FloatColor" };
        sc.field_names_ordered.push_back("Color");
        sc.fields["Size"] = { "Size", "15", "", "" };
        sc.field_names_ordered.push_back("Size");
        sc.fields["NumSegments"] = { "NumSegments", "18", "", "" };
        sc.field_names_ordered.push_back("NumSegments");
        sc.fields["Depth"] = { "Depth", "25", "", "" };
        sc.field_names_ordered.push_back("Depth");
        sc.fields["PulseAmount"] = { "PulseAmount", "2d", "", "" };
        sc.field_names_ordered.push_back("PulseAmount");
        sc.fields["PulseTime"] = { "PulseTime", "35", "", "" };
        sc.field_names_ordered.push_back("PulseTime");
        sc.fields["Offset"] = { "Offset", "3a", "", "Vector2" };
        sc.field_names_ordered.push_back("Offset");
    }
    {
        ScopeDef& sc = m_scopes["ParticleObjectComponent"];
        sc.name = "ParticleObjectComponent";
        sc.fields["ModelId"] = { "ModelId", "08", "", "" };
        sc.field_names_ordered.push_back("ModelId");
    }
    {
        ScopeDef& sc = m_scopes["OrbitControllerComponent"];
        sc.name = "OrbitControllerComponent";
        sc.fields["RotationAxis"] = { "RotationAxis", "0a", "", "Vector3" };
        sc.field_names_ordered.push_back("RotationAxis");
        sc.fields["RotationSpeed"] = { "RotationSpeed", "15", "", "" };
        sc.field_names_ordered.push_back("RotationSpeed");
        sc.fields["OrbitDistance"] = { "OrbitDistance", "1d", "", "" };
        sc.field_names_ordered.push_back("OrbitDistance");
    }
    {
        ScopeDef& sc = m_scopes["MonsterControllerComponent"];
        sc.name = "MonsterControllerComponent";
        sc.fields["WalkSpeed"] = { "WalkSpeed", "0d", "", "" };
        sc.field_names_ordered.push_back("WalkSpeed");
        sc.fields["AnimationControllerId"] = { "AnimationControllerId", "10", "", "" };
        sc.field_names_ordered.push_back("AnimationControllerId");
        sc.fields["EntityId"] = { "EntityId", "18", "", "" };
        sc.field_names_ordered.push_back("EntityId");
        sc.fields["RoamAreaId"] = { "RoamAreaId", "20", "", "" };
        sc.field_names_ordered.push_back("RoamAreaId");
    }
    {
        ScopeDef& sc = m_scopes["WalkingMonsterControllerComponent"];
        sc.name = "WalkingMonsterControllerComponent";
        sc.fields["WalkAnimationId"] = { "WalkAnimationId", "08", "", "" };
        sc.field_names_ordered.push_back("WalkAnimationId");
    }
    {
        ScopeDef& sc = m_scopes["ChargingMonsterControllerComponent"];
        sc.name = "ChargingMonsterControllerComponent";
        sc.fields["WalkAnimationId"] = { "WalkAnimationId", "08", "", "" };
        sc.field_names_ordered.push_back("WalkAnimationId");
        sc.fields["ChargeAnimationId"] = { "ChargeAnimationId", "10", "", "" };
        sc.field_names_ordered.push_back("ChargeAnimationId");
        sc.fields["RunAnimationId"] = { "RunAnimationId", "18", "", "" };
        sc.field_names_ordered.push_back("RunAnimationId");
        sc.fields["RunSpeed"] = { "RunSpeed", "25", "", "" };
        sc.field_names_ordered.push_back("RunSpeed");
        sc.fields["RunAcceleration"] = { "RunAcceleration", "2d", "", "" };
        sc.field_names_ordered.push_back("RunAcceleration");
    }
    {
        ScopeDef& sc = m_scopes["SnappingMonsterControllerComponent"];
        sc.name = "SnappingMonsterControllerComponent";
        sc.fields["StandAnimationId"] = { "StandAnimationId", "08", "", "" };
        sc.field_names_ordered.push_back("StandAnimationId");
        sc.fields["AttackAnimationId"] = { "AttackAnimationId", "10", "", "" };
        sc.field_names_ordered.push_back("AttackAnimationId");
        sc.fields["BlendAnimationId"] = { "BlendAnimationId", "18", "", "" };
        sc.field_names_ordered.push_back("BlendAnimationId");
        sc.fields["AttackAreaId"] = { "AttackAreaId", "20", "", "" };
        sc.field_names_ordered.push_back("AttackAreaId");
        sc.fields["AttackSoundId"] = { "AttackSoundId", "28", "", "" };
        sc.field_names_ordered.push_back("AttackSoundId");
    }
    {
        ScopeDef& sc = m_scopes["AttackComponent"];
        sc.name = "AttackComponent";
        sc.fields["AnimationId"] = { "AnimationId", "08", "", "" };
        sc.field_names_ordered.push_back("AnimationId");
        sc.fields["CollisionShapeId"] = { "CollisionShapeId", "10", "", "" };
        sc.field_names_ordered.push_back("CollisionShapeId");
        sc.fields["AttackAreaId"] = { "AttackAreaId", "18", "", "" };
        sc.field_names_ordered.push_back("AttackAreaId");
        sc.fields["SoundEffectId"] = { "SoundEffectId", "20", "", "" };
        sc.field_names_ordered.push_back("SoundEffectId");
        sc.fields["AttackInterval"] = { "AttackInterval", "2d", "", "" };
        sc.field_names_ordered.push_back("AttackInterval");
        sc.fields["AttackDuration"] = { "AttackDuration", "35", "", "" };
        sc.field_names_ordered.push_back("AttackDuration");
        sc.fields["DamageStartTime"] = { "DamageStartTime", "3d", "", "" };
        sc.field_names_ordered.push_back("DamageStartTime");
        sc.fields["DamageEndTime"] = { "DamageEndTime", "45", "", "" };
        sc.field_names_ordered.push_back("DamageEndTime");
        sc.fields["AnimationStartBlendTime"] = { "AnimationStartBlendTime", "4d", "", "" };
        sc.field_names_ordered.push_back("AnimationStartBlendTime");
        sc.fields["AnimationEndBlendTime"] = { "AnimationEndBlendTime", "55", "", "" };
        sc.field_names_ordered.push_back("AnimationEndBlendTime");
        sc.fields["OnAttack"] = { "OnAttack", "5a", "", "Program" };
        sc.field_names_ordered.push_back("OnAttack");
        sc.fields["DamageStartTime2"] = { "DamageStartTime2", "65", "", "" };
        sc.field_names_ordered.push_back("DamageStartTime2");
        sc.fields["DamageEndTime2"] = { "DamageEndTime2", "6d", "", "" };
        sc.field_names_ordered.push_back("DamageEndTime2");
    }
    {
        ScopeDef& sc = m_scopes["LeapingMonsterControllerComponent"];
        sc.name = "LeapingMonsterControllerComponent";
        sc.fields["WalkAnimationId"] = { "WalkAnimationId", "08", "", "" };
        sc.field_names_ordered.push_back("WalkAnimationId");
        sc.fields["LeapAttackId"] = { "LeapAttackId", "10", "", "" };
        sc.field_names_ordered.push_back("LeapAttackId");
    }
    {
        ScopeDef& sc = m_scopes["SkellyMonsterControllerComponent"];
        sc.name = "SkellyMonsterControllerComponent";
        sc.fields["CharControllerId"] = { "CharControllerId", "08", "", "" };
        sc.field_names_ordered.push_back("CharControllerId");
        sc.fields["AttackAreaId"] = { "AttackAreaId", "10", "", "" };
        sc.field_names_ordered.push_back("AttackAreaId");
    }
    {
        ScopeDef& sc = m_scopes["StaticMonsterControllerComponent"];
        sc.name = "StaticMonsterControllerComponent";
        sc.fields["AnimationId"] = { "AnimationId", "08", "", "" };
        sc.field_names_ordered.push_back("AnimationId");
        sc.fields["SoundId"] = { "SoundId", "10", "", "" };
        sc.field_names_ordered.push_back("SoundId");
    }
    {
        ScopeDef& sc = m_scopes["ShootingMonsterControllerComponent"];
        sc.name = "ShootingMonsterControllerComponent";
        sc.fields["WalkAnimationId"] = { "WalkAnimationId", "08", "", "" };
        sc.field_names_ordered.push_back("WalkAnimationId");
        sc.fields["ShootAnimationId"] = { "ShootAnimationId", "10", "", "" };
        sc.field_names_ordered.push_back("ShootAnimationId");
    }
    {
        ScopeDef& sc = m_scopes["BatMonsterControllerComponent"];
        sc.name = "BatMonsterControllerComponent";
        sc.fields["FlyAnimationId"] = { "FlyAnimationId", "08", "", "" };
        sc.field_names_ordered.push_back("FlyAnimationId");
        sc.fields["FlapSoundId"] = { "FlapSoundId", "10", "", "" };
        sc.field_names_ordered.push_back("FlapSoundId");
    }
    {
        ScopeDef& sc = m_scopes["BouncingMonsterControllerComponent"];
        sc.name = "BouncingMonsterControllerComponent";
        sc.fields["JumpAnimationId"] = { "JumpAnimationId", "08", "", "" };
        sc.field_names_ordered.push_back("JumpAnimationId");
        sc.fields["FallAnimationId"] = { "FallAnimationId", "10", "", "" };
        sc.field_names_ordered.push_back("FallAnimationId");
        sc.fields["JumpAngle"] = { "JumpAngle", "1d", "", "" };
        sc.field_names_ordered.push_back("JumpAngle");
        sc.fields["JumpSpeed"] = { "JumpSpeed", "25", "", "" };
        sc.field_names_ordered.push_back("JumpSpeed");
    }
    {
        ScopeDef& sc = m_scopes["MonsterDeathControllerComponent"];
        sc.name = "MonsterDeathControllerComponent";
        sc.fields["ParticleEmitterId"] = { "ParticleEmitterId", "08", "", "" };
        sc.field_names_ordered.push_back("ParticleEmitterId");
    }
    {
        ScopeDef& sc = m_scopes["GenericMonsterControllerComponent"];
        sc.name = "GenericMonsterControllerComponent";
        sc.fields["WalkAnimationId"] = { "WalkAnimationId", "08", "", "" };
        sc.field_names_ordered.push_back("WalkAnimationId");
    }
    {
        ScopeDef& sc = m_scopes["SwingableWeaponComponent"];
        sc.name = "SwingableWeaponComponent";
        sc.fields["ModelId"] = { "ModelId", "08", "", "" };
        sc.field_names_ordered.push_back("ModelId");
        sc.fields["TrailId"] = { "TrailId", "10", "", "" };
        sc.field_names_ordered.push_back("TrailId");
        sc.fields["ImpactParticleEmitterId"] = { "ImpactParticleEmitterId", "20", "", "" };
        sc.field_names_ordered.push_back("ImpactParticleEmitterId");
        sc.fields["SwingSoundId"] = { "SwingSoundId", "28", "", "" };
        sc.field_names_ordered.push_back("SwingSoundId");
        sc.fields["DamageImpactSoundId"] = { "DamageImpactSoundId", "30", "", "" };
        sc.field_names_ordered.push_back("DamageImpactSoundId");
        sc.fields["GlowTrailId"] = { "GlowTrailId", "38", "", "" };
        sc.field_names_ordered.push_back("GlowTrailId");
        sc.fields["CollisionShapeId"] = { "CollisionShapeId", "40", "", "" };
        sc.field_names_ordered.push_back("CollisionShapeId");
        sc.fields["GlowId"] = { "GlowId", "48", "", "" };
        sc.field_names_ordered.push_back("GlowId");
        sc.fields["BaseLength"] = { "BaseLength", "55", "", "" };
        sc.field_names_ordered.push_back("BaseLength");
        sc.fields["GlowLength"] = { "GlowLength", "5d", "", "" };
        sc.field_names_ordered.push_back("GlowLength");
        sc.fields["GlowIntensity"] = { "GlowIntensity", "65", "", "" };
        sc.field_names_ordered.push_back("GlowIntensity");
        sc.fields["Width"] = { "Width", "6d", "", "" };
        sc.field_names_ordered.push_back("Width");
        sc.fields["GlowColor"] = { "GlowColor", "72", "", "FloatColor" };
        sc.field_names_ordered.push_back("GlowColor");
    }
    {
        ScopeDef& sc = m_scopes["SwingableWeaponControllerComponent"];
        sc.name = "SwingableWeaponControllerComponent";
        sc.fields["ControllingModelId"] = { "ControllingModelId", "08", "", "" };
        sc.field_names_ordered.push_back("ControllingModelId");
        sc.fields["ControllingBoneName"] = { "ControllingBoneName", "12", "", "" };
        sc.field_names_ordered.push_back("ControllingBoneName");
        sc.fields["WeaponTemplateName"] = { "WeaponTemplateName", "1a", "", "" };
        sc.field_names_ordered.push_back("WeaponTemplateName");
    }
    {
        ScopeDef& sc = m_scopes["SwingComponent"];
        sc.name = "SwingComponent";
        sc.fields["AnimationId"] = { "AnimationId", "08", "", "" };
        sc.field_names_ordered.push_back("AnimationId");
        sc.fields["SwingLeftWeapon"] = { "SwingLeftWeapon", "10", "", "" };
        sc.field_names_ordered.push_back("SwingLeftWeapon");
        sc.fields["SwingRightWeapon"] = { "SwingRightWeapon", "18", "", "" };
        sc.field_names_ordered.push_back("SwingRightWeapon");
        sc.fields["StartFrame"] = { "StartFrame", "35", "", "" };
        sc.field_names_ordered.push_back("StartFrame");
        sc.fields["EndFrame"] = { "EndFrame", "3d", "", "" };
        sc.field_names_ordered.push_back("EndFrame");
    }
    {
        ScopeDef& sc = m_scopes["WeaponGlowComponent"];
        sc.name = "WeaponGlowComponent";
        sc.fields["ParticleEmitterId"] = { "ParticleEmitterId", "08", "", "" };
        sc.field_names_ordered.push_back("ParticleEmitterId");
        sc.fields["Color"] = { "Color", "12", "", "FloatColor" };
        sc.field_names_ordered.push_back("Color");
        sc.fields["ParticleColor"] = { "ParticleColor", "1a", "", "FloatColor" };
        sc.field_names_ordered.push_back("ParticleColor");
        sc.fields["Width"] = { "Width", "25", "", "" };
        sc.field_names_ordered.push_back("Width");
    }
    {
        ScopeDef& sc = m_scopes["WeaponTrailComponent"];
        sc.name = "WeaponTrailComponent";
        sc.fields["Color"] = { "Color", "0a", "", "FloatColor" };
        sc.field_names_ordered.push_back("Color");
    }
    {
        ScopeDef& sc = m_scopes["PortalComponent"];
        sc.name = "PortalComponent";
        sc.fields["DestinationSceneName"] = { "DestinationSceneName", "0a", "", "" };
        sc.field_names_ordered.push_back("DestinationSceneName");
        sc.fields["SpawnPointName"] = { "SpawnPointName", "12", "", "" };
        sc.field_names_ordered.push_back("SpawnPointName");
        sc.fields["TapToEnter"] = { "TapToEnter", "18", "", "" };
        sc.field_names_ordered.push_back("TapToEnter");
        sc.fields["TriggerShapeId"] = { "TriggerShapeId", "20", "", "" };
        sc.field_names_ordered.push_back("TriggerShapeId");
    }
    {
        ScopeDef& sc = m_scopes["SpawnPointComponent"];
        sc.name = "SpawnPointComponent";
        sc.fields["FacingDirection"] = { "FacingDirection", "08", "", "" };
        sc.field_names_ordered.push_back("FacingDirection");
        sc.fields["SpawnOffset"] = { "SpawnOffset", "12", "", "Vector3" };
        sc.field_names_ordered.push_back("SpawnOffset");
    }
    {
        ScopeDef& sc = m_scopes["CollectableItemComponent"];
        sc.name = "CollectableItemComponent";
        sc.fields["Type"] = { "Type", "08", "0:UNKNOWN 1:HEALTH_POTION 2:MANA_POTION 3:MAGIC_POWER\n4:COIN 5:EXPERIENCE", "" };
        sc.field_names_ordered.push_back("Type");
        sc.fields["Value"] = { "Value", "10", "", "" };
        sc.field_names_ordered.push_back("Value");
        sc.fields["OnCollect"] = { "OnCollect", "1a", "", "Program" };
        sc.field_names_ordered.push_back("OnCollect");
        sc.fields["Identifier"] = { "Identifier", "22", "", "" };
        sc.field_names_ordered.push_back("Identifier");
        sc.fields["ItemName"] = { "ItemName", "2a", "", "" };
        sc.field_names_ordered.push_back("ItemName");
        sc.fields["RequiresPickup"] = { "RequiresPickup", "30", "", "" };
        sc.field_names_ordered.push_back("RequiresPickup");
    }
    {
        ScopeDef& sc = m_scopes["TouchableComponent"];
        sc.name = "TouchableComponent";
        sc.fields["TouchRadius"] = { "TouchRadius", "0d", "", "" };
        sc.field_names_ordered.push_back("TouchRadius");
        sc.fields["OnTouch"] = { "OnTouch", "12", "", "Program" };
        sc.field_names_ordered.push_back("OnTouch");
    }
    {
        ScopeDef& sc = m_scopes["ItemDropComponent_ItemDropEntry"];
        sc.name = "ItemDropComponent_ItemDropEntry";
        sc.fields["TemplateName"] = { "TemplateName", "0a", "", "" };
        sc.field_names_ordered.push_back("TemplateName");
        sc.fields["ItemIdentifier"] = { "ItemIdentifier", "12", "", "" };
        sc.field_names_ordered.push_back("ItemIdentifier");
        sc.fields["DropChance"] = { "DropChance", "1d", "", "" };
        sc.field_names_ordered.push_back("DropChance");
        sc.fields["MinCount"] = { "MinCount", "20", "", "" };
        sc.field_names_ordered.push_back("MinCount");
        sc.fields["MaxCount"] = { "MaxCount", "28", "", "" };
        sc.field_names_ordered.push_back("MaxCount");
    }
    {
        ScopeDef& sc = m_scopes["ItemDropComponent"];
        sc.name = "ItemDropComponent";
        sc.fields["ItemName"] = { "ItemName", "0a", "", "" };
        sc.field_names_ordered.push_back("ItemName");
        sc.fields["ItemIdentifier"] = { "ItemIdentifier", "12", "", "" };
        sc.field_names_ordered.push_back("ItemIdentifier");
        sc.fields["DropEntry"] = { "DropEntry", "1a", "", "ItemDropComponent_ItemDropEntry" };
        sc.field_names_ordered.push_back("DropEntry");
        sc.fields["CanDropMultipleItems"] = { "CanDropMultipleItems", "20", "", "" };
        sc.field_names_ordered.push_back("CanDropMultipleItems");
        sc.fields["CanDropDefaultItems"] = { "CanDropDefaultItems", "28", "", "" };
        sc.field_names_ordered.push_back("CanDropDefaultItems");
    }
    {
        ScopeDef& sc = m_scopes["OverlayTextComponent"];
        sc.name = "OverlayTextComponent";
        sc.fields["Text"] = { "Text", "0a", "", "" };
        sc.field_names_ordered.push_back("Text");
        sc.fields["TextOffset"] = { "TextOffset", "10", "", "" };
        sc.field_names_ordered.push_back("TextOffset");
        sc.fields["SpriteName"] = { "SpriteName", "1a", "", "" };
        sc.field_names_ordered.push_back("SpriteName");
        sc.fields["SpriteOffset"] = { "SpriteOffset", "22", "", "Vector2" };
        sc.field_names_ordered.push_back("SpriteOffset");
    }
    {
        ScopeDef& sc = m_scopes["PortalEffectComponent"];
        sc.name = "PortalEffectComponent";
        sc.fields["PolygonId"] = { "PolygonId", "08", "", "" };
        sc.field_names_ordered.push_back("PolygonId");
        sc.fields["TextureMappingId"] = { "TextureMappingId", "10", "", "" };
        sc.field_names_ordered.push_back("TextureMappingId");
        sc.fields["Color"] = { "Color", "1a", "", "FloatColor" };
        sc.field_names_ordered.push_back("Color");
        sc.fields["Speed"] = { "Speed", "22", "", "Vector3" };
        sc.field_names_ordered.push_back("Speed");
    }
    {
        ScopeDef& sc = m_scopes["MagicBoltComponent"];
        sc.name = "MagicBoltComponent";
        sc.fields["ParticleEmitterId"] = { "ParticleEmitterId", "08", "", "" };
        sc.field_names_ordered.push_back("ParticleEmitterId");
        sc.fields["SwooshSoundId"] = { "SwooshSoundId", "10", "", "" };
        sc.field_names_ordered.push_back("SwooshSoundId");
        sc.fields["HitSoundId"] = { "HitSoundId", "18", "", "" };
        sc.field_names_ordered.push_back("HitSoundId");
        sc.fields["Color"] = { "Color", "22", "", "FloatColor" };
        sc.field_names_ordered.push_back("Color");
        sc.fields["Speed"] = { "Speed", "2d", "", "" };
        sc.field_names_ordered.push_back("Speed");
    }
    {
        ScopeDef& sc = m_scopes["MagicExplosionComponent"];
        sc.name = "MagicExplosionComponent";
        sc.fields["ParticleEmitterId"] = { "ParticleEmitterId", "08", "", "" };
        sc.field_names_ordered.push_back("ParticleEmitterId");
        sc.fields["SoundId"] = { "SoundId", "10", "", "" };
        sc.field_names_ordered.push_back("SoundId");
        sc.fields["Color"] = { "Color", "1a", "", "FloatColor" };
        sc.field_names_ordered.push_back("Color");
        sc.fields["Radius"] = { "Radius", "25", "", "" };
        sc.field_names_ordered.push_back("Radius");
        sc.fields["Duration"] = { "Duration", "2d", "", "" };
        sc.field_names_ordered.push_back("Duration");
    }
    {
        ScopeDef& sc = m_scopes["SkillComponent"];
        sc.name = "SkillComponent";
        sc.fields["CastFinishAnimationId"] = { "CastFinishAnimationId", "08", "", "" };
        sc.field_names_ordered.push_back("CastFinishAnimationId");
        sc.fields["Origin"] = { "Origin", "12", "", "Vector3" };
        sc.field_names_ordered.push_back("Origin");
        sc.fields["CastObjectTemplateName"] = { "CastObjectTemplateName", "1a", "", "" };
        sc.field_names_ordered.push_back("CastObjectTemplateName");
    }
    {
        ScopeDef& sc = m_scopes["MagicSpellCastComponent"];
        sc.name = "MagicSpellCastComponent";
        sc.fields["ParticleEmitterId"] = { "ParticleEmitterId", "08", "", "" };
        sc.field_names_ordered.push_back("ParticleEmitterId");
        sc.fields["SoundEffectId"] = { "SoundEffectId", "10", "", "" };
        sc.field_names_ordered.push_back("SoundEffectId");
    }
    {
        ScopeDef& sc = m_scopes["FireBreathComponent"];
        sc.name = "FireBreathComponent";
        sc.fields["ParticleEmitterId"] = { "ParticleEmitterId", "08", "", "" };
        sc.field_names_ordered.push_back("ParticleEmitterId");
        sc.fields["SwooshSoundId"] = { "SwooshSoundId", "10", "", "" };
        sc.field_names_ordered.push_back("SwooshSoundId");
        sc.fields["Color"] = { "Color", "1a", "", "FloatColor" };
        sc.field_names_ordered.push_back("Color");
    }
    {
        ScopeDef& sc = m_scopes["ProjectileControllerComponent"];
        sc.name = "ProjectileControllerComponent";
        sc.fields["AlignObjectRotation"] = { "AlignObjectRotation", "08", "", "" };
        sc.field_names_ordered.push_back("AlignObjectRotation");
        sc.fields["BreakOnGroundCollision"] = { "BreakOnGroundCollision", "10", "", "" };
        sc.field_names_ordered.push_back("BreakOnGroundCollision");
    }
    {
        ScopeDef& sc = m_scopes["MagicBombComponent"];
        sc.name = "MagicBombComponent";
        sc.fields["Color"] = { "Color", "0a", "", "FloatColor" };
        sc.field_names_ordered.push_back("Color");
    }
    {
        ScopeDef& sc = m_scopes["MagicHookshotComponent"];
        sc.name = "MagicHookshotComponent";
        sc.fields["ParticleEmitterId"] = { "ParticleEmitterId", "08", "", "" };
        sc.field_names_ordered.push_back("ParticleEmitterId");
        sc.fields["SwooshSoundId"] = { "SwooshSoundId", "10", "", "" };
        sc.field_names_ordered.push_back("SwooshSoundId");
        sc.fields["HitSoundId"] = { "HitSoundId", "18", "", "" };
        sc.field_names_ordered.push_back("HitSoundId");
        sc.fields["Color"] = { "Color", "22", "", "FloatColor" };
        sc.field_names_ordered.push_back("Color");
        sc.fields["GroundHitSoundId"] = { "GroundHitSoundId", "28", "", "" };
        sc.field_names_ordered.push_back("GroundHitSoundId");
    }
    {
        ScopeDef& sc = m_scopes["SpellComponent"];
        sc.name = "SpellComponent";
        sc.fields["OnCast"] = { "OnCast", "0a", "", "Program" };
        sc.field_names_ordered.push_back("OnCast");
    }
    m_component_classes = {
        "SpriteComponent",
        "ModelComponent",
        "KeyframeAnimationComponent",
        "BlendAnimationComponent",
        "ModelTransformControllerComponent",
        "GroundPolygonComponent",
        "GroundMeshComponent",
        "GroundMeshGeneratorComponent",
        "TextureMappingComponent",
        "WaterMeshComponent",
        "ShapeComponent",
        "CollisionShapeComponent",
        "DamageComponent",
        "HealthComponent",
        "BoneControlledCollisionShapeComponent",
        "ObjectLinkControllerComponent",
        "LightComponent",
        "ShadowComponent",
        "SoundEffectComponent",
        "AnimationControllerComponent",
        "CharAnimControllerComponent",
        "CharControllerComponent",
        "EntityComponent",
        "BushControllerComponent",
        "ElevatorControllerComponent",
        "PressureTriggerComponent",
        "DoorControllerComponent",
        "ProgramComponent",
        "MonsterEntityComponent",
        "PhysicsObjectComponent",
        "BreakableObjectComponent",
        "EntityControllerComponent",
        "EntityActionComponent",
        "PhysicsPlatformComponent",
        "EntityInfoComponent",
        "HeroEntityComponent",
        "BackgroundComponent",
        "PropertiesComponent",
        "ParticleEmitterComponent",
        "ParticleComponent",
        "FireEmitterComponent",
        "SimpleGlowComponent",
        "ParticleObjectComponent",
        "OrbitControllerComponent",
        "ParticleFieldComponent",
        "MonsterControllerComponent",
        "WalkingMonsterControllerComponent",
        "ChargingMonsterControllerComponent",
        "SnappingMonsterControllerComponent",
        "AttackComponent",
        "LeapingMonsterControllerComponent",
        "SkellyMonsterControllerComponent",
        "StaticMonsterControllerComponent",
        "ShootingMonsterControllerComponent",
        "BatMonsterControllerComponent",
        "BouncingMonsterControllerComponent",
        "MonsterDeathControllerComponent",
        "GenericMonsterControllerComponent",
        "SwingableWeaponComponent",
        "SwingableWeaponControllerComponent",
        "SwingComponent",
        "WeaponGlowComponent",
        "WeaponTrailComponent",
        "PortalComponent",
        "SpawnPointComponent",
        "CollectableItemComponent",
        "TouchableComponent",
        "ItemDropComponent",
        "OverlayTextComponent",
        "PortalEffectComponent",
        "MagicBoltComponent",
        "MagicExplosionComponent",
        "SkillComponent",
        "MagicSpellCastComponent",
        "FireBreathComponent",
        "ProjectileControllerComponent",
        "MagicBombComponent",
        "MagicHookshotComponent",
        "SpellComponent",
        "DimensionObjectComponent",
        "DimensionSpellComponent",
    };
}

} // namespace ruby::filerift
