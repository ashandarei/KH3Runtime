#include "kh3ap/features/world/space/space_service_impl.h"
#include "kh3ap/features/world/space/space_profile.h"
#include "kh3ap/features/world/space/space_policy.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/utilities.h"

#include <Unreal/AActor.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/World.hpp>

#include <cstring>
#include <ostream>
#include <utility>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    using space::native_profile;

    namespace
    {
        constexpr std::wstring_view flight_world_prefix = L"/Game/Levels/gm/";
        constexpr std::wstring_view overview_world = L"World /Game/Levels/wm/wm_01/wm_01.wm_01";
        constexpr int32_t overview_main_state = 20;
        constexpr int32_t overview_main_menu_mode = 0;
        constexpr uintptr_t menu_mode_offset = 0x3d8;

        // Steam's verified idle -> refresh -> idle tick branch.
        constexpr uintptr_t steam_refresh_blocked_offset = 0x3f8;
        constexpr uintptr_t steam_refresh_guard_rva = 0x17dd01c;
        constexpr uint32_t steam_refresh_guard_value = 0x17dbb1e;
        constexpr int32_t steam_overview_refresh_state = 56;

        bool overview_refresh_signature_matches(uintptr_t module_base)
        {
            const auto& profile = native_profile();
            return std::memcmp(
                    reinterpret_cast<const void*>(module_base + profile.refresh_overview_rva),
                    profile.refresh_overview_bytes.data(),
                    profile.refresh_overview_bytes.size()) == 0;
        }
    }

    std::optional<bool> RuntimeSpaceService::Impl::bool_field(UObject* object, const wchar_t* name)
    {
        if (!is_uobject_valid_fast(object))
        {
            return std::nullopt;
        }
        auto* property = object->GetPropertyByNameInChain(name);
        if (!property || !property->IsA<FBoolProperty>())
        {
            return std::nullopt;
        }
        return static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(object);
    }

    std::optional<RuntimeSpaceService::Impl::FlightSymbol>
    RuntimeSpaceService::Impl::read_flight_symbol(UObject* actor)
    {
        // Called only for actors returned by native current-world discovery.
        // UE4SS GetWorld walks outers and reports the package's template
        // world for streamed symbols, not the active flight world.
        if (!is_uobject_valid_fast(actor))
        {
            return std::nullopt;
        }
        const auto world_code = actor_world(actor);
        auto* icon = object_field(actor, L"WorldIcon");
        auto* mist = object_field(actor, L"WorldMist");
        auto* collision = object_field(actor, L"ApproachCollision");
        const auto locked = bool_field(actor, L"IsLocked");
        const auto boss_locked = bool_field(actor, L"IsLockedByBoss");
        const auto icon_visible = bool_field(icon, L"bVisible");
        const auto mist_visible = bool_field(mist, L"bVisible");
        auto* radius_property = collision ? collision->GetPropertyByNameInChain(L"SphereRadius") : nullptr;
        if (world_code.empty() || !locked || !boss_locked || !icon_visible || !mist_visible ||
            !radius_property || radius_property->GetClass().GetName() != L"FloatProperty" ||
            radius_property->GetElementSize() != sizeof(float))
        {
            return std::nullopt;
        }

        FlightSymbol result;
        result.actor = actor;
        result.code = world_code;
        result.path = actor->GetFullName();
        result.locked = *locked;
        result.boss_locked = *boss_locked;
        result.icon_visible = *icon_visible;
        result.mist_visible = *mist_visible;
        result.radius = *radius_property->ContainerPtrToValuePtr<float>(collision);
        return result;
    }

    bool RuntimeSpaceService::Impl::refresh_flight_symbols(std::chrono::steady_clock::time_point now)
    {
        if (current_world.find(flight_world_prefix) == current_world.npos)
        {
            return true;
        }
        auto* controller = delivery_context(true);
        if (!controller)
        {
            return false;
        }
        auto* symbol_class = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Script/TresGame.TresGumiShipWorldSymbolActor");
        if (!is_uobject_valid_fast(symbol_class))
        {
            return false;
        }
        // One current-world discovery per access/lifecycle event. Native
        // LockWorld handles streamed/replaced actors and queues a fresh
        // snapshot; there is no timer-driven actor scan.
        flight_symbols.clear();
        std::vector<AActor*> actors;
        UGameplayStatics::GetAllActorsOfClass(controller, symbol_class, actors);
        ++flight_discoveries;
        for (auto* actor : actors)
        {
            auto symbol = read_flight_symbol(actor);
            if (!symbol)
            {
                continue; // Components may still be initializing.
            }
            bool should_lock = !can_land(symbol->code);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            if (shadow_test && shadow_test->world == current_world && shadow_test->seed == access.seed &&
                shadow_test->actor.Get() == actor && now < shadow_test->release)
            {
                should_lock = true;
            }
#endif
            if (symbol->locked != should_lock || symbol->icon_visible == should_lock ||
                symbol->mist_visible != should_lock)
            {
                // LockWorld owns mesh visibility and approach radius.
                // Boss locking remains a separate native state.
                reinterpret_cast<Lock>(lock.original)(actor, should_lock);
                symbol = read_flight_symbol(actor);
                ++flight_visibility_updates;
            }
            if (symbol)
            {
                flight_symbols.push_back(std::move(*symbol));
            }
        }
        return true;
    }

    void RuntimeSpaceService::Impl::write_flight_symbols(
            std::ostream& json, const std::vector<FlightSymbol>& symbols)
    {
        json << '[';
        bool first = true;
        for (const auto& symbol : symbols)
        {
            if (!first)
            {
                json << ',';
            }
            first = false;
            json << "{\"code\":\"" << json_escape(symbol.code) << "\",\"actor\":\"" << json_escape(symbol.path)
                 << "\",\"locked\":" << (symbol.locked ? "true" : "false")
                 << ",\"boss_locked\":" << (symbol.boss_locked ? "true" : "false")
                 << ",\"icon_visible\":" << (symbol.icon_visible ? "true" : "false")
                 << ",\"mist_visible\":" << (symbol.mist_visible ? "true" : "false")
                 << ",\"approach_radius\":" << symbol.radius << '}';
        }
        json << ']';
    }

    UObject* RuntimeSpaceService::Impl::overview_manager()
    {
        auto* game_instance = object_field(world(), L"OwningGameInstance");
        auto* ui_manager = object_field(game_instance, L"m_UIManager");
        auto* hud = object_field(ui_manager, L"m_ActiveHUD");
        auto* menu = object_field(hud, L"m_pGummiMenu");
        return object_field(menu, L"m_pMenuManager");
    }

    UObject* RuntimeSpaceService::Impl::landing_manager()
    {
        auto* current_world_object = world();
        auto* game_mode = object_field(current_world_object, L"AuthorityGameMode");
        auto* manager = object_field(game_mode, L"m_pGummiMenuManager");
        return manager && static_cast<AActor*>(manager)->GetWorld() == current_world_object ? manager : nullptr;
    }

    bool RuntimeSpaceService::Impl::refresh_overview_native(UObject* manager)
    {
        const auto& profile = native_profile();
        const auto module_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!profile.supported || !is_uobject_valid_fast(manager) ||
            !overview_refresh_signature_matches(module_base))
        {
            return false;
        }
        if (!profile.refresh_is_tick)
        {
            reinterpret_cast<void(*)(UObject*)>(module_base + profile.refresh_overview_rva)(manager);
            return true;
        }
        // Steam inlines the region refresh into its native manager tick.
        // Dispatch only its verified idle -> refresh -> idle branch, with zero
        // elapsed time; never call the EGS single-argument refresh ABI here.
        auto* manager_bytes = reinterpret_cast<uint8_t*>(manager);
        auto& state = *reinterpret_cast<int32_t*>(manager_bytes + profile.overview_state);
        if (state != profile.overview_idle_state || manager_bytes[steam_refresh_blocked_offset] ||
            *reinterpret_cast<const uint32_t*>(module_base + steam_refresh_guard_rva) != steam_refresh_guard_value)
        {
            return false;
        }
        state = steam_overview_refresh_state;
        reinterpret_cast<void(*)(UObject*, float)>(module_base + profile.refresh_overview_rva)(manager, 0.0f);
        return state == profile.overview_idle_state;
    }

    bool RuntimeSpaceService::Impl::refresh_open_overview(UObject* manager)
    {
        // A closed selector has no cache to refresh. Opening it emits native
        // setup/UI events; do not keep a retry loop alive while it is closed.
        if (!is_uobject_valid_fast(manager))
        {
            return true;
        }
        // The native region refresh rebuilds cached cursor records and actor
        // visuals using the same region and selected icon. No travel or story edits.
        if (!loading_closed || !is_uobject_valid_fast(manager) ||
            current_world != overview_world || !delivery_context(true))
        {
            return false;
        }
        const auto& profile = native_profile();
        auto* manager_bytes = reinterpret_cast<uint8_t*>(manager);
        auto* menu = object_field(object_field(manager, L"m_pGummiHUD"), L"m_pGummiMenu");
        const auto state = *reinterpret_cast<int32_t*>(manager_bytes + profile.overview_state);
        if (state == overview_main_state && is_uobject_valid_fast(menu) &&
            *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(menu) + menu_mode_offset) == overview_main_menu_mode)
        {
            return true;
        }
        // Opening animations and focused dialogs are temporary. Completing the
        // request here loses a Garden-data/progress refresh before the selector
        // becomes idle, leaving its old first-visit route until space is reopened.
        if (state != profile.overview_idle_state || !landing_ui.ready())
        {
            return false;
        }
        const auto region = *reinterpret_cast<int32_t*>(manager_bytes + profile.overview_region);
        if (region < 0 || region >= static_cast<int32_t>(space::travel_maps.size()) ||
            manager_bytes[profile.overview_region_ids + region] >= space::travel_maps.size() ||
            !object_field(manager, L"m_pGummiHUD") || !object_field(manager, L"m_pController"))
        {
            return false;
        }
        // Ownership alone is not a cache key: Garden story progress also
        // chooses between the introduction route and the save-point menu.
        const auto module_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!overview_refresh_signature_matches(module_base))
        {
            return true;
        }
        apply_overview(manager);
        if (!refresh_overview_native(manager))
        {
            return false;
        }
        ++overview_refreshes;
        return true;
    }

    bool RuntimeSpaceService::Impl::is_unlocked_travel_icon(UObject* icon)
    {
        if (!is_uobject_valid_fast(icon))
        {
            return false;
        }
        const auto& profile = native_profile();
        auto* kind_property = icon->GetPropertyByNameInChain(L"m_nWmObjID");
        auto* id_property = icon->GetPropertyByNameInChain(L"m_nTravelPointID");
        auto* map_property = icon->GetPropertyByNameInChain(L"m_PresistenLevelPath");
        auto* tag_property = icon->GetPropertyByNameInChain(L"m_PlayerStartTagName");
        if (!kind_property || kind_property->GetOffset_Internal() != profile.icon_kind ||
            kind_property->GetElementSize() != sizeof(uint8_t) ||
            !id_property || id_property->GetOffset_Internal() != profile.icon_travel_id ||
            id_property->GetElementSize() != sizeof(uint8_t) ||
            !map_property || map_property->GetOffset_Internal() != profile.icon_map ||
            map_property->GetElementSize() != sizeof(FName) ||
            !tag_property || tag_property->GetOffset_Internal() != profile.icon_tag ||
            tag_property->GetElementSize() != sizeof(FName))
        {
            return false;
        }
        return space::unlock_travel_point(
                *kind_property->ContainerPtrToValuePtr<uint8_t>(icon),
                *id_property->ContainerPtrToValuePtr<uint8_t>(icon),
                map_property->ContainerPtrToValuePtr<FName>(icon)->ToString(),
                tag_property->ContainerPtrToValuePtr<FName>(icon)->ToString());
    }

    int RuntimeSpaceService::Impl::travel_icon_zone(UObject* icon)
    {
        if (!is_unlocked_travel_icon(icon))
        {
            return -1;
        }
        auto* map_property = icon->GetPropertyByNameInChain(L"m_PresistenLevelPath");
        return space::flight_zone(map_property->ContainerPtrToValuePtr<FName>(icon)->ToString());
    }

    void RuntimeSpaceService::Impl::icon_state_hook(UObject* manager, FWeakObjectPtr* icon, uint8_t* record)
    {
        auto& self = *active;
        reinterpret_cast<IconState>(self.icon_state.original)(manager, icon, record);
        const auto& profile = native_profile();
        // Saved discovery flags (and the Traveler missions) remain untouched.
        if (record && icon && record[profile.cursor_kind] == space::travel_point_kind &&
            is_unlocked_travel_icon(icon->Get()))
        {
            if (!record[profile.cursor_available])
            {
                ++self.travel_points_enabled;
            }
            const uint8_t available = self.can_travel(travel_icon_zone(icon->Get())) ? 1 : 0;
            record[profile.cursor_visible] = 1;
            record[profile.cursor_available] = available;
            record[profile.cursor_selectable] = available;
        }
        // First-visit symbols use AP landing eligibility without changing saved
        // discovery/story progress or allowing preview/dummy/endgame symbols.
        if (record && icon && record[profile.cursor_kind] == space::world_symbol_kind)
        {
            auto* actor = icon->Get();
            auto* id_property = is_uobject_valid_fast(actor)
                    ? actor->GetPropertyByNameInChain(L"m_nWorldSymbleID") : nullptr;
            if (id_property && id_property->GetOffset_Internal() == profile.icon_world_id &&
                id_property->GetElementSize() == sizeof(uint8_t))
            {
                const auto world_code = space::world_symbol_code(
                        record[profile.cursor_kind], *id_property->ContainerPtrToValuePtr<uint8_t>(actor));
                if (!world_code.empty())
                {
                    const uint8_t available = self.can_land(world_code) && self.landing_ui.ready() ? 1 : 0;
                    if (available && !record[profile.cursor_available])
                    {
                        ++self.world_fast_travel_enabled;
                    }
                    record[profile.cursor_visible] = available;
                    record[profile.cursor_available] = available;
                    record[profile.cursor_selectable] = available;
                }
            }
        }
    }
}
