#include "kh02_phantom_equipment.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/kh02/core/kh02_profile.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <cstring>
#include <stdexcept>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        struct ArrayView
        {
            std::byte* data;
            int count;
            int capacity;
        };

        auto field(UObject* owner, const wchar_t* name, const wchar_t* kind) -> FProperty*
        {
            auto* property = is_uobject_valid_fast(owner) ? owner->GetPropertyByNameInChain(name) : nullptr;
            if (!property || property->GetClass().GetName() != kind)
            {
                throw std::runtime_error("phantom_equipment_property");
            }
            return property;
        }

        auto structure(FArrayProperty* property, const wchar_t* expected) -> UScriptStruct*
        {
            auto* inner = property->GetInner();
            auto* schema =
                inner && inner->IsA<FStructProperty>() ? static_cast<FStructProperty*>(inner)->GetStruct() : nullptr;
            if (!is_uobject_valid_fast(schema) || schema->GetName() != expected ||
                schema->GetStructureSize() != inner->GetElementSize())
            {
                throw std::runtime_error("phantom_equipment_array_schema");
            }
            return schema;
        }

        auto single(ArrayView* values) -> std::byte*
        {
            if (!values || !values->data || values->count != 1 || values->capacity < 1 || values->capacity > 16)
            {
                throw std::runtime_error("phantom_equipment_expected_single_weapon");
            }
            return values->data;
        }

        auto prepare(UObject* pawn) -> bool
        {
            if (!is_uobject_valid_fast(pawn))
            {
                return false;
            }
            // Later encounters and transformations use derived pawn and avatar
            // classes. Match their imported base identity, not the leaf name.
            bool imported_phantom = false;
            auto* type = static_cast<UStruct*>(pawn->GetClassPrivate());
            constexpr int maximum_class_depth = 32;
            for (int depth = 0; is_uobject_valid_fast(type) && depth < maximum_class_depth; ++depth)
            {
                const auto path = type->GetPathName();
                if (path == L"/Game/Zlueprints/Enemy/e_dw405/e_dw405_Pawn.e_dw405_Pawn_C" ||
                    path == L"/Game/Zlueprints/Enemy/e_dw405/e_dw405_Avatar.e_dw405_Avatar_C")
                {
                    imported_phantom = true;
                    break;
                }
                auto* parent_type = type->GetSuperStruct();
                if (parent_type == type)
                {
                    break;
                }
                type = parent_type;
            }
            if (!imported_phantom)
            {
                return false;
            }
            auto* equipment = try_read_reflected_object_property_value(pawn, L"MyEquipment");
            auto* fixed = static_cast<FBoolProperty*>(field(equipment, L"m_bIsFixedEquip", L"BoolProperty"));
            auto* equipped =
                field(equipment, L"m_WeaponEquips", L"ArrayProperty")->ContainerPtrToValuePtr<ArrayView>(equipment);
            if (fixed->GetPropertyValueInContainer(equipment) || equipped->count != 0)
            {
                return false;
            }
            auto* weapon_set = try_read_reflected_object_property_value(equipment, L"MyWeaponSet");
            if (!is_uobject_valid_fast(weapon_set) ||
                weapon_set->GetPathName() != L"/Game/Zlueprints/Enemy/e_dw405/e_dw405_WepSet.e_dw405_WepSet")
            {
                throw std::runtime_error("phantom_equipment_weapon_set");
            }
            auto* sets = static_cast<FArrayProperty*>(field(weapon_set, L"Equips", L"ArrayProperty"));
            auto* set_schema = structure(sets, L"TresEquipmentSetAssetUnit");
            auto* set = single(sets->ContainerPtrToValuePtr<ArrayView>(weapon_set));
            auto* source = find_struct_field_by_name(set_schema, L"SetData");
            auto* destination = static_cast<FArrayProperty*>(field(equipment, L"m_FixedEquips", L"ArrayProperty"));
            if (!source || !source->IsA<FArrayProperty>() ||
                structure(static_cast<FArrayProperty*>(source), L"TresEquipmentAssetUnit") !=
                    structure(destination, L"TresEquipmentAssetUnit") ||
                destination->ContainerPtrToValuePtr<ArrayView>(equipment)->count != 0)
            {
                throw std::runtime_error("phantom_equipment_fixed_schema");
            }
            auto* source_array = source->ContainerPtrToValuePtr<ArrayView>(set);
            auto* unit = single(source_array);
            auto* unit_schema = structure(destination, L"TresEquipmentAssetUnit");
            auto* resource = find_struct_field_by_name(unit_schema, L"RsrcData");
            auto* socket = find_struct_field_by_name(unit_schema, L"AttachBoneName");
            if (!resource || resource->GetClass().GetName() != L"ClassProperty" || !socket ||
                socket->GetClass().GetName() != L"NameProperty")
            {
                throw std::runtime_error("phantom_equipment_weapon_schema");
            }
            auto* weapon_class = *resource->ContainerPtrToValuePtr<UObject*>(unit);
            if (!is_uobject_valid_fast(weapon_class) ||
                weapon_class->GetPathName() != L"/Game/Zlueprints/Enemy/e_dw405/e_dw406_Wep.e_dw406_Wep_C" ||
                socket->ContainerPtrToValuePtr<FName>(unit)->ToString() != L"R_wep_skt")
            {
                throw std::runtime_error("phantom_equipment_weapon_identity");
            }
            // KH3's non-fixed initializer consumes EquipData slots, not the
            // imported MyWeaponSet. Use its supported fixed-equipment path.
            // Property copying owns a separate array and preserves GC references.
            destination->CopyCompleteValue(destination->ContainerPtrToValuePtr<void>(equipment), source_array);
            fixed->SetPropertyValueInContainer(equipment, true);
            return true;
        }
    } // namespace

    struct Kh02PhantomEquipment::Impl
    {
        inline static uint64_t trampoline{};
        std::unique_ptr<PLH::x64Detour> hook;
        bool installed{};

        static void initialize(UObject* pawn, bool force)
        {
            bool adapted{};
            try
            {
                adapted = prepare(pawn);
            }
            catch (const std::exception& failure)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Phantom equipment adaptation failed: {}\n"),
                    std::wstring(failure.what(), failure.what() + std::strlen(failure.what())));
            }
            reinterpret_cast<void (*)(UObject*, bool)>(trampoline)(pawn, force);
            if (adapted && is_uobject_valid_fast(pawn))
            {
                auto* equipment = try_read_reflected_object_property_value(pawn, L"MyEquipment");
                auto* property =
                    is_uobject_valid_fast(equipment) ? equipment->GetPropertyByNameInChain(L"m_WeaponEquips") : nullptr;
                const int count = property ? property->ContainerPtrToValuePtr<ArrayView>(equipment)->count : -1;
                RC::Output::send<RC::LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Phantom equipment initialized actor={} weapons={}\n"), pawn->GetPathName(),
                    count);
            }
        }

        Impl()
        {
            using kh02_native::Site;
            if (!kh02_native::verify_sites({Site::initialize_equipment}))
            {
                throw std::runtime_error("phantom_equipment_native_profile");
            }
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            hook = std::make_unique<PLH::x64Detour>(base + kh02_native::rva(Site::initialize_equipment),
                                                    reinterpret_cast<uint64_t>(&initialize), &trampoline);
            installed = hook->hook();
            if (!installed)
            {
                throw std::runtime_error("phantom_equipment_native_hook");
            }
        }

        ~Impl()
        {
            if (installed && !hook->unHook())
            {
                // The callback has no instance pointer; retain its trampoline.
                hook.release();
            }
        }
    };

    Kh02PhantomEquipment::Kh02PhantomEquipment() : impl(std::make_unique<Impl>()) {}

    Kh02PhantomEquipment::~Kh02PhantomEquipment() = default;
} // namespace kh3ap
