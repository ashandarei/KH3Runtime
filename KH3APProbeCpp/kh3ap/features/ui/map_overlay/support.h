#pragma once
#include "kh3ap/features/ui/map_overlay/map_overlay_service.h"
#include "kh3ap/features/ui/map_overlay/map_overlay_math.h"
#include "kh3ap/features/ui/map_overlay/minimap_layout_cache.h"
#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <utility>
namespace RC::Unreal
{
    class FProperty;
}
namespace kh3ap::map_overlay_detail
{
    using RC::Unreal::FProperty;
    using RC::Unreal::UObject;
    inline constexpr uint8_t map_movie_priority = 121, icon_movie_priority = 122,
                             minimap_icon_priority = 123;
    struct GfxDisplayInfo
    {
        std::array<float, 3> position{}, scale{};
        float rotation{}, alpha{};
        bool visible{};
    };

    auto borrow_string(FProperty* property, void* buffer, const wchar_t* text) -> bool;

    auto gfx_number(const GfxProbeFunctionPaths& paths, UObject* object, const wchar_t* name,
                    bool boolean = false) -> std::optional<double>;

    auto gfx_object(const GfxProbeFunctionPaths& paths, UObject* object, const wchar_t* name)
        -> UObject*;

    auto gfx_set(const GfxProbeFunctionPaths& paths, UObject* object, const wchar_t* name,
                 double value, bool boolean = false) -> bool;

    auto gfx_numbers(const GfxProbeFunctionPaths& paths, UObject* object, const wchar_t* method,
                     std::initializer_list<double> values, std::optional<double>* number = nullptr)
        -> UObject*;

    auto gfx_object_argument(const GfxProbeFunctionPaths& paths, UObject* object,
                             const wchar_t* method, UObject* argument) -> UObject*;

    auto gfx_invoke(const GfxProbeFunctionPaths& paths, UObject* object, const wchar_t* method,
                    const wchar_t* value = nullptr) -> bool;

    auto gfx_display_info(UObject* object) -> std::optional<GfxDisplayInfo>;

    auto gfx_position(UObject* object, double x, double y) -> bool;

    auto viewport_size(UObject* pawn) -> std::optional<MinimapLayoutCache::Viewport>;

    auto object(UObject* owner, const wchar_t* field) -> UObject*;

    auto area_metadata(UObject* texture) -> std::optional<MapOverlayArea>;

    auto actor_location(UObject* actor) -> std::optional<std::array<float, 3>>;

    auto root(UObject* ui) -> UObject*;

    auto live_parts(UObject* pawn) -> std::pair<UObject*, UObject*>;

    auto movie_bool(UObject* movie, const wchar_t* path, const wchar_t* parameter, bool value)
        -> void;

    auto open_movie(UObject* asset, const GfxProbeFunctionPaths& paths, uint8_t priority)
        -> UObject*;
} // namespace kh3ap::map_overlay_detail
