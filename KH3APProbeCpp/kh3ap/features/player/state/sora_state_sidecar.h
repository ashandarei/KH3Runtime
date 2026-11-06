#pragma once

#include "kh3ap/features/player/state/sora_current_state_cache_view.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    struct RuntimeContextSnapshot;

    struct RuntimeSoraStateSidecarDeps
    {
        std::optional<std::filesystem::path> bridge_dir{};
        RC::Unreal::UObject* pawn{};
        RuntimeSoraCurrentStateCacheView current_state_cache{};
        std::wstring_view protocol_name{};
        int protocol_version{};
        std::wstring_view runtime_context{};
        const RuntimeContextSnapshot* runtime_context_snapshot{};
        bool special_battle_mode_active{false};
        std::wstring_view special_battle_mode_source{};
        std::wstring_view special_battle_mode_details{};
        std::wstring_view special_battle_mode_map_root{};
        bool event_log_enabled{false};
        std::function<void(std::wstring_view, std::optional<uint8_t>)> activate_blocking_ui_from_sora_state{};
    };

    auto write_runtime_sora_state_sidecar_on_change(
            std::wstring& last_signature,
            const RuntimeSoraStateSidecarDeps& deps) -> void;
}
