#pragma once
// player_profile.h — `Caver::PlayerProfile`, the thing that owns a save.
//
// Recovered chain (arm32_13 0x33BD14 CreateProfile, 0x33BDB0 LoadGameStateFromProtobufMessage,
// 0x33BCAC LocalFilePath, 0x33C32C Filename, 0x33BC5C ProfileExists):
//
//   CreateProfile(id):
//       PathForResourceOfType(id, "gstate")   -> e.g. newplayer.gstate
//       Proto::GameState <- LoadProtobufMessageFromFile
//       LoadGameStateFromProtobufMessage(state)
//       Save(true)
//
//   LoadGameStateFromProtobufMessage(state):
//       PathForResourceOfType("test", "scmap")    -> test.scmap      -> Proto::Map
//       PathForResourceOfType("gamedata", "gdata")-> gamedata.gdata  -> Proto::GameData
//       GameState(gamedata) <- state
//
//   Filename()      = id + ".gplayer"
//   LocalFilePath() = DocumentsDirectoryPath() + "/" + Filename()
//   ProfileExists() = FileExistsAtPath(LocalFilePath())
//
// So a "new profile" is literally: parse `<id>.gstate`, attach the world data,
// then write it to Documents as `<id>.gplayer`. There is no special new-game
// path in the engine, which is why this boot does the same thing.

#include <string>

#include "ruby/caver/game/game_data.h"
#include "ruby/caver/game/game_state.h"

namespace caver {
namespace game {

class PlayerProfile {
public:
    // `PathForResourceOfType(name, type)` — resolved through the platform data
    // path (the same `<data>/<assets>/resources/` root the game reads).
    static std::string resource_path(const std::string& name, const std::string& type);

    // DocumentsDirectoryPath() / "<id>.gplayer"
    static std::string local_file_path(const std::string& identifier);
    static bool profile_exists(const std::string& identifier);

    // CreateProfile(identifier): parse `<identifier>.gstate` and adopt it.
    //
    // The engine's own shell creates a brand-new profile whose *state* comes from
    // the `newplayer` template, and the profile id is what the save is named
    // after. Both are arguments here because getting them the same way round
    // matters: `template_id` is read as `<template_id>.gstate` and `identifier`
    // is written as `<identifier>.gplayer`.
    bool create_profile(const std::string& template_id, const std::string& identifier);
    bool create_profile(const std::string& identifier) {
        return create_profile(identifier, identifier);
    }

    // Load(path): read an existing `<id>.gplayer`.
    bool load_from_file(const std::string& path);

    // Save(write_to_disk): serialize the state back out.
    bool save(bool write_to_disk, std::string* error = nullptr);

    const std::string& identifier() const { return identifier_; }
    const GameData& data() const { return data_; }
    const GameMap&  map() const { return map_; }
    GameState&       state() { return state_; }
    const GameState& state() const { return state_; }

    // `currentLevelState()` / `currentLevelTitle()`.
    LevelState&       current_level_state();
    std::string       current_level_title() const;

    // Raw bytes the profile was created from, so OpenSwordigo can re-emit a
    // byte-identical .gplayer without re-deriving the schema.
    const std::string& state_bytes() const { return state_bytes_; }

private:
    bool load_world_data();

    std::string identifier_;
    GameData data_;
    GameMap  map_;
    GameState state_;
    std::string state_bytes_;
};

} // namespace game
} // namespace caver
