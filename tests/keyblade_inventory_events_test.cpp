#include "kh3ap/core/events/keyblade_inventory_events.h"
#include <cassert>

int main()
{
    using namespace kh3ap;
    const auto initial=read_runtime_keyblade_inventory_revision();
    notify_runtime_keyblade_inventory_changed(L"ITEM_POTION");
    notify_runtime_keyblade_inventory_changed(L"");
    assert(read_runtime_keyblade_inventory_revision()==initial);
    notify_runtime_keyblade_inventory_changed(L"WEP_KEYBLADE_SO_02");
    notify_runtime_keyblade_inventory_changed(L"WEP_KEYBLADE_SO_01");
    assert(read_runtime_keyblade_inventory_revision()==initial+2);
    assert(read_runtime_keyblade_inventory_revision(L"WEP_KEYBLADE_SO_02")==1);
    RuntimeKeybladeOwnershipCache cache;
    const std::wstring star=L"WEP_KEYBLADE_SO_02",hero=L"WEP_KEYBLADE_SO_01";
    bool star_owned=false;
    size_t star_queries=0,hero_queries=0;
    auto read_star=[&]{++star_queries;return star_owned;};
    auto read_hero=[&]{++hero_queries;return false;};
    for(int i=0;i<100;++i)assert(!cache.get(star,read_star).owned);
    assert(star_queries==1); // One or a hundred chests share one query.
    cache.get(hero,read_hero);
    star_owned=true;
    notify_runtime_keyblade_inventory_changed(star);
    assert(!cache.get(hero,read_hero).owned&&hero_queries==1);
    for(int i=0;i<100;++i)assert(cache.get(star,read_star).owned);
    assert(star_queries==2);
    notify_runtime_keyblade_inventory_changed(hero);
    assert(!cache.get(hero,read_hero).owned); // Dispatch is not ownership proof.
    assert(hero_queries==2);
    cache.invalidate(star); // Interaction catches non-AP inventory changes.
    star_owned=false;
    assert(!cache.get(star,read_star).owned&&star_queries==3);
    cache.clear(); // Save/pawn context changes cannot reuse old ownership.
    assert(!cache.get(star,read_star).owned&&star_queries==4);
    cache.invalidate(star);
    try { cache.get(star,[]{throw 1;return true;});assert(false); } catch(int) {}
    assert(!cache.get(star,read_star).owned&&star_queries==5);
}
