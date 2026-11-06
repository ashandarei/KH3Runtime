#include "kh3ap/features/player/context/player_context_internal.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/features/player/objects/object_resolver.h"
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
#include <format>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace player_context_detail;

    auto RuntimePlayerContextService::Impl::cache_controller_pawn_candidate_fast(
        UObject* controller, std::wstring_view controller_name, UObject* pawn,
        std::wstring_view source, bool trust_controller_pawn, bool allow_during_special_battle)
        -> bool
    {
        if (!pawn || (!trust_controller_pawn && !kh3ap::is_uobject_valid_fast(pawn)))
        {
            return false;
        }
        auto& pawn_state = config.runtime_state.grant_state().pawn_state();
        if (pawn == pawn_state.object() && pawn_state.candidate_trusted())
        {
            pawn_state.update_controller(weak_uobject_resolver(controller), controller_name);
            RuntimeObjectResolver::cache_world_context_object(config.runtime_state.object_cache(),
                                                              pawn);
            return true;
        }

        const std::wstring pawn_name =
            trust_controller_pawn ? pawn->GetFullName() : format_object_debug_name(pawn);
        const std::wstring class_name =
            pawn->GetClassPrivate() ? pawn->GetClassPrivate()->GetName() : std::wstring{};
        if (!is_validated_sora_pawn_name(pawn_name, class_name))
        {
            return false;
        }
        const bool cached = cache_validated_sora_pawn_candidate(pawn, pawn_name, source,
                                                                allow_during_special_battle);
        if (cached)
        {
            pawn_state.update_controller(weak_uobject_resolver(controller), controller_name);
        }
        return cached;
    }

    auto RuntimePlayerContextService::Impl::refresh_controller_from_sora_event(UObject* pawn)
        -> bool
    {
        auto& pawn_state = config.runtime_state.grant_state().pawn_state();
        if (pawn != pawn_state.object() || !pawn_state.candidate_trusted() ||
            !kh3ap::is_uobject_valid_fast(pawn))
        {
            return false;
        }
        UObject* const controller = read_object_property_in_chain(pawn, STR("Controller"));
        if (!kh3ap::is_uobject_valid_fast(controller) ||
            controller == pawn_state.controller_object())
        {
            return false;
        }
        if (!is_player_controller_object(controller) ||
            read_object_property_in_chain(controller, STR("Pawn")) != pawn)
        {
            return false;
        }
        const auto controller_name = format_object_debug_name(controller);
        pawn_state.update_controller(weak_uobject_resolver(controller), controller_name);
        Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Sora controller ownership refreshed "
                                           "source=state_event controller={} pawn={}\n"),
                                       controller_name, pawn_state.name());
        return true;
    }

    auto RuntimePlayerContextService::Impl::cache_pawn_from_player_controller(
        UObject* controller, std::wstring_view source, std::wstring_view controller_name_hint,
        bool trust_cached_controller) -> bool
    {
        if (!controller || (!trust_cached_controller && !kh3ap::is_uobject_valid_fast(controller)))
        {
            return false;
        }
        const std::wstring controller_name = controller_name_hint.empty()
                                                 ? format_object_debug_name(controller)
                                                 : std::wstring{controller_name_hint};
        if (is_runtime_ap_ignored_map_context(controller_name))
        {
            if (config.enter_ignored_map)
            {
                config.enter_ignored_map(controller_name, source);
            }
            return false;
        }
        if (!is_player_controller_name(controller_name))
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Controller pawn cache probe source={} controller={} "
                    "skipped=not_player_controller | {}\n"),
                source, controller_name, runtime_context());
            return false;
        }

        config.runtime_state.grant_state().pawn_state().update_controller(
            weak_uobject_resolver(controller), controller_name);
        UObject* const player = read_object_property_in_chain(controller, STR("Player"));
        if (kh3ap::is_uobject_valid_fast(player) &&
            read_object_property_in_chain(player, STR("PlayerController")) == controller)
        {
            local_player = player;
        }
        const std::array<const wchar_t*, 3> properties{{
            STR("Pawn"),
            STR("AcknowledgedPawn"),
            STR("Character"),
        }};
        const bool fast_probe =
            trust_cached_controller ||
            source.find(STR("HandleStartingNewPlayer")) != std::wstring_view::npos ||
            source.find(STR("SoraControllerReacquire")) != std::wstring_view::npos;
        if (fast_probe)
        {
            for (const wchar_t* const property_name : properties)
            {
                if (cache_controller_pawn_candidate_fast(
                        controller, controller_name,
                        read_object_property_in_chain(controller, property_name),
                        std::format(STR("{}:{}"), source, property_name), trust_cached_controller,
                        trust_cached_controller))
                {
                    return true;
                }
            }
            return false;
        }

        log_controller_pawn_cache_probe(controller, source);
        for (const wchar_t* const property_name : properties)
        {
            if (cache_pawn_candidate(read_object_property_in_chain(controller, property_name),
                                     std::format(STR("{}:{}"), source, property_name)))
            {
                return true;
            }
        }
        return false;
    }

    auto RuntimePlayerContextService::Impl::read_object_function_param(
        UFunction* function_object, void* params, std::wstring_view param_name) const -> UObject*
    {
        if (!function_object || !params)
        {
            return nullptr;
        }
        FProperty* const property = find_function_param_by_name(function_object, param_name);
        if (!property || property->GetClass().GetName() != STR("ObjectProperty"))
        {
            return nullptr;
        }
        if (UObject** const value_ptr = property->ContainerPtrToValuePtr<UObject*>(params))
        {
            return *value_ptr;
        }
        return nullptr;
    }

    auto RuntimePlayerContextService::Impl::cache_pawn_from_hook_context(UObject* context,
                                                                         UObject* frame_object,
                                                                         std::wstring_view source)
        -> bool
    {
        if (cache_pawn_from_object(frame_object, std::format(STR("{}:frame"), source)) ||
            cache_pawn_from_object(context, std::format(STR("{}:context"), source)))
        {
            return true;
        }
        Output::send<LogLevel::Verbose>(
            STR("[KH3APProbeCpp] Runtime grant pawn not cached source={} frame={} context={}\n"),
            source, format_object_debug_name(frame_object), format_object_debug_name(context));
        return false;
    }
} // namespace kh3ap
