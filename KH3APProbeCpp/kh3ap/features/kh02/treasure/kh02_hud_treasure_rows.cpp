#include "kh02_hud_treasure_rows.h"
#include "kh3ap/features/kh02/ui/kh02_hud_reflection.h"
#include "kh02_hud_treasure_policy.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_policy.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include <Unreal/NameTypes.hpp>

namespace
{
    constexpr int native_name_bytes = 8;
}

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02hud_detail;

    void kh02_prepare_treasure_rows(UObject* chests)
    {
        Table chest_rows(chests, L"/Script/TresGame.TresTreasureDataTable");
        auto* treasure = find_struct_field_by_name(chest_rows.schema, L"m_TreasureName");
        auto* area = find_struct_field_by_name(chest_rows.schema, L"m_WorldAreaCode");
        if (!treasure || treasure->GetClass().GetName() != L"NameProperty" ||
            treasure->GetElementSize() != native_name_bytes || !area || area->GetElementSize() != 1 ||
            (area->GetClass().GetName() != L"ByteProperty" && area->GetClass().GetName() != L"EnumProperty"))
        {
            throw std::runtime_error("The imported chest table has an unsupported reward or area field.");
        }
        for (auto identity : kh02_tracking::additional_chests)
        {
            auto* row = chest_rows.copy(std::wstring(identity).c_str(), chest_rows.find(L"DW_BOX017"));
            *treasure->ContainerPtrToValuePtr<FName>(row) = FName{};
            *area->ContainerPtrToValuePtr<uint8_t>(row) = kh02_imported_treasure_area(identity);
        }
        // Selected AP chests keep their native opening and journal record,
        // while their assigned reward arrives through the AP receipt queue.
        const auto ap_state = reconciliation::view();
        if (!ap_state->kh02_ap_chests.empty())
        {
            auto* reward = find_struct_field_by_name(chest_rows.schema, L"m_TreasureName");
            if (!reward || reward->GetClass().GetName() != L"NameProperty" ||
                reward->GetElementSize() != native_name_bytes)
            {
                throw std::runtime_error("kh02_ap_chest_reward_layout");
            }
            for (const auto& id : ap_state->kh02_ap_chests)
            {
                auto* row = chest_rows.find(id.c_str());
                if (!row)
                {
                    throw std::runtime_error("kh02_ap_chest_row_missing");
                }
                *reward->ContainerPtrToValuePtr<FName>(row) = FName{};
            }
        }
    }

} // namespace kh3ap
