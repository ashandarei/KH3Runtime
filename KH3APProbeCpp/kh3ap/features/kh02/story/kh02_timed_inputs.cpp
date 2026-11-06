#include "kh02_timed_inputs.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/input/runtime_controller_input.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UObject.hpp>
#include <exception>
#include <utility>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr auto action_hold_duration = std::chrono::milliseconds(150);
    }

    void Kh02TimedInputs::press(UObject* controller, std::wstring action)
    {
        held_action = std::move(action);
        runtime_controller_action_input(controller, held_action, true);
        action_controller = make_uobject_weak(controller);
        release_at = std::chrono::steady_clock::now() + action_hold_duration;
    }

    void Kh02TimedInputs::begin_walk(UObject* controller, UObject* pawn, UObject* world, float x, float y,
                                     std::chrono::milliseconds duration, bool stop_at_castle_scene)
    {
        walk_controller = make_uobject_weak(controller);
        walk_pawn = make_uobject_weak(pawn);
        walk_world = make_uobject_weak(world);
        walk_x = x;
        walk_y = y;
        castle_trigger = stop_at_castle_scene;
        walk_until = std::chrono::steady_clock::now() + duration;
    }

    auto Kh02TimedInputs::action_active() const -> bool
    {
        return is_uobject_valid_fast(action_controller.Get());
    }

    auto Kh02TimedInputs::walk_active() const -> bool
    {
        return is_uobject_valid_fast(walk_controller.Get());
    }

    auto Kh02TimedInputs::walk_stops_at_castle_scene() const -> bool
    {
        return castle_trigger;
    }

    void Kh02TimedInputs::tick_action()
    {
        if (auto* controller = action_controller.Get();
            is_uobject_valid_fast(controller) && std::chrono::steady_clock::now() >= release_at)
        {
            runtime_controller_action_input(controller, held_action, false);
            action_controller = {};
        }
    }

    void Kh02TimedInputs::tick_walk(UObject* pawn, bool castle_scene_started)
    {
        auto* controller = walk_controller.Get();
        if (!is_uobject_valid_fast(controller))
        {
            return;
        }
        const bool owned = is_uobject_valid_fast(pawn) && walk_pawn.Get() == pawn &&
                           kh02_actor_world(pawn) == walk_world.Get() &&
                           try_read_reflected_object_property_value(controller, L"Pawn") == pawn;
        if (!owned || (castle_trigger && castle_scene_started) || std::chrono::steady_clock::now() >= walk_until)
        {
            runtime_controller_move_input(controller, 0, 0);
            walk_controller = {};
            castle_trigger = false;
            return;
        }
        runtime_controller_move_input(controller, walk_x, walk_y);
    }

    void Kh02TimedInputs::cancel()
    {
        std::exception_ptr failure;
        try
        {
            if (auto* controller = action_controller.Get(); is_uobject_valid_fast(controller))
            {
                runtime_controller_action_input(controller, held_action, false);
            }
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        try
        {
            if (auto* controller = walk_controller.Get(); is_uobject_valid_fast(controller))
            {
                runtime_controller_move_input(controller, 0, 0);
            }
        }
        catch (...)
        {
            if (!failure)
            {
                failure = std::current_exception();
            }
        }
        action_controller = {};
        walk_controller = {};
        walk_pawn = {};
        walk_world = {};
        castle_trigger = false;
        if (failure)
        {
            std::rethrow_exception(failure);
        }
    }
} // namespace kh3ap
