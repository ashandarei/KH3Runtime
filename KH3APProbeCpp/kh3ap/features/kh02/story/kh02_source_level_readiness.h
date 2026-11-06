#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json.hpp>
#include <span>

namespace kh3ap
{
    class Kh02MapSession;

    // Borrowed for one synchronous poll; no references survive the call.
    struct Kh02CastleLevelBindings
    {
        RC::Unreal::FWeakObjectPtr& story_script;
        RC::Unreal::FWeakObjectPtr& gimmick_script;
        Kh02MapSession& session;
    };

    auto kh02_castle_source_levels_ready(RC::Unreal::UObject* world,
                                         std::span<const RC::Unreal::FWeakObjectPtr> flow_streams, bool entry_retry,
                                         bool first_castle_intro, Kh02CastleLevelBindings bindings,
                                         nlohmann::json& loading_proof) -> bool;
    auto kh02_mirror_source_levels_ready(RC::Unreal::UObject* world, RC::Unreal::FWeakObjectPtr& within_script,
                                         Kh02MapSession& map_session) -> bool;
} // namespace kh3ap
