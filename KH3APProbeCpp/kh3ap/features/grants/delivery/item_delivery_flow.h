#pragma once

#include "kh3ap/features/grants/grant_safety.h"
#include "kh3ap/features/grants/delivery/item_grant_poll_state.h"
#include "kh3ap/core/types.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <span>

namespace kh3ap
{
    // Request-wide permissions must hold for every member of a batch. A mixed
    // batch must never inherit the Game Over or space permission of one item.
    struct RuntimeItemDeliveryPolicy
    {
        bool allow_special_battle_game_over{};
        bool allow_special_battle_bad_status_trap{};
        bool contains_bad_status_trap{};
        bool require_fresh_playable_context{};
        bool supports_space_inventory{};
        bool supports_kh02_persistent_rewards{};
        bool supports_alternate_bad_status{};
        bool supports_alternate_betrayal{};
        bool supports_alternate_game_over{};
        bool bundle_visible_durable_ability_hud{};
    };

    auto runtime_item_delivery_policy(
            std::span<const RuntimeItemGrantRequest> grants,
            std::wstring_view action_kind,
            bool is_batch) -> RuntimeItemDeliveryPolicy;

    constexpr auto runtime_item_delivery_uses_priority(
            bool priority_exists, bool space_active, bool normal_exists) -> bool
    {
        return priority_exists && !(space_active && normal_exists);
    }

    constexpr auto runtime_item_delivery_requires_persistent_context(
            bool alternate_active, bool handoff_pending, bool kh02_world,
            bool kh02_possessed_sora) -> bool
    {
        return alternate_active || handoff_pending || (kh02_world && !kh02_possessed_sora);
    }

    struct RuntimeItemDeliverySafetyDecision
    {
        RuntimeGrantSafetyStatus status;
        bool recover_cached_pawn{};
    };

    // Preserve the existing gate precedence (last blocked gate wins), while
    // preventing pawn recovery from overriding a blocked global gate.
    auto evaluate_runtime_item_delivery_gates(
            RuntimeGrantSafetyStatus snapshot,
            std::span<const RuntimeGrantSafetyStatus> gates,
            bool require_fresh_playable_context) -> RuntimeItemDeliverySafetyDecision;

    struct RuntimeItemDeliveryAttempt
    {
        bool applied{};
        bool duplicate{};
        bool unsafe_abort{};
        RuntimeItemApplyPayload payload;
        std::wstring failure_details;
        // Borrowed only during this synchronous game-thread delivery call.
        RC::Unreal::UObject* world_context{};
    };

    using RuntimeItemDeliveryApply =
            std::function<RuntimeItemDeliveryAttempt(const RuntimeItemGrantRequest&)>;

    auto execute_runtime_item_delivery_grant(
            const RuntimeItemGrantRequest& request,
            std::wstring_view action_kind,
            const std::optional<std::filesystem::path>& bridge_dir,
            RuntimeItemGrantPollState& state,
            const RuntimeItemDeliveryApply& apply) -> RuntimeItemDeliveryAttempt;

    struct RuntimeItemDeliveryBatchCallbacks
    {
        RuntimeItemDeliveryApply apply;
        std::function<void(const RuntimeItemGrantRequest&)> duplicate;
        std::function<void(const RuntimeItemGrantRequest&, const RuntimeItemDeliveryAttempt&)> applied;
        std::function<void(const RuntimeItemGrantRequest&, const RuntimeItemDeliveryAttempt&,
                           const RuntimeItemApplyBatchPayload&)> failed;
    };

    struct RuntimeItemDeliveryBatchResult
    {
        RuntimeItemApplyBatchPayload payload;
        bool unsafe_abort{};
        bool budget_exhausted{};
        bool new_progress{};
    };

    // Owns grant order, fresh-delivery dedupe, partial successes and unsafe
    // aborts. Engine calls, diagnostics and presentation remain in callbacks.
    auto execute_runtime_item_delivery_batch(
            const RuntimeItemGrantBatchRequest& request,
            const std::optional<std::filesystem::path>& bridge_dir,
            RuntimeItemGrantPollState& state,
            const RuntimeItemDeliveryBatchCallbacks& callbacks) -> RuntimeItemDeliveryBatchResult;

    auto retire_runtime_item_delivery_request(
            RuntimeItemGrantPollState& state,
            const std::function<void()>& consume_request) -> void;

    auto acknowledge_runtime_item_delivery(
            bool event_written,
            RuntimeItemGrantPollState& state,
            std::wstring_view request_key,
            std::chrono::steady_clock::time_point now,
            const std::function<void()>& consume_request) -> bool;

    class RuntimeItemDeliveryApplyScope
    {
    public:
        RuntimeItemDeliveryApplyScope(RuntimeItemGrantPollState& state, std::wstring key);
        ~RuntimeItemDeliveryApplyScope();
        RuntimeItemDeliveryApplyScope(const RuntimeItemDeliveryApplyScope&) = delete;
        auto operator=(const RuntimeItemDeliveryApplyScope&) -> RuntimeItemDeliveryApplyScope& = delete;

    private:
        RuntimeItemGrantPollState& state_;
    };
}
