#include "kh3ap/features/story/cutscenes/shoot_flow_restore_helpers.h"

#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <format>
#include <sstream>
#include <utility>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;

    RuntimeShootFlowRestoreService::RuntimeShootFlowRestoreService(
            RuntimeShootFlowRestoreConfig config)
        : config_(std::move(config))
    {
    }

    auto RuntimeShootFlowRestoreService::append_event(
            std::wstring_view status,
            UObject* cached_controller,
            std::wstring_view cached_controller_name,
            std::wstring_view details) -> void
    {
        const auto bridge_dir = config_.bridge_dir_resolver
                ? config_.bridge_dir_resolver()
                : std::nullopt;
        if (!bridge_dir.has_value())
        {
            return;
        }

        const std::wstring runtime_context = config_.runtime_context_resolver
                ? config_.runtime_context_resolver()
                : std::wstring{};
        std::ostringstream json;
        json << "{";
        json << "\"schema\":\"kh3ap.runtime_he02_shoot_flow_restore.v6\"";
        json << ",\"recorded_at_utc\":\"" << now_iso8601_utc() << "\"";
        json << ",\"build_stamp\":\"" << json_escape(config_.build_stamp) << "\"";
        json << ",\"status\":\"" << json_escape(status) << "\"";
        json << ",\"cached_controller\":\"0x" << std::hex
             << reinterpret_cast<uintptr_t>(cached_controller) << std::dec << "\"";
        json << ",\"cached_controller_name\":\"" << json_escape(cached_controller_name) << "\"";
        json << ",\"trigger\":\"first_playable_state_after_post_tutorial_cinematic\"";
        json << ",\"settle_delay_ms\":" << config_.settle_delay.count();
        json << ",\"details\":\"" << json_escape(details) << "\"";
        json << ",\"special_battles\":" << state_.special_battle_count();
        json << ",\"tutorial_opens\":" << state_.tutorial_open_count();
        json << ",\"tutorial_releases\":" << state_.tutorial_release_count();
        json << ",\"post_tutorial_cinematics\":" << state_.post_tutorial_cinematic_count();
        json << ",\"cinematic_exit_queues\":" << state_.cinematic_exit_queue_count();
        json << ",\"restore_attempts\":" << state_.restore_attempt_count();
        json << ",\"restore_successes\":" << state_.restore_success_count();
        json << ",\"runtime_context\":\"" << json_escape(runtime_context) << "\"";
        json << "}\n";

        const std::string payload = json.str();
        write_text_file(
                *bridge_dir / std::filesystem::path{std::wstring{config_.event_filename}},
                payload);
        append_text_file(
                *bridge_dir / std::filesystem::path{std::wstring{config_.events_filename}},
                payload);
    }

    auto RuntimeShootFlowRestoreService::update_context() -> bool
    {
        auto& grant_state = config_.runtime_state.grant_state();
        auto& pawn_state = grant_state.pawn_state();
        UObject* const cached_pawn = pawn_state.object();
        const bool in_he02 =
                cached_pawn &&
                pawn_state.name().find(STR("/Game/Levels/he/he_02/")) != std::wstring::npos;
        if (cached_pawn != pawn_ || in_he02 != context_active_)
        {
            state_.reset_for_pawn();
            pawn_ = cached_pawn;
            context_active_ = in_he02;
        }

        if (!in_he02)
        {
            return false;
        }

        const RuntimeShootFlowRestoreObservation observation =
                state_.observe_special_battle(grant_state.special_battle_mode_active());
        if (observation == RuntimeShootFlowRestoreObservation::SpecialBattleArmed)
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] HE02ShootFlowRestore armed source=special_battle cached_controller=0x{:X} cached_controller_name={}\n"),
                    reinterpret_cast<uintptr_t>(pawn_state.controller_object()),
                    pawn_state.controller_name().empty()
                            ? std::wstring{STR("<none>")}
                            : pawn_state.controller_name());
        }
        return true;
    }

    auto RuntimeShootFlowRestoreService::observe_pause_ui(bool active) -> void
    {
        if (!update_context())
        {
            return;
        }

        const RuntimeShootFlowRestoreObservation observation = state_.observe_pause_ui(active);
        if (observation == RuntimeShootFlowRestoreObservation::TutorialOpened)
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] HE02ShootFlowRestore tutorial pause observed\n"));
        }
        else if (observation == RuntimeShootFlowRestoreObservation::TutorialReleased)
        {
            const auto& pawn_state = config_.runtime_state.grant_state().pawn_state();
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] HE02ShootFlowRestore tutorial released awaiting_post_tutorial_cinematic cached_controller=0x{:X} cached_controller_name={}\n"),
                    reinterpret_cast<uintptr_t>(pawn_state.controller_object()),
                    pawn_state.controller_name().empty()
                            ? std::wstring{STR("<none>")}
                            : pawn_state.controller_name());
        }
    }

    auto RuntimeShootFlowRestoreService::tick(
            std::wstring_view current_sora_state_class) -> void
    {
        if (!update_context())
        {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const RuntimeShootFlowRestoreObservation observation =
                state_.observe_player_state(
                        current_sora_state_class == STR("TresCharState_Cinematic"),
                        !current_sora_state_class.empty(),
                        now,
                        config_.settle_delay);
        if (observation == RuntimeShootFlowRestoreObservation::PostTutorialCinematicSeen)
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] HE02ShootFlowRestore post-tutorial cinematic observed state_class={}\n"),
                    current_sora_state_class);
        }
        else if (observation == RuntimeShootFlowRestoreObservation::RestoreQueued)
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] HE02ShootFlowRestore queued source=post_tutorial_cinematic_exit state_class={} settle_ms={}\n"),
                    current_sora_state_class,
                    config_.settle_delay.count());
        }

        if (!state_.consume_pending(now))
        {
            return;
        }

        auto& pawn_state = config_.runtime_state.grant_state().pawn_state();
        UObject* const cached_controller = pawn_state.controller_object();
        const std::wstring cached_controller_name = pawn_state.controller_name();
        bool restored = false;
        std::wstring status{STR("failed")};
        std::wstring details{};

        const std::wstring function_name{config_.ability_function_name};
        UFunction* const function_object = cached_controller
                ? cached_controller->GetFunctionByNameInChain(function_name.c_str())
                : nullptr;
        if (!cached_controller)
        {
            details = STR("cached controller unavailable");
        }
        else if (!function_object)
        {
            details = STR("BP_SetAbilityShootFlow function unavailable");
        }
        else
        {
            std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
            std::fill(params.begin(), params.end(), std::byte{0});
            FProperty* const in_enable_property =
                    find_function_param_by_name(function_object, STR("InEnable"));
            if (!write_bool_function_param(in_enable_property, params.data(), true))
            {
                details = STR("InEnable param unavailable");
            }
            else
            {
                try
                {
                    const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                    cached_controller->ProcessEvent(
                            function_object,
                            params.empty() ? nullptr : params.data());
                    restored = true;
                    status = STR("called");
                    details = std::format(
                            STR("BP_SetAbilityShootFlow(true) called once after post-tutorial cinematic exit function={}"),
                            format_object_debug_name(function_object));
                }
                catch (const std::exception& exception)
                {
                    const std::string message = exception.what()
                            ? exception.what()
                            : "<empty std::exception::what()>";
                    details = std::format(
                            STR("ProcessEvent exception={}"),
                            std::wstring{message.begin(), message.end()});
                }
                catch (...)
                {
                    details = STR("ProcessEvent unknown exception");
                }
            }
        }

        state_.record_restore_result(restored);
        append_event(status, cached_controller, cached_controller_name, details);
        Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] HE02ShootFlowRestore status={} cached_controller=0x{:X} cached_controller_name={} details={}\n"),
                status,
                reinterpret_cast<uintptr_t>(cached_controller),
                cached_controller_name.empty()
                        ? std::wstring{STR("<none>")}
                        : cached_controller_name,
                details);
    }
}
