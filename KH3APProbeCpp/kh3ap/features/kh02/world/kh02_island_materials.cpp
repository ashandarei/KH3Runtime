#include "kh02_island_materials.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;
        constexpr int actor_scan_limit = 16'384;
        constexpr int material_slot_limit = 128;

        struct ReflectedCall : Kh02ReflectedCall
        {
            ReflectedCall(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "kh02_story", reflected_parameter_limit_bytes)
            {
            }
        };

        auto path(UObject* object) -> std::string
        {
            return is_uobject_valid_fast(object) ? to_utf8(object->GetPathName()) : "";
        }
    } // namespace

    void Kh02IslandMaterials::prepare(UObject* context, UObject* world)
    {
        if (material_world.Get() != world)
        {
            material_slots = 0;
            material_world = make_uobject_weak(world);
            island_world =
                world->GetPathName() == L"/Game/Maps/APKH02Mirrors/APKH02DestinyIslands.APKH02DestinyIslands";
        }
        if (!island_world || material_slots)
        {
            return;
        }
        auto* klass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/Engine.StaticMeshActor");
        for (auto* actor : find_live_actors_of_class(context, klass, actor_scan_limit))
        {
            if (!actor->GetPathName().starts_with(L"/Game/Zaps/dw/umap/dw_10/"))
            {
                continue;
            }
            auto* mesh = try_read_reflected_object_property_value(actor, L"StaticMeshComponent");
            if (!is_uobject_valid_fast(mesh))
            {
                continue;
            }
            ReflectedCall count(mesh, L"GetNumMaterials");
            count.run();
            const auto slots =
                read_function_int_param(count.function, count.parameters.data(), L"ReturnValue").value_or(0);
            if (slots < 0 || slots > material_slot_limit)
            {
                throw std::runtime_error("kh02_island_material_slots");
            }
            for (int slot = 0; slot < slots; ++slot)
            {
                ReflectedCall get(mesh, L"GetMaterial");
                get.integer(L"ElementIndex", slot);
                get.run();
                auto* source = get.result();
                const auto name = path(source);
                if (name != "/Game/Zaps/dw/uasset/Materials/MI_dw_Env_di_CloudA.MI_dw_Env_di_CloudA" &&
                    name != "/Game/Zaps/dw/uasset/Materials/MI_dw_Env_di_CloudB.MI_dw_Env_di_CloudB")
                {
                    continue;
                }
                // Rebuild the component's render state through the native MID
                // API, rather than retaining the source static draw proxy.
                ReflectedCall create(mesh, L"CreateDynamicMaterialInstance");
                create.integer(L"ElementIndex", slot);
                create.object(L"SourceMaterial", source);
                create.run();
                auto* material = create.result();
                if (!is_uobject_valid_fast(material) ||
                    material->GetClassPrivate()->GetName() != L"MaterialInstanceDynamic")
                {
                    throw std::runtime_error("kh02_island_material_instance");
                }
                // OverrideMaterials owns this MID. Rooting a component's MID
                // would also retain its old world across retry/map teardown.
                ++material_slots;
            }
        }
    }

    auto Kh02IslandMaterials::adapted_slots() const -> std::size_t
    {
        return material_slots;
    }

    void Kh02IslandMaterials::clear_world()
    {
        material_world = {};
        material_slots = 0;
        island_world = false;
    }
} // namespace kh3ap
