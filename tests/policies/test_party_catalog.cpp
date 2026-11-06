#include "kh3ap/features/party/party_catalog.h"
#include <cassert>

int main()
{
    using namespace kh3ap::party_catalog;
    // These IDs are persisted in GoA preferences and mirrored by native data rows.
    assert(party_choices.size() == 28);
    assert(party_choices[0].unique_character == L"none");
    assert(party_choices[14].row_id == L"PartyMember_015");
    assert(party_choices[14].npc_id == L"n_ex011");
    assert(party_choices[26].unique_character == L"vanilla");
    assert(party_choices[27].row_id == L"PartyMember_010");
    assert(choice_for_row_id(L"PartyMember_015") == 14);
    assert(choice_for_row_id(L"PartyMember_010") == 27);
    assert(choice_for_row_id(L"PartyMember_021") == 0);
    assert(choice_for_row_id(L"unknown") == 0);
    // Golden outputs preserve the existing seed stream, including its offset basis.
    assert(stable_seed(L"") == 1469598103934665603ULL);
    assert(stable_seed(L"kh3-party") == 13854259415226160096ULL);
    assert(stable_seed(L"13927989323898826278") == 12412109911010588145ULL);
}
