#include <memory>
#include <optional>
#include <utility>
#include "kh3ap/features/journal/ingredient_journal_map_service.h"
#include "kh3ap/features/journal/ingredient_journal_catalog.h"
#include "kh3ap/features/journal/adversary_journal_catalog.h"
#include "kh3ap/features/ingredients/ingredient_spots.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/features/grants/abilities/ability_snapshot_helpers.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/features/ui/map_overlay/map_overlay_math.h"
#include "kh3ap/core/scene_attachment_math.h"
#include "kh3ap/core/performance_build.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <vector>
using namespace RC;
using namespace RC::Unreal;
namespace kh3ap
{
    namespace
    {
        constexpr std::wstring_view level_path_segment = L"/Levels/";
        constexpr auto world_code_chars = 2;
        constexpr auto map_area_code_offset = 4; // Slash, world code and underscore.
        constexpr auto map_family_chars = world_code_chars + world_code_chars;
        constexpr auto map_area_code_end = map_area_code_offset + world_code_chars;
        constexpr auto journal_map_pane_mode = 2;
        constexpr auto max_concurrent_map_packages = 4;
        constexpr uint32_t journal_map_load_uuid_base = 0x4b483400;
        constexpr uint64_t fnv1a_offset_basis = 14695981039346656037ULL;
        constexpr uint64_t fnv1a_prime = 1099511628211ULL;
        constexpr auto byte_mask = 0xff;

        constexpr int retained_reference_growth_guard = 128;
        constexpr int native_scene_vector_bytes = 12;
        constexpr int level_owning_world_offset = 0xd8;
        constexpr int level_world_settings_offset = 0x310;
        constexpr uint64_t level_actor_array_offset = 0xb8;
        constexpr int data_table_row_struct_offset = 0x40;
        constexpr uint64_t data_table_row_map_offset = 0x48;
        constexpr int legacy_asset_object_bytes = 32;
        constexpr uint64_t legacy_asset_path_offset = 0x10;

        constexpr auto ingredient_map_package_timeout = std::chrono::seconds(30);
        constexpr auto max_journal_map_world_coordinate = 10000000;
        constexpr auto max_journal_map_outer_depth = 32;
        constexpr auto max_journal_actor_array_capacity = 65536;
        constexpr auto max_journal_map_rows = 1024;
        constexpr auto max_journal_map_row_capacity = 2048;
        constexpr auto max_journal_map_asset_path_characters = 512;
        constexpr auto max_journal_map_texture_dimension = 16384;
        constexpr auto journal_map_preview_cache_capacity = 64;

        using Point = std::array<float, 3>;
        auto split(std::wstring_view text, wchar_t separator) -> std::vector<std::wstring>
        {
            std::vector<std::wstring> result;
            for (size_t p = 0; p < text.size();)
            {
                auto end = text.find(separator, p);
                if (end == text.npos)
                {
                    end = text.size();
                }
                if (end > p)
                {
                    result.emplace_back(text.substr(p, end - p));
                }
                p = end + 1;
            }
            return result;
        }
        auto package(std::wstring_view actor) -> std::wstring
        {
            return std::wstring(actor.substr(0, actor.find(L':')));
        }
        auto family(std::wstring_view actor) -> std::wstring
        {
            const auto start = actor.find(level_path_segment);
            if (start == actor.npos)
            {
                return {};
            }
            const auto map = actor.find(L'/', start + level_path_segment.size());
            if (map == actor.npos || actor.size() < map + map_area_code_end)
            {
                return {};
            }
            return std::wstring(actor.substr(start + level_path_segment.size(), world_code_chars)) +
                   std::wstring(actor.substr(map + map_area_code_offset, world_code_chars));
        }
        template <class T> auto memory(uint64_t address) -> std::optional<T>
        {
            auto bytes = try_read_process_memory_bytes(address, sizeof(T));
            if (!bytes || bytes->size() != sizeof(T))
            {
                return {};
            }
            T value{};
            std::memcpy(&value, bytes->data(), sizeof(T));
            return value;
        }
        auto scene_vector(UObject* component, const wchar_t* name) -> std::optional<Point>
        {
            if (!is_uobject_valid_fast(component))
            {
                return {};
            }
            auto* property = component->GetPropertyByNameInChain(name);
            if (!property || property->GetClass().GetName() != L"StructProperty" ||
                property->GetElementSize() != native_scene_vector_bytes)
            {
                return {};
            }
            Point result{};
            std::memcpy(result.data(), property->ContainerPtrToValuePtr<void>(component),
                        sizeof(result));
            for (auto v : result)
            {
                if (!std::isfinite(v) || std::abs(v) > max_journal_map_world_coordinate)
                {
                    return {};
                }
            }
            return result;
        }
        auto authored_location(UObject* actor) -> std::optional<Point>
        {
            auto* current = try_read_reflected_object_property_value(actor, L"RootComponent");
            auto result = scene_vector(current, L"RelativeLocation");
            if (!result)
            {
                return {};
            }
            // Inactive components have no initialized world transform. Compose their
            // serialized scene-parent chain, including enemy group/folder transforms.
            // Socket attachments and absolute transform flags need different rules;
            // omit those instead of inventing a placement. Retain only scalar results.
            std::set<UObject*> seen;
            for (int depth = 0; depth < max_journal_map_outer_depth; ++depth)
            {
                if (!is_uobject_valid_fast(current) || !seen.insert(current).second)
                {
                    return {};
                }
                auto* parent = try_read_reflected_object_property_value(current, L"AttachParent");
                if (!parent)
                {
                    return result;
                }
                if (!is_uobject_valid_fast(parent))
                {
                    return {};
                }
                for (const auto* name :
                     {L"bAbsoluteLocation", L"bAbsoluteRotation", L"bAbsoluteScale"})
                {
                    auto* flag = current->GetPropertyByNameInChain(name);
                    if (!flag || flag->GetClass().GetName() != L"BoolProperty" ||
                        static_cast<FBoolProperty*>(flag)->GetPropertyValueInContainer(current))
                    {
                        return {};
                    }
                }
                auto* socket = current->GetPropertyByNameInChain(L"AttachSocketName");
                if (!socket || socket->GetClass().GetName() != L"NameProperty" ||
                    socket->GetElementSize() != sizeof(FName) ||
                    socket->ContainerPtrToValuePtr<FName>(current)->ToString() != L"None")
                {
                    return {};
                }
                const auto location = scene_vector(parent, L"RelativeLocation"),
                           rotation = scene_vector(parent, L"RelativeRotation"),
                           scale = scene_vector(parent, L"RelativeScale3D");
                if (!location || !rotation || !scale)
                {
                    return {};
                }
                result = scene_parent_point(*result, *location, *rotation, *scale);
                if (!result)
                {
                    return {};
                }
                current = parent;
            }
            return {};
        }
        auto authored_level_actors(UObject* level, UObject* world)
            -> std::optional<std::vector<UObject*>>
        {
            if (!is_uobject_valid_fast(level) || !is_uobject_valid_fast(world) ||
                level->GetOuterPrivate() != world)
            {
                return {};
            }
            // KH3's serialized ULevel Actors array precedes ActorsForGC and the
            // reflected OwningWorld. Verify both reflected anchors and the array's
            // first WorldSettings actor before accepting this native layout.
            // The asset world is the level's outer, not necessarily OwningWorld:
            // streaming sets OwningWorld to the active gameplay world. A source
            // already streamed into gameplay is still a valid package to inspect.
            auto* owning = level->GetPropertyByNameInChain(L"OwningWorld");
            auto* settings = level->GetPropertyByNameInChain(L"WorldSettings");
            if (!owning || owning->GetClass().GetName() != L"ObjectProperty" ||
                owning->GetOffset_Internal() != level_owning_world_offset ||
                owning->GetElementSize() != sizeof(UObject*) || !settings ||
                settings->GetClass().GetName() != L"ObjectProperty" ||
                settings->GetOffset_Internal() != level_world_settings_offset ||
                settings->GetElementSize() != sizeof(UObject*))
            {
                return {};
            }
            struct Header
            {
                uint64_t data;
                int32_t count, capacity;
            };
            const auto address = reinterpret_cast<uint64_t>(level) + level_actor_array_offset;
            const auto header = memory<Header>(address);
            if (!header || !header->data || header->count < 1 || header->count > header->capacity ||
                header->capacity > max_journal_actor_array_capacity)
            {
                return {};
            }
            const auto bytes = try_read_process_memory_bytes(
                header->data, static_cast<size_t>(header->count) * sizeof(UObject*));
            if (!bytes)
            {
                return {};
            }
            std::vector<UObject*> actors(header->count);
            std::memcpy(actors.data(), bytes->data(), bytes->size());
            auto* first = actors.front();
            if (!is_uobject_valid_fast(first) || first->GetOuterPrivate() != level ||
                try_read_reflected_object_property_value(level, L"WorldSettings") != first)
            {
                return {};
            }
            const auto after = memory<Header>(address);
            if (!after || std::memcmp(&*header, &*after, sizeof(Header)))
            {
                return {};
            }
            return actors;
        }
        struct Sheet
        {
            std::wstring key, texture, label;
            MapOverlayArea area;
            int width{}, height{};
            std::vector<MapOverlayPoint> points;
        };
        struct Preview
        {
            std::vector<Sheet> sheets;
            std::wstring payload;
        };
        auto definitions(const std::set<std::wstring>& families) -> std::vector<Sheet>
        {
            auto* table = find_loaded_runtime_asset(L"/Game/UI/Data/NaviMapData.NaviMapData");
            auto* row = try_read_reflected_object_property_value(table, L"RowStruct");
            if (!is_uobject_a_fast(row, UScriptStruct::StaticClass()) ||
                row->GetPathName() != L"/Script/TresGame.TresNaviMapData")
            {
                return {};
            }
            auto* property = table->GetPropertyByNameInChain(L"RowStruct");
            if (!property || property->GetOffset_Internal() != data_table_row_struct_offset ||
                property->GetElementSize() != sizeof(UScriptStruct*))
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
            static_assert(sizeof(Entry) == 24);
            auto header =
                memory<Header>(reinterpret_cast<uint64_t>(table) + data_table_row_map_offset);
            if (!header || !header->data || header->count < 1 ||
                header->count > max_journal_map_rows || header->capacity < header->count ||
                header->capacity > max_journal_map_row_capacity)
            {
                return {};
            }
            auto* type = static_cast<UScriptStruct*>(row);
            auto* scale = find_struct_field_by_name(type, L"Scale");
            auto* origin = find_struct_field_by_name(type, L"Origin");
            auto* texture = find_struct_field_by_name(type, L"TextureAsset");
            if (!scale || scale->GetClass().GetName() != L"FloatProperty" ||
                scale->GetElementSize() != sizeof(float) || !origin ||
                origin->GetClass().GetName() != L"StructProperty" ||
                origin->GetElementSize() != sizeof(std::array<float, 2>) || !texture ||
                texture->GetClass().GetName() != L"AssetObjectProperty" ||
                texture->GetElementSize() != legacy_asset_object_bytes)
            {
                return {};
            }
            std::vector<Sheet> result;
            for (int i = 0; i < header->count; ++i)
            {
                const auto entry = memory<Entry>(header->data + i * sizeof(Entry));
                if (!entry || !entry->data)
                {
                    continue;
                }
                const auto key = entry->name.ToString();
                if (key.size() < map_family_chars ||
                    !families.contains(key.substr(0, map_family_chars)))
                {
                    continue;
                }
                auto path = memory<Header>(entry->data + texture->GetOffset_Internal() +
                                           legacy_asset_path_offset);
                if (!path || !path->data || path->count <= 1 ||
                    path->count > max_journal_map_asset_path_characters ||
                    path->capacity < path->count)
                {
                    continue;
                }
                auto bytes =
                    try_read_process_memory_bytes(path->data, path->count * sizeof(wchar_t));
                if (!bytes)
                {
                    continue;
                }
                std::wstring asset(static_cast<size_t>(path->count), L'\0');
                std::memcpy(asset.data(), bytes->data(), bytes->size());
                if (asset.back() != L'\0')
                {
                    continue;
                }
                asset.pop_back();
                if (!asset.starts_with(L"/Game/UI/Icon/Map/"))
                {
                    continue;
                }
                auto s = memory<float>(entry->data + scale->GetOffset_Internal());
                auto o = memory<std::array<float, 2>>(entry->data + origin->GetOffset_Internal());
                if (!s || !o)
                {
                    continue;
                }
                Sheet sheet{key, asset, key, {*s, (*o)[0], (*o)[1]}};
                if (!map_overlay_world_point(sheet.area, 0, 0))
                {
                    continue;
                }
                for (const auto& label : ingredient_journal_map_labels)
                {
                    if (label.key == key)
                    {
                        sheet.label = label.label;
                        break;
                    }
                }
                if (sheet.label == key)
                {
                    continue;
                }
                result.push_back(std::move(sheet));
            }
            return result;
        }
        auto dimension(UObject* texture, const wchar_t* name) -> int
        {
            if (!is_uobject_valid_fast(texture))
            {
                return 0;
            }
            auto* fn = texture->GetFunctionByNameInChain(name);
            auto* returned = is_uobject_valid_fast(fn)
                                 ? find_function_param_by_name(fn, L"ReturnValue")
                                 : nullptr;
            if (!returned || fn->GetParmsSize() != sizeof(int32_t) ||
                returned->GetClass().GetName() != L"IntProperty" ||
                returned->GetElementSize() != sizeof(int32_t) ||
                returned->GetOffset_Internal() != 0)
            {
                return 0;
            }
            int32_t value{};
            texture->ProcessEvent(fn, &value);
            return value > 0 && value <= max_journal_map_texture_dimension ? value : 0;
        }
    } // namespace
    struct RuntimeIngredientJournalMapService::Impl
    {
        GfxProbeFunctionPaths gfx;
        std::function<UObject*()> context, asset;
        std::wstring key, state{L"closed"}, pending;
        int serial{-1};
        bool done{}, presentation_ready{};
        int references_base{-1};
        FWeakObjectPtr references_asset;
        uint32_t load_uuid{journal_map_load_uuid_base};
        std::chrono::steady_clock::time_point deadline{};
        std::vector<std::wstring> actors, packages;
        std::vector<Sheet> sheets;
        std::set<std::wstring> cached_packages, requested_textures;
        std::set<std::wstring> requested_packages;
        std::map<std::wstring, Point> positions;
        std::map<std::wstring, std::set<std::wstring>> package_actors;
        std::map<std::wstring, Preview> previews;
        std::deque<std::wstring> preview_order;
        std::wstring cached_payload;
        bool actors_indexed{};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        std::chrono::steady_clock::time_point preview_started{};
        bool ready_logged{};
        int package_requests{}, texture_requests{};
        double coordinate_ms{};
        uint64_t payload_hash{};
#endif
        Impl(GfxProbeFunctionPaths g, std::function<UObject*()> c, std::function<UObject*()> a)
            : gfx(std::move(g)), context(std::move(c)), asset(std::move(a))
        {
        }
        auto references(UObject* owner) -> RC::Unreal::TArray<UObject*>*
        {
            if (!is_uobject_valid_fast(owner))
            {
                return nullptr;
            }
            auto* p = owner->GetPropertyByNameInChain(L"UserReferences");
            if (!p || p->GetClass().GetName() != L"ArrayProperty")
            {
                return nullptr;
            }
            auto* inner = static_cast<FArrayProperty*>(p)->GetInner();
            if (!inner || inner->GetClass().GetName() != L"ObjectProperty" ||
                inner->GetElementSize() != sizeof(UObject*))
            {
                return nullptr;
            }
            return p->ContainerPtrToValuePtr<RC::Unreal::TArray<UObject*>>(owner);
        }
        auto retain(UObject* texture) -> bool
        {
            auto* owner = asset();
            auto* values = references(owner);
            if (!values)
            {
                return false;
            }
            if (references_base < 0)
            {
                references_base = values->Num();
                references_asset = make_uobject_weak(owner);
            }
            if (references_asset.Get() != owner || values->Num() < references_base ||
                values->Num() > references_base + retained_reference_growth_guard)
            {
                return false;
            }
            for (int i = references_base; i < values->Num(); ++i)
            {
                if ((*values)[i] == texture)
                {
                    return true;
                }
            }
            values->Add(texture);
            return true;
        }
        auto cancel() -> void
        {
            if (auto* values = references(references_asset.Get());
                values && references_base >= 0 && values->Num() >= references_base)
            {
                values->SetNum(references_base);
            }
            references_base = -1;
            references_asset = nullptr;
            key.clear();
            pending.clear();
            serial = -1;
            done = false;
            presentation_ready = false;
            state = L"closed";
            actors.clear();
            packages.clear();
            sheets.clear();
            requested_textures.clear();
            requested_packages.clear();
            cached_payload.clear();
            // positions/cached_packages contain scalars and names only. Never root
            // or retain inactive worlds, levels, components or actors; UE GC owns
            // their release after the latent loader drops its temporary reference.
        }
        auto fail(UObject* page, const wchar_t* message) -> void
        {
            done = true;
            state = L"failed";
            try_gfx_invoke_string_arg_object(gfx, page, L"mapError", message);
            Output::send<LogLevel::Warning>(
                L"[KH3APProbeCpp] Ingredient journal map unavailable source={} reason={}\n", key,
                message);
        }
        auto request(std::wstring_view path) -> bool
        {
            return request_runtime_asset_load(context(), path, nullptr,
                                              static_cast<int32_t>(load_uuid++));
        }
        auto cache_package(std::wstring_view path, UObject* world) -> bool
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const auto started = std::chrono::steady_clock::now();
#endif
            if (!actors_indexed)
            {
                for (const auto& entry : ingredient_spot_actors)
                {
                    package_actors[package(entry.path)].emplace(entry.path);
                }
                for (const auto& source : adversary_journal_sources)
                {
                    for (const auto& actor : split(source.actors, L'|'))
                    {
                        package_actors[package(actor)].insert(actor);
                    }
                }
                actors_indexed = true;
            }
            auto* level = try_read_reflected_object_property_value(world, L"PersistentLevel");
            if (!is_uobject_valid_fast(level))
            {
                return false;
            }
            // Enumerate this source level only. UE4SS FindObject can search the
            // global object array, so per-path calls are not a direct hash lookup.
            const auto indexed = package_actors.find(std::wstring(path));
            if (indexed != package_actors.end())
            {
                const auto actors = authored_level_actors(level, world);
                if (!actors)
                {
                    return false;
                }
                for (auto* object : *actors)
                {
                    if (!is_uobject_valid_fast(object) || object->GetOuterPrivate() != level)
                    {
                        continue;
                    }
                    const auto actor = object->GetPathName();
                    if (indexed->second.contains(actor) && !positions.contains(actor))
                    {
                        if (auto location = authored_location(object))
                        {
                            positions.emplace(actor, *location);
                        }
                    }
                }
            }
            cached_packages.emplace(path);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            coordinate_ms += std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
#endif
            return true;
        }
        bool begin_preview(UObject* page, int next, const std::wstring& selected)
        {
            cancel();
            key = selected;
            serial = next;
            state = L"loading";
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            preview_started = std::chrono::steady_clock::now();
            ready_logged = false;
            package_requests = texture_requests = 0;
            coordinate_ms = 0;
#endif
            for (const auto& source : ingredient_journal_sources)
            {
                if (source.key == key)
                {
                    actors = split(source.actors, L'|');
                    break;
                }
            }
            if (actors.empty())
            {
                for (const auto& source : adversary_journal_sources)
                {
                    if (source.key == key)
                    {
                        actors = split(source.actors, L'|');
                        break;
                    }
                }
            }
            if (actors.empty())
            {
                fail(page, L"No verified source data is available for this location.");
                return false;
            }
            std::set<std::wstring> unique;
            for (const auto& actor : actors)
            {
                unique.insert(package(actor));
            }
            packages.assign(unique.begin(), unique.end());
            deadline = std::chrono::steady_clock::now() + ingredient_map_package_timeout;
            if (const auto cached = previews.find(key); cached != previews.end())
            {
                sheets = cached->second.sheets;
                cached_payload = cached->second.payload;
                packages.clear();
            }
            return true;
        }
        void observe_preview_presentation(UObject* page)
        {
            if (state == L"published")
            {
                const auto display = try_gfx_get_member_value_string(gfx, page, L"mapState");
                presentation_ready = display == L"ready" || display == L"failed";
                if (!presentation_ready && std::chrono::steady_clock::now() >= deadline)
                {
                    fail(page, L"Map display timed out. Try this location again.");
                }
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            if (!ready_logged && state == L"published" &&
                try_gfx_get_member_value_string(gfx, page, L"mapState") == L"ready")
            {
                ready_logged = true;
                Output::send<LogLevel::Normal>(
                    L"[KH3APProbeCpp] Journal preview timing source={} ready_ms={} "
                    L"coordinate_ms={} package_requests={} texture_requests={} "
                    L"payload_hash={}\n",
                    key,
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                              preview_started)
                        .count(),
                    coordinate_ms, package_requests, texture_requests, payload_hash);
            }
#endif
            return;
        }
        enum class AssetPreparation
        {
            Ready,
            Pending,
            Failed,
        };
        AssetPreparation prepare_preview_assets(UObject* page)
        {
            if (sheets.empty())
            {
                std::set<std::wstring> families;
                for (const auto& actor : actors)
                {
                    families.insert(family(actor));
                }
                if (!is_uobject_valid_fast(
                        find_loaded_runtime_asset(L"/Game/UI/Data/NaviMapData.NaviMapData")))
                {
                    if (pending != L"metadata")
                    {
                        request(L"/Game/UI/Data/NaviMapData.NaviMapData");
                        pending = L"metadata";
                    }
                    return AssetPreparation::Failed;
                }
                sheets = definitions(families);
                if (sheets.empty())
                {
                    fail(page, L"No map is available for this location.");
                    return AssetPreparation::Failed;
                }
            }
            bool waiting = false;
            for (auto& sheet : sheets)
            {
                auto* texture = find_loaded_runtime_asset(sheet.texture);
                if (!is_uobject_valid_fast(texture))
                {
                    waiting = true;
                    if (requested_textures.insert(sheet.texture).second)
                    {
                        if (!request(sheet.texture))
                        {
                            fail(page, L"A map texture could not be loaded.");
                            return AssetPreparation::Failed;
                        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                        ++texture_requests;
#endif
                    }
                    continue;
                }
                if (!retain(texture))
                {
                    fail(page, L"A map texture could not be retained.");
                    return AssetPreparation::Failed;
                }
                sheet.width = dimension(texture, L"Blueprint_GetSizeX");
                sheet.height = dimension(texture, L"Blueprint_GetSizeY");
                if (!sheet.width || !sheet.height)
                {
                    fail(page, L"Map dimensions could not be verified.");
                    return AssetPreparation::Failed;
                }
            }
            // Queue texture and geometry loads together, with at most four
            // inactive sublevels in flight. Inspect loaded packages before adding
            // another request; the latent loader's UUIDs remain distinct.
            for (auto it = packages.begin(); it != packages.end();)
            {
                if (cached_packages.contains(*it))
                {
                    requested_packages.erase(*it);
                    it = packages.erase(it);
                    continue;
                }
                auto* loaded = find_loaded_runtime_asset(*it);
                if (is_uobject_valid_fast(loaded))
                {
                    if (!cache_package(*it, loaded))
                    {
                        fail(page, L"Source level data could not be verified.");
                        return AssetPreparation::Failed;
                    }
                    requested_packages.erase(*it);
                    it = packages.erase(it);
                    continue;
                }
                ++it;
            }
            for (const auto& path : packages)
            {
                if (requested_packages.size() >= max_concurrent_map_packages)
                {
                    break;
                }
                if (requested_packages.contains(path))
                {
                    continue;
                }
                if (!request(path))
                {
                    fail(page, L"This location could not be loaded.");
                    return AssetPreparation::Failed;
                }
                requested_packages.insert(path);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                ++package_requests;
#endif
            }
            return waiting || !packages.empty() ? AssetPreparation::Pending
                                                : AssetPreparation::Ready;
        }
        std::optional<std::wstring> build_preview_payload(UObject* page, int& markers)
        {
            std::wstring serialized = cached_payload;
            if (serialized.empty())
            {
                std::set<std::wstring> textures;
                for (auto& sheet : sheets)
                {
                    for (const auto& actor : actors)
                    {
                        if (family(actor) != sheet.key.substr(0, map_family_chars))
                        {
                            continue;
                        }
                        auto found = positions.find(actor);
                        if (found == positions.end())
                        {
                            continue;
                        }
                        auto point =
                            map_overlay_world_point(sheet.area, found->second[0], found->second[1]);
                        if (!point || !map_overlay_contains(*point, sheet.width, sheet.height))
                        {
                            continue;
                        }
                        if (std::none_of(sheet.points.begin(), sheet.points.end(),
                                         [&](const auto& p)
                                         {
                                             return std::hypot(p.x - point->x, p.y - point->y) < 1.;
                                         }))
                        {
                            sheet.points.push_back(*point);
                        }
                    }
                }
                std::erase_if(sheets,
                              [&](const auto& sheet)
                              {
                                  return sheet.points.empty() ||
                                         !textures.insert(sheet.texture).second;
                              });
                if (sheets.empty())
                {
                    fail(page, L"No source position could be verified on this map.");
                    return std::nullopt;
                }
                // Prefer a useful overview, then allow paging through native detail
                // sheets. Overlapping floors use the same verified XY projection; this
                // is a source locator, not a claim that a random pickup has spawned.
                std::stable_sort(sheets.begin(), sheets.end(),
                                 [](const auto& a, const auto& b)
                                 {
                                     if (a.points.size() != b.points.size())
                                     {
                                         return a.points.size() > b.points.size();
                                     }
                                     return a.area.scale < b.area.scale;
                                 });
                std::wostringstream payload;
                payload.imbue(std::locale::classic());
                for (const auto& sheet : sheets)
                {
                    payload << sheet.texture << L'\t' << sheet.label << L'\t' << sheet.width
                            << L'\t' << sheet.height << L'\t';
                    bool comma = false;
                    for (const auto& point : sheet.points)
                    {
                        if (comma)
                        {
                            payload << L';';
                        }
                        comma = true;
                        payload << point.x << L',' << point.y;
                        ++markers;
                    }
                    payload << L'\n';
                }
                serialized = payload.str();
            }
            else
            {
                for (const auto& sheet : sheets)
                {
                    markers += static_cast<int>(sheet.points.size());
                }
            }
            return serialized;
        }

        auto tick(UObject* page) -> void
        {
            if (!is_uobject_valid_fast(page) ||
                try_gfx_get_member_value_number(gfx, page, L"paneMode") != journal_map_pane_mode)
            {
                if (serial != -1)
                {
                    cancel();
                }
                return;
            }
            const int next = static_cast<int>(
                try_gfx_get_member_value_number(gfx, page, L"mapRequestSerial").value_or(-1));
            const auto selected =
                try_gfx_get_member_value_string(gfx, page, L"mapRequestKey").value_or(L"");
            if (next != serial || selected != key)
            {
                if (!begin_preview(page, next, selected))
                {
                    return;
                }
            }
            if (done)
            {
                observe_preview_presentation(page);
                return;
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                fail(page, L"Map loading timed out. Try this location again.");
                return;
            }
            if (prepare_preview_assets(page) != AssetPreparation::Ready)
            {
                return;
            }
            int markers = 0;
            const auto payload = build_preview_payload(page, markers);
            if (!payload)
            {
                return;
            }
            const auto& serialized = *payload;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            payload_hash = fnv1a_offset_basis;
            for (auto value : serialized)
            {
                for (int shift : {0, 8})
                {
                    payload_hash ^= (value >> shift) & byte_mask;
                    payload_hash *= fnv1a_prime;
                }
            }
#endif
            if (!is_uobject_valid_fast(
                    try_gfx_invoke_string_arg_object(gfx, page, L"loadMap", serialized)))
            {
                fail(page, L"The map preview could not be opened.");
                return;
            }
            if (!previews.contains(key))
            {
                if (preview_order.size() >= journal_map_preview_cache_capacity)
                {
                    previews.erase(preview_order.front());
                    preview_order.pop_front();
                }
                previews.emplace(key, Preview{sheets, serialized});
                preview_order.push_back(key);
            }
            done = true;
            state = L"published";
            Output::send<LogLevel::Normal>(
                L"[KH3APProbeCpp] Ingredient journal map ready source={} sheets={} "
                L"projected_markers={} inactive_level_assets=true\n",
                key, sheets.size(), markers);
        }
    };
    RuntimeIngredientJournalMapService::RuntimeIngredientJournalMapService(
        GfxProbeFunctionPaths p, std::function<UObject*()> c, std::function<UObject*()> a)
        : impl_(std::make_unique<Impl>(std::move(p), std::move(c), std::move(a)))
    {
    }
    RuntimeIngredientJournalMapService::~RuntimeIngredientJournalMapService() = default;
    auto RuntimeIngredientJournalMapService::tick(UObject* page) -> void
    {
        impl_->tick(page);
    }
    auto RuntimeIngredientJournalMapService::cancel() -> void
    {
        impl_->cancel();
    }
    auto RuntimeIngredientJournalMapService::status() const -> const std::wstring&
    {
        return impl_->state;
    }
    auto RuntimeIngredientJournalMapService::pending() const -> bool
    {
        return impl_->serial != -1 &&
               (!impl_->done || (impl_->state == L"published" && !impl_->presentation_ready));
    }
} // namespace kh3ap
