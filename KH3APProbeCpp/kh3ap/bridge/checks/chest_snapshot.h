#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace kh3ap::chest_manifest
{
    struct Assignment
    {
        int64_t location{};
        int difficulty{};
        std::wstring song;
        bool cleared{}, placed{};
        std::wstring map, placement;
        std::array<float, 3> position{};
        float yaw{};
        std::wstring keyblade, keyblade_name;
        std::array<float, 4> body{}, trim{};

        auto same_layout(const Assignment& other) const -> bool
        {
            return location == other.location && placed == other.placed &&
                map == other.map && position == other.position && yaw == other.yaw &&
                keyblade == other.keyblade && body == other.body && trim == other.trim;
        }
    };

    struct MapKeyblade
    {
        std::wstring name, token;
        bool operator==(const MapKeyblade&) const = default;
    };

    // Worker-built values only: no UObject or JSON parsing on the game thread.
    struct Snapshot
    {
        std::filesystem::path bridge_dir;
        uint32_t process_id{};
        std::wstring seed;
        int64_t slot{};
        std::map<std::wstring, Assignment> assignments;
        // Includes native-world matches, which do not need an interaction gate.
        // Empty when Chestsanity is disabled; Melody chests are excluded.
        std::map<std::wstring, MapKeyblade> map_keyblades;
        // Full seed-option pool, independent of owned blades or loaded chests.
        std::map<std::wstring, std::wstring> eligible_keyblades; // Display name -> native item token.
        bool has_vanilla_assignments{};
        bool roxas_chestsanity{};

        auto same_layout(const Snapshot& other) const -> bool
        {
            if (bridge_dir != other.bridge_dir || process_id != other.process_id ||
                seed != other.seed || slot != other.slot || roxas_chestsanity != other.roxas_chestsanity ||
                assignments.size() != other.assignments.size()) return false;
            auto left = assignments.begin(), right = other.assignments.begin();
            for (; left != assignments.end(); ++left, ++right)
                if (left->first != right->first || !left->second.same_layout(right->second)) return false;
            return true;
        }
    };
}
