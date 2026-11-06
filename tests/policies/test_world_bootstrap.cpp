#include "kh3ap/features/story/bootstrap/world_bootstrap.h"
#include "kh3ap/features/story/progression/vbonus_tracking_policy.h"

#include <cassert>
#include <cstdint>

using namespace kh3ap;

int main()
{
    static_assert(runtime_required_world_actors.empty());
    static_assert(runtime_goa_shop_level == 7000);
    static_assert(runtime_world_bootstrap_class_object_names.size() == 1);
    static_assert(runtime_world_bootstrap_class_object_names[0] == L"W_ProofUI_C");

    RuntimeWorldBootstrapProgress progress{};
    assert(!progress.observe_world(0));
    assert(progress.observe_world(0x1000));
    assert(!progress.shop_level_set);
    assert(!progress.proof_ui_added);

    progress.shop_level_set = true;
    progress.proof_ui_added = true;
    assert(!progress.observe_world(0x1000));
    assert(progress.shop_level_set);
    assert(progress.proof_ui_added);

    assert(progress.observe_world(0x2000));
    assert(!progress.shop_level_set);
    assert(!progress.proof_ui_added);

    progress.shop_level_set = true;
    progress.reset();
    assert(progress.world_identity == 0);
    assert(!progress.shop_level_set);
    assert(!progress.proof_ui_added);

    VBonusFlagEvents bonuses;
    bonuses.configure({10,20});
    bonuses.write(99,true); bonuses.write(10,false);
    assert(!bonuses.pending() && bonuses.events()==0);
    bonuses.write(10,true); bonuses.write(10,true); bonuses.write(20,true);
    assert(bonuses.pending() && bonuses.events()==3);
    assert((bonuses.take()==std::set<uint64_t>{10,20}));
    assert(!bonuses.pending() && bonuses.take().empty());
    bonuses.write(10,true);
    bonuses.configure({30}); // scope replacement discards queued old names
    assert(!bonuses.pending());
    bonuses.write(10,true); assert(!bonuses.pending());
    bonuses.write(30,true); assert((bonuses.take()==std::set<uint64_t>{30}));
    return 0;
}
