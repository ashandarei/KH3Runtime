#include "kh02_doors.h"
#include "kh3ap/features/kh02/core/kh02_world_identity.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    auto kh02_remap_native_door(UObject* actor) -> bool
    {
        const auto actor_path = actor->GetPathName();
        const kh02_native_doors::Route* route = nullptr;
        for (const auto& candidate : kh02_native_doors::routes)
        {
            if (actor_path.starts_with(candidate.source_prefix))
            {
                route = kh02_native_doors::find(actor_path, actor->GetName());
                break;
            }
        }
        if (!route)
        {
            return false;
        }
        auto* world = kh02_actor_world(actor);
        const auto* room =
            is_uobject_valid_fast(world) ? kh02_world_identity::mirror_room(world->GetPathName()) : nullptr;
        auto* map = actor->GetPropertyByNameInChain(L"m_MapName");
        auto* tag = actor->GetPropertyByNameInChain(L"m_TargetTag");
        constexpr std::size_t native_name_bytes = sizeof(FName);
        static_assert(native_name_bytes == sizeof(uint64_t));
        if (!room || room->area != route->area || !map || !tag || map->GetClass().GetName() != L"NameProperty" ||
            tag->GetClass().GetName() != L"NameProperty" || map->GetElementSize() != native_name_bytes ||
            tag->GetElementSize() != native_name_bytes)
        {
            throw std::runtime_error("kh02_native_door_layout");
        }
        auto* value = map->ContainerPtrToValuePtr<FName>(actor);
        if ((*value != FName(route->source_map.data(), FNAME_Add) &&
             *value != FName(route->destination_map.data(), FNAME_Add)) ||
            *tag->ContainerPtrToValuePtr<FName>(actor) != FName(route->target_tag.data(), FNAME_Add))
        {
            throw std::runtime_error("kh02_native_door_identity");
        }
        *value = FName(route->destination_map.data(), FNAME_Add);
        return true;
    }
} // namespace kh3ap
