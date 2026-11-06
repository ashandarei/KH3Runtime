#include "kh02_mission_counter.h"
#include "kh02_area_row_publisher.h"
#include "kh02_nav_row_publisher.h"
#include "kh3ap/features/kh02/treasure/kh02_treasure_registration.h"
#include "kh3ap/features/kh02/treasure/kh02_hud_treasure_rows.h"
#include "kh02_hud_reflection.h"
#include "kh02_hud.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/kh02/core/kh02_data_table.h"
#include "kh02/runtime_metadata.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/World.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/NameTypes.hpp>
#include <nlohmann/json.hpp>
#include <cstring>
#include <set>
#include <stdexcept>
#include <vector>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02hud_detail;

    namespace
    {
        namespace asset_request_id
        {
            constexpr int source_nav = 0x4b483460;
            constexpr int native_nav = 0x4b483461;
            constexpr int mission_counter = 0x4b483462;
            constexpr int treasure_rows = 0x4b483463;
            constexpr int hud_rows = 0x4b483464;
            constexpr int source_area_names = 0x4b4834c1;
            constexpr int native_area_names = 0x4b4834c2;
        } // namespace asset_request_id

        auto start_name(UObject* start, const wchar_t* name) -> std::wstring
        {
            // PlayerStartTag lives on PlayerStart; save-point Blueprints inherit
            // the presentation fields from TresPlayerStart as well.
            auto* property = start->GetPropertyByNameInChain(name);
            if (!property || property->GetClass().GetName() != L"NameProperty" ||
                property->GetElementSize() != sizeof(FName))
            {
                throw std::runtime_error("kh02_hud_start_name_layout");
            }
            return property->ContainerPtrToValuePtr<FName>(start)->ToString();
        }

        auto arrival_nav(UObject* pawn, UObject* world, std::wstring& arrival_tag, std::wstring& source_start)
            -> std::wstring
        {
            const auto world_path = world->GetPathName();
            const std::wstring fallback(kh02metadata::initial_nav(world_path));
            const bool forest = world_path == L"/Game/Maps/APKH02Mirrors/APKH02Forest.APKH02Forest";
            const bool depths = world_path == L"/Game/Maps/APKH02Mirrors/APKH02Depths.APKH02Depths";
            if (!forest && !depths)
            {
                return fallback;
            }
            // Host starts retain the source tag/transform but not its area-name
            // metadata. Resolve the authored source start once after streaming.
            // Native overlaps continue to own subsequent section changes.
            Call get_tag(library, L"GetLastPlayerStartTag");
            get_tag.run();
            auto* returned = get_tag.param(L"ReturnValue");
            if (!returned || returned->GetClass().GetName() != L"StrProperty" ||
                returned->GetElementSize() != sizeof(FString))
            {
                throw std::runtime_error("kh02_hud_arrival_tag_layout");
            }
            const auto* tag_text = returned->ContainerPtrToValuePtr<FString>(get_tag.parameters.data())->GetCharArray();
            arrival_tag = tag_text ? tag_text : L"";
            auto* start_type = static_cast<UClass*>(kh02_require_loaded_object(L"/Script/TresGame.TresPlayerStart"));
            TArray<AActor*> starts;
            UGameplayStatics::GetAllActorsOfClass(pawn, start_type, starts);
            Table areas(find_loaded_runtime_asset(L"/Game/System/DataTable/TresAreaNameTable.TresAreaNameTable"),
                        L"/Script/TresGame.TresAreaNameDataTable");
            auto* nav_property = areas.schema->GetPropertyByNameInChain(L"NavMapNameKey");
            if (!nav_property || nav_property->GetClass().GetName() != L"NameProperty" ||
                nav_property->GetElementSize() != sizeof(FName))
            {
                throw std::runtime_error("kh02_hud_arrival_area_layout");
            }
            std::wstring selected;
            for (auto* start : starts)
            {
                if (!is_uobject_valid_fast(start) || kh02_actor_world(start) != world ||
                    !start->GetPathName().starts_with(L"/Game/Zevels/") ||
                    start_name(start, L"PlayerStartTag") != arrival_tag)
                {
                    continue;
                }
                auto nav = start_name(start, L"m_NavMapDataTableKey");
                if (nav == L"None")
                {
                    const auto area = start_name(start, L"m_DispMapName");
                    auto* row = areas.find(area.c_str());
                    if (!row)
                    {
                        continue;
                    }
                    nav = nav_property->ContainerPtrToValuePtr<FName>(row)->ToString();
                }
                if (!nav.starts_with(forest ? L"dw08_" : L"dw09_") || (!selected.empty() && selected != nav))
                {
                    throw std::runtime_error("kh02_hud_arrival_map_identity");
                }
                selected = std::move(nav);
                source_start = start->GetPathName();
            }
            return selected.empty() ? fallback : L"AP02_" + selected;
        }
    } // namespace

    struct Kh02Hud::Impl
    {
        std::set<std::wstring> requested;
        std::vector<FWeakObjectPtr> roots;
        bool ready{}, minimap_applied{}, chest_table_registered{};
        Kh02MissionCounter mission_counter;
        std::string error;
        uint64_t nav_applications{};
        std::wstring active_nav;
        std::wstring arrival_tag;
        std::wstring arrival_source_start;
        Kh02NavRowPublisher nav_rows;
        Kh02AreaRowPublisher area_rows;
        FWeakObjectPtr applied_world;
        FWeakObjectPtr applied_pawn;
        bool world_active{};
        uint64_t presentation_updates{};

        ~Impl()
        {
            for (const auto& weak : roots)
            {
                if (auto* o = weak.Get(); is_uobject_valid_fast(o))
                {
                    o->ClearRootSet();
                }
            }
        }

        void retain(UObject* o)
        {
            if (!o->HasAnyInternalFlags(EInternalObjectFlags::RootSet))
            {
                o->SetRootSet();
                roots.push_back(make_uobject_weak(o));
            }
        }

        UObject* load(UObject* context, const wchar_t* path, int uuid)
        {
            auto* o = find_loaded_runtime_asset(path);
            if (o)
            {
                retain(o);
                return o;
            }
            if (requested.insert(path).second)
            {
                std::wstring why;
                if (!request_runtime_asset_load(context, path, &why, uuid))
                {
                    throw std::runtime_error("kh02_hud_asset_request:" + to_utf8(why));
                }
            }
            return nullptr;
        }

        bool prepare(UObject* context)
        {
            auto* original = load(context, L"/Game/ZI/Data/NaviMapData.NaviMapData", asset_request_id::source_nav);
            auto* native = load(context, L"/Game/UI/Data/NaviMapData.NaviMapData", asset_request_id::native_nav);
            auto* counter = load(context, L"/Game/UI/Data/MissionCounterData.MissionCounterData",
                                 asset_request_id::mission_counter);
            auto* chests = load(context, L"/Game/Maps/APKH02CastleTown/AP02ChestData.AP02ChestData",
                                asset_request_id::treasure_rows);
            auto* hud =
                load(context, L"/Game/Maps/APKH02CastleTown/AP02HudData.AP02HudData", asset_request_id::hud_rows);
            auto* depths_areas = load(context, L"/Game/Zystem/DataTable/TresAreaNameTable.TresAreaNameTable",
                                      asset_request_id::source_area_names);
            auto* native_areas = load(context, L"/Game/System/DataTable/TresAreaNameTable.TresAreaNameTable",
                                      asset_request_id::native_area_names);
            if (!original || !native || !counter || !chests || !hud || !depths_areas || !native_areas)
            {
                return false;
            }
            kh02_prepare_treasure_rows(chests);
            kh02_register_treasure_table(chests, chest_table_registered);
            nav_rows.publish(original, native);
            area_rows.publish(depths_areas, native_areas);
            mission_counter.prepare(counter, hud);
            ready = true;
            return true;
        }

        void tick(UObject* pawn, bool active)
        {
            try
            {
                auto* world = is_uobject_valid_fast(pawn) ? static_cast<AActor*>(pawn)->GetWorld() : nullptr;
                const bool player_changed = applied_pawn.Get() != pawn;
                if (world_active == active && applied_world.Get() == world && !player_changed &&
                    (!active || ready && (minimap_applied || active_nav == L"None") && !mission_counter.dirty()))
                {
                    return;
                }
                if (active && player_changed)
                {
                    auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
                    auto* instance = try_read_reflected_object_property_value(world, L"OwningGameInstance");
                    auto* manager = try_read_reflected_object_property_value(instance, L"m_UIManager");
                    if (!is_uobject_valid_fast(controller) ||
                        try_read_reflected_object_property_value(controller, L"Pawn") != pawn ||
                        try_read_reflected_object_property_value(manager, L"m_pPlayerController") != controller)
                    {
                        return;
                    }
                    mission_counter.invalidate_presentation();
                }
                if (applied_world.Get() != world)
                {
                    active_nav.clear();
                    arrival_tag.clear();
                    arrival_source_start.clear();
                    minimap_applied = false;
                }
                world_active = active;
                applied_world = make_uobject_weak(world);
                applied_pawn = make_uobject_weak(pawn);
                ++presentation_updates;
                if (!active)
                {
                    minimap_applied = false;
                    mission_counter.close();
                    return;
                }
                if (!ready)
                {
                    return;
                }
                if (active_nav.empty())
                {
                    active_nav = arrival_nav(pawn, world, arrival_tag, arrival_source_start);
                }
                if (!minimap_applied && active_nav != L"None")
                {
                    Call map(L"/Script/TresGame.Default__TresNavMapStatics", L"ApplyNavMapFromNavMapKey");
                    map.name(L"navMapKey", active_nav.c_str());
                    map.number(L"switchNavMapTime", 0);
                    map.run();
                    minimap_applied = map.result();
                    if (minimap_applied)
                    {
                        Call show(library, L"TresUI_SetEnableNaviMap");
                        show.boolean(L"bEnable", true);
                        show.run();
                        ++nav_applications;
                    }
                }
                mission_counter.tick();
            }
            catch (const std::exception& e)
            {
                error = e.what();
            }
        }
    };

    Kh02Hud::Kh02Hud() : impl(std::make_unique<Impl>()) {}

    Kh02Hud::~Kh02Hud() = default;

    bool Kh02Hud::prepare(UObject* context)
    {
        return impl->prepare(context);
    }

    void Kh02Hud::tick(UObject* pawn, bool active)
    {
        impl->tick(pawn, active);
    }

    void Kh02Hud::reset_world()
    {
        // Native loading replaces the UI parts as well as the host World.
        // Retain loaded assets, but let the source entry reopen its counter
        // against the new UI instead of trusting the previous movie's state.
        impl->applied_world = {};
        impl->applied_pawn = {};
        impl->world_active = false;
        impl->active_nav.clear();
        impl->arrival_tag.clear();
        impl->arrival_source_start.clear();
        impl->minimap_applied = false;
        impl->mission_counter.reset_world();
    }

    void Kh02Hud::counter(int value, int maximum, bool visible)
    {
        impl->mission_counter.set(value, maximum, visible, impl->ready);
    }

    std::string Kh02Hud::snapshot() const
    {
        return nlohmann::json{{"ready", impl->ready},
                              {"minimap_applied", impl->minimap_applied},
                              {"initial_nav_key", to_utf8(impl->active_nav)},
                              {"arrival_tag", to_utf8(impl->arrival_tag)},
                              {"arrival_source_start", to_utf8(impl->arrival_source_start)},
                              {"nav_applications", impl->nav_applications},
                              {"counter_visible", impl->mission_counter.visible()},
                              {"counter_requested", impl->mission_counter.requested()},
                              {"rendered_count", impl->mission_counter.rendered_count()},
                              {"maximum", impl->mission_counter.limit()},
                              {"counter_updates", impl->mission_counter.updates()},
                              {"presentation_updates", impl->presentation_updates},
                              {"chest_table_registered", impl->chest_table_registered},
                              {"error", impl->error}}
            .dump();
    }
} // namespace kh3ap
