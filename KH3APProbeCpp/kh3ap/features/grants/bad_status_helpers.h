#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimeState;

    auto prewarm_direct_player_bad_status_runtime_cache(
        RC::Unreal::UObject* sora_pawn = nullptr,
        RC::Unreal::UObject* sora_state_component = nullptr,
        RC::Unreal::UObject* sora_current_state = nullptr) -> void;

    auto try_apply_direct_player_bad_status(std::wstring_view runtime_token,
                                            RC::Unreal::UObject* sora_pawn,
                                            RuntimeState& runtime_state,
                                            std::wstring* failure_details) -> bool;

    auto handle_direct_player_bad_status_state_transition(RC::Unreal::UObject* sora_pawn,
                                                          RC::Unreal::UObject* sora_state_component,
                                                          RC::Unreal::UObject* sora_current_state,
                                                          int32_t sora_current_state_id,
                                                          RuntimeState& runtime_state) -> void;

    // Existing state-write events wake cleanup only for an unfinished alternate trap.
    auto handle_alternate_bad_status_state_write(uintptr_t component, uintptr_t state,
                                                 RuntimeState& runtime_state) -> void;

} // namespace kh3ap
