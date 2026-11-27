#pragma once
#include "kh3ap/bridge/checks/chest_snapshot.h"
#include "kh3ap/features/treasure/chest_character_policy.h"
#include <vector>
#include <string_view>

namespace kh3ap
{
    inline constexpr double map_chest_unlocked_alpha = 1.0;
    inline constexpr double map_chest_locked_alpha = 0.70;
    inline constexpr double map_chest_hidden_alpha = 0.0;
    inline constexpr double map_chest_legend_base_height = 66.0;
    inline constexpr double map_chest_legend_row_height = 28.0;

    inline bool map_chest_palette_matches(const chest_manifest::Snapshot* palette, uint32_t process,
                                          std::wstring_view seed, int64_t slot)
    {
        return palette && palette->process_id == process && palette->seed == seed &&
               palette->slot == slot;
    }

    // Melody and ordinary chests have no assignment and retain their usual icons.
    template <class Owns>
    bool map_chest_is_locked(const chest_manifest::Snapshot* palette, const std::wstring& treasure,
                             Owns owns, int player_id = -1)
    {
        if (!palette)
        {
            return false;
        }
        const auto assigned = palette->map_keyblades.find(treasure);
        return assigned != palette->map_keyblades.end() &&
               !roxas_chest_key_allowed(palette->roxas_chestsanity, player_id, assigned->second.name) &&
               !owns(assigned->second.token);
    }

    template <class Owns>
    double map_chest_alpha(const chest_manifest::Snapshot* palette, const std::wstring& treasure,
                           bool show_locked_chests, Owns owns, int player_id = -1)
    {
        if (!map_chest_is_locked(palette, treasure, owns, player_id))
        {
            return map_chest_unlocked_alpha;
        }
        return show_locked_chests ? map_chest_locked_alpha : map_chest_hidden_alpha;
    }

    template <class Names>
    std::vector<size_t> map_chest_legend_rows(const chest_manifest::Snapshot* palette,
                                              const Names& names)
    {
        std::vector<size_t> rows;
        if (palette)
        {
            for (size_t i = 0; i < names.size(); ++i)
            {
                if (palette->eligible_keyblades.contains(std::wstring(names[i])))
                {
                    rows.push_back(i);
                }
            }
        }
        return rows;
    }

    inline double map_chest_legend_height(size_t rows)
    {
        return map_chest_legend_base_height + map_chest_legend_row_height * rows;
    }

    template <class Names, class Owns>
    std::vector<size_t> map_chest_owned_legend_rows(const chest_manifest::Snapshot* palette,
                                                    const Names& names, Owns owns)
    {
        std::vector<size_t> rows;
        if (palette)
        {
            for (size_t i = 0; i < names.size(); ++i)
            {
                const auto blade = palette->eligible_keyblades.find(std::wstring(names[i]));
                if (blade != palette->eligible_keyblades.end() && owns(blade->second))
                {
                    rows.push_back(i);
                }
            }
        }
        return rows;
    }
} // namespace kh3ap
