#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/story/remind/remind_completion_service.h"

#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/features/story/remind/remind_completion.h"
#include "kh3ap/app/state.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/transport/pipe_transport.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>

#include <cstdint>
#include <system_error>
#include <utility>

namespace kh3ap
{
    using namespace RC;

    struct RuntimeRemindCompletionService::Impl
    {
        explicit Impl(RuntimeRemindCompletionConfig value)
            : config(std::move(value))
        {
        }

        auto reset() -> void
        {
            reconcile_state.reset_for_runtime_lifecycle();
            game_state_signature_valid = false;
            failure_logged = false;
        }

        auto refresh(const std::optional<std::filesystem::path>& bridge_dir) -> void
        {
            if (bridge_dir != current_bridge_dir)
            {
                reset();
                current_bridge_dir = bridge_dir;
            }
            if (!bridge_dir.has_value() || reconcile_state.check_observed)
            {
                return;
            }

            const std::filesystem::path game_state_path = *bridge_dir / channels::game_state_file;
            const auto next_revision = pipe::revision(game_state_path);
            if (!next_revision || (game_state_signature_valid && next_revision == game_state_revision)) return;

            const auto game_state_text = read_text_file(game_state_path);
            if (!game_state_text.has_value())
            {
                return;
            }
            game_state_revision = next_revision;
            game_state_signature_valid = true;
            reconcile_state.observe_checked_locations(
                    JsonFieldReader::string_array_field(*game_state_text, "checked_locations"));
        }

        auto process(
                const std::optional<std::filesystem::path>& bridge_dir,
                std::wstring_view context_summary) -> void
        {
            if (!config.enabled)
            {
                return;
            }

            refresh(bridge_dir);
            if (!reconcile_state.should_attempt() ||
                config.runtime_state.grant_state().special_battle_mode_active())
            {
                return;
            }

            int32_t current_value{};
            std::wstring current_read_failure{};
            const bool current_read = try_call_get_game_flag(
                    runtime_remind_completion_flag,
                    current_value,
                    config.runtime_state,
                    config.get_game_flag_path,
                    config.blueprint_library_default_object_path,
                    config.debug_logging,
                    &current_read_failure);
            if (!current_read)
            {
                if (!failure_logged)
                {
                    failure_logged = true;
                    Output::send<LogLevel::Warning>(
                            STR("[KH3APProbeCpp] ReMindCompletionCheck waiting check={} flag={} reason=get_failed failure={} | {}\n"),
                            runtime_remind_completion_bridge_key,
                            runtime_remind_completion_flag,
                            current_read_failure.empty() ? STR("<none>") : current_read_failure,
                            context_summary);
                }
                return;
            }

            if (!runtime_remind_completion_needs_reconcile(current_value))
            {
                reconcile_state.record_success();
                failure_logged = false;
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] ReMindCompletionCheck reconciled check={} location={} flag={} current={} target={} label={} write=false | {}\n"),
                        runtime_remind_completion_bridge_key,
                        runtime_remind_completion_location_name,
                        runtime_remind_completion_flag,
                        current_value,
                        runtime_remind_completion_target,
                        runtime_remind_completion_target_label,
                        context_summary);
                return;
            }

            std::wstring write_failure{};
            const bool wrote = try_call_set_game_flag(
                    runtime_remind_completion_flag,
                    runtime_remind_completion_target,
                    config.runtime_state,
                    config.set_game_flag_path,
                    config.blueprint_library_default_object_path,
                    config.debug_logging,
                    &write_failure);
            int32_t verified_value{};
            std::wstring verification_failure{};
            const bool readback_succeeded =
                    wrote && try_call_get_game_flag(
                                     runtime_remind_completion_flag,
                                     verified_value,
                                     config.runtime_state,
                                     config.get_game_flag_path,
                                     config.blueprint_library_default_object_path,
                                     config.debug_logging,
                                     &verification_failure);
            const bool verified =
                    readback_succeeded && verified_value >= runtime_remind_completion_target;
            if (verified)
            {
                reconcile_state.record_success();
                failure_logged = false;
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] ReMindCompletionCheck reconciled check={} location={} flag={} before={} target={} label={} readback={} write=true verified=true | {}\n"),
                        runtime_remind_completion_bridge_key,
                        runtime_remind_completion_location_name,
                        runtime_remind_completion_flag,
                        current_value,
                        runtime_remind_completion_target,
                        runtime_remind_completion_target_label,
                        verified_value,
                        context_summary);
                return;
            }

            if (!failure_logged)
            {
                failure_logged = true;
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] ReMindCompletionCheck write failed check={} flag={} before={} target={} wrote={} readback={} failure={} verification_failure={} | {}\n"),
                        runtime_remind_completion_bridge_key,
                        runtime_remind_completion_flag,
                        current_value,
                        runtime_remind_completion_target,
                        wrote ? STR("true") : STR("false"),
                        readback_succeeded ? std::to_wstring(verified_value) : std::wstring{STR("<failed>")},
                        write_failure.empty() ? STR("<none>") : write_failure,
                        verification_failure.empty() ? STR("<none>") : verification_failure,
                        context_summary);
            }
        }

        RuntimeRemindCompletionConfig config;
        RuntimeRemindCompletionReconcileState reconcile_state{};
        std::optional<std::filesystem::path> current_bridge_dir{};
        uint64_t game_state_revision{};
        bool game_state_signature_valid{false};
        bool failure_logged{false};
    };

    RuntimeRemindCompletionService::RuntimeRemindCompletionService(RuntimeRemindCompletionConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeRemindCompletionService::~RuntimeRemindCompletionService() = default;
    RuntimeRemindCompletionService::RuntimeRemindCompletionService(RuntimeRemindCompletionService&&) noexcept = default;
    auto RuntimeRemindCompletionService::operator=(RuntimeRemindCompletionService&&) noexcept
            -> RuntimeRemindCompletionService& = default;

    auto RuntimeRemindCompletionService::reset() -> void
    {
        impl_->reset();
    }

    auto RuntimeRemindCompletionService::has_pending_work() const -> bool
    {
        return impl_->config.enabled && impl_->reconcile_state.should_attempt();
    }

    auto RuntimeRemindCompletionService::process(
            const std::optional<std::filesystem::path>& bridge_dir,
            std::wstring_view context_summary) -> void
    {
        impl_->process(bridge_dir, context_summary);
    }
}
