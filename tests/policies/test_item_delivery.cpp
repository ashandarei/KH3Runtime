#include "kh3ap/features/grants/delivery/item_delivery_flow.h"
#include "kh3ap/features/grants/grant_tokens.h"

#include <array>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace kh3ap;
using namespace std::chrono_literals;

namespace
{
    const auto now = std::chrono::steady_clock::time_point{10s};
    const std::optional<std::filesystem::path> bridge{L"test-bridge/seed-player"};

    auto grant(int64_t index, std::wstring method = L"bp_tres_get_item") -> RuntimeItemGrantRequest
    {
        RuntimeItemGrantRequest result;
        result.index = index;
        result.request_id = L"request-1";
        result.item_name = L"Potion";
        result.runtime_token = L"ITEM_POTION";
        result.grant_method = std::move(method);
        result.action_kind = L"runtime_item_grant_batch";
        result.reapply_context_key = L"loaded-slot-1";
        return result;
    }

    auto applied(const RuntimeItemGrantRequest& request) -> RuntimeItemDeliveryAttempt
    {
        RuntimeItemDeliveryAttempt result{.applied = true};
        result.payload = RuntimeGrantTokens::build_apply_payload(request);
        return result;
    }

    auto batch(std::vector<RuntimeItemGrantRequest> grants) -> RuntimeItemGrantBatchRequest
    {
        return {.request_id = L"batch-1", .action_kind = L"runtime_item_grant_batch", .grants = std::move(grants)};
    }

    struct Recorder
    {
        std::vector<int64_t> calls;
        std::vector<int64_t> observed;
        std::vector<int64_t> duplicates;
        std::vector<int64_t> failures;
        std::vector<bool> visibility;
        std::vector<std::wstring> order;
        int64_t fail_index{-1};
        bool unsafe{};

        auto callbacks() -> RuntimeItemDeliveryBatchCallbacks
        {
            return {
                    .apply = [&](const auto& request) {
                        calls.push_back(request.index);
                        visibility.push_back(request.show_info);
                        order.push_back(L"apply:" + std::to_wstring(request.index));
                        if (request.index == fail_index)
                        {
                            return RuntimeItemDeliveryAttempt{
                                    .unsafe_abort = unsafe, .failure_details = L"context_changed"};
                        }
                        return applied(request);
                    },
                    .duplicate = [&](const auto& request) { duplicates.push_back(request.index); },
                    .applied = [&](const auto& request, const auto&) {
                        observed.push_back(request.index);
                        order.push_back(L"observe:" + std::to_wstring(request.index));
                    },
                    .failed = [&](const auto& request, const auto&, const auto& progress) {
                        failures.push_back(request.index);
                        assert(progress.status == L"partial");
                        assert(progress.error == L"context_changed");
                    },
            };
        }
    };

    void request_permissions_do_not_leak_between_batch_members()
    {
        const auto game_over = grant(1, L"bp_tres_start_game_over");
        auto burn = grant(2, L"direct_player_bad_status");
        burn.runtime_token = L"ETresBadStatusType::TRES_BADSTAT_BURN";
        auto sneeze = burn;
        sneeze.runtime_token = L"ETresBadStatusType::TRES_BADSTAT_SNEEZE";
        auto stun = burn;
        stun.runtime_token = L"ETresBadStatusType::TRES_BADSTAT_STUN";
        const auto inspect = [](std::vector<RuntimeItemGrantRequest> grants) {
            return runtime_item_delivery_policy(grants, L"runtime_item_grant_batch", true);
        };
        assert(inspect({game_over, game_over}).allow_special_battle_game_over);
        assert(!inspect({game_over, grant(3)}).allow_special_battle_game_over);
        assert(inspect({burn, sneeze}).allow_special_battle_bad_status_trap);
        assert(inspect({burn, sneeze}).supports_alternate_bad_status);
        assert(!inspect({burn, grant(3)}).supports_alternate_bad_status);
        assert(!inspect({burn, stun}).supports_alternate_bad_status);
        assert(!inspect({}).supports_alternate_bad_status);
        auto mickey = grant(4, L"king_mickey_trap");
        mickey.runtime_token = L"KH3AP_TRAP::KING_MICKEY";
        assert(!inspect({mickey}).supports_alternate_bad_status);
        assert(!inspect({burn, mickey}).supports_alternate_bad_status);
        assert(!inspect({burn, stun}).allow_special_battle_bad_status_trap);
        assert(inspect({burn, grant(3)}).contains_bad_status_trap);
        const auto mixed = inspect({game_over, burn});
        assert(!mixed.allow_special_battle_game_over && !mixed.allow_special_battle_bad_status_trap);
        assert(inspect({grant(1), grant(2, L"bp_tres_get_item_add_munny"),
                        grant(3, L"ultima_weapon_shard")}).supports_space_inventory);
        for (const auto* method : {L"durable_sora_ability", L"durable_sora_vbonus", L"direct_player_bad_status",
                                   L"bp_tres_start_game_over", L"unknown_method"})
        {
            const auto request = grant(4, method);
            assert(!inspect({grant(1), request}).supports_space_inventory);
            assert(!runtime_item_delivery_policy(std::span{&request, size_t{1}}, request.action_kind, false)
                            .supports_space_inventory);
        }
        for (const auto* token : {L"KEY_ITEM06", L"KEY_ITEM07", L"KEY_ITEM08", L"KEY_ITEM09", L"KEY_ITEM10"})
        {
            auto heartbinder = grant(5);
            heartbinder.runtime_token = token;
            assert(!inspect({heartbinder}).supports_space_inventory);
            assert(!inspect({grant(1), heartbinder}).supports_space_inventory);
            assert(!runtime_item_delivery_policy(std::span{&heartbinder, size_t{1}},
                    L"save_load_replay", false).supports_space_inventory);
        }
        assert(inspect({grant(1)}).require_fresh_playable_context);
        assert(!inspect({}).allow_special_battle_game_over);
        assert(!inspect({}).allow_special_battle_bad_status_trap);
        auto manual_game_over = game_over;
        manual_game_over.action_kind = L"manual_game_over";
        manual_game_over.runtime_token = L"GAMEOVER_KIND_NORMAL";
        assert(inspect({manual_game_over}).supports_alternate_game_over);
        assert(!inspect({game_over}).supports_alternate_game_over);
        assert(!inspect({}).supports_alternate_game_over);
        assert(!inspect({manual_game_over, game_over}).supports_alternate_game_over);
        assert(!inspect({manual_game_over, burn}).supports_alternate_game_over);
        assert(!inspect({manual_game_over, grant(3)}).supports_alternate_game_over);
        manual_game_over.runtime_token = L"unknown";
        assert(!inspect({manual_game_over}).supports_alternate_game_over);
    }

    void kh02_sora_uses_normal_delivery_without_relaxing_alternate_handoffs()
    {
        assert(!runtime_item_delivery_requires_persistent_context(false, false, false, false));
        assert(!runtime_item_delivery_requires_persistent_context(false, false, true, true));
        // Aqua or unresolved possession in a KH0.2 world must not use cached Sora.
        assert(runtime_item_delivery_requires_persistent_context(false, false, true, false));
        for (const bool kh02_world : {false, true})
        {
            for (const bool possessed_sora : {false, true})
            {
                assert(runtime_item_delivery_requires_persistent_context(true, false, kh02_world, possessed_sora));
                assert(runtime_item_delivery_requires_persistent_context(false, true, kh02_world, possessed_sora));
                assert(runtime_item_delivery_requires_persistent_context(true, true, kh02_world, possessed_sora));
            }
        }
    }

    void aqua_permissions_require_every_grant_to_target_persistent_records()
    {
        const auto inspect = [](std::vector<RuntimeItemGrantRequest> grants) {
            return runtime_item_delivery_policy(grants, L"runtime_item_grant_batch", true);
        };
        assert(!inspect({}).supports_kh02_persistent_rewards);
        for (const auto* method : {L"bp_tres_get_item", L"bp_tres_get_item_add_munny", L"durable_sora_vbonus", L"durable_sora_ability", L"controller_unlock", L"party_member_unlock", L"character_unlock", L"ultima_weapon_shard"})
        {
            auto request = grant(7, method);
            request.runtime_token = L"KEY_ITEM06";
            assert(inspect({request}).supports_kh02_persistent_rewards);
            assert(inspect({grant(1), request}).supports_kh02_persistent_rewards);
        }
        for (const auto* method : {L"bp_tres_start_game_over", L"direct_player_bad_status", L"king_mickey_trap", L"progressive_keyblade_level", L"keyblade_style", L"unknown"})
        {
            assert(!inspect({grant(1), grant(2, method)}).supports_kh02_persistent_rewards);
        }
    }

    void munny_pouches_do_not_block_mixed_ally_rewards()
    {
        auto small_pouch = grant(1, L"bp_tres_get_item_add_munny");
        small_pouch.runtime_token = L"KEY_ITEM18";
        auto large_pouch = grant(2, L"bp_tres_get_item_add_munny");
        large_pouch.runtime_token = L"KEY_ITEM19";
        auto ability = grant(3, L"durable_sora_ability");
        ability.runtime_token = L"ETresAbilityKind::AIR_RECOVERY";
        const std::vector rewards{small_pouch, large_pouch, ability, grant(4)};
        const auto policy = runtime_item_delivery_policy(rewards, L"runtime_item_grant_batch", true);
        assert(policy.supports_kh02_persistent_rewards);
        assert(!policy.allow_special_battle_game_over);
        assert(!policy.allow_special_battle_bad_status_trap);
    }

    void space_inventory_does_not_starve_behind_priority_deathlink()
    {
        assert(runtime_item_delivery_uses_priority(true, false, true));
        assert(!runtime_item_delivery_uses_priority(true, true, true));
        assert(runtime_item_delivery_uses_priority(true, true, false));
        assert(!runtime_item_delivery_uses_priority(false, true, true));
        assert(!runtime_item_delivery_uses_priority(false, false, false));
    }

    void blocked_gates_prevent_pawn_recovery_and_keep_reason_precedence()
    {
        const RuntimeGrantSafetyStatus ready{true, L"ready", {}};
        const RuntimeGrantSafetyStatus map_wait{false, L"unsafe_map", L"travel"};
        const RuntimeGrantSafetyStatus state_wait{false, L"unsafe_state", L"cinematic"};
        const std::array gates{map_wait, ready, state_wait};
        const auto blocked = evaluate_runtime_item_delivery_gates(ready, gates, true);
        assert(!blocked.status.ready && !blocked.recover_cached_pawn);
        assert(blocked.status.reason == L"unsafe_state" && blocked.status.details == L"cinematic");
        const std::array ready_gates{ready, ready};
        assert(evaluate_runtime_item_delivery_gates(map_wait, ready_gates, false).recover_cached_pawn);
        assert(evaluate_runtime_item_delivery_gates(ready, ready_gates, true).recover_cached_pawn);
        assert(!evaluate_runtime_item_delivery_gates(ready, ready_gates, false).recover_cached_pawn);
    }

    void failed_single_acknowledgement_does_not_regrant_or_consume()
    {
        RuntimeItemGrantPollState state;
        state.arm(now);
        const auto request = grant(7);
        const auto key = RuntimeGrantTokens::make_request_key(request);
        int applied_count = 0, consumed = 0;
        const auto apply = [&](const auto& item) { ++applied_count; return applied(item); };
        const auto first = execute_runtime_item_delivery_grant(request, request.action_kind, bridge, state, apply);
        assert(first.applied && !first.duplicate);
        assert(!acknowledge_runtime_item_delivery(false, state, key, now, [&] { ++consumed; }));
        assert(consumed == 0 && state.is_armed() && !state.is_duplicate(key));
        const auto retry = execute_runtime_item_delivery_grant(request, request.action_kind, bridge, state, apply);
        assert(retry.applied && retry.duplicate && applied_count == 1);
        assert(retry.payload.error == L"duplicate_fresh_delivery_skipped" && retry.payload.index == 7);
        assert(acknowledge_runtime_item_delivery(true, state, key, now, [&] {
            assert(state.is_duplicate(key)); // Ack state precedes file consumption.
            assert(state.is_armed());       // Disarming follows file consumption.
            ++consumed;
        }));
        assert(consumed == 1 && !state.is_armed() && state.is_duplicate(key));
        retire_runtime_item_delivery_request(state, [&] { ++consumed; });
        assert(applied_count == 1 && consumed == 2);
    }

    void fresh_identity_is_scoped_to_delivery_context_and_reward()
    {
        RuntimeItemGrantPollState state;
        int count = 0;
        const auto apply = [&](const auto& item) { ++count; return applied(item); };
        auto request = grant(1);
        auto send = [&](const auto& dir) {
            return execute_runtime_item_delivery_grant(request, request.action_kind, dir, state, apply);
        };
        assert(!send(bridge).duplicate);
        request.request_id = L"resent-request";
        assert(send(bridge).duplicate); // Transport identity is not item identity.
        request.reapply_context_key = L"loaded-slot-2";
        assert(!send(bridge).duplicate);
        assert(!send(std::optional<std::filesystem::path>{L"test-bridge/other-seed"}).duplicate);
        request.runtime_token = L"ITEM_ETHER";
        assert(!send(bridge).duplicate);
        request.runtime_flag_name = L"flag-1";
        assert(!send(bridge).duplicate);
        request.grant_method = L"bp_tres_get_item_add_munny";
        assert(!send(bridge).duplicate);
        request.index = 2;
        assert(!send(bridge).duplicate);
        request.index = -1;
        assert(!send(bridge).duplicate && !send(bridge).duplicate);
        assert(count == 9);
    }

    void partial_batches_continue_and_retry_only_failed_fresh_items()
    {
        RuntimeItemGrantPollState state;
        Recorder recorder;
        recorder.fail_index = 2;
        const auto request = batch({grant(1), grant(2), grant(3)});
        const auto first = execute_runtime_item_delivery_batch(request, bridge, state, recorder.callbacks());
        assert((recorder.calls == std::vector<int64_t>{1,2,3}));
        assert((recorder.observed == std::vector<int64_t>{1,3}));
        assert((recorder.order == std::vector<std::wstring>{L"apply:1",L"observe:1",L"apply:2",L"apply:3",L"observe:3"}));
        assert(first.payload.status == L"partial" && !first.unsafe_abort);
        assert(first.payload.applied.size() == 2);
        assert(first.payload.applied[0].index == 1 && first.payload.applied[1].index == 3);
        assert(!acknowledge_runtime_item_delivery(false, state, L"batch-1", now, [] { assert(false); }));
        recorder.fail_index = -1;
        const auto retry = execute_runtime_item_delivery_batch(request, bridge, state, recorder.callbacks());
        assert((recorder.calls == std::vector<int64_t>{1,2,3,2}));
        assert((recorder.duplicates == std::vector<int64_t>{1,3}));
        assert(retry.payload.status == L"applied" && retry.payload.applied.size() == 3);
        for (size_t i = 0; i != 3; ++i) assert(retry.payload.applied[i].index == static_cast<int64_t>(i + 1));
        assert(retry.payload.applied[0].error == L"duplicate_fresh_delivery_skipped");
        assert(retry.payload.applied[1].error.empty());
        assert(acknowledge_runtime_item_delivery(true, state, L"batch-1", now, [] {}));
    }

    void unsafe_batch_abort_stops_before_later_items_and_preserves_partial_ack()
    {
        for (const auto failing_index : {int64_t{1}, int64_t{2}})
        {
            RuntimeItemGrantPollState state;
            Recorder recorder;
            recorder.fail_index = failing_index;
            recorder.unsafe = true;
            const auto request = batch({grant(1),grant(2),grant(3)});
            const auto result = execute_runtime_item_delivery_batch(request, bridge, state, recorder.callbacks());
            assert(result.unsafe_abort && result.payload.status == L"partial");
            assert(recorder.calls.size() == static_cast<size_t>(failing_index));
            assert(result.payload.applied.size() == static_cast<size_t>(failing_index - 1));
            int consumed = 0;
            const bool wrote_nonempty_ack = !result.payload.applied.empty();
            assert(acknowledge_runtime_item_delivery(wrote_nonempty_ack, state, L"partial", now,
                    [&] { ++consumed; }) == wrote_nonempty_ack);
            assert(consumed == (failing_index == 2 ? 1 : 0));
        }
    }

    void replay_grants_are_not_suppressed_by_fresh_delivery_history()
    {
        RuntimeItemGrantPollState state;
        Recorder recorder;
        auto request = batch({grant(1),grant(2)});
        execute_runtime_item_delivery_batch(request, bridge, state, recorder.callbacks());
        request.action_kind = L"save_load_replay_batch";
        // Deliberately retain per-item fresh metadata: the enclosing replay
        // action must still disable dedupe.
        execute_runtime_item_delivery_batch(request, bridge, state, recorder.callbacks());
        execute_runtime_item_delivery_batch(request, bridge, state, recorder.callbacks());
        assert(recorder.calls.size() == 6 && recorder.duplicates.empty());
        const auto single = grant(1);
        int calls = 0;
        const auto result = execute_runtime_item_delivery_grant(single, L"save_load_replay", bridge, state,
                [&](const auto& item) { ++calls; return applied(item); });
        assert(result.applied && !result.duplicate && calls == 1);
    }

    void ability_bundling_changes_only_engine_presentation()
    {
        RuntimeItemGrantPollState state;
        Recorder recorder;
        auto a = grant(1, L"durable_sora_ability");
        a.runtime_token = L"ETresAbilityKind::TEST_A";
        auto b = grant(2, L"durable_sora_ability");
        b.runtime_token = L"ETresAbilityKind::TEST_B";
        const auto request = batch({a,b});
        auto callbacks = recorder.callbacks();
        callbacks.applied = [&](const auto& original, const auto&) { assert(original.show_info); };
        execute_runtime_item_delivery_batch(request, bridge, state, callbacks);
        assert((recorder.visibility == std::vector<bool>{false,false}));
        assert(request.grants[0].show_info && request.grants[1].show_info);
        execute_runtime_item_delivery_batch(request, bridge, state, callbacks);
        assert(recorder.calls.size() == 2 && recorder.duplicates.size() == 2);
        const auto mixed = batch({a,grant(3)});
        assert(!runtime_item_delivery_policy(mixed.grants, mixed.action_kind, true).bundle_visible_durable_ability_hud);
    }

    void apply_scope_releases_reentrancy_state_on_exception()
    {
        RuntimeItemGrantPollState state;
        try
        {
            const RuntimeItemDeliveryApplyScope scope{state,L"batch"};
            assert(state.apply_in_progress() && state.apply_in_progress_key() == L"batch");
            throw std::runtime_error("engine adapter failure");
        }
        catch (const std::runtime_error&) {}
        assert(!state.apply_in_progress() && state.apply_in_progress_key().empty());
    }
}

int main()
{
    request_permissions_do_not_leak_between_batch_members();
    kh02_sora_uses_normal_delivery_without_relaxing_alternate_handoffs();
    aqua_permissions_require_every_grant_to_target_persistent_records();
    munny_pouches_do_not_block_mixed_ally_rewards();
    space_inventory_does_not_starve_behind_priority_deathlink();
    blocked_gates_prevent_pawn_recovery_and_keep_reason_precedence();
    failed_single_acknowledgement_does_not_regrant_or_consume();
    fresh_identity_is_scoped_to_delivery_context_and_reward();
    partial_batches_continue_and_retry_only_failed_fresh_items();
    unsafe_batch_abort_stops_before_later_items_and_preserves_partial_ack();
    replay_grants_are_not_suppressed_by_fresh_delivery_history();
    ability_bundling_changes_only_engine_presentation();
    apply_scope_releases_reentrancy_state_on_exception();
    std::cout << "10 item delivery scenarios passed\n";
}
