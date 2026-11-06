#pragma once
#include <nlohmann/json.hpp>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02StoryFlags;
    void kh02_restore_castle_scenery(RC::Unreal::UObject* script, RC::Unreal::UObject* gimmick,
                                     const Kh02StoryFlags& flags, bool backtrack, nlohmann::json& loading_proof);
} // namespace kh3ap
