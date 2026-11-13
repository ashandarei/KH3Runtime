#pragma once

#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/core/settings/color_randomization_mode.h"
#include "kh3ap/core/settings/seed_slot_options.h"
#include "kh3ap/features/enemies/experience_multiplier_math.h"

namespace kh3ap
{
    inline constexpr int32_t max_seed_music_shuffle_mode = 3;

    struct SeedMusicOptions
    {
        int32_t shuffle_mode{};
        bool include_custom{true};
        bool form_themes{};
    };

    [[nodiscard]] inline auto seed_music_options(const JsonObject& slot) -> SeedMusicOptions
    {
        return {seed_choice_option(slot, "music_shuffle", max_seed_music_shuffle_mode),
                seed_bool_option(slot, "include_custom_music", true),
                seed_bool_option(slot, "form_themes")};
    }
    // runtime_seed_settings.json keeps generation options inside slot_data.
    // Read explicit object paths: unrelated nested keys must never win.
    [[nodiscard]] inline auto seed_color_randomization_default(const JsonObject& metadata)
        -> ColorRandomizationMode
    {
        const auto mode =
            metadata.object_field("slot_data")
                .int64_field("color_randomization")
                .value_or(color_randomization_mode_id(ColorRandomizationMode::Coordinated));
        return color_randomization_mode_from_id(mode).value_or(ColorRandomizationMode::Coordinated);
    }

    [[nodiscard]] inline auto seed_battle_level_sync_default(const JsonObject& metadata) -> bool
    {
        const auto slot = metadata.object_field("slot_data");
        return slot.object_field("kh3_randomizer")
            .bool_field("battle_level_sync")
            .value_or(slot.int64_field("battle_level_mode").value_or(1) != 0);
    }

    [[nodiscard]] inline auto seed_experience_multiplier_default(const JsonObject& metadata)
        -> int32_t
    {
        const auto multiplier = metadata.object_field("slot_data")
                                    .object_field("kh3_randomizer")
                                    .number_field("exp_multiplier");
        return multiplier.has_value() ? experience_multiplier_half_steps_from_value(*multiplier)
                                            .value_or(experience_half_steps_per_unit)
                                      : experience_half_steps_per_unit;
    }
} // namespace kh3ap
