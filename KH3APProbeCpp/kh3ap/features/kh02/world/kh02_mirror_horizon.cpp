#include "kh02_mirror_horizon.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/AActor.hpp>

#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;

        struct ReflectedCall : Kh02ReflectedCall
        {
            ReflectedCall(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "kh02_story", reflected_parameter_limit_bytes)
            {
            }
        };

        constexpr int actor_scan_limit = 16'384;

        std::string path(UObject* object)
        {
            return is_uobject_valid_fast(object) ? to_utf8(object->GetPathName()) : "";
        }

    } // namespace

    bool Kh02MirrorHorizon::prepare(UObject* pawn)
    {
        if (!is_uobject_valid_fast(within_horizon.Get()))
        {
            for (auto* actor : find_live_actors_of_class(pawn, AActor::StaticClass(), actor_scan_limit))
            {
                if (actor->GetName() != L"SM_SkySphere_Horizon" ||
                    !actor->GetPathName().starts_with(L"/Game/Zaps/dw/umap/dw_03/dw_03_Saloon."))
                {
                    continue;
                }
                auto* component = try_read_reflected_object_property_value(actor, L"StaticMeshComponent");
                ReflectedCall material(component, L"GetMaterial");
                material.integer(L"ElementIndex", 0);
                material.run();
                if (path(material.result()) !=
                    "/Game/Zaps/dw/uasset/Materials/M_dw_Env_MirrorSky_Horizon.M_dw_Env_MirrorSky_Horizon")
                {
                    throw std::runtime_error("kh02_within_compiled_horizon_material");
                }
                within_horizon = make_uobject_weak(material.result());
            }
            if (!is_uobject_valid_fast(within_horizon.Get()))
            {
                return false;
            }
        }

        return true;
    }

} // namespace kh3ap
