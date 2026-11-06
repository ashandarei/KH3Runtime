#include "kh3ap/features/grants/delivery/runtime_loot_policy.h"
#include "kh3ap/bridge/checks/check_policy.h"
#include <cassert>
#include <iostream>
using namespace kh3ap;
int main()
{
    checks::Policy checks;
    checks.locations["VBonus:Vbonus_038"] = {"VBonus:Vbonus_038", "VBonus", "Vbonus_038"};
    checks.locations["Event:EVENT_HEARTBINDER_003"] = {"Event:EVENT_HEARTBINDER_003", "Event", "EVENT_HEARTBINDER_003"};
    checks.observe("VBonus", "Vbonus_037");
    assert(checks.resolve().size() == 2);
    checks::Policy incomplete;
    incomplete.locations = checks.locations;
    incomplete.observe("Event", "EVENT_HEARTBINDER_003");
    incomplete.observe("VBonus", "Vbonus_037");
    assert(incomplete.resolve() == std::set<std::string>{"Event:EVENT_HEARTBINDER_003"});
    assert(checks::Policy::encounter("BP_TresGetItem", "/Game/Levels/tt/tt_01/umap/tt_01_enemy_06").empty());
    const std::string riku_key = "CharacterUnlock:Riku";
    for (int missing = 1; missing <= 13; ++missing)
    {
        checks::Policy data;
        data.locations[riku_key] = {riku_key, "CharacterUnlock", "Riku"};
        const auto event_id = [](int index)
        {
            return "EVENT_DATAB_" + std::string(index < 10 ? "00" : "0") + std::to_string(index);
        };
        // Even clearing Data Xehanort first cannot skip another missing fight.
        for (int index = 13; index >= 1; --index)
        {
            if (index != missing)
            {
                data.observe("Event", event_id(index));
            }
            assert(data.resolve().empty());
        }
        data.observe("Event", event_id(missing));
        assert(data.resolve() == std::set<std::string>{riku_key});
        data.observe("Event", event_id(missing));
        assert(data.resolve().empty());
        // Persisted observations re-earn the milestone on cold recovery.
        checks::Policy recovered_data;
        recovered_data.locations = data.locations;
        recovered_data.observed = data.observed;
        assert(recovered_data.resolve() == std::set<std::string>{riku_key});
    }
    runtime_loot::Policy policy;
    runtime_loot::Location foreign{.name = "Boss", .item = "Missile Tank", .recipient = "Samus", .foreign = true, .priority = true};
    assert(runtime_loot::foreign_message(foreign) == "AP: Found Samus's Missile Tank (p)");
    foreign.priority = false; foreign.recipient.clear();
    assert(runtime_loot::foreign_message(foreign) == "AP: Found Missile Tank");
    foreign.foreign = false;
    assert(runtime_loot::foreign_message(foreign).empty());
    runtime_loot::Observation event{.key = "VBonus:Vbonus_016", .type = "vbonus", .epoch = 1};
    assert(policy.observe(event, foreign) == runtime_loot::Decision::fresh);
    assert(policy.observe(event, foreign) == runtime_loot::Decision::duplicate);
    // Different callback sources and repeated actor observations in the same
    // epoch cannot reopen a receipt, no matter how long the encounter lasts.
    event.source = "another native hook";
    assert(policy.observe(event, foreign) == runtime_loot::Decision::duplicate);
    event.epoch = 2;
    assert(policy.observe(event, foreign) == runtime_loot::Decision::repeated);
    assert(policy.observe(event, foreign) == runtime_loot::Decision::duplicate);
    event.epoch = 3;
    assert(policy.observe(event, foreign) == runtime_loot::Decision::repeated);
    event.epoch = 4; foreign.always_on = true;
    assert(policy.observe(event, foreign) == runtime_loot::Decision::duplicate);
    runtime_loot::Policy recovered;
    recovered.checked.insert(event.key);
    foreign.always_on = false;
    assert(recovered.observe(event, foreign) == runtime_loot::Decision::repeated);
    assert(recovered.observe(event, foreign) == runtime_loot::Decision::duplicate);
    event.key = "Event:UNSAFE"; event.type = "event";
    event.source = "UTresGameBlueprintLibrary::BP_TresGetItem"; event.resolution = "item_direct";
    event.mapping = "resolved from slot_data.json event vanilla_item";
    assert(policy.observe(event, foreign) == runtime_loot::Decision::ignored);
    assert(!policy.checked.contains(event.key));
    item_delivery::ReceiptTemplate receipt{.primary = {.grant = {.grant_method = L"durable_sora_vbonus"}},
        .supported = true, .local = true, .bridge_key = "VBonus:Vbonus_016"};
    assert(item_delivery::repairable(receipt));
    receipt.inventory = true; assert(!item_delivery::repairable(receipt));
    receipt.inventory = false; receipt.always_on = true; assert(!item_delivery::repairable(receipt));
    receipt.always_on = false; receipt.level_up = true; assert(!item_delivery::repairable(receipt));
    receipt.level_up = false; receipt.primary.grant.grant_method = L"direct_player_bad_status";
    assert(!item_delivery::repairable(receipt));
    std::cout << "Native loot event deduplication, retry epochs, unsafe tokens and repair eligibility tests passed\n";
}
