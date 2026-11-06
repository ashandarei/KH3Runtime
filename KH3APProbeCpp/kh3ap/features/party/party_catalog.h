#pragma once

#include "kh3ap/features/party/party_map_policy.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kh3ap
{
    struct RuntimePartyMemberDefinition
    {
        std::wstring_view row_id{};
        std::wstring_view display_name{};
        std::wstring_view npc_id{};
        std::wstring_view unique_character{};
    };

    namespace party_catalog
    {
        inline constexpr std::size_t ventus_choice = 15;
        inline constexpr std::size_t lea_choice = 16;
        inline constexpr std::size_t kairi_choice = 17;
        inline constexpr std::size_t roxas_choice = 18;
        struct CustomPartyAiDefinition
        {
            std::size_t choice{};
            std::wstring_view display_name{};
            std::wstring_view pawn_class_name{};
            std::wstring_view tree_path{};
            std::wstring_view follow_query_path{};
        };
        inline constexpr std::array<CustomPartyAiDefinition, 4> custom_party_ai_definitions{{
            {
                ventus_choice,
                L"Ventus",
                L"n_ex006_Pawn_C",
                L"/Game/Blueprints/GoA/PartyAI/BT_GoA_VentusParty.BT_GoA_VentusParty",
                L"/Game/Blueprints/Npc/Common/BT/Follow/"
                L"n_Common_FollowPlayerQuery.n_Common_FollowPlayerQuery",
            },
            {
                lea_choice,
                L"Lea",
                L"n_ex010_Pawn_C",
                L"/Game/Blueprints/GoA/PartyAI/BT_GoA_LeaParty.BT_GoA_LeaParty",
                L"/Game/Blueprints/Npc/Common/BT/Follow/"
                L"n_Common_FollowPlayerQuery.n_Common_FollowPlayerQuery",
            },
            {
                kairi_choice,
                L"Kairi",
                L"n_ex005_Pawn_C",
                L"/Game/Blueprints/GoA/PartyAI/BT_GoA_KairiParty.BT_GoA_KairiParty",
            },
            {
                roxas_choice,
                L"Roxas",
                L"n_ex009_Pawn_C",
                L"/Game/Blueprints/GoA/PartyAI/BT_GoA_RoxasParty.BT_GoA_RoxasParty",
            },
        }};
        struct ActionViableStateDefinition
        {
            std::size_t choice{};
            std::wstring_view class_path{};
        };
        inline constexpr std::array<ActionViableStateDefinition, 13>
            action_viable_state_definitions{{
                {ventus_choice, L"/Game/Blueprints/Npc/n_ex006/BT/"
                                L"n_ex006_Attack10_LandCombo.n_ex006_Attack10_LandCombo_C"},
                {ventus_choice, L"/Game/Blueprints/Npc/n_ex006/BT/"
                                L"n_ex006_Attack11_AirCombo.n_ex006_Attack11_AirCombo_C"},
                {ventus_choice, L"/Game/Blueprints/Npc/n_ex006/BT/"
                                L"n_ex006_Attack12_WindRaid.n_ex006_Attack12_WindRaid_C"},
                {lea_choice, L"/Game/Blueprints/Npc/n_ex010/BT/"
                             L"n_ex010_Attack1_LandCombo.n_ex010_Attack1_LandCombo_C"},
                {lea_choice, L"/Game/Blueprints/Npc/n_ex010/BT/"
                             L"n_ex010_Attack2_FigaCutter.n_ex010_Attack2_FigaCutter_C"},
                {lea_choice, L"/Game/Blueprints/Npc/n_ex010/BT/"
                             L"n_ex010_Attack3_FigaRaid.n_ex010_Attack3_FigaRaid_C"},
                {lea_choice, L"/Game/Blueprints/Npc/n_ex010/BT/"
                             L"n_ex010_Attack5_FigaSlicer.n_ex010_Attack5_FigaSlicer_C"},
                {kairi_choice, L"/Game/Blueprints/Npc/n_ex005/BT/"
                               L"n_ex005_Attack1_LandCombo.n_ex005_Attack1_LandCombo_C"},
                {kairi_choice, L"/Game/Blueprints/Npc/n_ex005/BT/"
                               L"n_ex005_Attack2_SpiralBloom.n_ex005_Attack2_SpiralBloom_C"},
                {kairi_choice, L"/Game/Blueprints/Npc/n_ex005/BT/"
                               L"n_ex005_Attack3_FlowerShoot.n_ex005_Attack3_FlowerShoot_C"},
                {roxas_choice, L"/Game/Blueprints/Npc/n_ex009/BT/"
                               L"n_ex009_Attack1_XCutDown.n_ex009_Attack1_XCutDown_C"},
                {roxas_choice, L"/Game/Blueprints/Npc/n_ex009/BT/"
                               L"n_ex009_Attack2_LandCombo.n_ex009_Attack2_LandCombo_C"},
                {roxas_choice, L"/Game/Blueprints/Npc/n_ex009/BT/"
                               L"n_ex009_Attack3_CrossShot.n_ex009_Attack3_CrossShot_C"},
            }};

        // Choice order is an external protocol: it is persisted in
        // goa_runtime_settings.json and mirrors every TresPartyData row.
        inline constexpr std::array<RuntimePartyMemberDefinition, 28> party_choices{{
            {L"", L"None", L"", L"none"},
            {L"PartyMember_001", L"Donald", L"n_ex001", L"donald"},
            {L"PartyMember_002", L"Goofy", L"n_ex002", L"goofy"},
            {L"PartyMember_003", L"Hercules", L"n_he201", L"hercules"},
            {L"PartyMember_004", L"Woody", L"n_ts201", L"woody"},
            {L"PartyMember_005", L"Buzz Lightyear", L"n_ts202", L"buzz"},
            {L"PartyMember_006", L"Rapunzel", L"n_ra201", L"rapunzel"},
            {L"PartyMember_007", L"Flynn Rider", L"n_ra203", L"flynn"},
            {L"PartyMember_008", L"Sulley", L"n_mi201", L"sulley"},
            {L"PartyMember_009", L"Mike", L"n_mi202", L"mike"},
            {L"PartyMember_011", L"Captain Jack Sparrow", L"n_ca201", L"jack"},
            {L"PartyMember_012", L"Baymax", L"n_bx202", L"baymax"},
            {L"PartyMember_013", L"Mickey", L"n_ex003", L"mickey"},
            {L"PartyMember_014", L"Riku", L"n_ex004", L"riku"},
            // FriendIdDataTable maps FRD_AQUA to n_ex011; n_ex040 is an event pawn.
            {L"PartyMember_015", L"Aqua", L"n_ex011", L"aqua"},
            {L"PartyMember_016", L"Ventus", L"n_ex006", L"ventus"},
            {L"PartyMember_017", L"Lea", L"n_ex010", L"lea"},
            {L"PartyMember_018", L"Kairi", L"n_ex005", L"kairi"},
            {L"PartyMember_019", L"Roxas", L"n_ex009", L"roxas"},
            {L"PartyMember_020", L"Xion", L"n_ex033", L"xion"},
            {L"PartyMember_022", L"Donald (Caribbean)", L"n_ca001", L"donald"},
            {L"PartyMember_023", L"Goofy (Caribbean)", L"n_ca002", L"goofy"},
            {L"PartyMember_024", L"Donald (Monstropolis)", L"n_mi001", L"donald"},
            {L"PartyMember_025", L"Goofy (Monstropolis)", L"n_mi002", L"goofy"},
            {L"PartyMember_026", L"Donald (Toy Box)", L"n_ts001", L"donald"},
            {L"PartyMember_027", L"Goofy (Toy Box)", L"n_ts002", L"goofy"},
            {L"", L"Vanilla", L"", L"vanilla"},
            {L"PartyMember_010", L"Marshmallow", L"n_fz214", L"marshmallow"},
        }};

        inline constexpr std::size_t vanilla_choice = runtime_party_vanilla_choice;
        static_assert(party_choices.size() == 28);
        static_assert(party_choices[vanilla_choice].unique_character == L"vanilla");
        inline constexpr std::array<std::size_t, 8> default_unlocked_choices{{
            1,
            2,
            20,
            21,
            22,
            23,
            24,
            25,
        }};
        static_assert(party_choices[default_unlocked_choices[0]].row_id == L"PartyMember_001");
        static_assert(party_choices[default_unlocked_choices[1]].row_id == L"PartyMember_002");
        static_assert(
            []
            {
                for (const std::size_t choice : default_unlocked_choices)
                {
                    if (party_choices[choice].unique_character != L"donald" &&
                        party_choices[choice].unique_character != L"goofy")
                    {
                        return false;
                    }
                }
                return true;
            }());

        [[nodiscard]] inline auto stable_seed(std::wstring_view value) -> uint64_t
        {
            constexpr uint64_t hash_offset_basis = 1469598103934665603ULL;
            constexpr uint64_t hash_prime = 1099511628211ULL;
            uint64_t hash = hash_offset_basis;
            for (const wchar_t character : value)
            {
                hash ^= static_cast<uint64_t>(character);
                hash *= hash_prime;
            }
            return hash;
        }

        [[nodiscard]] inline auto choice_for_row_id(std::wstring_view row_id) -> std::size_t
        {
            for (std::size_t choice = 1; choice < party_choices.size(); ++choice)
            {
                if (party_choices[choice].row_id == row_id)
                {
                    return choice;
                }
            }
            return 0;
        }

    } // namespace party_catalog
} // namespace kh3ap
