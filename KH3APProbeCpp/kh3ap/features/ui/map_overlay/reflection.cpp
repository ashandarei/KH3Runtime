#include "kh3ap/features/ui/map_overlay/gfx_bindings.h"
#include "kh3ap/features/ui/map_overlay/support.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/ui/live_ui_parts_cache.h"
#include "kh3ap/core/reflection/owned_function_cache.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/features/grants/abilities/ability_snapshot_helpers.h"
#include <Unreal/NameTypes.hpp>
#include <Unreal/UObject.hpp>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace map_overlay_detail;
    namespace map_overlay_detail
    {
        namespace reflection_limits
        {
            constexpr size_t cached_properties = 32;
            constexpr int32_t maximum_map_rows = 1024;
            constexpr int32_t maximum_map_row_capacity = 2048;
            constexpr int32_t maximum_asset_path_chars = 512;
            constexpr int32_t maximum_ui_parts = 128;
            constexpr int32_t row_struct_offset = 0x40;
            constexpr int32_t row_map_offset = 0x48;
            constexpr size_t native_row_entry_bytes = 24;
            constexpr size_t soft_object_path_bytes = 32;
        } // namespace reflection_limits
        using Vector3 = std::array<float, 3>;
        thread_local ReflectedPropertyCache<reflection_limits::cached_properties>
            overlay_properties;
        template <class T> auto memory_value(uint64_t address) -> std::optional<T>
        {
            const auto bytes = try_read_process_memory_bytes(address, sizeof(T));
            if (!bytes || bytes->size() != sizeof(T))
            {
                return {};
            }
            T value{};
            std::memcpy(&value, bytes->data(), sizeof(T));
            return value;
        }
        auto viewport_size(UObject* pawn) -> std::optional<MinimapLayoutCache::Viewport>
        {
            static ReflectedPropertyCache<1> properties;
            auto* property = properties.resolve_object(pawn, L"Controller");
            if (!property)
            {
                return {};
            }
            auto* controller = *property->ContainerPtrToValuePtr<UObject*>(pawn);
            static OwnedFunctionCache function{L"GetViewportSize"};
            auto* fn = function.resolve(controller);
            if (!is_uobject_valid_fast(fn) ||
                fn->GetParmsSize() != sizeof(MinimapLayoutCache::Viewport))
            {
                return {};
            }
            static FWeakObjectPtr bound;
            if (bound.Get() != fn)
            {
                constexpr int32_t dimension_bytes = sizeof(int32_t);
                for (const auto& [name, offset] :
                     {std::pair{L"SizeX", 0}, std::pair{L"SizeY", dimension_bytes}})
                {
                    auto* field = find_function_param_by_name(fn, name);
                    if (!field || field->GetClass().GetName() != L"IntProperty" ||
                        field->GetElementSize() != dimension_bytes ||
                        field->GetOffset_Internal() != offset)
                    {
                        return {};
                    }
                }
                bound = make_uobject_weak(fn);
            }
            MinimapLayoutCache::Viewport size{};
            controller->ProcessEvent(fn, size.data());
            return size[0] > 0 && size[1] > 0 ? std::optional{size} : std::nullopt;
        }

        auto object(UObject* owner, const wchar_t* field) -> UObject*
        {
            if (!is_uobject_valid_fast(owner))
            {
                return nullptr;
            }
            auto* property = overlay_properties.resolve_object(owner, field);
            if (!property)
            {
                return nullptr;
            }
            UObject* result{};
            std::memcpy(&result, property->ContainerPtrToValuePtr<void>(owner), sizeof(result));
            return is_uobject_valid_fast(result) ? result : nullptr;
        }

        auto area_metadata(UObject* texture) -> std::optional<MapOverlayArea>
        {
            const RuntimeHotPathProfileScope profile{L"map_overlay:area_metadata"};
            auto* table = find_loaded_runtime_asset(L"/Game/UI/Data/NaviMapData.NaviMapData");
            auto* row = object(table, L"RowStruct");
            if (!is_uobject_a_fast(row, UScriptStruct::StaticClass()) ||
                row->GetPathName() != L"/Script/TresGame.TresNaviMapData")
            {
                return {};
            }
            auto* row_struct = static_cast<UScriptStruct*>(row);
            auto* row_property = table->GetPropertyByNameInChain(L"RowStruct");
            // KH3's immutable DataTable RowMap immediately follows its reflected RowStruct.
            if (!row_property ||
                row_property->GetOffset_Internal() != reflection_limits::row_struct_offset ||
                row_property->GetElementSize() != sizeof(UObject*))
            {
                return {};
            }
            struct Header
            {
                uint64_t data;
                int32_t count, capacity;
            };
            struct Entry
            {
                FName name;
                uint64_t data;
                int64_t padding;
            };
            static_assert(sizeof(Entry) == reflection_limits::native_row_entry_bytes);
            const auto header = memory_value<Header>(reinterpret_cast<uint64_t>(table) +
                                                     reflection_limits::row_map_offset);
            if (!header || !header->data || header->count <= 0 ||
                header->count > reflection_limits::maximum_map_rows ||
                header->capacity < header->count ||
                header->capacity > reflection_limits::maximum_map_row_capacity)
            {
                return {};
            }
            auto* scale = find_struct_field_by_name(row_struct, L"Scale");
            auto* origin = find_struct_field_by_name(row_struct, L"Origin");
            auto* texture_asset = find_struct_field_by_name(row_struct, L"TextureAsset");
            if (!scale || scale->GetClass().GetName() != L"FloatProperty" ||
                scale->GetElementSize() != sizeof(float) || !origin ||
                origin->GetClass().GetName() != L"StructProperty" ||
                origin->GetElementSize() != sizeof(std::array<float, 2>) || !texture_asset ||
                texture_asset->GetClass().GetName() != L"AssetObjectProperty" ||
                texture_asset->GetElementSize() != reflection_limits::soft_object_path_bytes)
            {
                return {};
            }
            const auto texture_path = texture->GetPathName();
            for (int i = 0; i < header->count; ++i)
            {
                const auto entry = memory_value<Entry>(header->data + i * sizeof(Entry));
                if (!entry || !entry->data)
                {
                    continue;
                }
                // Row keys are logical areas, not texture names: e.g. ca02_Lagoon
                // references ca02_02. KH3's legacy AssetObjectProperty stores its
                // FString asset path at +0x10, after the weak pointer/cache fields.
                // Read the reference only; never load or copy a map asset.
                const auto path =
                    memory_value<Header>(entry->data + texture_asset->GetOffset_Internal() + 0x10);
                if (!path || !path->data || path->count <= 1 ||
                    path->count > reflection_limits::maximum_asset_path_chars ||
                    path->capacity < path->count)
                {
                    continue;
                }
                const auto bytes =
                    try_read_process_memory_bytes(path->data, path->count * sizeof(wchar_t));
                if (!bytes || bytes->size() != path->count * sizeof(wchar_t))
                {
                    continue;
                }
                std::wstring asset_path(static_cast<size_t>(path->count), L'\0');
                std::memcpy(asset_path.data(), bytes->data(), bytes->size());
                if (asset_path.back() != L'\0')
                {
                    continue;
                }
                asset_path.pop_back();
                if (asset_path != texture_path)
                {
                    continue;
                }
                const auto s = memory_value<float>(entry->data + scale->GetOffset_Internal());
                const auto o =
                    memory_value<std::array<float, 2>>(entry->data + origin->GetOffset_Internal());
                if (!s || !o)
                {
                    return {};
                }
                const MapOverlayArea area{*s, (*o)[0], (*o)[1]};
                return map_overlay_world_point(area, 0, 0) ? std::optional{area} : std::nullopt;
            }
            return {};
        }

        auto actor_location(UObject* actor) -> std::optional<std::array<float, 3>>
        {
            if (!is_uobject_valid_fast(actor))
            {
                return {};
            }
            static OwnedFunctionCache location{L"K2_GetActorLocation"};
            auto* fn = location.resolve(actor);
            if (!is_uobject_valid_fast(fn) || fn->GetParmsSize() <= 0 ||
                fn->GetParmsSize() > max_gfx_parameter_bytes)
            {
                return {};
            }
            auto* result = find_function_param_by_name(fn, L"ReturnValue");
            if (!result || result->GetClass().GetName() != L"StructProperty" ||
                result->GetElementSize() != sizeof(Vector3) || result->GetOffset_Internal() < 0 ||
                result->GetOffset_Internal() + sizeof(Vector3) > fn->GetParmsSize())
            {
                return {};
            }
            alignas(std::max_align_t) std::array<std::byte, max_gfx_parameter_bytes> params{};
            actor->ProcessEvent(fn, params.data());
            std::array<float, 3> value{};
            std::memcpy(value.data(), result->ContainerPtrToValuePtr<void>(params.data()),
                        sizeof(value));
            for (auto v : value)
            {
                if (!std::isfinite(v))
                {
                    return {};
                }
            }
            return value;
        }

        auto root(UObject* ui) -> UObject*
        {
            return object(object(ui, L"m_ASProxyUIComponent"), L"m_ASObject");
        }

        auto live_parts(UObject* pawn) -> std::pair<UObject*, UObject*>
        {
            if (!is_uobject_valid_fast(pawn))
            {
                return {};
            }
            auto* level = pawn->GetOuterPrivate();
            auto* world = is_uobject_valid_fast(level) ? level->GetOuterPrivate() : nullptr;
            auto* manager = object(object(world, L"OwningGameInstance"), L"m_UIManager");
            if (!manager)
            {
                return {};
            }
            auto* property = overlay_properties.resolve(manager, L"m_UIPartsAry");
            if (!property || property->GetClass().GetName() != L"ArrayProperty")
            {
                return {};
            }
            struct Array
            {
                UObject** data;
                int32_t count, capacity;
            };
            const auto* parts = property->ContainerPtrToValuePtr<Array>(manager);
            if (!parts || !parts->data || parts->count < 0 ||
                parts->count > reflection_limits::maximum_ui_parts ||
                parts->capacity < parts->count)
            {
                return {};
            }
            static LiveUiPartsCache slots;
            return slots.resolve(manager, parts->data, parts->count);
        }
    } // namespace map_overlay_detail
} // namespace kh3ap
