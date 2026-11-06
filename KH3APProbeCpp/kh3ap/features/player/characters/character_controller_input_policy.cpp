#include "kh3ap/features/player/characters/character_controller_input_policy.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/movement/character_traversal.h"
#include "kh3ap/features/player/characters/character_camera.h"
#include "kh3ap/features/player/movement/character_rails.h"
#include "kh3ap/features/world/garden_return/garden_return_service.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;
    namespace
    {
        // Alternate controllers inherit the verified input handlers. Admit only
        // the owned experimental hero or Aqua in the private 0.2 world.
        bool alternate_controller_allowed(UObject* controller)
        {
            if (!is_uobject_valid_fast(controller))
            {
                return false;
            }
            const auto name = controller->GetClassPrivate()->GetName();
            const bool aqua = name == L"TresPlayerControllerAqua";
            const bool known = aqua || name == L"TresPlayerControllerKairi" ||
                               name == L"TresPlayerControllerRiku" ||
                               name == L"TresPlayerControllerRoxas" ||
                               name == L"TresPlayerControllerMiRxMickey";
            if (!known)
            {
                return false;
            }
            auto* pawn = try_read_reflected_object_property_value(controller, L"Pawn");
            const bool owned = character_swap::owns_swap_pawn(pawn);
            auto* local = try_read_reflected_object_property_value(controller, L"Player");
            return is_uobject_valid_fast(pawn) &&
                   (owned || (aqua && kh02_current_story_world() &&
                              kh02_actor_world(pawn) == kh02_current_story_world())) &&
                   try_read_reflected_object_property_value(pawn, L"Controller") == controller &&
                   is_uobject_valid_fast(local) &&
                   try_read_reflected_object_property_value(local, L"PlayerController") ==
                       controller;
        }

        auto route_action(std::wstring_view action, const wchar_t* key, bool pressed) -> bool
        {
            if (character_rails::route(0, key, pressed ? 0 : 1, true) ||
                character_traversal::route(0, key, pressed ? 0 : 1, true) ||
                character_camera::route(0, key, pressed ? 0 : 1, true))
            {
                return true;
            }
            if (action == L"interact" &&
                RuntimeGardenReturnService::route_kh02_character(0, key, pressed ? 0 : 1, true))
            {
                return true;
            }

            return false;
        }
    } // namespace

    auto character_controller_input_policy() -> RuntimeControllerInputPolicy
    {
        return {alternate_controller_allowed, route_action, kh02_actor_world};
    }
} // namespace kh3ap
