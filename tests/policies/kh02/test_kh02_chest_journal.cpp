#include "kh3ap/features/kh02/treasure/kh02_chest_journal.h"
#include <cassert>
#include <map>
#include <set>

int main()
{
    using namespace kh3ap::kh02_chest_journal;
    // Independent ordered native fixture: every identity must keep its exact bit.
    const std::wstring_view expected[]{
        L"DW_BOX009", L"DW_BOX026", L"DW_BOX027", L"DW_BOX028", L"DW_BOX029", L"DW_LBOX04", L"DW_BOX002", L"DW_BOX005",
        L"DW_BOX010", L"DW_BOX012", L"DW_BOX014", L"DW_BOX030", L"DW_BOX031", L"DW_BOX032", L"DW_BOX036", L"DW_BOX037",
        L"DW_BOX017", L"DW_BOX018", L"DW_BOX019", L"DW_BOX020", L"DW_BOX021", L"DW_BOX022", L"DW_BOX023", L"DW_BOX024",
        L"DW_BOX025", L"DW_LBOX03", L"DW_BOX034", L"DW_BOX035", L"DW_BOX011", L"DW_BOX013", L"DW_BOX033", L"DW_BOX015",
        L"DW_BOX016", L"DW_BOX008", L"DW_LBOX02", L"DW_LBOX01", L"DW_BOX003", L"DW_BOX004", L"DW_BOX006", L"DW_BOX007"};
    std::map<std::wstring_view, int> assignments;
    std::set<int> bits;
    for (const auto& group : groups)
    {
        for (std::size_t index = 0; index < group.count; ++index)
        {
            const int bit = group.first_bit + static_cast<int>(index);
            assert(assignments.emplace(group.identities[index], bit).second);
            assert(bits.insert(bit).second);
        }
    }
    assert(assignments.size() == std::size(expected));
    for (std::size_t index = 0; index < std::size(expected); ++index)
    {
        assert(assignments.at(expected[index]) == 4056 + static_cast<int>(index));
    }

    // The reset mask must clear exactly the owned tail, including a partial word.
    for (std::size_t bit = 0; bit < bit_count; ++bit)
    {
        uint32_t word = UINT32_MAX;
        const auto word_index = bit / bits_per_word;
        if (word_index == first_owned_word)
        {
            word &= preserved_native_mask;
        }
        else if (word_index > first_owned_word)
        {
            word = 0;
        }
        const bool retained = (word & (uint32_t{1} << (bit % bits_per_word))) != 0;
        assert(retained == (bit < 4056));
    }
}
