#include "kh3ap/features/world/space/space_service_impl.h"
#include "kh3ap/features/world/space/space_profile.h"
#include "kh3ap/features/player/state/sora_pawn_catalog.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include <Unreal/AActor.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/World.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    using space::native_profile;

    namespace
    {
        constexpr std::wstring_view world_object_prefix = L"World ";
        constexpr int32_t unpaused_game_instance_state = 0;
    } // namespace

    UObject* RuntimeSpaceService::Impl::object_field(UObject* object, const wchar_t* name)
    {
        if (!is_uobject_valid_fast(object))
        {
            return nullptr;
        }
        auto* property = object->GetPropertyByNameInChain(name);
        if (!property || property->GetClass().GetName() != L"ObjectProperty" ||
            property->GetElementSize() != sizeof(UObject*))
        {
            return nullptr;
        }
        auto* value = *property->ContainerPtrToValuePtr<UObject*>(object);
        return is_uobject_valid_fast(value) ? value : nullptr;
    }

    bool RuntimeSpaceService::Impl::library_bool(const wchar_t* name)
    {
        auto* library = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        if (!is_uobject_valid_fast(library))
        {
            return false;
        }
        auto* function = library->GetFunctionByNameInChain(name);
        if (!is_uobject_valid_fast(function) || function->GetParmsSize() != sizeof(uint8_t))
        {
            return false;
        }
        auto* result_property = find_function_param_by_name(function, L"ReturnValue");
        if (!result_property || !result_property->IsA<FBoolProperty>())
        {
            return false;
        }
        uint8_t parameters{};
        library->ProcessEvent(function, &parameters);
        return static_cast<FBoolProperty*>(result_property)
            ->GetPropertyValueInContainer(&parameters);
    }

    void RuntimeSpaceService::Impl::sample_space()
    {
        loading_closed = library_bool(L"TresUI_IsClosedLoadingScreen");
        controller_ready = false;
        if (!space::is_space(current_world))
        {
            space_controller = nullptr;
            return;
        }

        const auto world_path = current_world.substr(world_object_prefix.size());
        auto* controller = space_controller.Get();
        if (!is_uobject_valid_fast(controller) ||
            controller->GetFullName().find(world_path) == std::wstring::npos)
        {
            controller = nullptr;
            const auto* controller_class = current_world.find(L"/wm/") != std::wstring::npos
                                               ? L"TresWorldMapPlayerController"
                                               : L"TresGumiShipPlayerControllerDefault";
            std::vector<UObject*> candidates;
            UObjectGlobals::FindAllOf(controller_class, candidates);
            for (auto* candidate : candidates)
            {
                if (is_uobject_valid_fast(candidate) &&
                    candidate->GetFullName().find(world_path) != std::wstring::npos)
                {
                    controller = candidate;
                    break;
                }
            }
            space_controller = controller;
        }
        controller_ready = object_field(controller, L"PlayerInput") &&
                           object_field(controller, L"AcknowledgedPawn");
    }

    bool RuntimeSpaceService::Impl::delivery_observed(UObject* controller) const
    {
        return controller && delivery_controller.Get() == controller;
    }

    UObject* RuntimeSpaceService::Impl::delivery_context(bool require_observed,
                                                         bool recheck_loading)
    {
        if (!installed || !access.access_ready || !space::inventory_delivery_world(current_world) ||
            current_world != world_name() || !loading_closed ||
            (recheck_loading && !library_bool(L"TresUI_IsClosedLoadingScreen")))
        {
            return nullptr;
        }
        auto* controller = space_controller.Get();
        auto* current_world_object = world();
        auto* pawn = object_field(controller, L"Pawn");
        auto* game_instance = object_field(current_world_object, L"OwningGameInstance");
        if (!is_uobject_valid_fast(controller) || !is_uobject_valid_fast(pawn) ||
            !is_uobject_valid_fast(game_instance) ||
            game_instance->GetClassPrivate()->GetName() != L"TresGameInstance")
        {
            return nullptr;
        }
        if (static_cast<AActor*>(controller)->GetWorld() != current_world_object ||
            static_cast<AActor*>(pawn)->GetWorld() != current_world_object ||
            object_field(controller, L"AcknowledgedPawn") != pawn ||
            object_field(pawn, L"Controller") != controller ||
            !object_field(controller, L"PlayerInput"))
        {
            return nullptr;
        }
        // The storefront image was checked before service installation. This is
        // the owning instance's pause/cutscene state, not a cached Sora state.
        const auto pause_state = *reinterpret_cast<int32_t*>(
            reinterpret_cast<uint8_t*>(game_instance) + native_profile().game_instance_pause_state);
        if (pause_state != unpaused_game_instance_state)
        {
            return nullptr;
        }
        if (require_observed && !delivery_observed(controller))
        {
            return nullptr;
        }
        return controller;
    }

    UObject* RuntimeSpaceService::Impl::ship_inventory_context()
    {
        const RuntimeHotPathProfileScope profile{L"space:ship_inventory_context"};
        if (!installed || !access.access_ready || !library_bool(L"TresUI_IsClosedLoadingScreen"))
        {
            return nullptr;
        }
        if (space::is_space(world_name()))
        {
            return delivery_context(false);
        }
        auto* current_world_object = world();
        auto* game_instance = object_field(current_world_object, L"OwningGameInstance");
        if (!is_uobject_valid_fast(current_world_object) || !is_uobject_valid_fast(game_instance) ||
            game_instance->GetClassPrivate()->GetName() != L"TresGameInstance" ||
            *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(game_instance) +
                                        native_profile().game_instance_pause_state) !=
                unpaused_game_instance_state)
        {
            return nullptr;
        }
        // Reuse the player-context service's current, weakly held Sora.
        // A map-ready event must not trigger a full object-array scan just
        // to find the controller we already validated for ordinary gameplay.
        auto* pawn = sora_pawn ? sora_pawn() : nullptr;
        auto* controller = object_field(pawn, L"Controller");
        if (!is_uobject_valid_fast(controller) || !is_uobject_valid_fast(pawn) ||
            !is_uobject_valid_fast(pawn->GetClassPrivate()) ||
            !runtime_sora_pawn_catalog_matches_any(pawn->GetPathName(),
                                                   pawn->GetClassPrivate()->GetName()))
        {
            return nullptr;
        }
        if (static_cast<AActor*>(controller)->GetWorld() != current_world_object ||
            static_cast<AActor*>(pawn)->GetWorld() != current_world_object ||
            object_field(controller, L"Pawn") != pawn ||
            object_field(controller, L"AcknowledgedPawn") != pawn ||
            !object_field(controller, L"PlayerInput"))
        {
            return nullptr;
        }
        return pawn;
    }

    auto RuntimeSpaceService::Impl::world() -> UObject*
    {
        auto* game_engine = engine.Get();
        if (!is_uobject_valid_fast(game_engine))
        {
            game_engine = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, L"/Engine/Transient.TresGameEngine_0");
            if (!is_uobject_valid_fast(game_engine))
            {
                return nullptr;
            }
            engine = game_engine;
        }
        auto* viewport = object_field(game_engine, L"GameViewport");
        return object_field(viewport, L"World");
    }

    auto RuntimeSpaceService::Impl::world_name() -> std::wstring
    {
        auto* current_world_object = world();
        return current_world_object ? current_world_object->GetFullName() : L"";
    }

    bool RuntimeSpaceService::Impl::redirect(std::wstring_view source,
                                             std::wstring_view target) const
    {
        return space::redirect_to_garden(source, target, space_journey);
    }

    bool RuntimeSpaceService::Impl::can_land(std::wstring_view code) const
    {
        return can_travel(space::landing_zone(code)) && access.allowed.contains(std::wstring{code});
    }

    std::wstring_view RuntimeSpaceService::Impl::actor_world(UObject* actor)
    {
        if (!is_uobject_valid_fast(actor))
        {
            return {};
        }
        auto* world_code_property = actor->GetPropertyByNameInChain(L"WorldCode");
        if (!world_code_property || world_code_property->GetClass().GetName() != L"ByteProperty" ||
            world_code_property->GetElementSize() != sizeof(uint8_t))
        {
            return {};
        }
        const auto world_code = *world_code_property->ContainerPtrToValuePtr<uint8_t>(actor);
        for (const auto& landing : space::landings)
        {
            if (landing.world_code == world_code)
            {
                return landing.code;
            }
        }
        return {};
    }
} // namespace kh3ap
