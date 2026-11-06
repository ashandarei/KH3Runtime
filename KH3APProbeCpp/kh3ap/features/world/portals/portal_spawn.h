#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UClass;
    class UObject;
} // namespace RC::Unreal
namespace kh3ap
{
    struct RuntimePortalSpawnDeps;
}

// Native portal operations shared by manual requests and the automatic lifecycle.
namespace kh3ap::portal_spawn_detail
{
    struct PortalSpawnVector
    {
        double x{};
        double y{};
        double z{};
    };

    auto append_resolution_detail(std::wstring* resolution_source, std::wstring_view detail)
        -> void;

    auto request_portal_class_load(const RuntimePortalSpawnDeps& deps,
                                   RC::Unreal::UObject* world_context_object,
                                   std::wstring* resolution_source) -> bool;

    auto try_find_loaded_portal_class(const RuntimePortalSpawnDeps& deps,
                                      std::wstring* resolution_source) -> RC::Unreal::UClass*;

    auto try_get_actor_location(RC::Unreal::UObject* pawn,
                                std::wstring_view actor_location_function_path,
                                std::wstring* failure_reason) -> std::optional<PortalSpawnVector>;

    auto spawn_portal(const RuntimePortalSpawnDeps& deps, RC::Unreal::UObject* pawn,
                      const PortalSpawnVector& location, bool allow_asset_load_request,
                      std::wstring* class_resolution_source, std::wstring* failure_reason)
        -> RC::Unreal::UObject*;

} // namespace kh3ap::portal_spawn_detail
