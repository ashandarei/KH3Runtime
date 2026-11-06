#pragma once
#include <filesystem>
#include <optional>
#include <map>
#include <string>

namespace kh3ap
{
    class JsonObject;
    // Bounded V4 reader for the stored settings record in newly generated paks.
    // Compressed game assets are skipped; no archive extraction or loose-file fallback.
    auto read_pak_seed_settings(const std::filesystem::path& pak) -> std::string;
    // Reads the immutable options, compact location/reward catalog and optional
    // report targets in one index pass. All three records are stored uncompressed.
    auto read_pak_seed_records(const std::filesystem::path& pak) -> std::map<std::string, std::string>;
    auto bind_installed_pak_seed_source() -> void;
    auto find_seed_pak(const std::filesystem::path& mods) -> std::filesystem::path;
    auto installed_pak_seed_settings() -> const std::optional<std::string>&;
    // Immutable parsed options from the installed pak, shared by all services.
    auto installed_seed_slot_data() -> const JsonObject&;
}
