#include "kh3ap/features/player/combat/character_attack_completion.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/player_animation_support.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/kh02/core/kh02_data_table.h"
#include <Unreal/FMemory.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <vector>

namespace kh3ap::character_attack_completion
{
    using namespace RC::Unreal;
    namespace
    {
        constexpr int maximum_attack_row_bytes = 4096;
        constexpr int maximum_weapon_equips = 16;

        UObject** reference(UObject* owner, const wchar_t* name)
        {
            auto* field = is_uobject_valid_fast(owner) ? owner->GetPropertyByNameInChain(name) : nullptr;
            if (!field || field->GetClass().GetName() != L"ObjectProperty" ||
                field->GetElementSize() != sizeof(UObject*))
            {
                throw std::runtime_error("character_attack_reference_layout");
            }
            return field->ContainerPtrToValuePtr<UObject*>(owner);
        }

        UObject* object(UObject* owner, const wchar_t* name)
        {
            return is_uobject_valid_fast(owner) ? try_read_reflected_object_property_value(owner, name) : nullptr;
        }

        struct CopyRows
        {
            const Kh02DataTableView& destination;
            FBoolProperty* killer;
            size_t count{};
            bool failed{};
        };

        void copy_row(unsigned long long key, void* original, void* context)
        {
            auto& copy = *static_cast<CopyRows*>(context);
            if (copy.failed)
            {
                return;
            }
            auto* schema = copy.destination.schema;
            auto* row = FMemory::Malloc(schema->GetStructureSize());
            if (!row)
            {
                copy.failed = true;
                return;
            }
            schema->InitializeStruct(row);
            schema->CopyScriptStruct(row, original);
            // Sora's regular attacks carry this independently of the finisher
            // flag. Native story bosses (including TT's Demon Tide) require it
            // to accept a concluding hit and fire their own ending events.
            copy.killer->SetPropertyValueInContainer(row, true);
            if (!copy.destination.insert(key, row))
            {
                schema->DestroyStruct(row);
                FMemory::Free(row);
                copy.failed = true;
                return;
            }
            ++copy.count;
        }
    }

    bool prepare(UObject* pawn)
    {
        if (!character_swap::owns_swap_pawn(pawn))
        {
            return true;
        }
        auto* id = pawn->GetPropertyByNameInChain(L"m_PlayerUniqueID");
        if (!id || id->GetElementSize() != sizeof(uint8_t))
        {
            return false;
        }
        using player_animation_support::PlayerId;
        const auto player = static_cast<PlayerId>(*id->ContainerPtrToValuePtr<uint8_t>(pawn));
        if (player != PlayerId::Kairi && player != PlayerId::Aqua && player != PlayerId::Roxas)
        {
            return true;
        }
        auto* collision = object(pawn, L"MyAtkColl");
        auto* original_set = object(collision, L"m_pOwnerDataTableSet");
        auto* original_table = object(original_set, L"m_AttackData");
        if (!is_uobject_valid_fast(original_table))
        {
            return false;
        }
        // The private table and set are held by reflected references on this
        // pawn/components. They die with the pawn and never affect party copies.
        if (original_set->GetOuterPrivate() == pawn && original_table->GetOuterPrivate() == original_set)
        {
            return true;
        }
        constexpr auto schema_path = L"/Script/TresGame.TresAttackDataTable";
        Kh02DataTableView source(original_table, schema_path, "character_attack_source_layout");
        auto* field = source.schema->GetPropertyByNameInChain(L"m_bIsKillerAttack");
        if (!field || !field->IsA<FBoolProperty>() || source.schema->GetStructureSize() < 1 ||
            source.schema->GetStructureSize() > maximum_attack_row_bytes)
        {
            throw std::runtime_error("character_attack_killer_layout");
        }
        std::vector<UObject**> owners{reference(pawn, L"m_pDataTableSet"),
                                      reference(collision, L"m_pOwnerDataTableSet")};
        auto* equipment = object(pawn, L"MyEquipment");
        auto* weapons_field = is_uobject_valid_fast(equipment)
                                  ? equipment->GetPropertyByNameInChain(L"m_WeaponEquips") : nullptr;
        if (!weapons_field || !weapons_field->IsA<FArrayProperty>() ||
            static_cast<FArrayProperty*>(weapons_field)->GetInner()->GetElementSize() != sizeof(UObject*))
        {
            throw std::runtime_error("character_attack_weapons_layout");
        }
        auto* weapons = weapons_field->ContainerPtrToValuePtr<TArray<UObject*>>(equipment);
        if (weapons->Num() < 0 || weapons->Num() > maximum_weapon_equips)
        {
            throw std::runtime_error("character_attack_weapons_range");
        }
        for (auto* weapon : *weapons)
        {
            if (auto* component = object(weapon, L"MyAtkColl"); is_uobject_valid_fast(component))
            {
                owners.push_back(reference(component, L"m_pOwnerDataTableSet"));
            }
        }
        for (auto** owner : owners)
        {
            if (*owner != original_set)
            {
                throw std::runtime_error("character_attack_owner_mismatch");
            }
        }
        FStaticConstructObjectParameters set_parameters(original_set->GetClassPrivate(), pawn);
        set_parameters.Template = original_set;
        set_parameters.SetFlags = RF_Transient;
        auto* private_set = UObjectGlobals::StaticConstructObject(set_parameters);
        if (!is_uobject_valid_fast(private_set))
        {
            throw std::runtime_error("character_attack_set_create");
        }
        // DataTable rows are native, not reflected fields: construct an empty
        // table and deep-copy every row with its schema instead of templating it.
        FStaticConstructObjectParameters table_parameters(original_table->GetClassPrivate(), private_set);
        table_parameters.SetFlags = RF_Transient;
        auto* private_table = UObjectGlobals::StaticConstructObject(table_parameters);
        if (!is_uobject_valid_fast(private_table))
        {
            throw std::runtime_error("character_attack_table_create");
        }
        *reference(private_table, L"RowStruct") = source.schema;
        Kh02DataTableView destination(private_table, schema_path, "character_attack_destination_layout");
        CopyRows copy{destination, static_cast<FBoolProperty*>(field)};
        if (!source.visit(copy_row, &copy) || copy.failed || copy.count == 0)
        {
            throw std::runtime_error("character_attack_copy_rows");
        }
        *reference(private_set, L"m_AttackData") = private_table;
        for (auto** owner : owners)
        {
            *owner = private_set;
        }
        RC::Output::send<RC::LogLevel::Normal>(
            STR("[KH3APProbeCpp] CharacterAttackCompletion player={} rows={} components={} source={}\n"),
            static_cast<int>(player), copy.count, owners.size(), original_table->GetPathName());
        return true;
    }
}
