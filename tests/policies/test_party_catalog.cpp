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
    assert(party_choices[xion_choice].npc_id == L"n_ex033");
    bool xion_tree = false;
    for (const auto& definition : custom_party_ai_definitions)
    {
        if (definition.choice == xion_choice)
        {
            xion_tree = definition.tree_path ==
                L"/Game/Blueprints/GoA/PartyAI/BT_GoA_XionParty.BT_GoA_XionParty";
        }
    }
    assert(xion_tree);
    bool aqua_tree = false;
    for (const auto& definition : custom_party_ai_definitions)
    {
        if (definition.choice == aqua_choice)
        {
            aqua_tree = definition.tree_path ==
                L"/Game/Blueprints/GoA/PartyAI/BT_GoA_AquaParty.BT_GoA_AquaParty";
        }
    }
    assert(aqua_tree && aqua_choice == 14);
    int xion_actions = 0;
    for (const auto& definition : action_viable_state_definitions)
    {
        if (definition.choice == xion_choice) { ++xion_actions; }
    }
    assert(xion_actions == 2);
    // Golden outputs preserve the existing seed stream, including its offset basis.
    assert(stable_seed(L"") == 1469598103934665603ULL);
    assert(stable_seed(L"kh3-party") == 13854259415226160096ULL);
    assert(stable_seed(L"13927989323898826278") == 12412109911010588145ULL);
}
