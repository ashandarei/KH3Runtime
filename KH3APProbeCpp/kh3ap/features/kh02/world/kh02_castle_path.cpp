#include "kh02_castle_path.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FProperty.hpp>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;

    namespace
    {
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;
        constexpr std::size_t native_vector_bytes = 3 * sizeof(float);
        constexpr int actor_scan_limit = 16'384;

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

        void capture(UObject* actor, Kh02TriggerPlacement& placement)
        {
            ReflectedCall position(actor, L"K2_GetActorLocation");
            position.run();
            placement.center = position.result_vector();
            ReflectedCall facing(actor, L"K2_GetActorRotation");
            facing.run();
            placement.rotation = facing.result_vector();
            auto* collision = try_read_reflected_object_property_value(actor, L"CollisionComponent");
            auto* bounds =
                is_uobject_valid_fast(collision) ? collision->GetPropertyByNameInChain(L"BoxExtent") : nullptr;
            if (!bounds || bounds->GetElementSize() != native_vector_bytes)
            {
                throw std::runtime_error("kh02_castle_trigger_bounds");
            }
            std::memcpy(placement.extent.data(), bounds->ContainerPtrToValuePtr<float>(collision), native_vector_bytes);
            for (std::size_t axis = 0; axis < placement.center.size(); ++axis)
            {
                if (!std::isfinite(placement.center[axis]) || !std::isfinite(placement.extent[axis]) ||
                    !std::isfinite(placement.rotation[axis]) || placement.extent[axis] <= 0.f)
                {
                    throw std::runtime_error("kh02_castle_trigger_bounds");
                }
            }
        }
    } // namespace

    auto Kh02CastlePath::configure(UObject* context, bool expanded) -> UObject*
    {
        UObject* trigger = nullptr;
        UObject* road_actor = nullptr;
        for (auto* actor : find_live_actors_of_class(context, AActor::StaticClass(), actor_scan_limit))
        {
            if (!actor->GetPathName().starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_gameflow."))
            {
                continue;
            }
            if (actor->GetName() == L"lv_EventTriggerBox_dw012")
            {
                if (trigger)
                {
                    throw std::runtime_error("kh02_castle_trigger_ambiguous");
                }
                trigger = actor;
            }
            else if (actor->GetName() == L"lv_EventTriggerBox_dw013")
            {
                if (road_actor)
                {
                    throw std::runtime_error("kh02_castle_road_ambiguous");
                }
                road_actor = actor;
            }
        }
        auto* box = try_read_reflected_object_property_value(trigger, L"CollisionComponent");
        if (!is_uobject_valid_fast(box) || !is_uobject_valid_fast(road_actor))
        {
            throw std::runtime_error("kh02_castle_trigger_box");
        }
        if (path_trigger.Get() != trigger)
        {
            capture(trigger, path_geometry);
            capture(road_actor, road_geometry);
            path_trigger = make_uobject_weak(trigger);
            road_reference = make_uobject_weak(road_actor);
            path_expanded = false;
        }
        if (path_expanded != expanded)
        {
            // Reuse the source road volume's placement while retaining the
            // wired actor and its original overlap delegate.
            const auto placement = kh02_castle_path::placement(expanded ? road_geometry : path_geometry, expanded);
            ReflectedCall size(box, L"SetBoxExtent");
            size.vector(L"InBoxExtent", placement.extent[0], placement.extent[1], placement.extent[2]);
            size.boolean(L"bUpdateOverlaps", false);
            size.run();
            ReflectedCall move(trigger, L"K2_SetActorLocationAndRotation");
            move.vector(L"NewLocation", placement.center[0], placement.center[1], placement.center[2]);
            move.vector(L"NewRotation", placement.rotation[0], placement.rotation[1], placement.rotation[2]);
            move.boolean(L"bSweep", false);
            move.boolean(L"bTeleport", true);
            move.run();
            path_expanded = expanded;
        }
        return trigger;
    }

    auto Kh02CastlePath::expanded() const -> bool
    {
        return path_expanded;
    }

    auto Kh02CastlePath::original() const -> const Kh02TriggerPlacement&
    {
        return path_geometry;
    }

    auto Kh02CastlePath::road() const -> const Kh02TriggerPlacement&
    {
        return road_geometry;
    }

    auto Kh02CastlePath::road_actor() const -> UObject*
    {
        return road_reference.Get();
    }

    auto Kh02CastlePath::snapshot() const -> Json
    {
        return {{"actor", path(path_trigger.Get())},
                {"original_center", path_geometry.center},
                {"original_extent", path_geometry.extent},
                {"upper_reach_cm", kh02_castle_path::aerial_upper_reach_cm},
                {"expanded", path_expanded}};
    }

    void Kh02CastlePath::clear_world()
    {
        path_trigger = {};
        road_reference = {};
        path_geometry = {};
        road_geometry = {};
        path_expanded = false;
    }
} // namespace kh3ap
