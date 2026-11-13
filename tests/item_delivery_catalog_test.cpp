#include "kh3ap/features/grants/delivery/item_delivery_catalog.h"
#include "kh3ap/features/grants/delivery/item_delivery_queue.h"
#include "kh3ap/features/enemies/experience_multiplier_math.h"
#include <cassert>
#include <iostream>
using namespace kh3ap;
using namespace kh3ap::item_delivery;

auto reward(std::wstring token, std::wstring method = L"bp_tres_get_item") -> RewardTemplate
{ return {.grant = {.runtime_token = std::move(token), .grant_method = std::move(method)}}; }
int main()
{
    std::map<int64_t, ReceiptTemplate> receipts;
    auto level = reward(L"KH3AP_PROGRESSIVE_KEYBLADE_LEVEL", L"progressive_keyblade_level");
    level.progressive_base = level.grant.runtime_token;
    auto ability = reward(L"ETresAbilityKind::TEST", L"durable_sora_ability");
    ability.flag_rows = {L"FLAG_A", L"FLAG_B"};
    for (int64_t i = 0; i < 12; ++i) receipts[i] = {.primary = level, .supported = true};
    receipts[0].rewards = {ability};
    receipts[2].rewards = {level, ability}; // Both members count in receipt order.
    receipts[20] = {.primary = ability, .supported = true, .local = true, .inventory = true, .bridge_key = "Treasure:X"};
    receipts[21] = {.primary = ability, .rewards = {level}, .supported = true, .local = true, .level_up = true};
    receipts[22] = {.primary = reward(L"ETresBadStatusType::TRES_BADSTAT_BURN", L"direct_player_bad_status"), .supported = true};
    receipts[23] = {.primary = reward(L"KH3AP_TRAP::KING_MICKEY", L"king_mickey_trap"), .supported = true};
    receipts[24] = {.primary = reward(L"KH3AP_TRAP::BETRAYAL", L"betrayal_trap"), .supported = true};
    auto expanded = expand_catalog(receipts);
    assert(expanded.at(0)[0].runtime_token.ends_with(L"::1"));
    assert(expanded.at(2)[0].runtime_token.ends_with(L"::3"));
    assert(expanded.at(2)[1].runtime_token.ends_with(L"::4"));
    assert(expanded.at(3)[0].runtime_token.ends_with(L"::5"));
    assert(expanded.at(11)[0].runtime_token.ends_with(L"::10"));
    assert(expanded.at(0)[1].runtime_flag_name == L"FLAG_A");
    assert(expanded.at(2)[2].runtime_flag_name == L"FLAG_B");
    assert(expanded.at(21).size() == 1); // Level-up route metadata never duplicates reward.
    reconciliation::Receipts saved{.through = 1, .extra = {3, 5}};
    const auto plan = schedule_catalog(receipts, expanded, saved, {}, {"Treasure:X"});
    assert(plan.grants.front().index == 2 && plan.grants.front().runtime_token.ends_with(L"::3"));
    assert(std::find(plan.pending.begin(), plan.pending.end(), 20) == plan.pending.end()); // Inventory remains treasure-owned.
    assert(plan.traps.size() == 3 && plan.traps.front().index == 22);
    assert(plan.traps.back().index == 24 && plan.traps.back().grant_method == L"betrayal_trap");
    const auto forced = schedule_catalog(receipts, expanded, saved, {20}, {});
    assert(std::find(forced.pending.begin(), forced.pending.end(), 20) != forced.pending.end());
    receipts[20].always_on = true;
    assert(!delivery_eligible(receipts.at(20), 20, {20}, {"Treasure:X"}));
    receipts[20].always_on = false; receipts[20].inventory = false;
    assert(!delivery_eligible(receipts.at(20), 20, {}, {"Treasure:X"}));
    receipts[20].bridge_key = "VBonus:X";
    receipts[20].rewards = {reward(L"ITEM_POTION"), ability};
    assert(delivery_eligible(receipts.at(20), 20, {}, {"VBonus:X"}));
    expanded = expand_catalog(receipts);
    const auto repaired = schedule_catalog(receipts, expanded, saved, {}, {"VBonus:X"});
    assert(std::count_if(repaired.grants.begin(), repaired.grants.end(), [](const auto& g) { return g.index == 20; }) == 2);
    assert(std::none_of(repaired.grants.begin(), repaired.grants.end(), [](const auto& g) { return g.index == 20 && g.runtime_token == L"ITEM_POTION"; }));

    RuntimeItemGrantBatchRequest command{.request_id = L"manual", .action_kind = L"ap_replay_batch",
        .grants = {{.index = 2}, {.index = 2}}, .expand_catalog = true};
    auto manual = expand_command(command, expanded);
    assert(manual && manual->grants.size() == 3 && manual->grants[1].runtime_token.ends_with(L"::4"));
    assert(manual->grants[0].request_id == L"manual" && manual->grants[0].action_kind == L"ap_replay_batch");
    command.grants.push_back({.index = 100}); assert(!expand_command(command, expanded));

    auto shard = reward(L"KH3AP_ULTIMA_WEAPON_SHARD", L"ultima_weapon_shard"); shard.shard_required = 7;
    receipts[30] = {.primary = shard, .supported = true}; receipts[31] = receipts[30];
    expanded = expand_catalog(receipts);
    assert(expanded.at(30)[0].runtime_token == L"KH3AP_ULTIMA_WEAPON_SHARD::1::7");
    assert(expanded.at(31)[0].runtime_token == L"KH3AP_ULTIMA_WEAPON_SHARD::2::7");
    for (int64_t index = 32; index < 39; ++index) receipts[index] = receipts[30];
    expanded = expand_catalog(receipts);
    assert(expanded.at(36)[0].runtime_token == L"KH3AP_ULTIMA_WEAPON_SHARD::7::7");
    assert(expanded.at(37)[0].runtime_token == L"KH3AP_ULTIMA_WEAPON_SHARD::8::7");
    assert(expanded.at(38)[0].runtime_token == L"KH3AP_ULTIMA_WEAPON_SHARD::9::7");
    // Excess receipts still have a supported whole group and can be acknowledged.
    const auto excess = schedule_catalog(receipts, expanded, {.through = 36}, {}, {});
    assert(excess.grants.size() == 2 && excess.grants[0].index == 37 && excess.grants[1].index == 38);
    receipts[40] = {.primary = ability, .rewards = std::vector<RewardTemplate>(40, ability), .supported = true};
    bool rejected{}; try { (void)expand_catalog(receipts); } catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);

    for (const auto half_steps : runtime_experience_multiplier_half_steps)
    {
        assert(scale_experience_for_runtime_award(100, half_steps) == 50 * half_steps);
        assert(scale_experience_for_runtime_award(3, half_steps) == (3 * half_steps + 1) / 2);
        assert(scale_experience_for_runtime_award(0, half_steps) == 0);
    }
    assert(!scale_experience_for_runtime_award(-1, 2));
    assert(!scale_experience_for_runtime_award(100, 0));
    assert(!scale_experience_for_runtime_award(INT32_MAX, 40));
    assert(scale_experience_for_runtime_award(INT32_MAX, 2) == INT32_MAX);
    std::cout << "KH3 native catalog scheduling, progressive expansion and vanilla EXP tests passed\n";
}
