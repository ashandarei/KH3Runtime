#pragma once

#include "kh3ap/features/grants/abilities/sora_ability_snapshot.h"

#include <cstdint>
#include <array>
#include <string>
#include <vector>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimeState;
    struct RuntimeItemGrantRequest;

    auto try_call_persistent_inventory_grant(std::wstring_view token, RC::Unreal::UObject* aqua,
            bool show_info, std::wstring* failure_reason) -> bool;

    // Reconcile native command/shortcut lists from existing spell ownership.
    // Does not increment bonuses or change the Magic controller restriction.
    auto try_refresh_runtime_sora_magic_commands(
            RC::Unreal::UObject* sora_pawn,
            std::wstring* failure_reason = nullptr) -> bool;

    // Read the six highest unlocked spell commands from Sora's authoritative
    // bonus bank. Owned swap pawns may query it without granting any spells.
    auto try_read_runtime_sora_magic_commands(
            RC::Unreal::UObject* context, std::array<uint8_t, 6>& commands,
            std::wstring* failure_reason = nullptr) -> bool;

// Reconcile owned Heartbinders through the native post-inventory command path.
    // Inventory counts, AP receipts and the Links controller gate are unchanged.
    auto try_refresh_runtime_sora_link_commands(
            RC::Unreal::UObject* sora_pawn, RuntimeState& runtime_state,
            std::wstring* failure_reason = nullptr) -> bool;

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto inspect_persistent_character_reward(RC::Unreal::UObject* context, const std::wstring& token,
            int& active, int& staging, int& magic_command) -> bool;
    // Diagnostic fixture for controller Magic only; compare-before-write both banks.
    auto runtime_magic_fixture(RC::Unreal::UObject* sora, std::array<uint32_t, 2>& values,
            const std::array<uint32_t, 2>* expected = nullptr,
            const std::array<uint32_t, 2>* replacement = nullptr) -> bool;
#endif

    auto try_read_runtime_sora_ability_entry_state(
            uint8_t ability_value,
            RC::Unreal::UObject* sora_pawn,
            RuntimeSoraAbilityEntryState* state,
            std::wstring* failure_reason = nullptr) -> bool;

    auto try_read_runtime_sora_ability_entry_states(
            uint8_t first_ability,
            RC::Unreal::UObject* sora_pawn,
            std::span<RuntimeSoraAbilityEntryState> states,
            std::wstring* failure_reason = nullptr) -> bool;

    // Query non-contiguous IDs with one validated bank/range read. State is
    // local to this call; no ownership or bank address survives an event.
    auto try_read_runtime_sora_ability_entry_states(
            std::span<const uint8_t> abilities,
            RC::Unreal::UObject* sora_pawn,
            std::span<RuntimeSoraAbilityEntryState> states,
            std::wstring* failure_reason = nullptr) -> bool;

    auto try_grant_unique_equipped_runtime_sora_ability(
            uint8_t ability_value,
            RC::Unreal::UObject* sora_pawn,
            bool* newly_granted,
            RuntimeSoraAbilityEntryState* state,
            std::wstring* failure_reason = nullptr) -> bool;

    // Remove one direct durable source in both authoritative banks, preserving
    // other copies and VBonus sources. Used for 0.2's temporary Combo Master.
    auto try_remove_runtime_sora_durable_ability_copy(
            uint8_t ability_value, RC::Unreal::UObject* sora_pawn,
            std::wstring* failure_reason = nullptr) -> bool;

    auto try_present_native_vbonus_hud_ability_tokens(
            const std::vector<std::wstring>& ability_tokens,
            RC::Unreal::UObject* sora_pawn,
            RuntimeState& runtime_state,
            std::wstring* failure_reason = nullptr) -> bool;

    auto try_call_durable_sora_ability_grant(
            const RuntimeItemGrantRequest& request,
            RC::Unreal::UObject* sora_pawn,
            RuntimeState& runtime_state,
            std::wstring* failure_reason = nullptr,
            bool auto_equip_movement = false) -> bool;

    auto try_call_durable_sora_vbonus_grant(
            const RuntimeItemGrantRequest& request,
            RC::Unreal::UObject* sora_pawn,
            RuntimeState& runtime_state,
            std::wstring* failure_reason = nullptr) -> bool;
}
