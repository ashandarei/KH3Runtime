#include <optional>
#include "kh3ap/core/settings/pak_seed_settings.h"
#include "kh3ap/features/music/events/music_update_events.h"
#include "kh3ap/core/settings/seed_ui_options.h"
#include "kh3ap/core/settings/seed_gameplay_options.h"
#include "kh3ap/core/settings/goa_settings_service.h"

#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/features/grants/battle_level/battle_level_progress.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/features/enemies/experience_multiplier_math.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string_view>
#include <utility>

namespace kh3ap
{
    using namespace RC;

    namespace
    {
        constexpr auto first_party_friend_slot = 1;
        constexpr auto second_party_friend_slot = 2;
        constexpr auto randomized_friend_1_choice = 1;
        constexpr auto randomized_friend_2_choice = 2;

        constexpr std::wstring_view runtime_settings_filename = STR("goa_runtime_settings.json");
        constexpr std::string_view runtime_settings_protocol = "kh3_ap_goa_runtime_settings";
        constexpr int64_t legacy_runtime_settings_protocol_version = 1;
        constexpr int64_t battle_level_runtime_settings_protocol_version = 2;
        constexpr int64_t experience_runtime_settings_protocol_version = 3;
        constexpr int64_t party_runtime_settings_protocol_version = 4;
        constexpr int64_t party_reroll_runtime_settings_protocol_version = 5;
        constexpr int64_t runtime_settings_protocol_version = 6;
        constexpr int32_t vanilla_party_member_choice = 26;
        constexpr int32_t max_party_member_choice = 27;
        constexpr int32_t legacy_max_party_member_choice = 26;
        constexpr int32_t retired_marshmallow_choice = 10;

        struct PersistedRuntimeSettings
        {
            bool enemy_randomization_enabled{true};
            bool boss_randomization_enabled{true};
            bool battle_level_sync_enabled{true};
            int32_t battle_level_offset{};
            int32_t experience_multiplier_half_steps{experience_half_steps_per_unit};
            bool party_randomization_enabled{};
            bool party_reroll_every_room{};
            // These defaults use the YAML-derived initial toggle. Persisted
            // manual choices, when present, are read over them below.
            int32_t party_friend_1_choice{party_randomization_enabled
                                              ? randomized_friend_1_choice
                                              : vanilla_party_member_choice};
            int32_t party_friend_2_choice{party_randomization_enabled
                                              ? randomized_friend_2_choice
                                              : vanilla_party_member_choice};
            std::optional<bool> legacy_controller_focus_enabled{};
            int32_t color_randomization_mode{seed_color_mode_coordinated};
            int32_t music_shuffle_mode{};
            bool include_custom_music{true};
            bool form_themes_enabled{};
            bool map_overlay_enabled{true};
            bool song_display{true};
            bool auto_equip_movement_abilities{true};
            uint32_t map_icon_options{map_icon_defaults};
            bool loaded{};
            bool needs_migration{};
        };

        [[nodiscard]] auto seed_enemy_randomization_default(const std::string& metadata) -> bool
        {
            return JsonFieldReader::bool_field(metadata, "enemy_randomization_initially_enabled")
                .value_or(true);
        }

        [[nodiscard]] auto seed_boss_randomization_default(const std::string& metadata) -> bool
        {
            return JsonFieldReader::bool_field(metadata, "boss_randomization_initially_enabled")
                .value_or(true);
        }

        [[nodiscard]] auto seed_party_randomization_default(const std::string& metadata) -> bool
        {
            return JsonFieldReader::bool_field(metadata, "party_randomization_initially_enabled")
                .value_or(false);
        }

        [[nodiscard]] auto seed_party_reroll_default(const std::string& metadata) -> bool
        {
            return JsonFieldReader::bool_field(metadata, "party_randomize_on_room_change")
                .value_or(false);
        }

        auto read_persisted_runtime_settings(
            const std::optional<std::filesystem::path>& bridge_dir, std::wstring_view seed_identity,
            std::wstring_view slot_name, bool seed_enemy_randomization_enabled,
            bool seed_boss_randomization_enabled, bool seed_battle_level_sync_enabled,
            int32_t seed_experience_multiplier_half_steps, bool seed_party_randomization_enabled,
            bool seed_party_reroll_every_room, int32_t seed_color_randomization_mode,
            int32_t seed_music_shuffle_mode, bool seed_include_custom_music,
            bool seed_form_themes_enabled, const RuntimeUiSeedDefaults& seed_ui)
            -> PersistedRuntimeSettings
        {
            PersistedRuntimeSettings result{
                .enemy_randomization_enabled = seed_enemy_randomization_enabled,
                .boss_randomization_enabled = seed_boss_randomization_enabled,
                .battle_level_sync_enabled = seed_battle_level_sync_enabled,
                .experience_multiplier_half_steps = seed_experience_multiplier_half_steps,
                .party_randomization_enabled = seed_party_randomization_enabled,
                .party_reroll_every_room = seed_party_reroll_every_room,
                .color_randomization_mode = seed_color_randomization_mode,
                .music_shuffle_mode = seed_music_shuffle_mode,
                .include_custom_music = seed_include_custom_music,
                .form_themes_enabled = seed_form_themes_enabled,
            };
            result.battle_level_offset = seed_ui.battle_level_offset;
            result.map_overlay_enabled = seed_ui.map_overlay_enabled;
            result.song_display = seed_ui.song_display;
            result.auto_equip_movement_abilities = seed_ui.auto_equip_movement_abilities;
            result.map_icon_options = seed_ui.map_icon_options;
            const auto defaults = result;
            if (!bridge_dir)
            {
                return result;
            }
            const auto settings_path =
                *bridge_dir / std::filesystem::path{runtime_settings_filename};
            const auto text = read_text_file(settings_path);
            if (!text.has_value())
            {
                return result;
            }

            const auto protocol = JsonFieldReader::string_field(*text, "protocol_name");
            const auto protocol_version = JsonFieldReader::int64_field(*text, "protocol_version");
            const auto stored_seed = JsonFieldReader::string_field(*text, "seed_identity");
            const auto stored_slot = JsonFieldReader::string_field(*text, "slot_name");
            const auto enemy_enabled =
                JsonFieldReader::bool_field(*text, "enemy_randomization_enabled");
            const auto boss_enabled =
                JsonFieldReader::bool_field(*text, "boss_randomization_enabled");
            const bool supported_version =
                protocol_version.has_value() &&
                (*protocol_version == legacy_runtime_settings_protocol_version ||
                 *protocol_version == battle_level_runtime_settings_protocol_version ||
                 *protocol_version == experience_runtime_settings_protocol_version ||
                 *protocol_version == party_runtime_settings_protocol_version ||
                 *protocol_version == party_reroll_runtime_settings_protocol_version ||
                 *protocol_version == runtime_settings_protocol_version);
            if (!protocol.has_value() || to_utf8(*protocol) != runtime_settings_protocol ||
                !supported_version || !stored_seed.has_value() || *stored_seed != seed_identity ||
                !stored_slot.has_value() || *stored_slot != slot_name ||
                !enemy_enabled.has_value() || !boss_enabled.has_value())
            {
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] GoA runtime settings ignored "
                                                    "reason=identity_or_schema_mismatch path={}\n"),
                                                settings_path.wstring());
                return result;
            }

            result.enemy_randomization_enabled = *enemy_enabled;
            result.boss_randomization_enabled = *boss_enabled;
            result.loaded = true;
            const auto saved_ui = apply_saved_map_ui_options(JsonObject{*text}, seed_ui);
            result.map_overlay_enabled = saved_ui.map_overlay_enabled;
            result.song_display = saved_ui.song_display;
            result.auto_equip_movement_abilities = saved_ui.auto_equip_movement_abilities;
            result.map_icon_options = saved_ui.map_icon_options;
            // Read-only compatibility with the retired seed-scoped preference.
            result.legacy_controller_focus_enabled =
                JsonFieldReader::bool_field(*text, "controller_focus_enabled");
            if (*protocol_version == legacy_runtime_settings_protocol_version)
            {
                result.needs_migration = true;
                return result;
            }

            const auto battle_sync_enabled =
                JsonFieldReader::bool_field(*text, "battle_level_sync_enabled");
            const auto battle_level_offset =
                JsonFieldReader::int64_field(*text, "battle_level_offset");
            if (!battle_sync_enabled.has_value() || !battle_level_offset.has_value())
            {
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] GoA runtime settings ignored "
                                                    "reason=missing_battle_level_fields path={}\n"),
                                                settings_path.wstring());
                return defaults;
            }
            result.battle_level_sync_enabled = *battle_sync_enabled;
            result.battle_level_offset = clamp_battle_level_offset(static_cast<int32_t>(
                std::clamp<int64_t>(*battle_level_offset, std::numeric_limits<int32_t>::min(),
                                    std::numeric_limits<int32_t>::max())));
            if (*protocol_version == battle_level_runtime_settings_protocol_version)
            {
                result.needs_migration = true;
                return result;
            }

            const auto experience_multiplier_half_steps =
                JsonFieldReader::int64_field(*text, "experience_multiplier_half_steps");
            if (!experience_multiplier_half_steps.has_value() ||
                *experience_multiplier_half_steps < std::numeric_limits<int32_t>::min() ||
                *experience_multiplier_half_steps > std::numeric_limits<int32_t>::max() ||
                !is_supported_experience_multiplier_half_steps(
                    static_cast<int32_t>(*experience_multiplier_half_steps)))
            {
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] GoA runtime settings ignored "
                                                    "reason=missing_or_invalid_exp_multiplier "
                                                    "path={}\n"),
                                                settings_path.wstring());
                return defaults;
            }
            result.experience_multiplier_half_steps =
                static_cast<int32_t>(*experience_multiplier_half_steps);
            if (*protocol_version == experience_runtime_settings_protocol_version)
            {
                result.needs_migration = true;
                return result;
            }

            const auto party_randomization_enabled =
                JsonFieldReader::bool_field(*text, "party_randomization_enabled");
            const auto party_friend_1_choice =
                JsonFieldReader::int64_field(*text, "party_friend_1_choice");
            // Absent in any settings file written before this option
            // existed, so a missing key keeps the seed default rather
            // than failing the whole read.
            if (const auto color_mode =
                    JsonFieldReader::int64_field(*text, "color_randomization_mode");
                color_mode.has_value() && *color_mode >= seed_color_mode_off &&
                *color_mode <= seed_color_mode_wild)
            {
                result.color_randomization_mode = static_cast<int32_t>(*color_mode);
            }
            if (const auto music_mode = JsonFieldReader::int64_field(*text, "music_shuffle_mode");
                music_mode.has_value() && *music_mode >= 0 &&
                *music_mode <= max_seed_music_shuffle_mode)
            {
                result.music_shuffle_mode = static_cast<int32_t>(*music_mode);
            }
            if (const auto include_custom =
                    JsonFieldReader::bool_field(*text, "include_custom_music"))
            {
                result.include_custom_music = *include_custom;
            }
            if (const auto form_themes = JsonFieldReader::bool_field(*text, "form_themes_enabled"))
            {
                result.form_themes_enabled = *form_themes;
            }
            const auto party_friend_2_choice =
                JsonFieldReader::int64_field(*text, "party_friend_2_choice");
            const int32_t persisted_max_party_member_choice =
                *protocol_version == runtime_settings_protocol_version
                    ? max_party_member_choice
                    : legacy_max_party_member_choice;
            const auto valid_party_choice =
                [persisted_max_party_member_choice](const int64_t choice)
            {
                return choice >= 0 && choice <= persisted_max_party_member_choice;
            };
            if (!party_randomization_enabled.has_value() || !party_friend_1_choice.has_value() ||
                !party_friend_2_choice.has_value() || !valid_party_choice(*party_friend_1_choice) ||
                !valid_party_choice(*party_friend_2_choice))
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] GoA runtime settings ignored "
                        "reason=missing_or_invalid_party_fields path={}\n"),
                    settings_path.wstring());
                return defaults;
            }
            result.party_randomization_enabled = *party_randomization_enabled;
            result.party_friend_1_choice = static_cast<int32_t>(*party_friend_1_choice);
            result.party_friend_2_choice = static_cast<int32_t>(*party_friend_2_choice);
            if (*protocol_version < runtime_settings_protocol_version)
            {
                const auto migrate_party_choice = [](const int32_t choice)
                {
                    if (choice == retired_marshmallow_choice)
                    {
                        return 0;
                    }
                    return choice > retired_marshmallow_choice ? choice - 1 : choice;
                };
                result.party_friend_1_choice = migrate_party_choice(result.party_friend_1_choice);
                result.party_friend_2_choice = migrate_party_choice(result.party_friend_2_choice);
                result.needs_migration = true;
            }
            if (*protocol_version == party_runtime_settings_protocol_version)
            {
                return result;
            }

            const auto party_reroll_every_room =
                JsonFieldReader::bool_field(*text, "party_reroll_every_room");
            if (!party_reroll_every_room.has_value())
            {
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] GoA runtime settings ignored "
                                                    "reason=missing_party_reroll_field path={}\n"),
                                                settings_path.wstring());
                return defaults;
            }
            result.party_reroll_every_room = *party_reroll_every_room;
            return result;
        }
    } // namespace

    RuntimeGoaSettingsService::RuntimeGoaSettingsService(RuntimeGoaSettingsConfig config)
        : config_(std::move(config))
    {
    }

    auto RuntimeGoaSettingsService::install_once() -> void
    {
        if (installed_)
        {
            return;
        }
        const auto& metadata = installed_pak_seed_settings();
        if (!metadata)
        {
            return;
        }
        installed_ = true;
        const auto bridge_dir =
            config_.bridge_dir_resolver ? config_.bridge_dir_resolver() : std::nullopt;

        bridge_dir_ = bridge_dir;
        const JsonObject seed_document{*metadata};
        seed_identity_ = JsonFieldReader::string_field(*metadata, "seed").value();
        slot_name_ =
            JsonFieldReader::string_field(*metadata, "slot_name").value_or(STR("<unknown>"));
        const bool seed_enemy_enabled = seed_enemy_randomization_default(*metadata);
        const bool seed_boss_enabled = seed_boss_randomization_default(*metadata);
        const bool seed_sync_enabled = seed_battle_level_sync_default(seed_document);
        const int32_t seed_experience_multiplier_half_steps =
            seed_experience_multiplier_default(seed_document);
        const bool seed_party_enabled = seed_party_randomization_default(*metadata);
        const bool seed_party_reroll_every_room = seed_party_reroll_default(*metadata);
        const int32_t seed_color_randomization_mode =
            seed_color_randomization_default(seed_document);
        const auto music = seed_music_options(seed_document.object_field("slot_data"));
        const int32_t seed_music_shuffle_mode = music.shuffle_mode;
        const bool seed_include_custom_music = music.include_custom;
        const bool seed_form_themes_enabled = music.form_themes;
        const PersistedRuntimeSettings persisted = read_persisted_runtime_settings(
            bridge_dir, seed_identity_, slot_name_, seed_enemy_enabled, seed_boss_enabled,
            seed_sync_enabled, seed_experience_multiplier_half_steps, seed_party_enabled,
            seed_party_reroll_every_room, seed_color_randomization_mode, seed_music_shuffle_mode,
            seed_include_custom_music, seed_form_themes_enabled,
            read_runtime_ui_seed_defaults(seed_document.object_field("slot_data")));
        enemy_randomization_enabled_.store(persisted.enemy_randomization_enabled);
        boss_randomization_enabled_.store(persisted.boss_randomization_enabled);
        battle_level_sync_enabled_.store(persisted.battle_level_sync_enabled);
        battle_level_offset_.store(persisted.battle_level_offset);
        experience_multiplier_half_steps_.store(persisted.experience_multiplier_half_steps);
        seed_experience_multiplier_half_steps_.store(seed_experience_multiplier_half_steps);
        party_randomization_enabled_.store(persisted.party_randomization_enabled);
        party_reroll_every_room_.store(persisted.party_reroll_every_room);
        party_friend_1_choice_.store(persisted.party_friend_1_choice);
        party_friend_2_choice_.store(persisted.party_friend_2_choice);
        legacy_controller_focus_enabled_ = persisted.legacy_controller_focus_enabled;
        color_randomization_mode_.store(persisted.color_randomization_mode);
        music_shuffle_mode_.store(persisted.music_shuffle_mode);
        include_custom_music_.store(persisted.include_custom_music);
        form_themes_enabled_.store(persisted.form_themes_enabled);
        map_overlay_enabled_.store(persisted.map_overlay_enabled);
        song_display_.store(persisted.song_display);
        auto_equip_movement_abilities_.store(persisted.auto_equip_movement_abilities);
        map_icon_options_.store(persisted.map_icon_options);
        loaded_from_disk_.store(persisted.loaded);
        request_music_update(MusicUpdate::Settings);

        const bool migrated = persisted.needs_migration && persist_settings();
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] GoA runtime settings initialized "
                "loaded={} migrated={} enemy={} boss={} "
                "battle_level_sync={} battle_level_offset={} "
                "exp_multiplier_half_steps={} "
                "seed_enemy_randomization={} "
                "seed_boss_randomization={} "
                "seed_battle_level_sync={} "
                "seed_exp_multiplier_half_steps={} "
                "party_randomization={} party_friend_1={} "
                "party_friend_2={} party_reroll_every_room={} "
                "seed_party_randomization={} "
                "seed_party_reroll_every_room={} "
                "persistence_ready={}\n"),
            persisted.loaded, migrated, persisted.enemy_randomization_enabled,
            persisted.boss_randomization_enabled, persisted.battle_level_sync_enabled,
            persisted.battle_level_offset, persisted.experience_multiplier_half_steps,
            seed_enemy_enabled, seed_boss_enabled, seed_sync_enabled,
            seed_experience_multiplier_half_steps, persisted.party_randomization_enabled,
            persisted.party_friend_1_choice, persisted.party_friend_2_choice,
            persisted.party_reroll_every_room, seed_party_enabled, seed_party_reroll_every_room,
            true);
    }

    auto RuntimeGoaSettingsService::seed_identity() const -> std::wstring
    {
        return seed_identity_;
    }

    auto RuntimeGoaSettingsService::status() const -> RuntimeGoaSettingsStatus
    {
        return RuntimeGoaSettingsStatus{
            .enemy_randomization_enabled = enemy_randomization_enabled_.load(),
            .boss_randomization_enabled = boss_randomization_enabled_.load(),
            .battle_level_sync_enabled = battle_level_sync_enabled_.load(),
            .battle_level_offset = battle_level_offset_.load(),
            .experience_multiplier_half_steps = experience_multiplier_half_steps_.load(),
            .seed_experience_multiplier_half_steps = seed_experience_multiplier_half_steps_.load(),
            .party_randomization_enabled = party_randomization_enabled_.load(),
            .party_reroll_every_room = party_reroll_every_room_.load(),
            .party_friend_1_choice = party_friend_1_choice_.load(),
            .party_friend_2_choice = party_friend_2_choice_.load(),
            .legacy_controller_focus_enabled = legacy_controller_focus_enabled_,
            .color_randomization_mode = color_randomization_mode_.load(),
            .music_shuffle_mode = music_shuffle_mode_.load(),
            .include_custom_music = include_custom_music_.load(),
            .form_themes_enabled = form_themes_enabled_.load(),
            .map_overlay_enabled = map_overlay_enabled(),
            .song_display = song_display(),
            .auto_equip_movement_abilities = auto_equip_movement_abilities(),
            .map_icon_options = map_icon_options(),
            .loaded_from_disk = loaded_from_disk_.load(),
            .persistence_ready =
                bridge_dir_.has_value() && !seed_identity_.empty() && !slot_name_.empty(),
        };
    }

    auto RuntimeGoaSettingsService::set_enemy_randomization_enabled(bool enabled) -> bool
    {
        enemy_randomization_enabled_.store(enabled);
        return persist_settings();
    }

    auto RuntimeGoaSettingsService::set_experience_multiplier_half_steps(int32_t half_steps) -> bool
    {
        if (!is_supported_experience_multiplier_half_steps(half_steps))
        {
            return false;
        }
        experience_multiplier_half_steps_.store(half_steps);
        return persist_settings();
    }

    auto RuntimeGoaSettingsService::set_boss_randomization_enabled(bool enabled) -> bool
    {
        boss_randomization_enabled_.store(enabled);
        return persist_settings();
    }

    auto RuntimeGoaSettingsService::set_battle_level_sync_enabled(bool enabled) -> bool
    {
        battle_level_sync_enabled_.store(enabled);
        return persist_settings();
    }

    auto RuntimeGoaSettingsService::set_battle_level_offset(int32_t offset) -> bool
    {
        battle_level_offset_.store(clamp_battle_level_offset(offset));
        return persist_settings();
    }

    auto RuntimeGoaSettingsService::set_party_randomization_enabled(bool enabled) -> bool
    {
        party_randomization_enabled_.store(enabled);
        return persist_settings();
    }

    auto RuntimeGoaSettingsService::set_color_randomization_mode(int32_t mode) -> bool
    {
        // Clamped rather than rejected: an out-of-range value can only
        // come from a hand-edited settings file, and falling back to the
        // default beats refusing to save anything.
        color_randomization_mode_.store(mode < seed_color_mode_off || mode > seed_color_mode_wild
                                            ? seed_color_mode_coordinated
                                            : mode);
        return persist_settings();
    }

    auto RuntimeGoaSettingsService::set_music_shuffle_mode(int32_t mode) -> bool
    {
        if (mode < 0 || mode > max_seed_music_shuffle_mode)
        {
            return false;
        }
        if (music_shuffle_mode_.exchange(mode) != mode)
        {
            request_music_update(MusicUpdate::Settings);
        }
        return persist_settings();
    }

    auto RuntimeGoaSettingsService::set_include_custom_music(bool enabled) -> bool
    {
        if (include_custom_music_.exchange(enabled) != enabled)
        {
            request_music_update(MusicUpdate::Settings);
        }
        return persist_settings();
    }

    auto RuntimeGoaSettingsService::set_form_themes_enabled(bool enabled) -> bool
    {
        if (form_themes_enabled_.exchange(enabled) != enabled)
        {
            request_music_update(MusicUpdate::Settings);
        }
        return persist_settings();
    }

    auto RuntimeGoaSettingsService::set_map_overlay_enabled(bool enabled) -> bool
    {
        map_overlay_enabled_.store(enabled);
        return persist_settings();
    }

    auto RuntimeGoaSettingsService::set_auto_equip_movement_abilities(bool enabled) -> bool
    {
        auto_equip_movement_abilities_.store(enabled);
        return persist_settings();
    }

    auto RuntimeGoaSettingsService::set_song_display(bool enabled) -> bool
    {
        song_display_.store(enabled);
        return persist_settings();
    }

    auto RuntimeGoaSettingsService::set_party_reroll_every_room(bool enabled) -> bool
    {
        party_reroll_every_room_.store(enabled);
        return persist_settings();
    }

    auto RuntimeGoaSettingsService::set_party_friend_choice(int32_t friend_slot, int32_t choice)
        -> bool
    {
        if ((friend_slot != first_party_friend_slot && friend_slot != second_party_friend_slot) ||
            choice < 0 || choice > max_party_member_choice)
        {
            return false;
        }
        if (friend_slot == first_party_friend_slot)
        {
            party_friend_1_choice_.store(choice);
        }
        else
        {
            party_friend_2_choice_.store(choice);
        }
        return persist_settings();
    }

    auto RuntimeGoaSettingsService::set_map_icon_option(MapIconOption option, bool enabled) -> bool
    {
        const auto bit = static_cast<uint32_t>(option);
        if (!bit || (bit & (bit - 1)) || (bit & ~map_icon_defaults))
        {
            return false;
        }
        if (enabled)
        {
            map_icon_options_.fetch_or(bit);
        }
        else
        {
            map_icon_options_.fetch_and(~bit);
        }
        return persist_settings();
    }

    auto RuntimeGoaSettingsService::persist_settings() const -> bool
    {
        if (!bridge_dir_.has_value() || seed_identity_.empty() || slot_name_.empty())
        {
            return false;
        }

        const RuntimeGoaSettingsStatus snapshot = status();
        std::ostringstream json{};
        json << "{\n"
             << "  \"protocol_name\": \"" << runtime_settings_protocol << "\",\n"
             << "  \"protocol_version\": " << runtime_settings_protocol_version << ",\n"
             << "  \"seed_identity\": \"" << json_escape(seed_identity_) << "\",\n"
             << "  \"slot_name\": \"" << json_escape(slot_name_) << "\",\n"
             << "  \"enemy_randomization_enabled\": "
             << (snapshot.enemy_randomization_enabled ? "true" : "false") << ",\n"
             << "  \"boss_randomization_enabled\": "
             << (snapshot.boss_randomization_enabled ? "true" : "false") << ",\n"
             << "  \"battle_level_sync_enabled\": "
             << (snapshot.battle_level_sync_enabled ? "true" : "false") << ",\n"
             << "  \"battle_level_offset\": " << snapshot.battle_level_offset << ",\n"
             << "  \"experience_multiplier_half_steps\": "
             << snapshot.experience_multiplier_half_steps << ",\n"
             << "  \"party_randomization_enabled\": "
             << (snapshot.party_randomization_enabled ? "true" : "false") << ",\n"
             << "  \"color_randomization_mode\": " << snapshot.color_randomization_mode << ",\n"
             << "  \"music_shuffle_mode\": " << snapshot.music_shuffle_mode << ",\n"
             << "  \"include_custom_music\": " << (snapshot.include_custom_music ? "true" : "false")
             << ",\n"
             << "  \"form_themes_enabled\": " << (snapshot.form_themes_enabled ? "true" : "false")
             << ",\n"
             << "  \"map_overlay_enabled\": " << (snapshot.map_overlay_enabled ? "true" : "false")
             << ",\n"
             << "  \"song_display\": " << (snapshot.song_display ? "true" : "false") << ",\n"
             << "  \"auto_equip_movement_abilities\": "
             << (snapshot.auto_equip_movement_abilities ? "true" : "false") << ",\n"
             << "  \"map_chest_icons\": "
             << (map_icon_enabled(snapshot.map_icon_options, MapIconOption::Chests) ? "true"
                                                                                    : "false")
             << ",\n"
             << "  \"map_locked_chest_icons\": "
             << (map_icon_enabled(snapshot.map_icon_options, MapIconOption::LockedChests) ? "true"
                                                                                          : "false")
             << ",\n"
             << "  \"map_emblem_icons\": "
             << (map_icon_enabled(snapshot.map_icon_options, MapIconOption::Emblems) ? "true"
                                                                                     : "false")
             << ",\n"
             << "  \"map_ingredient_icons\": "
             << (map_icon_enabled(snapshot.map_icon_options, MapIconOption::Ingredients) ? "true"
                                                                                         : "false")
             << ",\n"
             << "  \"minimap_icons\": "
             << (map_icon_enabled(snapshot.map_icon_options, MapIconOption::Minimap) ? "true"
                                                                                     : "false")
             << ",\n"
             << "  \"large_map_icons\": "
             << (map_icon_enabled(snapshot.map_icon_options, MapIconOption::LargeMap) ? "true"
                                                                                      : "false")
             << ",\n"
             << "  \"party_reroll_every_room\": "
             << (snapshot.party_reroll_every_room ? "true" : "false") << ",\n"
             << "  \"party_friend_1_choice\": " << snapshot.party_friend_1_choice << ",\n"
             << "  \"party_friend_2_choice\": " << snapshot.party_friend_2_choice << ",\n"
             << "  \"updated_at\": \"" << now_iso8601_utc() << "\"\n"
             << "}\n";
        return write_text_file_atomic_verified(
            *bridge_dir_ / std::filesystem::path{runtime_settings_filename}, json.str());
    }
} // namespace kh3ap
