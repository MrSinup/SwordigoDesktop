#include "ruby/caver/game/player_profile.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "platform/data_path.h"
#include "platform/protobuf_reader.h"

namespace fs = std::filesystem;

// The asset root the platform data path prepends to "resources/". data_path.cpp
// declares it weak and every executable defines it, so the resource lookup below
// follows the same root the rest of the app reads.
extern std::string g_instance_assets_dir;

namespace caver {
namespace game {
namespace {

std::string read_file(const std::string& path, bool* ok) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { if (ok) *ok = false; return std::string(); }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (ok) *ok = true;
    return bytes;
}

bool write_file(const std::string& path, const std::string& bytes, std::string* error) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "cannot open " + path + " for writing";
        return false;
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

// Proto::PlayerProfile — the .gplayer container. Field numbers are the engine's
// own (`Caver::Proto::PlayerProfile::k<Field>FieldNumber`).
namespace PlayerProfileF {
constexpr int Name = 1, ExperienceLevel = 2, TimePlayed = 3, GameState = 4,
              EquippedWeaponName = 5, EquippedArmorName = 6, WeaponTrinketName = 7,
              ArmorTrinketName = 8, CurrentLevelTitle = 9, LastPlayedTime = 10,
              PercentCompleted = 11, Counter = 12, CheatEnabled = 13, Identifier = 14;
constexpr int CounterName = 1, CounterValue = 2;
}

} // namespace

std::string PlayerProfile::resource_path(const std::string& name, const std::string& type) {
    // `PathForResourceOfType(name, type)` appends ".type" and looks it up in the
    // game's resource bundle. A resource bundle is <root>/resources, where <root>
    // is the asset directory the instance was launched with (assets13, assets, a
    // mod overlay, …). The engine looks in the instance's bundle first and falls
    // back to the base game's, which is what the candidate list mirrors.
    const std::string file = name + "." + type;
    if (!file.empty() && file[0] == '/') return file;   // already an absolute path

    std::vector<std::string> roots;
    const char* env = std::getenv("SWORDIGO_DATA_DIR");
    if (env && *env) roots.push_back(env);
    const std::string user_dir = get_user_data_dir();
    if (!user_dir.empty()) roots.push_back(user_dir);

    std::vector<std::string> candidates;
    for (const auto& root : roots) {
        std::string base = root;
        if (!base.empty() && base.back() != '/' && base.back() != '\\') base += "/";
        if (!g_instance_assets_dir.empty())
            candidates.push_back(base + g_instance_assets_dir + "/resources/" + file);
        if (g_instance_assets_dir != "assets")
            candidates.push_back(base + "assets/resources/" + file);
        candidates.push_back(base + "resources/" + file);
    }
    std::error_code ec;
    for (const auto& candidate : candidates)
        if (fs::exists(candidate, ec)) return candidate;

    // Nothing on disk: hand back the primary location so the caller's error
    // message names the path the engine would have used.
    if (!candidates.empty()) return candidates.front();
    return get_data_path(file);
}

std::string PlayerProfile::local_file_path(const std::string& identifier) {
    // DocumentsDirectoryPath() is the VFS Documents folder the game writes saves to.
    std::string documents = get_vfs_save_dir();
    if (!documents.empty() && documents.back() != '/' && documents.back() != '\\')
        documents += "/";
    return documents + identifier + ".gplayer";
}

bool PlayerProfile::profile_exists(const std::string& identifier) {
    std::error_code ec;
    return fs::exists(local_file_path(identifier), ec);
}

bool PlayerProfile::load_world_data() {
    bool ok = false;

    // PathForResourceOfType("test", "scmap") — the world graph. The literal
    // resource name in the engine is "test"; the file on disk is test.scmap.
    std::string scmap = read_file(resource_path("test", "scmap"), &ok);
    if (ok) map_.load_from_bytes(scmap);

    // PathForResourceOfType("gamedata", "gdata")
    std::string gdata = read_file(resource_path("gamedata", "gdata"), &ok);
    if (!ok) return false;
    return data_.load_from_bytes(gdata);
}

bool PlayerProfile::create_profile(const std::string& template_id, const std::string& identifier) {
    identifier_ = identifier;

    bool ok = false;
    // PathForResourceOfType(template_id, "gstate")
    std::string gstate = read_file(resource_path(template_id, "gstate"), &ok);
    if (!ok || gstate.empty()) return false;

    if (!load_world_data()) return false;

    state_bytes_ = gstate;
    if (!state_.load_from_bytes(gstate)) return false;

    // The freshly created profile is saved immediately (Save(true) in
    // CreateProfile), which is what makes ProfileExists() true afterwards.
    return save(true, nullptr);
}

bool PlayerProfile::load_from_file(const std::string& path) {
    bool ok = false;
    std::string bytes = read_file(path, &ok);
    if (!ok) return false;

    if (!load_world_data()) return false;

    // A .gplayer is a Proto::PlayerProfile whose field 4 is the GameState. The
    // identifier comes from field 14 when present, otherwise from the file name.
    std::string state;
    std::string identity;
    try {
        proto::Reader reader(bytes);
        proto::Field field;
        while (reader.read_field(field)) {
            if (field.wire_type != proto::WIRE_LEN) continue;
            if (field.field_number == PlayerProfileF::GameState) state = field.bytes_val;
            else if (field.field_number == PlayerProfileF::Identifier) identity = field.bytes_val;
            else if (field.field_number == PlayerProfileF::Name && identity.empty())
                identity = field.bytes_val;
        }
    } catch (...) {
        return false;
    }
    if (state.empty()) {
        // Tolerate a bare GameState file: some tooling writes those directly.
        state = bytes;
    }
    identifier_ = identity.empty() ? fs::path(path).stem().string() : identity;
    state_bytes_ = state;
    return state_.load_from_bytes(state);
}

bool PlayerProfile::save(bool write_to_disk, std::string* error) {
    if (!write_to_disk) return true;

    // Save() writes a Proto::PlayerProfile: the identifier, the derived
    // presentation fields the profile panel shows, the counters, and the
    // GameState as field 4. The GameState itself is written from the bytes it
    // was read as, so an untouched profile round-trips exactly.
    proto::Writer out;
    out.write_string_field(PlayerProfileF::Name, identifier_);
    out.write_varint_field(PlayerProfileF::ExperienceLevel, state_.character().experience_level);
    out.write_bytes_field(PlayerProfileF::GameState, state_bytes_);
    out.write_string_field(PlayerProfileF::Identifier, identifier_);
    out.write_string_field(PlayerProfileF::CurrentLevelTitle, current_level_title());
    return write_file(local_file_path(identifier_), out.to_string(), error);
}

LevelState& PlayerProfile::current_level_state() {
    return state_.state_for_level(state_.current_level());
}

std::string PlayerProfile::current_level_title() const {
    std::string title = map_.title_for_level(state_.current_level());
    if (title.empty()) title = state_.current_level();
    return title;
}

} // namespace game
} // namespace caver
