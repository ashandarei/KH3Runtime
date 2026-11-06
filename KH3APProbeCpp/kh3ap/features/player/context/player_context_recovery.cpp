#include "kh3ap/features/player/context/player_context_internal.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/text_helpers.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <chrono>
#include <format>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace player_context_detail;

    auto RuntimePlayerContextService::Impl::maybe_clear_special_battle_from_map_root_change(
        std::wstring_view observed_name, std::wstring_view source) -> bool
    {
        auto& grant_state = config.runtime_state.grant_state();
        if (!grant_state.special_battle_mode_active())
        {
            return false;
        }
        const std::wstring armed_map_root = grant_state.special_battle_mode_map_root();
        if (armed_map_root.empty())
        {
            return false;
        }
        const auto observed_map_root =
            RuntimeTextHelpers::extract_game_map_root(std::wstring{observed_name});
        if (!observed_map_root.has_value() || *observed_map_root == armed_map_root)
        {
            return false;
        }

        const std::wstring armed_source = grant_state.special_battle_mode_source();
        const std::wstring armed_details = grant_state.special_battle_mode_details();
        const bool changed = grant_state.update_special_battle_mode(
            false, source,
            std::format(STR("observed={} observed_map_root={} armed_map_root={} armed_source={} "
                            "armed_details={}"),
                        observed_name, *observed_map_root, armed_map_root, armed_source,
                        armed_details),
            std::chrono::steady_clock::now(), *observed_map_root);
        if (!changed)
        {
            return false;
        }
        special_battle_game_over_recovery_pending = false;
        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] Runtime special battle grant gate cleared by map change source={} "
                "observed={} observed_map_root={} armed_map_root={} armed_source={}\n"),
            source, observed_name, *observed_map_root, armed_map_root, armed_source);
        process_pending_grant_from_runtime_ready_event(source, true);
        return true;
    }

    auto RuntimePlayerContextService::Impl::maybe_clear_special_battle_from_game_over_recovery(
        std::wstring_view observed_name, std::wstring_view source) -> bool
    {
        auto& grant_state = config.runtime_state.grant_state();
        if (!grant_state.special_battle_mode_active())
        {
            special_battle_game_over_recovery_pending = false;
            return false;
        }
        const auto blocking_ui =
            config.runtime_state.ui_diagnostic_state().blocking_ui_state().snapshot();
        const bool death_screen_active =
            blocking_ui.active && blocking_ui.reason == STR("death_screen");
        if (!special_battle_game_over_recovery_pending && !death_screen_active)
        {
            return false;
        }

        const auto observed_map_root =
            RuntimeTextHelpers::extract_game_map_root(std::wstring{observed_name});
        const std::wstring observed_map_root_value = observed_map_root.value_or(std::wstring{});
        const std::wstring armed_map_root = grant_state.special_battle_mode_map_root();
        const std::wstring armed_source = grant_state.special_battle_mode_source();
        const std::wstring armed_details = grant_state.special_battle_mode_details();
        const bool changed = grant_state.update_special_battle_mode(
            false, source,
            std::format(
                STR("observed={} observed_map_root={} armed_map_root={} armed_source={} "
                    "armed_details={} recovery_pending={} blocking_active={} blocking_reason={} "
                    "blocking_context={} blocking_event={}"),
                observed_name,
                observed_map_root_value.empty() ? std::wstring{STR("<unknown>")}
                                                : observed_map_root_value,
                armed_map_root.empty() ? std::wstring{STR("<unknown>")} : armed_map_root,
                armed_source, armed_details,
                special_battle_game_over_recovery_pending ? STR("true") : STR("false"),
                blocking_ui.active ? STR("true") : STR("false"),
                blocking_ui.reason.empty() ? std::wstring{STR("<none>")} : blocking_ui.reason,
                blocking_ui.context.empty() ? std::wstring{STR("<none>")} : blocking_ui.context,
                blocking_ui.event.empty() ? std::wstring{STR("<none>")} : blocking_ui.event),
            std::chrono::steady_clock::now(), observed_map_root_value);
        special_battle_game_over_recovery_pending = false;
        if (!changed)
        {
            return false;
        }
        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] Runtime special battle grant gate cleared by game over recovery "
                "source={} observed={} observed_map_root={} armed_map_root={} armed_source={} "
                "blocking_reason={}\n"),
            source, observed_name,
            observed_map_root_value.empty() ? std::wstring{STR("<unknown>")}
                                            : observed_map_root_value,
            armed_map_root.empty() ? std::wstring{STR("<unknown>")} : armed_map_root, armed_source,
            blocking_ui.reason.empty() ? std::wstring{STR("<none>")} : blocking_ui.reason);
        process_pending_grant_from_runtime_ready_event(source, true);
        return true;
    }

    auto RuntimePlayerContextService::Impl::maybe_clear_special_battle_from_sora_level_owner(
        UObject* level_owner) -> bool
    {
        return level_owner && kh3ap::is_uobject_valid_fast(level_owner) &&
               maybe_clear_special_battle_from_map_root_change(
                   format_object_debug_name(level_owner),
                   STR("VanillaLevelWritePolyhook:level_owner:map_root_changed"));
    }

    auto RuntimePlayerContextService::Impl::is_bistro_starting_player(
        std::wstring_view new_player_name) const -> bool
    {
        const std::wstring lowered = lower_ascii_copy(new_player_name);
        return lowered.find(STR("tresremyplayercontroller")) != std::wstring::npos ||
               lowered.find(STR("tresremypawnbp")) != std::wstring::npos;
    }

    auto RuntimePlayerContextService::Impl::mark_bistro_visited_from_starting_player(
        std::wstring_view new_player_name) -> void
    {
        config.runtime_state.grant_state().mark_bistro_visited(
            STR("HandleStartingNewPlayer:bistro"), new_player_name,
            std::chrono::steady_clock::now());
        config.runtime_state.grant_state().pawn_state().reset();
        config.runtime_state.context_subsystem_state().context_state().reset_playable_publish();
        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] Runtime Bistro grant gate visited source=HandleStartingNewPlayer "
                "new_player={} grants_safe=false\n"),
            truncate_for_log(new_player_name, 180));
    }

    auto RuntimePlayerContextService::Impl::mark_bistro_return_waiting_for_map_name(
        std::wstring_view source) -> void
    {
        UObject* const cached_pawn = config.runtime_state.grant_state().pawn_state().object();
        const std::wstring cached_pawn_name = format_object_debug_name(cached_pawn);
        if (!config.runtime_state.grant_state().mark_bistro_return_waiting_for_map_name(
                source, cached_pawn_name, std::chrono::steady_clock::now()))
        {
            return;
        }
        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] Runtime Bistro grant gate waiting_for_map_name source={} pawn={} "
                "grants_safe=false\n"),
            source, cached_pawn_name);
    }

    auto RuntimePlayerContextService::Impl::release_bistro_return_gate_from_map_name(
        std::wstring_view source) -> bool
    {
        if (!config.runtime_state.grant_state().bistro_return_waiting_for_map_name() ||
            !config.runtime_state.grant_state().clear_bistro_return_gate(
                source, std::chrono::steady_clock::now()))
        {
            return false;
        }
        Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Runtime Bistro grant gate cleared "
                                            "source={} grants_safe=true | context={}\n"),
                                        source, runtime_context());
        return true;
    }
} // namespace kh3ap
