#pragma once
#include <nlohmann/json.hpp>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02IntroPresentation;
    inline constexpr float kh02_completed_fade_tolerance = .001f;

    inline auto kh02_arrival_view_ready(bool settled, bool changing, float amount) -> bool
    {
        return settled && !changing && amount < kh02_completed_fade_tolerance;
    }

    auto kh02_sample_arrival_readiness(RC::Unreal::UObject* context, RC::Unreal::UObject* world,
                                       Kh02IntroPresentation& intro_presentation) -> nlohmann::json;
} // namespace kh3ap
