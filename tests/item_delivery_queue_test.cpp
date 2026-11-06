#include "kh3ap/features/grants/delivery/item_delivery_queue.h"
#include "kh3ap/features/kh02/rewards/kh02_reward_policy.h"
#include <cassert>
#include <iostream>
using namespace kh3ap;
using namespace kh3ap::item_delivery;

auto grant(int64_t index, std::wstring token = L"ITEM_POTION", std::wstring method = L"bp_tres_get_item")
    -> RuntimeItemGrantRequest
{
    return {.index = index, .runtime_token = std::move(token), .grant_method = std::move(method),
            .action_kind = L"runtime_item_grant_batch", .reapply_context_key = L"context1"};
}
auto payload(const RuntimeItemGrantRequest& g) -> RuntimeItemApplyPayload
{
    return {.index = g.index, .runtime_token = g.runtime_token, .grant_method = g.grant_method, .status = L"applied"};
}
int main()
{
    const auto space_allowed = [](auto group) {
        return std::all_of(group.begin(), group.end(), [](const auto& g) { return g.grant_method == L"bp_tres_get_item"; });
    };
    DeliveryQueue queue;
    RuntimeItemGrantBatchRequest request{.process_id = 123, .seed_name = L"seed", .slot = 1,
            .request_id = L"request1", .action_kind = L"runtime_item_grant_batch", .reapply_context_key = L"context1"};
    for (int64_t i = 0; i < 400; ++i) request.grants.push_back(grant(i));
    assert(queue.replace(request));
    auto batch = queue.select(false, space_allowed);
    assert(batch.grants.size() == 128 && batch.reapply_context_key == L"context1");
    std::vector<RuntimeItemApplyPayload> applied;
    for (const auto& g : batch.grants) applied.push_back(payload(g));
    for (size_t i = 0; i < batch.grants.size(); ++i) queue.remember(batch.grants[i], applied[i]);
    assert(queue.complete_receipts(batch).size() == 128);
    assert(!queue.completes_request(applied));
    queue.acknowledge(applied);
    assert(queue.select(false, space_allowed).grants.front().index == 128);
    // A refreshed mailbox containing an already emitted receipt must dedupe.
    assert(queue.replace(request));
    assert(queue.cached(request.grants.front()));
    assert(queue.select(false, space_allowed).grants.front().index == 128);

    queue.clear();
    request.grants = {grant(0, L"ABILITY", L"durable_sora_ability"), grant(1), grant(2)};
    assert(queue.replace(request));
    batch = queue.select(true, space_allowed);
    assert(batch.grants.size() == 2 && batch.grants.front().index == 1);
    request.grants = {grant(0), grant(0, L"SECONDARY", L"durable_sora_ability"), grant(1)};
    queue.clear(); assert(queue.replace(request));
    batch = queue.select(false, space_allowed);
    queue.remember(batch.grants[0], payload(batch.grants[0]));
    assert(queue.complete_receipts(batch).empty());
    assert(queue.cached(batch.grants[0]));
    assert(!queue.cached(batch.grants[1]));
    // Refresh after a partial group retains the successful primary action.
    request.request_id = L"request2";
    assert(queue.replace(request)); assert(queue.cached(batch.grants[0]));
    queue.remember(batch.grants[1], payload(batch.grants[1]));
    applied = queue.complete_receipts(batch);
    assert(applied.size() == 2 && applied[0].index == 0 && applied[1].index == 0);
    queue.acknowledge(applied);
    assert(queue.select(false, space_allowed).grants.front().index == 1);
    request.request_id = L"worker-snapshot-refresh";
    assert(queue.replace(request));
    assert(queue.select(false, space_allowed).grants.front().index == 1);
    request.grants[0].reapply_context_key = L"retry_context2";
    assert(queue.replace(request)); assert(!queue.cached(request.grants[0]));
    request.grants = {grant(0), grant(1, L"TRAP", L"direct_player_bad_status"), grant(2)};
    queue.clear();
    assert(queue.replace(request)); batch = queue.select(false, space_allowed);
    assert(batch.grants.size() == 1 && batch.grants.front().index == 0);
    queue.acknowledge({payload(batch.grants.front())});
    assert(queue.select(false, space_allowed).grants.size() == 1);
    request.grants.resize(DeliveryQueue::capacity + 1);
    assert(!queue.replace(request));
    request.grants.clear(); assert(!queue.replace(request));
    request.grants = {grant(0), grant(1), grant(0)}; assert(!queue.replace(request));
    auto primary = grant(0); primary.runtime_flag_name = L"flag1";
    auto secondary = primary; secondary.runtime_flag_name = L"flag2";
    request.grants = {primary, secondary}; queue.clear(); assert(queue.replace(request));
    batch = queue.select(false, space_allowed);
    queue.remember(primary, payload(primary)); assert(queue.complete_receipts(batch).empty());
    queue.remember(secondary, payload(secondary)); assert(queue.complete_receipts(batch).size() == 2);
    request.grants.assign(41, grant(0)); assert(!queue.replace(request));
    auto deathlink = grant(-9001, L"GAMEOVER", L"bp_tres_start_game_over");
    deathlink.request_id = L"deathlink-a";
    request.grants = {deathlink}; queue.clear(); assert(queue.replace(request));
    queue.remember(deathlink, payload(deathlink)); queue.acknowledge({payload(deathlink)});
    assert(queue.replace(request)); assert(queue.cached(deathlink)); // Retry the same command safely.
    deathlink.request_id = L"deathlink-b"; request.grants = {deathlink};
    assert(queue.replace(request)); assert(!queue.cached(deathlink)); // A second DeathLink must execute.
    auto replay = grant(0); replay.action_kind = L"ap_replay_batch"; replay.request_id = L"replay-a";
    request.action_kind = L"ap_replay_batch"; request.grants = {replay}; queue.clear();
    assert(queue.replace(request)); queue.remember(replay, payload(replay)); queue.acknowledge({payload(replay)});
    replay.request_id = L"replay-b"; request.grants = {replay};
    assert(queue.replace(request)); assert(!queue.cached(replay));
    // A Sora-only receipt at the front or middle must not starve Aqua's
    // inventory, party ownership, character ownership, or shard receipts.
    const auto aqua_allowed = [](auto group) {
        return std::all_of(group.begin(), group.end(), [](const auto& g) {
            return kh02rewards::supports_persistent_grant(g.grant_method);
        });
    };
    for (const auto* deferred : {L"keyblade_style", L"progressive_keyblade_level",
                                 L"king_mickey_trap", L"unknown_future_method"})
    {
        queue.clear();
        request.action_kind = L"runtime_item_grant_batch";
        request.grants = {grant(0, L"DEFER", deferred), grant(1),
            grant(2, L"PARTY", L"party_member_unlock"), grant(3, L"DEFER", deferred),
            grant(4, L"CHARACTER", L"character_unlock"), grant(5, L"SHARD", L"ultima_weapon_shard")};
        assert(queue.replace(request));
        batch = queue.select(true, aqua_allowed);
        assert(batch.grants.size() == 4);
        assert(batch.grants[0].index == 1 && batch.grants[1].index == 2);
        assert(batch.grants[2].index == 4 && batch.grants[3].index == 5);
        for (const auto& item : batch.grants) queue.remember(item, payload(item));
        auto completed = queue.complete_receipts(batch);
        assert(!queue.completes_request(completed));
        queue.acknowledge(completed);
        assert(queue.select(true, aqua_allowed).grants.empty());
        // A stale mailbox refresh must not redeliver those successes.
        assert(queue.replace(request));
        assert(queue.select(true, aqua_allowed).grants.empty());
        const auto sora = queue.select(false, aqua_allowed);
        assert(!sora.grants.empty() && sora.grants.front().index == 0);
    }
    queue.clear();
    request.grants = {grant(0), grant(0, L"STYLE", L"keyblade_style"), grant(1)};
    assert(queue.replace(request));
    batch = queue.select(true, aqua_allowed);
    assert(batch.grants.size() == 1 && batch.grants[0].index == 1);
    // Never acknowledge or partially grant the blocked primary/secondary group.
    assert(queue.complete_receipts(batch).empty());
    std::cout << "Native delivery queue tests passed\n";
}
