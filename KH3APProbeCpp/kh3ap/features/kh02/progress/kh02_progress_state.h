#pragma once

#include "kh3ap/features/kh02/wardrobe/kh02_wardrobe_color_mode.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace kh3ap::kh02_progress
{
    using Flags = std::map<std::string, int, std::less<>>;
    using ChallengeValues = std::map<int, int>;
    using EncounterEvents = std::map<int, bool>;

    struct CosmeticSelection
    {
        std::array<int, 4> equipped{};
        WardrobeColorMode color_mode{WardrobeColorMode::Default};
        std::array<int, 3> top{255, 255, 255};
        std::array<int, 3> bottom{};

        bool operator==(const CosmeticSelection&) const = default;
    };

    // A value snapshot contains no UObject or view into the game-thread bank.
    // Event windows are transported intact for observations, but are never
    // restored into a new encounter when loading a native save.
    struct Snapshot
    {
        Flags flags;
        ChallengeValues challenges;
        ChallengeValues counts;
        EncounterEvents events;
        std::optional<CosmeticSelection> cosmetics;

        bool operator==(const Snapshot&) const = default;
    };

    using SnapshotPtr = std::shared_ptr<const Snapshot>;

    struct Binding
    {
        std::string seed;
        int64_t player_slot{};
        uint64_t revision{};
        int64_t epoch{};

        bool operator==(const Binding&) const = default;
    };

    struct Update
    {
        Binding binding;
        SnapshotPtr snapshot;
    };

    // Used at the worker boundary for both updates and chapter resets. Checking
    // every part prevents a queued pre-load/old-player value replacing a bank
    // whose revision or native save session has changed in the meantime.
    inline auto accepts(const Binding& update, std::string_view seed, int64_t player_slot, uint64_t revision,
                        int64_t epoch, bool loaded, bool dead) -> bool
    {
        return loaded && !dead && update.seed == seed && update.player_slot == player_slot &&
               update.revision == revision && update.epoch == epoch;
    }

    inline auto flag_value(const Flags& flags, std::string_view name, int fallback = 0) -> int
    {
        const auto found = flags.find(name);
        return found == flags.end() ? fallback : found->second;
    }
} // namespace kh3ap::kh02_progress
