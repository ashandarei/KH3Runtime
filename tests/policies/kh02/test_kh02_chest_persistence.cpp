#include "kh3ap/features/kh02/treasure/kh02_chest_persistence.h"
#include <cassert>

int main()
{
    using namespace kh3ap;
    const std::set<std::string> empty;
    std::set<std::wstring> identities;
    for (const auto& group : kh02_chest_journal::groups)
    {
        for (std::size_t index = 0; index < group.count; ++index)
        {
            const auto id = group.identities[index];
            assert(identities.emplace(id).second);
            assert(!kh02_chest_collected(id, 0, empty));
            assert(!kh02_chest_collected(id, 2, empty));
            // Ordinary 0.2 play persists even when this chest is not an AP check.
            assert(kh02_chest_collected(id, 1, empty));
            // AP completion repairs presentation after an older native save
            // reload, without requiring its reward to be delivered again.
            const auto name = kh02_chest_source_id(id);
            const std::set<std::string> checked{"KH02:Treasure:" + name};
            assert(kh02_chest_collected(id, 0, checked));
            assert(!kh02_chest_collected(id, 0, {"KH02:Boss:" + name}));
        }
    }
    assert(identities.size() == 40);
    assert(!kh02_chest_collected(L"DW_BOX_FAKE", 1, {"KH02:Treasure:DW_BOX_FAKE"}));
    assert(!kh02_chest_collected(L"MI_LBOX_001", 1, empty));
}
