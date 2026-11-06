#include "kh3ap/features/kh02/combat/kh02_combo_master_visit.h"
#include <cassert>

int main()
{
    kh3ap::Kh02ComboMasterVisit visit;
    int grants = 0;
    int removals = 0;
    bool inventory_ready = true;
    auto ensure = [&]
    {
        ++grants;
        return inventory_ready;
    };
    auto remove = [&]
    {
        ++removals;
        return inventory_ready;
    };

    visit.update(false, false, ensure, remove);
    assert(grants == 0 && removals == 0);
    visit.update(true, false, ensure, remove);
    assert(grants == 1 && visit.active());
    // Room transitions and Sora/Aqua possession changes stay within one visit.
    visit.update(true, false, ensure, remove);
    visit.update(true, false, ensure, remove);
    assert(grants == 1 && removals == 0);
    inventory_ready = false;
    visit.update(false, false, ensure, remove);
    assert(removals == 1 && visit.active());
    inventory_ready = true;
    visit.update(false, false, ensure, remove);
    visit.update(false, false, ensure, remove);
    assert(removals == 2 && !visit.active());

    // A real AP receipt received during the visit prevents exit removal.
    visit.update(true, false, ensure, remove);
    visit.update(true, true, ensure, remove);
    visit.update(false, true, ensure, remove);
    assert(grants == 2 && removals == 2 && !visit.active());

    // Retry an unavailable native bank, without repeating successful grants.
    inventory_ready = false;
    visit.update(true, false, ensure, remove);
    assert(grants == 3 && !visit.entry_complete());
    inventory_ready = true;
    visit.update(true, false, ensure, remove);
    visit.update(true, false, ensure, remove);
    assert(grants == 4 && visit.entry_complete());
    visit.update(false, false, ensure, remove);
    assert(removals == 3 && !visit.active());
}
