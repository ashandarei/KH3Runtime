#pragma once
#include <nlohmann/json.hpp>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02CutsceneSession;
    class Kh02MovieBridge;
    void kh02_prepare_collapse(RC::Unreal::UObject* context, RC::Unreal::UObject* world,
                               RC::Unreal::UObject* event_aqua, Kh02CutsceneSession& cutscenes, Kh02MovieBridge& movies,
                               nlohmann::json& proof);
} // namespace kh3ap
