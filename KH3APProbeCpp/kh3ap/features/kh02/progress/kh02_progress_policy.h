#pragma once
#include "kh02_progress_codec.h"
#include "kh02_checkpoint_rules.h"
#include <limits>

namespace kh3ap::kh02_progress
{
    namespace checkpoint_json_detail
    {
        // Projection preserves every key without reading unrelated JSON values.
        // Only assignments made by the typed operation are written back.
        inline constexpr int untouched = std::numeric_limits<int>::min();

        inline auto keys(const Json& flags) -> Flags
        {
            Flags projected;
            for (auto entry = flags.begin(); entry != flags.end(); ++entry)
            {
                projected.emplace(entry.key(), untouched);
            }
            return projected;
        }

        inline auto apply_assignments(Json flags, const Flags& result) -> Json
        {
            for (const auto& [name, value] : result)
            {
                if (value != untouched)
                {
                    flags[name] = value;
                }
            }
            return flags;
        }
    } // namespace checkpoint_json_detail

    inline auto hub_entry_flags(Json flags) -> Json
    {
        if (!flags.is_object())
        {
            throw std::runtime_error("kh02_hub_entry_flags");
        }
        return checkpoint_json_detail::apply_assignments(flags, hub_entry_flags(checkpoint_json_detail::keys(flags)));
    }

    inline auto mirror_room_entry_flags(Json flags, int area) -> Json
    {
        if (!flags.is_object())
        {
            throw std::runtime_error("kh02_mirror_room_entry");
        }
        return checkpoint_json_detail::apply_assignments(
            flags, mirror_room_entry_flags(checkpoint_json_detail::keys(flags), area));
    }

    inline auto resume_castle_entry(const Json& flags) -> bool
    {
        return flags.is_object() &&
               resume_castle_entry(Flags{{"gameflow_DW_VISITED_2", flags.value("gameflow_DW_VISITED_2", 0)},
                                        {"gameflow_DW", flags.value("gameflow_DW", 0)}});
    }

    inline auto enter_flags(Json flags, int checkpoint, bool backtrack = false) -> Json
    {
        if (backtrack)
        {
            if (!flags.is_object())
            {
                throw std::runtime_error("kh02_backtrack_checkpoint");
            }
            (void)enter_flags(Flags{}, checkpoint, true);
            return flags;
        }
        // Preserve .value()'s native JSON validation and only decode the saved
        // mirror checkpoint when the typed rule can actually consume it.
        const int flow = flags.value("gameflow_DW", 0);
        auto projected = checkpoint_json_detail::keys(flags);
        projected["gameflow_DW"] = flow;
        constexpr const char* memory = "gameflow_DW_CHALLENGE_MIRROR_CHECKPOINT";
        if (checkpoint >= kh02_checkpoint::mirror_scene_dw015 && flow < kh02_checkpoint::mirror_scene_dw015)
        {
            projected[memory] = flags.value(memory, checkpoint);
        }
        const auto change = enter_flags_change(std::move(projected), checkpoint);
        for (auto entry = flags.begin(); entry != flags.end();)
        {
            if (!change.values.contains(entry.key()))
            {
                entry = flags.erase(entry);
            }
            else
            {
                ++entry;
            }
        }
        for (const auto& name : change.assigned)
        {
            flags[name] = change.values.at(name);
        }
        return flags;
    }

    inline auto white_gem_count(const Json& flags) -> int
    {
        Flags gems;
        for (auto entry = flags.begin(); entry != flags.end(); ++entry)
        {
            if (white_gem_flag(entry.key()))
            {
                gems.emplace(entry.key(), integer(entry.value(), 0, 1));
            }
        }
        return white_gem_count(gems);
    }

    // Single worker-owned authority. Save-open/autosave-begin captures retain the
    // current immutable pointer; later progress replaces active, never that saved
    // value. The existing save worker still owns durable commit and rollback.
    struct Bank
    {
        SnapshotPtr active;
        std::map<int64_t, SnapshotPtr> slots;
        uint64_t revision{};

        void load(int64_t slot)
        {
            active = slots.contains(slot) ? slots.at(slot) : SnapshotPtr{};
            ++revision;
        }

        void new_game()
        {
            active.reset();
            ++revision;
        }

        auto update(SnapshotPtr value, uint64_t expected_revision) -> bool
        {
            if (expected_revision != revision)
            {
                return false;
            }
            if (value)
            {
                validate(*value);
            }
            active = std::move(value);
            return true;
        }
    };
} // namespace kh3ap::kh02_progress
