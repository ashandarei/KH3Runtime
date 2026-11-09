#include "kh3ap/features/journal/chest_journal.h"
#include <cassert>

int main()
{
    using namespace kh3ap;
    NativeChestJournalSnapshot snapshot;
    auto page = chest_journal_page(snapshot);
    assert(page.count == 0 && !page.catalog.empty() && page.collected.empty());
    snapshot.registered = {L"HE_LBOX_001", L"AP_LBOX_001", L"DW_LBOX01"};
    snapshot.opened = {L"HE_LBOX_001", L"TT_LBOX_001"};
    // Disabled or not-yet-verified seeds expose no imported entries.
    page = chest_journal_page(snapshot);
    assert(page.count == 2 && page.catalog.find(L"DW_LBOX01") == page.catalog.npos);
    assert(!portals::kh02_access(false, true, true).unlocked);
    assert(!portals::kh02_access(true, false, true).unlocked);
    assert(!portals::kh02_access(true, true, false).unlocked);
    page = chest_journal_page(snapshot, portals::kh02_access(true, true, false));
    assert(page.count == 3);
    assert(page.catalog.find(L"DW_LBOX01") != page.catalog.npos);
    assert(page.catalog.find(L"~chest/DW_LBOX01") == page.catalog.npos);
    page = chest_journal_page(snapshot, portals::kh02_access(true, true, true));
    assert(page.count == 3);
    assert(page.collected == L"HE_LBOX_001");
    assert(page.catalog.find(L"HE_LBOX_001\tChest 29\t\tOlympus~") != page.catalog.npos);
    assert(page.catalog.find(L"Realm of the Gods: Corridors") != page.catalog.npos);
    assert(page.catalog.find(L"~chest/HE_LBOX_001\n") != page.catalog.npos);
    assert(page.catalog.find(L"~chest/AP_LBOX_001\n") != page.catalog.npos);
    assert(page.catalog.find(L"~chest/DW_LBOX01\n") != page.catalog.npos);
    assert(page.catalog.find(L"AP_LBOX_001\tMelody Chest") != page.catalog.npos);
    assert(page.catalog.find(L"DW_LBOX01\tChest DW_LBOX01\t\t0.2 - Castle Town~") != page.catalog.npos);
    assert(page.catalog.find(L"TT_LBOX_001") == page.catalog.npos);
    // A new seed must not retain entries or completion from the previous seed.
    page = chest_journal_page({{L"TT_LBOX_001"}, {}});
    assert(page.count == 1 && page.collected.empty());
    assert(page.catalog.find(L"HE_LBOX_001") == page.catalog.npos);
    page = chest_journal_page({{L"EW_LBOX_001"}, {}});
    assert(page.count == 1 && page.catalog.find(L"~chest/") == page.catalog.npos);
}
