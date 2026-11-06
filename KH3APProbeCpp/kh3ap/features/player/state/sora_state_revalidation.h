#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace kh3ap
{
    inline constexpr uintptr_t runtime_sora_state_comp_outer_offset = 0x28;
    inline constexpr uintptr_t runtime_sora_current_state_pointer_offset = 0x270;
    inline constexpr uintptr_t runtime_sora_current_state_id_offset = 0x58;

    enum class RuntimeSoraStateRevalidationResult
    {
        CacheUnavailable,
        PawnMismatch,
        StateCompOwnerMismatch,
        CurrentStateUnavailable,
        StateIdUnavailable,
        CatalogMiss,
        Unchanged,
        Changed,
    };

    struct RuntimeSoraStateRevalidationSnapshot
    {
        uintptr_t active_pawn{};
        uintptr_t cached_pawn{};
        uintptr_t state_comp{};
        uintptr_t state_comp_outer{};
        uintptr_t current_state{};
        uintptr_t cached_current_state{};
        std::optional<uint8_t> current_state_id{};
        std::optional<uint8_t> cached_current_state_id{};
        bool catalog_match{false};
    };

    constexpr auto classify_runtime_sora_state_revalidation(
            const RuntimeSoraStateRevalidationSnapshot& snapshot) -> RuntimeSoraStateRevalidationResult
    {
        if (snapshot.active_pawn == 0 || snapshot.cached_pawn == 0 || snapshot.state_comp == 0)
        {
            return RuntimeSoraStateRevalidationResult::CacheUnavailable;
        }
        if (snapshot.active_pawn != snapshot.cached_pawn)
        {
            return RuntimeSoraStateRevalidationResult::PawnMismatch;
        }
        if (snapshot.state_comp_outer != snapshot.active_pawn)
        {
            return RuntimeSoraStateRevalidationResult::StateCompOwnerMismatch;
        }
        if (snapshot.current_state == 0)
        {
            return RuntimeSoraStateRevalidationResult::CurrentStateUnavailable;
        }
        if (!snapshot.current_state_id.has_value())
        {
            return RuntimeSoraStateRevalidationResult::StateIdUnavailable;
        }
        if (snapshot.current_state == snapshot.cached_current_state &&
            snapshot.current_state_id == snapshot.cached_current_state_id)
        {
            return RuntimeSoraStateRevalidationResult::Unchanged;
        }
        if (!snapshot.catalog_match)
        {
            return RuntimeSoraStateRevalidationResult::CatalogMiss;
        }
        return RuntimeSoraStateRevalidationResult::Changed;
    }

    constexpr auto runtime_sora_state_revalidation_result_label(
            RuntimeSoraStateRevalidationResult result) -> std::wstring_view
    {
        switch (result)
        {
        case RuntimeSoraStateRevalidationResult::CacheUnavailable:
            return L"cache_unavailable";
        case RuntimeSoraStateRevalidationResult::PawnMismatch:
            return L"pawn_mismatch";
        case RuntimeSoraStateRevalidationResult::StateCompOwnerMismatch:
            return L"state_comp_owner_mismatch";
        case RuntimeSoraStateRevalidationResult::CurrentStateUnavailable:
            return L"current_state_unavailable";
        case RuntimeSoraStateRevalidationResult::StateIdUnavailable:
            return L"current_state_id_unavailable";
        case RuntimeSoraStateRevalidationResult::CatalogMiss:
            return L"catalog_miss";
        case RuntimeSoraStateRevalidationResult::Unchanged:
            return L"unchanged";
        case RuntimeSoraStateRevalidationResult::Changed:
            return L"changed";
        }
        return L"unknown";
    }

    constexpr auto runtime_sora_state_revalidation_requires_cache_invalidation(
            RuntimeSoraStateRevalidationResult result) -> bool
    {
        return result == RuntimeSoraStateRevalidationResult::CacheUnavailable ||
               result == RuntimeSoraStateRevalidationResult::PawnMismatch ||
               result == RuntimeSoraStateRevalidationResult::StateCompOwnerMismatch ||
               result == RuntimeSoraStateRevalidationResult::CurrentStateUnavailable ||
               result == RuntimeSoraStateRevalidationResult::StateIdUnavailable;
    }

    constexpr auto runtime_sora_state_revalidation_invalidates_state_comp(
            RuntimeSoraStateRevalidationResult result) -> bool
    {
        return result == RuntimeSoraStateRevalidationResult::CacheUnavailable ||
               result == RuntimeSoraStateRevalidationResult::PawnMismatch ||
               result == RuntimeSoraStateRevalidationResult::StateCompOwnerMismatch;
    }
}
