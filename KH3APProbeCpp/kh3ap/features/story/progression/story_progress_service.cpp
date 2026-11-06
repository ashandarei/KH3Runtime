#include "kh3ap/app/config.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/story/progression/story_progress_service.h"

#include "kh3ap/features/grants/caribbean_chart_unlock_state.h"
#include "kh3ap/features/story/cutscenes/guardians_of_light_skip.h"
#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/app/state.h"
#include "kh3ap/bridge/catalog/update_catalog_state.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/events/event_work.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <cwctype>
#include <format>
#include <utility>

using namespace RC;

namespace kh3ap
{
    namespace
    {
        constexpr auto guardian_readiness_retry = std::chrono::seconds(1);

        using ::kh3ap::settings::s_caribbean_chart_visit_flags;
        using ::kh3ap::settings::s_caribbean_chart_visit_unlock_max_attempts;
        using ::kh3ap::settings::s_caribbean_chart_visit_unlock_post_map_ready_delay;
        using ::kh3ap::settings::s_caribbean_chart_visit_unlock_retry_delay;
        using ::kh3ap::settings::s_caribbean_level_path_token;
        using ::kh3ap::settings::s_debug_logging;
        using ::kh3ap::settings::s_enable_caribbean_chart_visit_unlock_on_player_start;
        using ::kh3ap::settings::s_enable_guardians_of_light_slow_mickey_skip;
        using ::kh3ap::settings::s_get_game_flag_path;
        using ::kh3ap::settings::s_set_game_flag_boolean_path;
        using ::kh3ap::settings::s_set_game_flag_path;
        using ::kh3ap::settings::s_tres_game_blueprint_library_default_object_path;

        auto lower_ascii_copy(std::wstring_view value) -> std::wstring
        {
            std::wstring lowered{value};
            std::transform(
                    lowered.begin(),
                    lowered.end(),
                    lowered.begin(),
                    [](wchar_t ch) {
                        return static_cast<wchar_t>(std::towlower(ch));
                    });
            return lowered;
        }
    }

    struct RuntimeStoryProgressService::Impl
    {
        explicit Impl(RuntimeStoryProgressConfig value)
            : config(std::move(value))
        {
        }

        auto runtime_context() const -> std::wstring
        {
            return config.runtime_context_resolver
                    ? config.runtime_context_resolver()
                    : std::wstring{};
        }

        auto process_guardians_of_light_skip(
                std::chrono::steady_clock::time_point now) -> void
        {
            if constexpr (!s_enable_guardians_of_light_slow_mickey_skip)
            {
                return;
            }
            if (!guardians_work.take(now))
            {
                return;
            }

            const auto current_context = config.frame_context_resolver
                    ? config.frame_context_resolver()
                    : std::nullopt;
            const bool target_map_active =
                    current_context.has_value() &&
                    is_runtime_guardians_of_light_skip_map_context(
                            current_context->world,
                            current_context->outer,
                            current_context->object);
            if(!current_context) {guardians_work.finish(now,false);return;}
            if (!target_map_active)
            {
                static_cast<void>(guardians_entry_state.should_attempt(false));
                guardians_write_failure_logged = false;
                return;
            }

            // The option resolver reads immutable installed-pak settings. Once this entry's
            // flag swap succeeded, skip that work as well as the flag write.
            if (!guardians_entry_state.should_attempt(true))
            {
                guardians_write_failure_logged = false;
                return;
            }

            const bool option_enabled =
                    !config.guardians_and_mickey_walk_skip_enabled_resolver ||
                    config.guardians_and_mickey_walk_skip_enabled_resolver();
            if (!option_enabled)
            {
                static_cast<void>(guardians_entry_state.should_attempt(false));
                guardians_write_failure_logged = false;
                return;
            }

            std::wstring failure_reason{};
            const bool applied = try_call_set_game_flag(
                    runtime_guardians_of_light_skip_flag,
                    runtime_guardians_of_light_skip_target,
                    config.runtime_state,
                    s_set_game_flag_path,
                    s_tres_game_blueprint_library_default_object_path,
                    s_debug_logging,
                    &failure_reason);
            if (applied)
            {
                guardians_entry_state.record_success();
                guardians_write_failure_logged = false;
                int32_t verified_story_value{};
                std::wstring verification_failure{};
                const bool readback_succeeded = try_call_get_game_flag(
                        runtime_guardians_of_light_skip_flag,
                        verified_story_value,
                        config.runtime_state,
                        s_get_game_flag_path,
                        s_tres_game_blueprint_library_default_object_path,
                        s_debug_logging,
                        &verification_failure);
                const bool verified =
                        readback_succeeded &&
                        verified_story_value == runtime_guardians_of_light_skip_target;
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] GuardiansOfLightSlowMickeySkip map_entry flag={} target={} label={} verified={} readback={} map={} failure={} | {}\n"),
                        runtime_guardians_of_light_skip_flag,
                        runtime_guardians_of_light_skip_target,
                        runtime_guardians_of_light_skip_target_label,
                        verified ? STR("true") : STR("false"),
                        readback_succeeded
                                ? std::to_wstring(verified_story_value)
                                : std::wstring{STR("<failed>")},
                        runtime_guardians_of_light_skip_map_path,
                        verification_failure.empty()
                                ? STR("<none>")
                                : verification_failure,
                        runtime_context());
                return;
            }

            if (!guardians_write_failure_logged)
            {
                guardians_write_failure_logged = true;
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] GuardiansOfLightSlowMickeySkip map_entry write failed flag={} target={} map={} failure={} | {}\n"),
                        runtime_guardians_of_light_skip_flag,
                        runtime_guardians_of_light_skip_target,
                        runtime_guardians_of_light_skip_map_path,
                        failure_reason.empty() ? STR("<none>") : failure_reason,
                        runtime_context());
            }
            guardians_work.finish(now,false,guardian_readiness_retry);
        }

        auto arm_caribbean_chart_unlock(
                std::wstring_view starting_player_context) -> void
        {
            if constexpr (!s_enable_caribbean_chart_visit_unlock_on_player_start)
            {
                return;
            }

            const std::wstring lowered_context = lower_ascii_copy(std::format(
                    STR("{} {}"),
                    starting_player_context,
                    runtime_context()));
            if (lowered_context.find(s_caribbean_level_path_token) == std::wstring::npos)
            {
                caribbean_unlock_state.reset();
                return;
            }
            if (!caribbean_unlock_state.arm())
            {
                return;
            }

            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Caribbean chart visit unlock armed stage=player_start waiting_for=map_name_callback context={}\n"),
                    truncate_for_log(starting_player_context, 260));
        }

        auto mark_caribbean_chart_map_ready(
                std::wstring_view map_ready_context) -> void
        {
            if constexpr (!s_enable_caribbean_chart_visit_unlock_on_player_start)
            {
                return;
            }

            const std::wstring lowered_context = lower_ascii_copy(std::format(
                    STR("{} {}"),
                    map_ready_context,
                    runtime_context()));
            if (lowered_context.find(s_caribbean_level_path_token) == std::wstring::npos)
            {
                return;
            }
            if (!caribbean_unlock_state.mark_map_ready(
                        std::chrono::steady_clock::now(),
                        s_caribbean_chart_visit_unlock_post_map_ready_delay))
            {
                return;
            }

            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Caribbean chart visit unlock deferred stage=map_name_callback delay_ms={} context={}\n"),
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                            s_caribbean_chart_visit_unlock_post_map_ready_delay)
                            .count(),
                    truncate_for_log(map_ready_context, 260));
        }

        auto process_caribbean_chart_unlock(
                std::chrono::steady_clock::time_point now) -> void
        {
            if constexpr (!s_enable_caribbean_chart_visit_unlock_on_player_start)
            {
                return;
            }
            if (!caribbean_unlock_state.ready(now))
            {
                return;
            }

            const std::wstring current_context = runtime_context();
            if (lower_ascii_copy(current_context).find(s_caribbean_level_path_token) ==
                std::wstring::npos)
            {
                caribbean_unlock_state.reset();
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Caribbean chart visit unlock cancelled reason=map_changed context={}\n"),
                        truncate_for_log(current_context, 320));
                return;
            }

            size_t applied_count = 0;
            std::wstring first_failure{};
            for (const std::wstring_view flag_name : s_caribbean_chart_visit_flags)
            {
                std::wstring failure_reason{};
                if (try_call_set_game_flag_boolean(
                            flag_name,
                            true,
                            config.runtime_state,
                            s_set_game_flag_boolean_path,
                            s_tres_game_blueprint_library_default_object_path,
                            s_debug_logging,
                            &failure_reason))
                {
                    ++applied_count;
                }
                else if (first_failure.empty())
                {
                    first_failure = std::format(
                            STR("{}: {}"),
                            flag_name,
                            failure_reason);
                }
            }

            constexpr size_t flag_count = sizeof(s_caribbean_chart_visit_flags) /
                                          sizeof(s_caribbean_chart_visit_flags[0]);
            const bool complete = applied_count == flag_count;
            const int attempt = caribbean_unlock_state.attempts + 1;
            caribbean_unlock_state.record_attempt(
                    complete,
                    now,
                    s_caribbean_chart_visit_unlock_retry_delay,
                    s_caribbean_chart_visit_unlock_max_attempts);
            const bool exhausted =
                    caribbean_unlock_state.phase ==
                    RuntimeCaribbeanChartUnlockState::Phase::failed;
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Caribbean chart visit unlock post_map_ready attempt={} applied={}/{} complete={} exhausted={} context={} failure={}\n"),
                    attempt,
                    applied_count,
                    flag_count,
                    complete ? STR("true") : STR("false"),
                    exhausted ? STR("true") : STR("false"),
                    truncate_for_log(current_context, 260),
                    first_failure.empty()
                            ? STR("<none>")
                            : truncate_for_log(first_failure, 320));
        }

        RuntimeStoryProgressConfig config;
        RuntimeEventWork guardians_work;
        RuntimeGuardiansOfLightSkipEntryState guardians_entry_state{};
        bool guardians_write_failure_logged{false};
        RuntimeCaribbeanChartUnlockState caribbean_unlock_state{};
    };

    RuntimeStoryProgressService::RuntimeStoryProgressService(
            RuntimeStoryProgressConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeStoryProgressService::~RuntimeStoryProgressService() = default;
    RuntimeStoryProgressService::RuntimeStoryProgressService(
            RuntimeStoryProgressService&&) noexcept = default;
    auto RuntimeStoryProgressService::operator=(
            RuntimeStoryProgressService&&) noexcept
            -> RuntimeStoryProgressService& = default;

    auto RuntimeStoryProgressService::process_guardians_of_light_skip(
            std::chrono::steady_clock::time_point now) -> void
    {
        impl_->process_guardians_of_light_skip(now);
    }

    auto RuntimeStoryProgressService::arm_caribbean_chart_unlock(
            std::wstring_view starting_player_context) -> void
    {
        impl_->arm_caribbean_chart_unlock(starting_player_context);
    }

    auto RuntimeStoryProgressService::mark_caribbean_chart_map_ready(
            std::wstring_view map_ready_context) -> void
    {
        impl_->mark_caribbean_chart_map_ready(map_ready_context);
    }

    auto RuntimeStoryProgressService::process_caribbean_chart_unlock(
            std::chrono::steady_clock::time_point now) -> void
    {
        impl_->process_caribbean_chart_unlock(now);
    }

    auto RuntimeStoryProgressService::reset_map_lifecycle() -> void
    {
        impl_->caribbean_unlock_state.reset();
        impl_->guardians_entry_state={};
        impl_->guardians_work.request();
    }
    auto RuntimeStoryProgressService::notify_context_ready() -> void {impl_->guardians_work.request();}
}
