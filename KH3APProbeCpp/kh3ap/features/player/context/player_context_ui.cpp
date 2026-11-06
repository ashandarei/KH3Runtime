#include "kh3ap/app/config.h"
#include "kh3ap/features/player/context/player_context_internal.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/diagnostics/output/diagnostic_probe_output_helpers.h"
#include "kh3ap/features/player/state/sora_state_catalog.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace player_context_detail;
    namespace
    {
        using ::kh3ap::settings::s_build_stamp;
    }

    auto RuntimePlayerContextService::Impl::clear_blocking_ui_from_starting_player(
        std::wstring_view new_player_name) -> void
    {
        auto& blocking_state = config.runtime_state.ui_diagnostic_state().blocking_ui_state();
        const auto previous = blocking_state.snapshot();
        const bool changed = blocking_state.update(false, STR("handle_starting_new_player"),
                                                   STR("handle_starting_new_player"),
                                                   STR("HandleStartingNewPlayer"), -1);
        write_runtime_blocking_ui_state_file(make_runtime_diagnostic_probe_output_deps(
                                                 bridge_dir(), s_build_stamp, runtime_context()),
                                             false, STR("handle_starting_new_player"),
                                             STR("handle_starting_new_player"),
                                             STR("HandleStartingNewPlayer"), -1);
        if (changed || previous.active)
        {
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Runtime blocking UI cleared reason=handle_starting_new_player "
                    "previous_context={} new_player={}\n"),
                previous.context, truncate_for_log(new_player_name, 180));
        }
    }

    auto RuntimePlayerContextService::Impl::clear_blocking_ui_for_playable_context(
        std::wstring_view reason, std::wstring_view event_name) -> void
    {
        auto& blocking_state = config.runtime_state.ui_diagnostic_state().blocking_ui_state();
        const auto previous = blocking_state.snapshot();
        const bool changed = blocking_state.update(false, reason, reason, event_name, -1);
        write_runtime_blocking_ui_state_file(make_runtime_diagnostic_probe_output_deps(
                                                 bridge_dir(), s_build_stamp, runtime_context()),
                                             false, reason, reason, event_name, -1);
        if (changed || previous.active)
        {
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Runtime blocking UI cleared reason={} previous_context={} "
                    "event={} | context={}\n"),
                reason, previous.context, event_name, runtime_context());
        }
    }

    auto RuntimePlayerContextService::Impl::activate_blocking_ui_from_sora_game_over_state(
        std::wstring_view state_class, const std::optional<uint8_t>& state_id) -> void
    {
        if (!runtime_sora_state_matches_kind(RuntimeSoraStateKind::GameOver, state_id, state_class))
        {
            return;
        }
        const std::wstring_view game_over_state_class =
            runtime_sora_state_class_name(RuntimeSoraStateKind::GameOver);
        auto& blocking_state = config.runtime_state.ui_diagnostic_state().blocking_ui_state();
        if (config.runtime_state.grant_state().special_battle_mode_active())
        {
            special_battle_game_over_recovery_pending = true;
        }
        const bool changed = blocking_state.update(
            true, STR("death_screen"), STR("sora_state_game_over"), game_over_state_class,
            state_id.has_value() ? static_cast<int64_t>(*state_id) : -1);
        if (!changed)
        {
            return;
        }
        write_runtime_blocking_ui_state_file(
            make_runtime_diagnostic_probe_output_deps(bridge_dir(), s_build_stamp,
                                                      runtime_context()),
            true, STR("death_screen"), STR("sora_state_game_over"), game_over_state_class,
            state_id.has_value() ? static_cast<int64_t>(*state_id) : -1);
        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] Runtime blocking UI active reason=death_screen "
                "source=sora_state_game_over state_id={} | context={}\n"),
            state_id.has_value() ? std::to_wstring(*state_id) : std::wstring{STR("<none>")},
            runtime_context());
    }
} // namespace kh3ap
