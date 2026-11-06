#pragma once
#include "kh3ap/features/ui/map_overlay/map_icon_options.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace kh3ap
{
    struct RuntimeGoaSettingsConfig
    {
        std::function<std::optional<std::filesystem::path>()>
                bridge_dir_resolver{};
    };

    struct RuntimeGoaSettingsStatus
    {
        bool enemy_randomization_enabled{true};
        bool boss_randomization_enabled{true};
        bool battle_level_sync_enabled{true};
        int32_t battle_level_offset{};
        int32_t experience_multiplier_half_steps{2};
        int32_t seed_experience_multiplier_half_steps{2};
        bool party_randomization_enabled{};
        bool party_reroll_every_room{};
        int32_t party_friend_1_choice{1};
        int32_t party_friend_2_choice{2};
        std::optional<bool> legacy_controller_focus_enabled{};
        // 0 off, 1 coordinated, 2 wild.
        int32_t color_randomization_mode{1};
        // 0 vanilla, 1 shuffle worlds, 2 shuffle battle themes, 3 chaos.
        int32_t music_shuffle_mode{};
        bool include_custom_music{true};
        bool form_themes_enabled{};
        bool map_overlay_enabled{true};
        bool song_display{true};
        bool auto_equip_movement_abilities{true};
        uint32_t map_icon_options{map_icon_defaults};
        bool loaded_from_disk{};
        bool persistence_ready{};
    };

    // Owns seed-scoped settings that can be changed from the injected GoA
    // menu. The installed seed pak supplies initial values; subsequent
    // changes are persisted beside that seed's bridge files.
    class RuntimeGoaSettingsService
    {
    public:
        explicit RuntimeGoaSettingsService(RuntimeGoaSettingsConfig config);

        auto install_once() -> void;
        [[nodiscard]] auto status() const -> RuntimeGoaSettingsStatus;
        // Identifies the generated seed. Written once while loading the
        // installed pak and read-only afterwards, same as the fields
        // status() already reads without synchronisation. Empty when no
        // seed metadata is present.
        [[nodiscard]] auto seed_identity() const -> std::wstring;

        auto set_enemy_randomization_enabled(bool enabled) -> bool;
        auto set_boss_randomization_enabled(bool enabled) -> bool;
        auto set_battle_level_sync_enabled(bool enabled) -> bool;
        auto set_battle_level_offset(int32_t offset) -> bool;
        auto set_experience_multiplier_half_steps(int32_t half_steps) -> bool;
        auto set_party_randomization_enabled(bool enabled) -> bool;
        auto set_party_reroll_every_room(bool enabled) -> bool;
        auto set_party_friend_choice(int32_t friend_slot, int32_t choice) -> bool;
        auto set_color_randomization_mode(int32_t mode) -> bool;
        auto set_music_shuffle_mode(int32_t mode) -> bool;
        [[nodiscard]] auto music_shuffle_mode() const -> int32_t { return music_shuffle_mode_.load(); }
        auto set_include_custom_music(bool enabled) -> bool;
        [[nodiscard]] auto include_custom_music() const -> bool { return include_custom_music_.load(); }
        auto set_form_themes_enabled(bool enabled) -> bool;
        [[nodiscard]] auto form_themes_enabled() const -> bool { return form_themes_enabled_.load(); }
        auto set_map_overlay_enabled(bool enabled) -> bool;
        [[nodiscard]] auto map_overlay_enabled() const -> bool { return map_overlay_enabled_.load(); }
        auto set_song_display(bool enabled) -> bool;
        [[nodiscard]] auto song_display() const -> bool { return song_display_.load(); }
        auto set_auto_equip_movement_abilities(bool enabled) -> bool;
        [[nodiscard]] auto auto_equip_movement_abilities() const -> bool
        {
            return auto_equip_movement_abilities_.load();
        }
        [[nodiscard]] auto map_icon_options() const -> uint32_t { return map_icon_options_.load(); }
        auto set_map_icon_option(MapIconOption option,bool enabled) -> bool;

    private:
        auto persist_settings() const -> bool;

        RuntimeGoaSettingsConfig config_{};
        std::optional<std::filesystem::path> bridge_dir_{};
        std::wstring seed_identity_{};
        std::wstring slot_name_{};
        std::atomic_bool enemy_randomization_enabled_{true};
        std::atomic_bool boss_randomization_enabled_{true};
        std::atomic_bool battle_level_sync_enabled_{true};
        std::atomic_int32_t battle_level_offset_{};
        std::atomic_int32_t experience_multiplier_half_steps_{2};
        std::atomic_int32_t seed_experience_multiplier_half_steps_{2};
        std::atomic_bool party_randomization_enabled_{};
        std::atomic_bool party_reroll_every_room_{};
        std::atomic_int32_t party_friend_1_choice_{1};
        std::atomic_int32_t party_friend_2_choice_{2};
        std::optional<bool> legacy_controller_focus_enabled_{};
        std::atomic_int32_t color_randomization_mode_{1};
        std::atomic_int32_t music_shuffle_mode_{};
        std::atomic_bool include_custom_music_{true};
        std::atomic_bool form_themes_enabled_{};
        std::atomic_bool map_overlay_enabled_{true};
        std::atomic_bool song_display_{true};
        std::atomic_bool auto_equip_movement_abilities_{true};
        std::atomic_uint32_t map_icon_options_{map_icon_defaults};
        std::atomic_bool loaded_from_disk_{};
        bool installed_{};
    };
}
