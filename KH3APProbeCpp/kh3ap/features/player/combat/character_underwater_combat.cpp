#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/combat/character_underwater_combat.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/characters/character_swap_assets.h"
#include "kh3ap/features/player/player_animation_support.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/core/kh02_data_table.h"
#include <Unreal/FMemory.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <array>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <vector>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap::character_underwater_combat
{
    using namespace RC::Unreal;
    namespace
    {
        constexpr int maximum_attack_row_bytes = 4096;
        constexpr std::array damage_rows{L"SEA_VERTICAL", L"SEA_PULSE", L"SEA_SPIRAL", L"SEA_FINISH"};
        // EGS CL1065258: native BeginPlay's individual collision constructor.
        // It copies the shape parameters into a registered primitive and adds
        // that primitive to the component's collision/name lookup arrays.
        const uintptr_t create_collision_rva = character_native::rva(character_native::Site::underwater_collision_create);
        constexpr size_t collision_shape_bytes = 0x100;
        constexpr int maximum_weapon_shapes = 64;
        constexpr std::array collision_groups{L"KB_StickM", L"KB_SphereS", L"ScrewL"};
        FWeakObjectPtr sora_collision_template;
        struct CollisionShape
        {
            FName group;
            FName default_attack;
            std::array<std::byte, collision_shape_bytes - sizeof(FName) * 2> parameters;
        };
        static_assert(sizeof(CollisionShape) == collision_shape_bytes);

        UObject* object(UObject* owner, const wchar_t* name)
        {
            auto* result = is_uobject_valid_fast(owner)
                               ? try_read_reflected_object_property_value(owner, name)
                               : nullptr;
            return is_uobject_valid_fast(result) ? result : nullptr;
        }

        template <typename T> TArray<T>* array(UObject* owner, const wchar_t* name)
        {
            auto* field = is_uobject_valid_fast(owner) ? owner->GetPropertyByNameInChain(name) : nullptr;
            if (!field || !field->IsA<FArrayProperty>() ||
                static_cast<FArrayProperty*>(field)->GetInner()->GetElementSize() != sizeof(T))
            {
                throw std::runtime_error("underwater_array_layout");
            }
            if constexpr (std::is_same_v<T, CollisionShape>)
            {
                auto* inner = static_cast<FArrayProperty*>(field)->GetInner();
                auto* schema = inner->IsA<FStructProperty>()
                                   ? static_cast<FStructProperty*>(inner)->GetStruct()
                                   : nullptr;
                if (!is_uobject_valid_fast(schema) || schema->GetName() != L"TresAtkCollShapeAssetUnit" ||
                    schema->GetStructureSize() != sizeof(CollisionShape))
                {
                    throw std::runtime_error("underwater_collision_schema");
                }
            }
            auto* values = field->ContainerPtrToValuePtr<TArray<T>>(owner);
            if (values->Num() < 0 || values->Num() > maximum_weapon_shapes)
            {
                throw std::runtime_error("underwater_array_range");
            }
            return values;
        }

        bool has_shape(UObject* component, const FName& name)
        {
            for (const auto& shape : *array<CollisionShape>(component, L"CollisionShapesSrc"))
            {
                if (shape.group == name)
                {
                    return true;
                }
            }
            return false;
        }

        bool prepare_collisions(UObject* pawn)
        {
            auto* weapons = array<UObject*>(object(pawn, L"MyEquipment"), L"m_WeaponEquips");
            UObject* primary{};
            std::vector<FName> missing;
            for (const auto* group : collision_groups)
            {
                const FName name(group, FNAME_Add);
                bool present = false;
                for (auto* weapon : *weapons)
                {
                    if (auto* component = object(weapon, L"MyAtkColl"))
                    {
                        if (!primary)
                        {
                            primary = component;
                        }
                        present = present || has_shape(component, name);
                    }
                }
                if (!present)
                {
                    missing.push_back(name);
                }
            }
            if (missing.empty())
            {
                return true;
            }
            auto* source = sora_collision_template.Get();
            if (!primary || !source)
            {
                return false;
            }
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            if (!player_animation_support::supported_skeleton_layout() || !character_native::verify_sites({character_native::Site::underwater_collision_create}))
            {
                throw std::runtime_error("underwater_collision_native_profile");
            }
            auto* destination = array<CollisionShape>(primary, L"CollisionShapesSrc");
            const auto first_new = destination->Num();
            for (const auto& name : missing)
            {
                bool found = false;
                for (const auto& shape : *array<CollisionShape>(source, L"CollisionShapesSrc"))
                {
                    if (shape.group == name)
                    {
                        destination->Add(shape);
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    throw std::runtime_error("underwater_sora_collision_missing");
                }
            }
            // Add only globally missing groups to the primary weapon. Giving
            // both Roxas weapons the same groups would duplicate contact hits.
            // The fixed-size native descriptor owns no dynamic arrays; its
            // UObject/curve references remain traced by the reflected array.
            // Append all descriptors before creating their native primitives.
            auto* primitives = array<UObject*>(primary, L"m_AtkColls");
            const auto before = primitives->Num();
            for (int index = first_new; index < destination->Num(); ++index)
            {
                reinterpret_cast<void (*)(UObject*, const CollisionShape*)>(base + create_collision_rva)(
                    primary, &(*destination)[index]);
            }
            if (primitives->Num() != before + static_cast<int>(missing.size()))
            {
                throw std::runtime_error("underwater_collision_creation_failed");
            }
            return true;
        }

        void prepare_damage(UObject* source, UObject* destination)
        {
            constexpr auto schema_path = L"/Script/TresGame.TresAttackDataTable";
            Kh02DataTableView sora(source, schema_path, "underwater_source_damage_layout");
            Kh02DataTableView alternate(destination, schema_path, "underwater_destination_damage_layout");
            const auto bytes = sora.schema->GetStructureSize();
            if (sora.schema != alternate.schema || bytes < 1 || bytes > maximum_attack_row_bytes)
            {
                throw std::runtime_error("underwater_damage_schema");
            }
            for (const auto* name : damage_rows)
            {
                if (alternate.find(name))
                {
                    continue;
                }
                auto* original = sora.find(name);
                if (!original)
                {
                    throw std::runtime_error("underwater_damage_row_missing");
                }
                auto* copy = FMemory::Malloc(bytes);
                if (!copy)
                {
                    throw std::bad_alloc();
                }
                sora.schema->InitializeStruct(copy);
                sora.schema->CopyScriptStruct(copy, original);
                // Preserve native physical damage coefficients, hit reactions,
                // target/team checks and finisher behavior.
                if (!alternate.insert(name, copy))
                {
                    sora.schema->DestroyStruct(copy);
                    FMemory::Free(copy);
                    throw std::runtime_error("underwater_damage_row_insert");
                }
            }
        }

        void prepare_projectiles(UObject* equipment, UObject* source, UClass* vertical, UClass* pulse)
        {
            struct Entry
            {
                FName name;
                UClass* type;
            };
            static_assert(sizeof(Entry) == 16);
            auto* list_field = source->GetPropertyByNameInChain(L"Projectiles");
            auto* set_field = equipment->GetPropertyByNameInChain(L"m_ProjectileSet");
            if (!list_field || !list_field->IsA<FArrayProperty>() ||
                static_cast<FArrayProperty*>(list_field)->GetInner()->GetElementSize() != sizeof(Entry) ||
                !set_field || set_field->GetClass().GetName() != L"ObjectProperty" ||
                set_field->GetElementSize() != sizeof(UObject*))
            {
                throw std::runtime_error("underwater_projectile_set_layout");
            }
            auto* original = list_field->ContainerPtrToValuePtr<TArray<Entry>>(source);
            const std::array required{Entry{FName(L"SEA_VERTICAL", FNAME_Add), vertical},
                                      Entry{FName(L"SEA_PULSE", FNAME_Add), pulse}};
            std::vector<Entry> missing;
            for (const auto& entry : required)
            {
                bool authored = false;
                for (const auto& current : *original)
                {
                    authored =
                        authored || (current.name == entry.name && is_uobject_valid_fast(current.type));
                }
                if (!authored)
                {
                    missing.push_back(entry);
                }
            }
            if (missing.empty())
            {
                return;
            }
            FStaticConstructObjectParameters parameters(source->GetClassPrivate(), equipment);
            parameters.SetFlags = RF_Transient;
            auto* private_set = UObjectGlobals::StaticConstructObject(parameters);
            if (!is_uobject_valid_fast(private_set))
            {
                throw std::runtime_error("underwater_projectile_set_create");
            }
            auto* entries = list_field->ContainerPtrToValuePtr<TArray<Entry>>(private_set);
            for (const auto& entry : *original)
            {
                entries->Add(entry);
            }
            for (const auto& entry : missing)
            {
                entries->Add(entry);
            }
            *set_field->ContainerPtrToValuePtr<UObject*>(equipment) = private_set;
        }
    } // namespace

    void capture_sora(UObject* pawn)
    {
        sora_collision_template = {};
        for (auto* weapon : *array<UObject*>(object(pawn, L"MyEquipment"), L"m_WeaponEquips"))
        {
            if (!is_uobject_valid_fast(weapon))
            {
                continue;
            }
            auto* type = weapon->GetClassPrivate();
            auto* component = object(type->GetClassDefaultObject(), L"MyAtkColl");
            if (!component)
            {
                continue;
            }
            bool complete = true;
            for (const auto* group : collision_groups)
            {
                complete = complete && has_shape(component, FName(group, FNAME_Add));
            }
            if (complete)
            {
                // Reuse the swap's return-class lease, released on return to
                // Sora/cancellation. Never retain a retired weapon actor.
                character_swap_assets::retain_return_dependency(type);
                sora_collision_template = make_uobject_weak(component);
                return;
            }
        }
    }

    bool prepare(UObject* pawn)
    {
        if (!character_swap::owns_swap_pawn(pawn))
        {
            return true;
        }
        auto* source = find_loaded_runtime_asset(player_animation_support::sora_attack_data_path);
        auto* destination =
            object(object(object(pawn, L"MyAtkColl"), L"m_pOwnerDataTableSet"), L"m_AttackData");
        auto* equipment = object(pawn, L"MyEquipment");
        auto* projectiles = object(equipment, L"m_ProjectileSet");
        auto* vertical = find_loaded_runtime_asset_class(vertical_projectile_path);
        auto* pulse = find_loaded_runtime_asset_class(pulse_projectile_path);
        if (!source || !destination || !projectiles || !vertical || !pulse)
        {
            return false;
        }
        prepare_damage(source, destination);
        prepare_projectiles(equipment, projectiles, vertical, pulse);
        return prepare_collisions(pawn);
    }
} // namespace kh3ap::character_underwater_combat
