#include "kh02_treasure_registration.h"
#include "kh3ap/features/kh02/core/kh02_profile.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/kh02/ui/kh02_hud_reflection.h"
#include "kh02_chest_journal.h"
#include <Unreal/UClass.hpp>
#define NOMINMAX
#include <Windows.h>
extern "C" bool kh02_treasure_index_bind(void*, unsigned long long, int);

namespace
{
    constexpr int treasure_table_slots = 32;
    constexpr int imported_treasure_table_slot = 3;
    constexpr std::size_t treasure_index_map_offset = 0x370;

} // namespace

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02hud_detail;

    void kh02_register_treasure_table(UObject* chests, bool& chest_table_registered)
    {
        // Load preparation can originate from non-actor UI owners. The active
        // viewport world owns the item manager used by both entry and Continue.
        auto* world = kh02_current_world();
        auto* gi = try_read_reflected_object_property_value(world, L"OwningGameInstance");
        auto* itemman = try_read_reflected_object_property_value(gi, L"m_ItemMan");
        auto* set = try_read_reflected_object_property_value(itemman, L"m_TreasureData");
        auto* property = is_uobject_valid_fast(set) ? set->GetPropertyByNameInChain(L"m_TreasureDataTables") : nullptr;
        if (!property || property->GetClass().GetName() != L"ObjectProperty" ||
            property->GetElementSize() != sizeof(UObject*) || property->GetArrayDim() != treasure_table_slots)
        {
            throw std::runtime_error("kh02_chest_dataset_layout");
        }
        auto** tables = property->ContainerPtrToValuePtr<UObject*>(set);
        if (tables[imported_treasure_table_slot] && tables[imported_treasure_table_slot] != chests)
        {
            throw std::runtime_error("kh02_native_dw_chest_table_occupied");
        }
        tables[imported_treasure_table_slot] = chests;
        chest_table_registered = tables[imported_treasure_table_slot] == chests;
        // TreasureBox caches the item manager's stable native journal index at
        // initialization. Appending a table does not update that separate map.
        // Bind only unused tail bits in its existing 4096-bit journal, leaving
        // every shipped and seed chest's index unchanged.
        using kh02_native::Site;
        if (!kh02_native::verify_sites({Site::treasure_index_lookup}) ||
            itemman->GetClassPrivate()->GetPathName() != L"/Script/TresGame.TresItemMan")
        {
            throw std::runtime_error("kh02_chest_index_profile");
        }
        auto* index_map = reinterpret_cast<std::byte*>(itemman) + treasure_index_map_offset;
        for (const auto& group : kh02_chest_journal::groups)
        {
            for (std::size_t index = 0; index < group.count; ++index)
            {
                if (!kh02_treasure_index_bind(index_map, key(group.identities[index].data()),
                                              group.first_bit + static_cast<int>(index)))
                {
                    throw std::runtime_error(group.conflict_error);
                }
            }
        }
    }

} // namespace kh3ap
