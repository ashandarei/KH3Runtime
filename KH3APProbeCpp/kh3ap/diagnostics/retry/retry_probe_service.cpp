#include "kh3ap/diagnostics/retry/retry_probe_service.h"

#include "kh3ap/diagnostics/retry/retry_blueprint_probe_state.h"
#include "kh3ap/diagnostics/retry/retry_point_probe_state.h"
#include "kh3ap/bridge/catalog/update_catalog_state.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>

#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>

#define NOMINMAX
#include <windows.h>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        constexpr auto retry_probe_log_interval = std::chrono::seconds(1);
    }

    struct RuntimeRetryProbeService::Impl
    {
        explicit Impl(RuntimeRetryProbeConfig value)
            : config(std::move(value))
        {
        }

        auto refresh_blueprint_process_observer_targets(bool log_status) -> void
        {
            if (!config.blueprint_observer_enabled)
            {
                return;
            }

            const bool notify_already_resolved =
                    config.retry_anim_notify_target.load(std::memory_order_relaxed) != nullptr;
            const bool logic_already_resolved =
                    config.retry_logic_begin_play_target.load(std::memory_order_relaxed) != nullptr;
            if (!log_status && notify_already_resolved && logic_already_resolved)
            {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            if (!log_status &&
                last_blueprint_target_resolution_attempt != std::chrono::steady_clock::time_point{} &&
                !has_elapsed_since(
                        last_blueprint_target_resolution_attempt,
                        now,
                        retry_probe_log_interval))
            {
                return;
            }
            last_blueprint_target_resolution_attempt = now;

            const auto resolve_if_needed = [](
                                                   std::atomic<UFunction*>& target,
                                                   std::wstring_view path) -> bool {
                if (target.load(std::memory_order_relaxed))
                {
                    return false;
                }
                UFunction* const resolved = UObjectGlobals::StaticFindObject<UFunction*>(
                        nullptr,
                        nullptr,
                        std::wstring{path}.c_str());
                if (!resolved)
                {
                    return false;
                }
                target.store(resolved, std::memory_order_relaxed);
                return true;
            };

            const bool notify_resolved_now = resolve_if_needed(
                    config.retry_anim_notify_target,
                    config.retry_anim_notify_path);
            const bool logic_resolved_now = resolve_if_needed(
                    config.retry_logic_begin_play_target,
                    config.retry_logic_begin_play_path);
            if (log_status || notify_resolved_now || logic_resolved_now)
            {
                Output::send<LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] RetryBlueprint observer targets notify_resolved={} logic_begin_play_resolved={}\n"),
                        config.retry_anim_notify_target.load(std::memory_order_relaxed)
                                ? STR("true")
                                : STR("false"),
                        config.retry_logic_begin_play_target.load(std::memory_order_relaxed)
                                ? STR("true")
                                : STR("false"));
            }
        }

        auto flush_deferred_blueprint_events() -> void
        {
            if (!config.blueprint_observer_enabled)
            {
                return;
            }

            const RuntimeRetryBlueprintProbeSnapshot snapshot =
                    config.blueprint_state.snapshot();
            if (!blueprint_persistence_state.should_persist(snapshot))
            {
                return;
            }

            const auto bridge_dir = config.bridge_dir_resolver
                    ? config.bridge_dir_resolver()
                    : std::nullopt;
            if (!bridge_dir.has_value())
            {
                return;
            }

            const uint64_t sequence = blueprint_persistence_state.next_sequence();
            std::ostringstream json;
            json << "{\n"
                 << "  \"protocol_name\": \"" << json_escape(config.protocol_name) << "\",\n"
                 << "  \"protocol_version\": " << config.protocol_version << ",\n"
                 << "  \"event\": \"kh3_retry_blueprint_counter_snapshot\",\n"
                 << "  \"build_stamp\": \"" << json_escape(config.build_stamp) << "\",\n"
                 << "  \"at\": \"" << now_iso8601_utc() << "\",\n"
                 << "  \"timestamp_semantics\": \"game_thread_drain_time\",\n"
                 << "  \"observer_mode\": \"cached_pointer_process_event_and_process_internal\",\n"
                 << "  \"payload_captured\": false,\n"
                 << "  \"routes_are_distinct_not_generations\": true,\n"
                 << "  \"process_id\": " << static_cast<uint64_t>(GetCurrentProcessId()) << ",\n"
                 << "  \"snapshot_sequence\": " << sequence << ",\n"
                 << "  \"retry_anim_notify_target_resolved\": "
                 << (config.retry_anim_notify_target.load(std::memory_order_relaxed)
                             ? "true"
                             : "false")
                 << ",\n"
                 << "  \"retry_logic_begin_play_target_resolved\": "
                 << (config.retry_logic_begin_play_target.load(std::memory_order_relaxed)
                             ? "true"
                             : "false")
                 << ",\n";

            const auto append_observation = [&](const char* name,
                                                 RuntimeRetryBlueprintProbeRoute route,
                                                 RuntimeRetryBlueprintProbeTarget target,
                                                 RuntimeRetryBlueprintProbePhase phase) {
                const auto& observation = snapshot.at(route, target, phase);
                json << "  \"" << name << "_hits\": " << observation.hits << ",\n"
                     << "  \"" << name << "_last_ordinal\": "
                     << observation.last_ordinal << ",\n";
            };
            append_observation(
                    "retry_anim_notify_process_event_pre",
                    RuntimeRetryBlueprintProbeRoute::ProcessEvent,
                    RuntimeRetryBlueprintProbeTarget::RetryAnimNotify,
                    RuntimeRetryBlueprintProbePhase::Pre);
            append_observation(
                    "retry_anim_notify_process_event_post",
                    RuntimeRetryBlueprintProbeRoute::ProcessEvent,
                    RuntimeRetryBlueprintProbeTarget::RetryAnimNotify,
                    RuntimeRetryBlueprintProbePhase::Post);
            append_observation(
                    "retry_anim_notify_process_internal_pre",
                    RuntimeRetryBlueprintProbeRoute::ProcessInternal,
                    RuntimeRetryBlueprintProbeTarget::RetryAnimNotify,
                    RuntimeRetryBlueprintProbePhase::Pre);
            append_observation(
                    "retry_anim_notify_process_internal_post",
                    RuntimeRetryBlueprintProbeRoute::ProcessInternal,
                    RuntimeRetryBlueprintProbeTarget::RetryAnimNotify,
                    RuntimeRetryBlueprintProbePhase::Post);
            append_observation(
                    "retry_logic_begin_play_process_event_pre",
                    RuntimeRetryBlueprintProbeRoute::ProcessEvent,
                    RuntimeRetryBlueprintProbeTarget::RetryLogicBeginPlay,
                    RuntimeRetryBlueprintProbePhase::Pre);
            append_observation(
                    "retry_logic_begin_play_process_event_post",
                    RuntimeRetryBlueprintProbeRoute::ProcessEvent,
                    RuntimeRetryBlueprintProbeTarget::RetryLogicBeginPlay,
                    RuntimeRetryBlueprintProbePhase::Post);
            append_observation(
                    "retry_logic_begin_play_process_internal_pre",
                    RuntimeRetryBlueprintProbeRoute::ProcessInternal,
                    RuntimeRetryBlueprintProbeTarget::RetryLogicBeginPlay,
                    RuntimeRetryBlueprintProbePhase::Pre);
            append_observation(
                    "retry_logic_begin_play_process_internal_post",
                    RuntimeRetryBlueprintProbeRoute::ProcessInternal,
                    RuntimeRetryBlueprintProbeTarget::RetryLogicBeginPlay,
                    RuntimeRetryBlueprintProbePhase::Post);
            json << "  \"total_observations\": " << snapshot.total_observations << "\n"
                 << "}\n";

            const std::string payload = json.str();
            const std::filesystem::path latest_path =
                    *bridge_dir /
                    std::filesystem::path{std::wstring{config.retry_blueprint_latest_filename}};
            if (!write_text_file(latest_path, payload))
            {
                return;
            }
            append_text_file(
                    *bridge_dir /
                            std::filesystem::path{
                                    std::wstring{config.retry_blueprint_events_filename}},
                    payload);
            blueprint_persistence_state.mark_persisted(snapshot);
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] RetryBlueprintCounterSnapshot seq={} observations={}\n"),
                    sequence,
                    snapshot.total_observations);
        }

        auto flush_deferred_retry_point_events() -> void
        {
            if (!config.retry_point_direct_hooks_enabled &&
                !config.retry_point_process_event_observer_enabled)
            {
                return;
            }

            const RuntimeRetryPointProbeSnapshot snapshot =
                    config.retry_point_state.snapshot();
            if (!retry_point_persistence_state.should_persist(snapshot))
            {
                return;
            }

            const auto bridge_dir = config.bridge_dir_resolver
                    ? config.bridge_dir_resolver()
                    : std::nullopt;
            if (!bridge_dir.has_value())
            {
                // Counters are cumulative, so the same snapshot remains pending
                // until a bridge directory is available and the latest-file write
                // succeeds.
                return;
            }

            const uint64_t sequence = retry_point_persistence_state.next_sequence();
            std::ostringstream json;
            json << "{\n"
                 << "  \"protocol_name\": \"" << json_escape(config.protocol_name) << "\",\n"
                 << "  \"protocol_version\": " << config.protocol_version << ",\n"
                 << "  \"event\": \"kh3_retry_point_counter_snapshot\",\n"
                 << "  \"build_stamp\": \"" << json_escape(config.build_stamp) << "\",\n"
                 << "  \"at\": \"" << now_iso8601_utc() << "\",\n"
                 << "  \"timestamp_semantics\": \"game_thread_drain_time\",\n"
                 << "  \"hook_mode\": \"process_event_pointer_observer\",\n"
                 << "  \"payload_captured\": false,\n"
                 << "  \"ordering_captured\": false,\n"
                 << "  \"negative_observation_is_conclusive\": false,\n"
                 << "  \"process_id\": " << static_cast<uint64_t>(GetCurrentProcessId()) << ",\n"
                 << "  \"snapshot_sequence\": " << sequence << ",\n"
                 << "  \"create_direct_hook_enabled\": "
                 << (config.retry_point_direct_hooks_enabled &&
                             config.create_retry_point_hook_enabled
                             ? "true"
                             : "false")
                 << ",\n"
                 << "  \"create_process_event_observer_enabled\": "
                 << (config.retry_point_process_event_observer_enabled ? "true" : "false")
                 << ",\n"
                 << "  \"create_process_event_target_resolved\": "
                 << (config.create_retry_point_target.load(std::memory_order_relaxed)
                             ? "true"
                             : "false")
                 << ",\n"
                 << "  \"discard_direct_hook_enabled\": "
                 << (config.retry_point_direct_hooks_enabled &&
                             config.discard_retry_point_hook_enabled
                             ? "true"
                             : "false")
                 << ",\n"
                 << "  \"create_pre_hits\": " << snapshot.create_pre_hits << ",\n"
                 << "  \"create_post_hits\": " << snapshot.create_post_hits << ",\n"
                 << "  \"discard_pre_hits\": " << snapshot.discard_pre_hits << ",\n"
                 << "  \"discard_post_hits\": " << snapshot.discard_post_hits << "\n"
                 << "}\n";
            const std::string payload = json.str();
            const std::filesystem::path latest_path =
                    *bridge_dir /
                    std::filesystem::path{std::wstring{config.retry_point_latest_filename}};
            if (!write_text_file(latest_path, payload))
            {
                return;
            }

            // The latest snapshot is authoritative. JSONL is best-effort history;
            // do not duplicate it on a later tick if only the append fails.
            append_text_file(
                    *bridge_dir /
                            std::filesystem::path{
                                    std::wstring{config.retry_point_events_filename}},
                    payload);
            retry_point_persistence_state.mark_persisted(snapshot);
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] RetryPointCounterSnapshot seq={} create=({}, {}) discard=({}, {}) mode=process_event_pointer_observer\n"),
                    sequence,
                    snapshot.create_pre_hits,
                    snapshot.create_post_hits,
                    snapshot.discard_pre_hits,
                    snapshot.discard_post_hits);
        }

        RuntimeRetryProbeConfig config;
        std::chrono::steady_clock::time_point last_blueprint_target_resolution_attempt{};
        RuntimeRetryBlueprintProbePersistenceState blueprint_persistence_state{};
        RuntimeRetryPointProbePersistenceState retry_point_persistence_state{};
    };

    RuntimeRetryProbeService::RuntimeRetryProbeService(RuntimeRetryProbeConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeRetryProbeService::~RuntimeRetryProbeService() = default;
    RuntimeRetryProbeService::RuntimeRetryProbeService(
            RuntimeRetryProbeService&&) noexcept = default;
    auto RuntimeRetryProbeService::operator=(RuntimeRetryProbeService&&) noexcept
            -> RuntimeRetryProbeService& = default;

    auto RuntimeRetryProbeService::refresh_blueprint_process_observer_targets(
            bool log_status) -> void
    {
        impl_->refresh_blueprint_process_observer_targets(log_status);
    }

    auto RuntimeRetryProbeService::flush_deferred_blueprint_events() -> void
    {
        impl_->flush_deferred_blueprint_events();
    }

    auto RuntimeRetryProbeService::flush_deferred_retry_point_events() -> void
    {
        impl_->flush_deferred_retry_point_events();
    }
}
