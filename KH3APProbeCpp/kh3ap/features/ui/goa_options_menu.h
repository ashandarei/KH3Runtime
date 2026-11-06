#pragma once

#include "kh3ap/features/ui/options_menu.h"
#include <filesystem>
#include <optional>

namespace kh3ap
{
    class RuntimeBattleLevelSyncService;
    class RuntimeEnemyRemapService;
    class RuntimeGoaSettingsService;
    class RuntimeExperienceMultiplierService;
    class RuntimePartyMemberService;
    class RuntimeControllerPreferencesService;

    [[nodiscard]] auto make_goa_runtime_options_menu(
            RuntimeExperienceMultiplierService& experience_multiplier,
            RuntimeBattleLevelSyncService& battle_level_sync,
            RuntimeControllerPreferencesService& settings,
            RuntimeGoaSettingsService& goa_settings,
            std::function<std::optional<std::filesystem::path>()> bridge_dir)
            -> RuntimeOptionsMenuDefinition;

    [[nodiscard]] auto make_goa_credits_menu()
            -> RuntimeOptionsMenuDefinition;

    [[nodiscard]] auto make_randomization_runtime_options_menu(
            RuntimeEnemyRemapService& enemy_remaps,
            RuntimePartyMemberService& party_members,
            RuntimeGoaSettingsService& goa_settings)
            -> RuntimeOptionsMenuDefinition;

    [[nodiscard]] auto make_music_runtime_options_menu(RuntimeGoaSettingsService& goa_settings)
            -> RuntimeOptionsMenuDefinition;

    [[nodiscard]] auto make_mom_runtime_options_menu(
            std::function<std::optional<std::filesystem::path>()> bridge_dir)
            -> RuntimeOptionsMenuDefinition;
}
