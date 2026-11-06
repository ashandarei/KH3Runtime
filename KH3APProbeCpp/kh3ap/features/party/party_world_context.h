#pragma once
namespace RC::Unreal
{
    class UObject;
}
namespace kh3ap
{
    class RuntimeState;
    enum class RuntimePartyMapPolicy;
    namespace party_world
    {
        auto observed_party_map_policy(RC::Unreal::UObject* level, RuntimeState& runtime_state)
            -> RuntimePartyMapPolicy;
        auto is_party_spawn_excluded_level(RC::Unreal::UObject* level) -> bool;
        auto is_party_presentation_story_level(RC::Unreal::UObject* level) -> bool;
    } // namespace party_world
} // namespace kh3ap
