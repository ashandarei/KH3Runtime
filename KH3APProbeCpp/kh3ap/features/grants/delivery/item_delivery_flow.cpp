#include "kh3ap/features/grants/trap_catalog.h"
#include "kh3ap/features/grants/game_over_policy.h"
#include "kh3ap/features/grants/delivery/item_delivery_flow.h"

#include "kh3ap/features/grants/grant_tokens.h"
#include "kh3ap/features/world/space/space_policy.h"

#include "kh3ap/features/kh02/rewards/kh02_reward_policy.h"

#include <algorithm>
#include <format>
#include <utility>

namespace kh3ap
{
    namespace
    {
        constexpr auto item_delivery_frame_budget = std::chrono::milliseconds(1);

        auto is_special_battle_bad_status_trap(const RuntimeItemGrantRequest& request) -> bool
        {
            return find_runtime_trap_spec(request.runtime_token, request.grant_method) != nullptr;
        }

        auto fresh_delivery_key(
                const std::optional<std::filesystem::path>& bridge_dir,
                const RuntimeItemGrantRequest& request) -> std::wstring
        {
            if (request.action_kind != L"runtime_item_grant_batch" || request.index < 0)
            {
                return {};
            }
            return std::format(
                    L"{}|fresh_ap_delivery|{}|{}|{}|{}|{}",
                    bridge_dir ? bridge_dir->wstring() : std::wstring{L"<no-bridge>"},
                    request.reapply_context_key.empty() ? std::wstring{L"<no-delivery-context>"} : request.reapply_context_key,
                    request.index,
                    request.runtime_token.empty() ? std::wstring{L"<no-token>"} : request.runtime_token,
                    request.runtime_flag_name.empty() ? std::wstring{L"<no-flag>"} : request.runtime_flag_name,
                    request.grant_method.empty() ? std::wstring{L"<no-method>"} : request.grant_method);
        }
    }

    auto runtime_item_delivery_policy(
            std::span<const RuntimeItemGrantRequest> grants,
            std::wstring_view action_kind,
            bool is_batch) -> RuntimeItemDeliveryPolicy
    {
        return {
                .allow_special_battle_game_over = !grants.empty() && std::all_of(
                        grants.begin(), grants.end(), [](const auto& grant) {
                            return grant.grant_method == L"bp_tres_start_game_over";
                        }),
                .allow_special_battle_bad_status_trap = !grants.empty() && std::all_of(
                        grants.begin(), grants.end(), is_special_battle_bad_status_trap),
                .contains_bad_status_trap = std::any_of(
                        grants.begin(), grants.end(), is_special_battle_bad_status_trap),
                .require_fresh_playable_context = !grants.empty() ||
                        action_kind == (is_batch ? L"save_load_replay_batch" : L"save_load_replay"),
                .supports_space_inventory = std::all_of(
                        grants.begin(), grants.end(), [](const auto& grant) {
                            return space::supports_inventory_grant(grant.grant_method, grant.runtime_token);
                        }),
                .supports_kh02_persistent_rewards = !grants.empty() && std::all_of(
                        grants.begin(), grants.end(), [](const auto& grant) {
                            return kh02rewards::supports_persistent_grant(grant.grant_method);
                        }),
                .supports_alternate_bad_status = !grants.empty() && std::all_of(
                        grants.begin(), grants.end(), [](const auto& grant) {
                            return supports_alternate_bad_status(grant.runtime_token, grant.grant_method);
                        }),
                .supports_alternate_game_over = !grants.empty() && std::all_of(
                        grants.begin(), grants.end(), [](const auto& grant) {
                            return supports_alternate_game_over(grant.action_kind, grant.runtime_token, grant.grant_method);
                        }),
                .bundle_visible_durable_ability_hud = is_batch && grants.size() >= 2 && std::all_of(
                        grants.begin(), grants.end(), [](const auto& grant) {
                            return grant.show_info && grant.grant_method == L"durable_sora_ability" &&
                                   grant.runtime_token.starts_with(L"ETresAbilityKind::") &&
                                   !grant.runtime_token.ends_with(L"::NONE");
                        }),
        };
    }

    auto evaluate_runtime_item_delivery_gates(
            RuntimeGrantSafetyStatus snapshot,
            std::span<const RuntimeGrantSafetyStatus> gates,
            bool require_fresh_playable_context) -> RuntimeItemDeliverySafetyDecision
    {
        bool blocked = false;
        for (const auto& gate : gates)
        {
            if (!gate.ready)
            {
                snapshot = gate;
                blocked = true;
            }
        }
        const bool recover = !blocked && (require_fresh_playable_context || !snapshot.ready);
        return {std::move(snapshot), recover};
    }

    auto execute_runtime_item_delivery_grant(
            const RuntimeItemGrantRequest& request,
            std::wstring_view action_kind,
            const std::optional<std::filesystem::path>& bridge_dir,
            RuntimeItemGrantPollState& state,
            const RuntimeItemDeliveryApply& apply) -> RuntimeItemDeliveryAttempt
    {
        const bool use_dedupe = action_kind == L"runtime_item_grant_batch";
        const auto key = fresh_delivery_key(bridge_dir, request);
        if (use_dedupe && state.has_fresh_delivery_key(key))
        {
            RuntimeItemDeliveryAttempt duplicate{.applied = true, .duplicate = true};
            duplicate.payload = RuntimeGrantTokens::build_apply_payload(request);
            duplicate.payload.error = L"duplicate_fresh_delivery_skipped";
            return duplicate;
        }

        auto result = apply(request);
        if (result.applied && use_dedupe)
        {
            // Record the successful grant before acknowledgement I/O. If that
            // write fails, retrying fresh delivery must not grant the item again.
            state.remember_fresh_delivery_key(key);
        }
        return result;
    }

    auto execute_runtime_item_delivery_batch(
            const RuntimeItemGrantBatchRequest& request,
            const std::optional<std::filesystem::path>& bridge_dir,
            RuntimeItemGrantPollState& state,
            const RuntimeItemDeliveryBatchCallbacks& callbacks) -> RuntimeItemDeliveryBatchResult
    {
        RuntimeItemDeliveryBatchResult result{
                .payload = {.request_id = request.request_id,
                            .action_kind = request.action_kind,
                            .status = L"applied"}};
        const auto policy = runtime_item_delivery_policy(request.grants, request.action_kind, true);
        const RuntimeItemDeliveryApply apply = [&](const RuntimeItemGrantRequest& grant) {
            // Prepare the engine request only after dedupe, retaining the
            // original presentation metadata for successful-grant observers.
            auto grant_for_apply = grant;
            if (policy.bundle_visible_durable_ability_hud)
            {
                grant_for_apply.show_info = false;
            }
            return callbacks.apply(grant_for_apply);
        };
        const auto frame_started = std::chrono::steady_clock::now();
        for (size_t position = 0; position < request.grants.size(); ++position)
        {
            const auto& grant = request.grants[position];
            // Ordinary primary/secondary groups stay in one frame, preserving
            // save semantics. A costly engine call or group may exceed the
            // soft budget, but stops further receipt groups this frame.
            if (position && grant.index != request.grants[position - 1].index &&
                std::chrono::steady_clock::now() - frame_started >= item_delivery_frame_budget)
            {
                result.budget_exhausted = true;
                break;
            }
            const auto attempt = execute_runtime_item_delivery_grant(
                    grant, request.action_kind, bridge_dir, state, apply);
            if (attempt.duplicate)
            {
                result.payload.applied.push_back(attempt.payload);
                callbacks.duplicate(grant);
                continue;
            }
            if (!attempt.applied)
            {
                result.payload.status = L"partial";
                result.payload.error = attempt.failure_details.empty() ? L"grant_failed" : attempt.failure_details;
                callbacks.failed(grant, attempt, result.payload);
                if (attempt.unsafe_abort)
                {
                    result.unsafe_abort = true;
                    break;
                }
                continue;
            }
            result.payload.applied.push_back(attempt.payload);
            result.new_progress = true;
            callbacks.applied(grant, attempt);
        }
        return result;
    }

    auto retire_runtime_item_delivery_request(
            RuntimeItemGrantPollState& state,
            const std::function<void()>& consume_request) -> void
    {
        consume_request();
        state.disarm();
    }

    auto acknowledge_runtime_item_delivery(
            bool event_written,
            RuntimeItemGrantPollState& state,
            std::wstring_view request_key,
            std::chrono::steady_clock::time_point now,
            const std::function<void()>& consume_request) -> bool
    {
        if (!event_written)
        {
            return false;
        }
        state.mark_applied(std::wstring{request_key}, now);
        retire_runtime_item_delivery_request(state, consume_request);
        return true;
    }

    RuntimeItemDeliveryApplyScope::RuntimeItemDeliveryApplyScope(
            RuntimeItemGrantPollState& state, std::wstring key) : state_(state)
    {
        state_.begin_apply(std::move(key));
    }

    RuntimeItemDeliveryApplyScope::~RuntimeItemDeliveryApplyScope()
    {
        state_.end_apply();
    }
}
