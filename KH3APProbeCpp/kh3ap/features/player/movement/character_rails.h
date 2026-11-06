#pragma once
#include <cstdint>
#include <string_view>

namespace RC::Unreal { class UObject; }

namespace kh3ap::character_rails
{
    inline constexpr auto shot_projectile_path =
        L"/Game/Blueprints/Projectile/TresProj_SlideShotMI.TresProj_SlideShotMI_C";
    bool initialize();
    void prepare(RC::Unreal::UObject* pawn);
    bool route(int controller, std::wstring_view key, int event, bool gamepad);
    void observe_state_write(uintptr_t queue, uintptr_t state);
}
