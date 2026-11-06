#pragma once
#include <nlohmann/json.hpp>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    auto kh02_disable_castle_save_notice(RC::Unreal::UObject* world, RC::Unreal::UObject* script,
                                         nlohmann::json& loading_proof) -> RC::Unreal::UObject*;
}
