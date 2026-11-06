#include "kh3ap/features/player/context/player_context_internal.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/features/player/state/sora_pawn_catalog.h"
#include "kh3ap/core/objects/weak_object_resolver.h"
#include "kh3ap/features/grants/ap_ignored_map.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <array>
#include <chrono>
#include <format>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace player_context_detail;

    auto RuntimePlayerContextService::Impl::is_validated_sora_pawn_name(
        std::wstring_view pawn_name, std::wstring_view class_name) const -> bool
    {
        if (pawn_name.find(STR("Default__")) != std::wstring_view::npos)
        {
            return false;
        }
        const bool sora_name = runtime_sora_pawn_catalog_matches_any(pawn_name, class_name) ||
                               contains_any_wide(pawn_name, {STR("Sora")}) ||
                               contains_any_wide(class_name, {STR("Sora")});
        const bool pawn_class = contains_any_wide(class_name, {STR("Pawn")}) ||
                                runtime_sora_pawn_catalog_match(class_name) != nullptr;
        return sora_name && pawn_class;
    }

    auto RuntimePlayerContextService::Impl::cache_pawn_candidate(UObject* pawn,
                                                                 std::wstring_view source) -> bool
    {
        if (config.runtime_state.grant_state().special_battle_mode_active() ||
            !RuntimeObjectResolver::is_sora_ability_pawn_candidate(pawn))
        {
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        const std::wstring pawn_name = format_object_debug_name(pawn);
        if (is_runtime_ap_ignored_map_context(pawn_name))
        {
            if (config.enter_ignored_map)
            {
                config.enter_ignored_map(pawn_name, source);
            }
            return false;
        }
        if (config.leave_ignored_map)
        {
            config.leave_ignored_map(pawn_name, source);
        }

        const auto previous_pawn_seen =
            config.runtime_state.grant_state().pawn_state().first_seen_at();
        config.runtime_state.grant_state().pawn_state().update_candidate(
            pawn, pawn_name, now, weak_uobject_resolver(pawn));
        if (previous_pawn_seen != config.runtime_state.grant_state().pawn_state().first_seen_at() &&
            config.on_pawn_changed)
        {
            config.on_pawn_changed();
        }
        RuntimeObjectResolver::cache_world_context_object(config.runtime_state.object_cache(),
                                                          pawn);
        bootstrap_sora_state_from_trusted_pawn(pawn, source);
        if (config.runtime_state.grant_state().item_grant_poll_state().is_armed())
        {
            revalidate_sora_state_from_event(source, true);
        }
        if (config.publish_cached_playable_context)
        {
            config.publish_cached_playable_context(now, source);
        }
        if (source.find(STR("VanillaLevelWritePolyhook:level_owner")) != std::wstring_view::npos)
        {
            clear_blocking_ui_for_playable_context(STR("runtime_playable_pawn_context"),
                                                   STR("RuntimePlayablePawnContext"));
        }
        if (config.disarm_controller_reacquire)
        {
            config.disarm_controller_reacquire(source);
        }
        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] Runtime grant pawn cached source={} pawn={}\n"), source,
            pawn_name);
        return true;
    }

    auto RuntimePlayerContextService::Impl::cache_validated_sora_pawn_candidate(
        UObject* pawn, std::wstring_view pawn_name, std::wstring_view source,
        bool allow_during_special_battle) -> bool
    {
        if (config.runtime_state.grant_state().special_battle_mode_active() &&
            !allow_during_special_battle)
        {
            return false;
        }
        if (is_runtime_ap_ignored_map_context(pawn_name))
        {
            if (config.enter_ignored_map)
            {
                config.enter_ignored_map(pawn_name, source);
            }
            return false;
        }
        if (config.leave_ignored_map)
        {
            config.leave_ignored_map(pawn_name, source);
        }

        const auto now = std::chrono::steady_clock::now();
        const auto previous_pawn_seen =
            config.runtime_state.grant_state().pawn_state().first_seen_at();
        config.runtime_state.grant_state().pawn_state().update_candidate(
            pawn, std::wstring{pawn_name}, now, weak_uobject_resolver(pawn));
        if (previous_pawn_seen != config.runtime_state.grant_state().pawn_state().first_seen_at() &&
            config.on_pawn_changed)
        {
            config.on_pawn_changed();
        }
        RuntimeObjectResolver::cache_world_context_object(config.runtime_state.object_cache(),
                                                          pawn);
        // Some character-select battles replace Sora without calling
        // HandleStartingNewPlayer. Seed state immediately whenever an
        // event supplies a validated replacement pawn.
        bootstrap_sora_state_from_trusted_pawn(pawn, source);
        publish_playable_context_from_validated_pawn_name(pawn, pawn_name, now, source);
        if (source.find(STR("VanillaLevelWritePolyhook:level_owner")) != std::wstring_view::npos)
        {
            clear_blocking_ui_for_playable_context(STR("runtime_playable_pawn_context"),
                                                   STR("RuntimePlayablePawnContext"));
        }
        if (config.disarm_controller_reacquire)
        {
            config.disarm_controller_reacquire(source);
        }
        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] Runtime grant pawn cached source={} pawn={}\n"), source,
            pawn_name);
        return true;
    }

    auto RuntimePlayerContextService::Impl::cache_constructed_sora_pawn(UObject* pawn,
                                                                        std::wstring_view source)
        -> bool
    {
        if (!pawn)
        {
            return false;
        }

        // The observer resolves the queued FUObjectItem identity on the
        // game thread before dispatch. Validate the live instance by name
        // here, but do not rescan the UObject array or read controller
        // properties.
        const std::wstring pawn_name = pawn->GetFullName();
        const std::wstring class_name =
            pawn->GetClassPrivate() ? pawn->GetClassPrivate()->GetName() : std::wstring{};
        if (!is_validated_sora_pawn_name(pawn_name, class_name))
        {
            return false;
        }

        UObject* const previous_pawn = config.runtime_state.grant_state().pawn_state().object();
        const bool cached = cache_validated_sora_pawn_candidate(pawn, pawn_name, source, true);
        if (cached && pawn != previous_pawn)
        {
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] SoraPawnCreateCache updated previous=0x{:X} pawn={}\n"),
                reinterpret_cast<uintptr_t>(previous_pawn), pawn_name);
        }
        return cached;
    }

    auto RuntimePlayerContextService::Impl::cache_pawn_from_object(UObject* object,
                                                                   std::wstring_view source) -> bool
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return false;
        }
        if (cache_pawn_candidate(object, source))
        {
            return true;
        }

        const std::array<const wchar_t*, 7> direct_pawn_properties{{
            STR("Pawn"),
            STR("AcknowledgedPawn"),
            STR("Character"),
            STR("MyPawn"),
            STR("m_Pawn"),
            STR("m_PlayerPawn"),
            STR("m_AttachPawn"),
        }};
        for (const wchar_t* const property_name : direct_pawn_properties)
        {
            if (cache_pawn_candidate(read_object_property_in_chain(object, property_name),
                                     std::format(STR("{}:{}"), source, property_name)))
            {
                return true;
            }
        }

        const std::array<const wchar_t*, 8> controller_properties{{
            STR("PlayerController"),
            STR("OwningPlayer"),
            STR("OwnerPlayer"),
            STR("Controller"),
            STR("InstigatorController"),
            STR("m_PlayerController"),
            STR("m_Controller"),
            STR("m_OwningPlayer"),
        }};
        for (const wchar_t* const property_name : controller_properties)
        {
            UObject* const controller = read_object_property_in_chain(object, property_name);
            if (controller && kh3ap::is_uobject_valid_fast(controller) &&
                cache_pawn_from_object(controller,
                                       std::format(STR("{}:{}"), source, property_name)))
            {
                return true;
            }
        }
        return false;
    }
} // namespace kh3ap
