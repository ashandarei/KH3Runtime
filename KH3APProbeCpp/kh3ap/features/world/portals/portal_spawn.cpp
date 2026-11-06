#include "kh3ap/features/world/portals/portal_spawn_helpers.h"
#include "kh3ap/features/world/portals/portal_spawn.h"
#include "kh3ap/bridge/utilities.h"
#include <UnrealDef.hpp>
#include <Unreal/UObject.hpp>
#include <string>
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/reflection/owned_function_cache.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include <Unreal/AActor.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/Transform.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <vector>

namespace kh3ap::portal_spawn_detail
{
    using namespace RC::Unreal;
    namespace
    {
        std::atomic<uint32_t> s_runtime_portal_class_load_sequence{};
    }
    auto append_resolution_detail(std::wstring* resolution_source, std::wstring_view detail) -> void
    {
        if (!resolution_source || detail.empty())
        {
            return;
        }
        if (!resolution_source->empty())
        {
            *resolution_source += STR("|");
        }
        *resolution_source += truncate_for_log(detail, 220);
    }

    [[nodiscard]] static auto next_runtime_portal_class_load_uuid() -> int32_t
    {
        constexpr uint32_t uuid_prefix = 0x4b500000;
        constexpr uint32_t sequence_mask = 0x000fffff;
        const uint32_t sequence =
            s_runtime_portal_class_load_sequence.fetch_add(1, std::memory_order_relaxed);
        return static_cast<int32_t>(uuid_prefix | (sequence & sequence_mask));
    }

    auto request_portal_class_load(const RuntimePortalSpawnDeps& deps,
                                   UObject* world_context_object, std::wstring* resolution_source)
        -> bool
    {
        const int32_t latent_uuid = next_runtime_portal_class_load_uuid();
        std::wstring load_source{};
        const bool requested = request_runtime_asset_class_load(
            world_context_object, deps.portal_class_path, &load_source, latent_uuid);
        append_resolution_detail(resolution_source, load_source);
        append_resolution_detail(resolution_source,
                                 std::format(STR("latent_uuid={}"), latent_uuid));
        return requested;
    }

    static auto try_get_generated_class_from_blueprint(UObject* blueprint_object,
                                                       std::wstring_view success_source,
                                                       std::wstring_view failure_source,
                                                       std::wstring* resolution_source) -> UClass*
    {
        if (!blueprint_object || !kh3ap::is_uobject_valid_fast(blueprint_object))
        {
            return nullptr;
        }

        UClass** const generated_class =
            blueprint_object->GetValuePtrByPropertyName<UClass*>(STR("GeneratedClass"));
        if (generated_class && *generated_class && kh3ap::is_uobject_valid_fast(*generated_class))
        {
            if (resolution_source)
            {
                *resolution_source = std::wstring{success_source};
            }
            return *generated_class;
        }
        if (resolution_source)
        {
            *resolution_source = std::wstring{failure_source};
        }
        return nullptr;
    }

    static auto try_find_portal_class_path(const RuntimePortalSpawnDeps& deps,
                                           std::wstring_view source,
                                           std::wstring* resolution_source) -> UClass*
    {
        UClass* const direct_class = find_loaded_runtime_asset_class(deps.portal_class_path);
        if (direct_class && kh3ap::is_uobject_valid_fast(direct_class))
        {
            if (resolution_source)
            {
                *resolution_source = std::wstring{source};
            }
            if (deps.class_cache && deps.class_cache->retain(direct_class))
            {
                append_resolution_detail(resolution_source, STR("class_rooted"));
            }
            return direct_class;
        }
        return nullptr;
    }

    auto try_find_loaded_portal_class(const RuntimePortalSpawnDeps& deps,
                                      std::wstring* resolution_source) -> UClass*
    {
        if (deps.class_cache)
        {
            if (UClass* const retained_class = deps.class_cache->retained_class())
            {
                if (resolution_source)
                {
                    *resolution_source = STR("rooted_class_cache");
                }
                return retained_class;
            }
        }

        if (UClass* const direct_class =
                try_find_portal_class_path(deps, STR("class_path"), resolution_source))
        {
            return direct_class;
        }

        UObject* const blueprint_object = find_loaded_runtime_asset(deps.portal_blueprint_path);
        if (UClass* const generated_class = try_get_generated_class_from_blueprint(
                blueprint_object, STR("blueprint_generated_class"),
                STR("blueprint_missing_generated_class"), resolution_source))
        {
            if (deps.class_cache && deps.class_cache->retain(generated_class))
            {
                append_resolution_detail(resolution_source, STR("class_rooted"));
            }
            return generated_class;
        }

        return nullptr;
    }

    static auto try_resolve_portal_class(const RuntimePortalSpawnDeps& deps,
                                         UObject* world_context_object,
                                         bool allow_asset_load_request,
                                         std::wstring* resolution_source) -> UClass*
    {
        if (UClass* const loaded_class = try_find_loaded_portal_class(deps, resolution_source))
        {
            return loaded_class;
        }

        if (!allow_asset_load_request)
        {
            append_resolution_detail(resolution_source, STR("portal_class_not_loaded"));
            return nullptr;
        }

        if (!world_context_object || !kh3ap::is_uobject_valid_fast(world_context_object))
        {
            append_resolution_detail(resolution_source,
                                     STR("kismet_load_skipped_missing_world_context"));
            return nullptr;
        }

        if (!request_portal_class_load(deps, world_context_object, resolution_source))
        {
            append_resolution_detail(resolution_source,
                                     STR("kismet_load_asset_class_request_failed"));
        }
        return nullptr;
    }

    auto try_get_actor_location(UObject* pawn, std::wstring_view actor_location_function_path,
                                std::wstring* failure_reason) -> std::optional<PortalSpawnVector>
    {
        if (!pawn || !kh3ap::is_uobject_valid_fast(pawn))
        {
            if (failure_reason)
            {
                *failure_reason = STR("invalid_pawn");
            }
            return std::nullopt;
        }

        static OwnedFunctionCache actor_location{STR("K2_GetActorLocation")};
        UFunction* const function_object =
            actor_location_function_path == STR("/Script/Engine.Actor:K2_GetActorLocation")
                ? actor_location.resolve(pawn)
                : nullptr;
        if (!function_object)
        {
            if (failure_reason)
            {
                *failure_reason = STR("missing_actor_location_function");
            }
            return std::nullopt;
        }
        FProperty* const return_property =
            find_function_param_by_name(function_object, STR("ReturnValue"));
        if (!return_property)
        {
            if (failure_reason)
            {
                *failure_reason = STR("missing_actor_location_return_value");
            }
            return std::nullopt;
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        std::fill(params.begin(), params.end(), std::byte{0});
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            pawn->ProcessEvent(function_object, params.data());
        }
        void* const value_ptr = return_property->ContainerPtrToValuePtr<void>(params.data());
        const int element_size = return_property->GetElementSize();
        if (!value_ptr || element_size < static_cast<int>(sizeof(float) * 3))
        {
            if (failure_reason)
            {
                *failure_reason =
                    std::format(STR("invalid_actor_location_return_size:{}"), element_size);
            }
            return std::nullopt;
        }

        if (element_size >= static_cast<int>(sizeof(double) * 3))
        {
            double values[3]{};
            std::memcpy(values, value_ptr, sizeof(values));
            return PortalSpawnVector{values[0], values[1], values[2]};
        }

        float values[3]{};
        std::memcpy(values, value_ptr, sizeof(values));
        return PortalSpawnVector{static_cast<double>(values[0]), static_cast<double>(values[1]),
                                 static_cast<double>(values[2])};
    }

    auto spawn_portal(const RuntimePortalSpawnDeps& deps, UObject* pawn,
                      const PortalSpawnVector& location, bool allow_asset_load_request,
                      std::wstring* class_resolution_source, std::wstring* failure_reason)
        -> UObject*
    {
        UClass* const actor_class =
            try_resolve_portal_class(deps, pawn, allow_asset_load_request, class_resolution_source);
        if (!actor_class || !kh3ap::is_uobject_valid_fast(actor_class) || !pawn ||
            !kh3ap::is_uobject_valid_fast(pawn))
        {
            if (failure_reason)
            {
                *failure_reason = std::format(STR("missing_prereq class={} pawn={}"),
                                              format_object_debug_name(actor_class),
                                              format_object_debug_name(pawn));
            }
            return nullptr;
        }

        const FTransform spawn_transform{
            FQuat{0.0, 0.0, 0.0, 1.0},
            FVector{location.x, location.y, location.z},
            FVector{1.0, 1.0, 1.0},
        };

        AActor* spawned_actor = nullptr;
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            spawned_actor = UGameplayStatics::BeginDeferredActorSpawnFromClass(
                pawn, actor_class, spawn_transform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn,
                nullptr);
        }
        if (!spawned_actor || !kh3ap::is_uobject_valid_fast(spawned_actor))
        {
            if (failure_reason)
            {
                *failure_reason = STR("typed_begin_spawn_returned_null");
            }
            return nullptr;
        }

        AActor* finished_actor = nullptr;
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            finished_actor = UGameplayStatics::FinishSpawningActor(spawned_actor, spawn_transform);
        }
        if (finished_actor && kh3ap::is_uobject_valid_fast(finished_actor))
        {
            spawned_actor = finished_actor;
        }

        return spawned_actor;
    }
} // namespace kh3ap::portal_spawn_detail

namespace kh3ap
{
    using namespace RC::Unreal;
    auto RuntimePortalClassCache::retained_class() const -> UClass*
    {
        return kh3ap::is_uobject_valid_fast(retained_class_) ? retained_class_ : nullptr;
    }

    auto RuntimePortalClassCache::retain(UClass* portal_class) -> bool
    {
        if (!kh3ap::is_uobject_valid_fast(portal_class))
        {
            return false;
        }
        if (retained_class())
        {
            return false;
        }

        // The cache intentionally owns this root for the process lifetime.
        // Clearing it during DLL teardown risks touching GUObjectArray after
        // Unreal has begun shutdown. Callers retain only a bounded set of
        // generated classes.
        portal_class->SetRootSet();
        retained_class_ = portal_class;
        return true;
    }
} // namespace kh3ap
