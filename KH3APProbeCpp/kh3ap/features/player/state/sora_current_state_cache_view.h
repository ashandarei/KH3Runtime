#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    struct RuntimeSoraCurrentStateCacheView
    {
        bool available{false};
        RC::Unreal::UObject* state_comp{};
        RC::Unreal::UObject* current_state{};
        bool state_comp_valid{false};
        bool current_state_valid{false};
        std::optional<uint8_t> current_state_id{};
        std::wstring state_class{};
        std::wstring state_source{};
        uintptr_t pawn_address{};
        bool polyhook_installed{false};
        uintptr_t polyhook_target{};
        uint64_t event_index{};
        uintptr_t state_comp_address{};
    };
}
