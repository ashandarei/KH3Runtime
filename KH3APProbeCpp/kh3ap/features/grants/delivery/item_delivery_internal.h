#pragma once

#include "kh3ap/features/grants/delivery/item_grant_poll_helpers.h"
#include "kh3ap/features/grants/delivery/item_delivery_flow.h"

namespace kh3ap::item_delivery
{
    auto requires_persistent_delivery_context() -> bool;
    struct DeliveryContext
    {
        RuntimeGrantSafetyStatus safety;
        std::vector<RC::Unreal::UObject*> candidates;
    };

    auto prepare_delivery_context(
            const RuntimeItemGrantPollDeps& deps,
            const RuntimeItemDeliveryPolicy& policy,
            bool is_batch,
            std::chrono::steady_clock::time_point now) -> DeliveryContext;

    auto collect_delivery_world_candidates(
            const RuntimeItemGrantPollDeps& deps, bool is_batch)
            -> std::vector<RC::Unreal::UObject*>;

    auto process_batch_delivery(
            const RuntimeItemGrantPollDeps& deps,
            const RuntimeItemGrantBatchRequest& batch_request,
            const std::optional<std::filesystem::path>& request_path,
            std::chrono::steady_clock::time_point now,
            DeliveryQueue* queue = nullptr) -> void;

    auto process_single_delivery(
            const RuntimeItemGrantPollDeps& deps,
            const RuntimeItemGrantRequest& request,
            const std::optional<std::filesystem::path>& request_path,
            std::chrono::steady_clock::time_point now) -> void;

    auto runtime_item_grant_wait_ms(
            const RuntimeItemGrantPollDeps& deps,
            std::chrono::steady_clock::time_point now) -> uint64_t;

    auto elapsed_ms_since(std::chrono::steady_clock::time_point started_at) -> uint64_t;

    auto delivery_attempt_status(bool applied, bool unsafe_abort) -> std::wstring_view;

    auto format_runtime_item_grant_world_for_log(
            const RuntimeItemGrantPollDeps& deps,
            RC::Unreal::UObject* world_context_object) -> std::wstring;

    // Borrowed for one delivery call; candidate counts reflect the current
    // stage. A null grant denotes the aggregate batch event.
    struct DeliveryTrace
    {
        const RuntimeItemGrantPollDeps& deps;
        bool is_batch;
        std::wstring_view request_id;
        std::wstring_view action_kind;
        size_t grant_count;
        const std::vector<RC::Unreal::UObject*>& candidates;

        auto profile(std::wstring_view event, const RuntimeItemGrantRequest* grant,
                     uint64_t elapsed_ms, std::wstring_view status, std::wstring_view details) const -> void;
        auto deathlink(std::wstring_view event, const RuntimeItemGrantRequest* grant,
                       uint64_t waited_ms, std::wstring_view status, std::wstring_view reason,
                       std::wstring_view details, bool allow_special_battle_game_over) const -> void;
    };

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
            std::wstring_view details) -> void;

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
            bool allow_special_battle_game_over) -> void;

    auto log_runtime_item_grant_wait_if_changed(
            const RuntimeItemGrantPollDeps& deps,
            std::chrono::steady_clock::time_point now,
            std::wstring_view request_id,
            std::wstring_view phase,
            std::wstring_view reason,
            std::wstring_view details,
            uint64_t grant_count) -> void;

    auto format_runtime_apply_indices(const RuntimeItemApplyBatchPayload& payload) -> std::wstring;

    auto publish_delivery_readiness(const RuntimeItemGrantPollDeps& deps,
            std::wstring_view request_id, std::wstring_view status,
            std::wstring_view reason, std::wstring_view details, uint64_t grant_count) -> void;
}
