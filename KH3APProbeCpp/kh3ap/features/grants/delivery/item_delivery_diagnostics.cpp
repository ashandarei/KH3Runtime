#include "kh3ap/app/config.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/grants/delivery/item_delivery_internal.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include <Windows.h>

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <chrono>
#include <format>
#include <sstream>

namespace kh3ap::item_delivery
{
    using ::kh3ap::settings::s_enable_runtime_grant_cpp_profile_events;

    using namespace RC;
    using namespace RC::Unreal;

    auto publish_delivery_readiness(const RuntimeItemGrantPollDeps& deps,
            std::wstring_view request_id, std::wstring_view status,
            std::wstring_view reason, std::wstring_view details, uint64_t grant_count) -> void
    {
        if (!deps.bridge_dir) return;
        std::ostringstream json;
        json << "{\"protocol_name\":\"" << json_escape(deps.protocol_name)
             << "\",\"protocol_version\":" << deps.protocol_version
             << ",\"schema\":\"kh3ap.item_delivery_readiness.v1\",\"owner\":\"dll\""
             << ",\"process_id\":" << GetCurrentProcessId()
             << ",\"request_id\":\"" << json_escape(request_id)
             << "\",\"status\":\"" << json_escape(status)
             << "\",\"reason\":\"" << json_escape(reason)
             << "\",\"details\":\"" << json_escape(details)
             << "\",\"grant_count\":" << grant_count
             << ",\"updated_at_utc\":\"" << now_iso8601_utc() << "\"}\n";
        write_text_file_atomic_verified(*deps.bridge_dir / channels::runtime_item_grant_readiness, json.str());
    }

    auto delivery_attempt_status(bool applied, bool unsafe_abort) -> std::wstring_view
    {
        return applied ? L"applied" : unsafe_abort ? L"unsafe_abort" : L"failed";
    }

    auto DeliveryTrace::profile(
            std::wstring_view event, const RuntimeItemGrantRequest* grant,
            uint64_t elapsed_ms, std::wstring_view status, std::wstring_view details) const -> void
    {
        append_runtime_grant_cpp_profile_event(
                deps, event,
                grant && (!is_batch || !grant->request_id.empty()) ? grant->request_id : request_id,
                action_kind,
                grant ? grant->index : -1,
                grant ? std::wstring_view{grant->item_name} : L"",
                grant ? std::wstring_view{grant->runtime_token} : L"",
                grant ? std::wstring_view{grant->grant_method} : L"",
                grant_count, candidates.size(), elapsed_ms, status, details);
    }

    auto DeliveryTrace::deathlink(
            std::wstring_view event, const RuntimeItemGrantRequest* grant,
            uint64_t waited_ms, std::wstring_view status, std::wstring_view reason,
            std::wstring_view details, bool allow_special_battle_game_over) const -> void
    {
        append_deathlink_game_over_diagnostic(
                deps, event,
                grant && (!is_batch || !grant->request_id.empty()) ? grant->request_id : request_id,
                action_kind,
                grant ? grant->index : -9001,
                grant ? std::wstring_view{grant->item_name} : L"DeathLink Game Over",
                grant ? std::wstring_view{grant->runtime_token} : L"GAMEOVER_KIND_NORMAL",
                grant ? std::wstring_view{grant->grant_method} : L"bp_tres_start_game_over",
                grant_count, candidates.size(), waited_ms, status, reason, details,
                allow_special_battle_game_over);
    }

    auto runtime_item_grant_wait_ms(
            const RuntimeItemGrantPollDeps& deps,
            std::chrono::steady_clock::time_point now) -> uint64_t
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                             deps.runtime_state.grant_state().item_grant_poll_state().armed_wait(now))
                                              .count());
    }

    auto elapsed_ms_since(std::chrono::steady_clock::time_point started_at) -> uint64_t
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now() - started_at)
                                             .count());
    }

    auto format_runtime_item_grant_world_for_log(
            const RuntimeItemGrantPollDeps& deps,
            UObject* world_context_object) -> std::wstring
    {
        if (!world_context_object)
        {
            return std::wstring{STR("<unknown>")};
        }

        const auto& pawn_state = deps.runtime_state.grant_state().pawn_state();
        if (world_context_object == pawn_state.object() &&
            pawn_state.candidate_trusted())
        {
            // The name was captured when this pawn became trusted. Reusing it
            // keeps verbose per-item bookkeeping logs from calling IsReal for
            // every grant in the batch.
            return pawn_state.name().empty()
                    ? std::wstring{STR("<trusted_sora_pawn>")}
                    : pawn_state.name();
        }

        return format_object_debug_name(world_context_object);
    }

    auto append_runtime_grant_cpp_profile_event(
            const RuntimeItemGrantPollDeps& deps,
            std::wstring_view event,
            std::wstring_view request_id,
            std::wstring_view action_kind,
            int64_t index,
            std::wstring_view item_name,
            std::wstring_view runtime_token,
            std::wstring_view grant_method,
            uint64_t grant_count,
            uint64_t candidate_count,
            uint64_t elapsed_ms,
            std::wstring_view status,
            std::wstring_view details) -> void
    {
        if constexpr (!s_enable_runtime_grant_cpp_profile_events)
        {
            return;
        }
        if (!deps.bridge_dir.has_value())
        {
            return;
        }

        std::ostringstream json;
        json << "{";
        json << "\"recorded_at_utc\":\"" << now_iso8601_utc() << "\"";
        json << ",\"event\":\"" << json_escape(event) << "\"";
        json << ",\"request_id\":\"" << json_escape(request_id) << "\"";
        json << ",\"action_kind\":\"" << json_escape(action_kind) << "\"";
        json << ",\"index\":" << index;
        json << ",\"item_name\":\"" << json_escape(item_name) << "\"";
        json << ",\"runtime_token\":\"" << json_escape(runtime_token) << "\"";
        json << ",\"grant_method\":\"" << json_escape(grant_method) << "\"";
        json << ",\"grant_count\":" << grant_count;
        json << ",\"candidate_count\":" << candidate_count;
        json << ",\"elapsed_ms\":" << elapsed_ms;
        json << ",\"status\":\"" << json_escape(status) << "\"";
        json << ",\"details\":\"" << json_escape(details) << "\"";
        json << ",\"context\":\"" << json_escape(deps.runtime_context_summary) << "\"";
        json << "}\n";
        append_text_file(*deps.bridge_dir / "runtime_grant_cpp_profile.jsonl", json.str());
    }

    auto append_deathlink_game_over_diagnostic(
            const RuntimeItemGrantPollDeps& deps,
            std::wstring_view event,
            std::wstring_view request_id,
            std::wstring_view action_kind,
            int64_t index,
            std::wstring_view item_name,
            std::wstring_view runtime_token,
            std::wstring_view grant_method,
            uint64_t grant_count,
            uint64_t candidate_count,
            uint64_t waited_ms,
            std::wstring_view status,
            std::wstring_view reason,
            std::wstring_view details,
            bool allow_special_battle_game_over) -> void
    {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        if (!deps.bridge_dir.has_value())
        {
            return;
        }

        const RuntimeGrantState& grant_state = deps.runtime_state.grant_state();
        std::ostringstream json;
        json << "{";
        json << "\"schema\":\"kh3ap.deathlink_gameover_diagnostics.v1\"";
        json << ",\"recorded_at_utc\":\"" << now_iso8601_utc() << "\"";
        json << ",\"event\":\"" << json_escape(event) << "\"";
        json << ",\"request_id\":\"" << json_escape(request_id) << "\"";
        json << ",\"action_kind\":\"" << json_escape(action_kind) << "\"";
        json << ",\"index\":" << index;
        json << ",\"item_name\":\"" << json_escape(item_name) << "\"";
        json << ",\"runtime_token\":\"" << json_escape(runtime_token) << "\"";
        json << ",\"grant_method\":\"" << json_escape(grant_method) << "\"";
        json << ",\"grant_count\":" << grant_count;
        json << ",\"candidate_count\":" << candidate_count;
        json << ",\"waited_ms\":" << waited_ms;
        json << ",\"status\":\"" << json_escape(status) << "\"";
        json << ",\"reason\":\"" << json_escape(reason) << "\"";
        json << ",\"details\":\"" << json_escape(details) << "\"";
        json << ",\"allow_special_battle_game_over\":"
             << (allow_special_battle_game_over ? "true" : "false");
        json << ",\"special_battle_mode_active\":"
             << (grant_state.special_battle_mode_active() ? "true" : "false");
        json << ",\"special_battle_mode_source\":\""
             << json_escape(grant_state.special_battle_mode_source()) << "\"";
        json << ",\"special_battle_mode_details\":\""
             << json_escape(grant_state.special_battle_mode_details()) << "\"";
        json << ",\"special_battle_mode_map_root\":\""
             << json_escape(grant_state.special_battle_mode_map_root()) << "\"";
        json << ",\"special_battle_mode_wait_ms\":"
             << grant_state.special_battle_mode_wait_ms(std::chrono::steady_clock::now());
        json << ",\"context\":\"" << json_escape(deps.runtime_context_summary) << "\"";
        json << "}\n";
        append_text_file(*deps.bridge_dir / "runtime_deathlink_gameover_diagnostics.jsonl", json.str());
#endif
    }

    auto log_runtime_item_grant_wait_if_changed(
            const RuntimeItemGrantPollDeps& deps,
            std::chrono::steady_clock::time_point now,
            std::wstring_view request_id,
            std::wstring_view phase,
            std::wstring_view reason,
            std::wstring_view details,
            uint64_t grant_count) -> void
    {
        auto& poll_state = deps.runtime_state.grant_state().item_grant_poll_state();
        const auto wait_ms = runtime_item_grant_wait_ms(deps, now);
        // Details can contain volatile counters such as waited_ms. Treating
        // the full text as identity made an unchanged safety gate look new
        // on every 500 ms poll and bypassed the slow-wait throttle.
        const std::wstring signature = std::format(
                STR("{}|{}|{}|{}"),
                request_id,
                phase,
                reason,
                std::wstring{deps.runtime_context_summary});
        const bool reason_changed = poll_state.mark_wait_signature(signature);
        if (reason_changed)
        {
            publish_delivery_readiness(deps, request_id, L"waiting", reason, details, grant_count);
        }
        const bool slow_wait_log =
                wait_ms >= static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                         deps.slow_wait_log_delay)
                                                         .count()) &&
                deps.runtime_state.grant_state().log_throttle_state().mark_runtime_item_grant_armed_wait_log(
                        now,
                        deps.slow_wait_log_delay);
        if (!reason_changed && !slow_wait_log)
        {
            return;
        }

        Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Runtime item grant wait request_id={} phase={} grants={} reason={} details={} waited_ms={} context={}\n"),
                request_id.empty() ? std::wstring_view{STR("<none>")} : request_id,
                phase,
                grant_count,
                reason,
                details.empty()
                        ? std::wstring{STR("<none>")}
                        : truncate_for_log(details, 280),
                wait_ms,
                truncate_for_log(deps.runtime_context_summary, 320));
    }

    auto format_runtime_apply_indices(const RuntimeItemApplyBatchPayload& payload) -> std::wstring
    {
        std::wostringstream stream{};
        bool first = true;
        for (const RuntimeItemApplyPayload& applied : payload.applied)
        {
            if (!first)
            {
                stream << L",";
            }
            first = false;
            stream << applied.index;
        }
        return stream.str();
    }
}
