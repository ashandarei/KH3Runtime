#pragma once
#include <string_view>
#include <array>
#include "kh3ap/core/performance_build.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Composition supplies feature ownership/routing. These non-owning function
    // pointers have static lifetime and are configured on the game thread.
    struct RuntimeControllerInputPolicy
    {
        bool (*alternate_controller_allowed)(RC::Unreal::UObject*){};
        bool (*route_action)(std::wstring_view action, const wchar_t* key, bool pressed){};
        RC::Unreal::UObject* (*actor_world)(RC::Unreal::UObject*){};
    };
    auto configure_runtime_controller_input(RuntimeControllerInputPolicy policy) -> void;

    void runtime_controller_look_input(RC::Unreal::UObject* controller, float x, float y);
    void runtime_controller_move_input(RC::Unreal::UObject* controller, float world_x,
                                       float world_y);
    void runtime_controller_stick_input(RC::Unreal::UObject* controller, float right,
                                        float forward);
    struct RuntimeControllerStickState
    {
        // raw includes pending device samples; committed_raw/processed describe
        // the last completed input pass.
        std::array<float, 2> raw{}, committed_raw{}, processed{};
        std::array<bool, 2> pending_samples{};
    };
    auto runtime_controller_stick_state(RC::Unreal::UObject* controller, bool right_stick = false)
        -> std::array<float, 2>;
    auto runtime_controller_capture_stick(RC::Unreal::UObject* controller, bool right_stick)
        -> RuntimeControllerStickState;
    void runtime_controller_restore_stick(RC::Unreal::UObject* controller, bool right_stick,
                                          const RuntimeControllerStickState& state);
    // Retired controllers have no player or pawn. Discard their old stick
    // samples and button presses before the next possession.
    void runtime_controller_clear_parked_input(RC::Unreal::UObject* controller);
    // Carry current jump/glide hold across a synchronous possession handoff,
    // without replaying a press edge or any parked controller's event queue.
    bool runtime_controller_jump_held(RC::Unreal::UObject* controller);
    void runtime_controller_restore_jump_hold(RC::Unreal::UObject* controller, bool held);
    // Pair native InputKey press/release across ticks; no desktop input is used.
    void runtime_controller_action_input(RC::Unreal::UObject* controller, std::wstring_view action,
                                         bool pressed);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    // A bounded native stick test owns movement axes while physical controllers
    // can continue reporting centered-stick noise. Other input stays native.
    void runtime_controller_test_stick_lease(RC::Unreal::UObject* controller, bool owned);
#endif
} // namespace kh3ap
