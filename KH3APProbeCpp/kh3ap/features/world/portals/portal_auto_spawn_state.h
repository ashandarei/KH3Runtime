#pragma once

#include <Unreal/FWeakObjectPtr.hpp>
#include <chrono>
#include <cstdint>
#include <string>

namespace kh3ap
{
    struct RuntimeAutoPortalSpawnState
    {
        std::wstring last_context_key{};
        std::wstring last_playable_context_key{};
        std::chrono::steady_clock::time_point last_attempt_at{};
        bool spawn_attempted{};
        bool asset_load_requested{};
        bool playable_asset_load_requested{};
        RC::Unreal::FWeakObjectPtr spawned_actor{};
        std::wstring spawned_actor_name{};
        std::chrono::steady_clock::time_point verification_not_before{};
        bool verification_complete{};
        bool verification_retry_armed{};
        bool verification_retry_attempted{};
        bool context_entered_logged{};
        bool map_ready_wait_logged{};
        uint32_t class_load_request_count{};
        bool class_load_failed_closed{};
        std::wstring last_ineligibility_reason{};

        // A new context discards pending work; the caller owns context identities.
        auto reset_spawn_work() -> void
        {
            spawn_attempted = false;
            asset_load_requested = false;
            playable_asset_load_requested = false;
            spawned_actor = RC::Unreal::FWeakObjectPtr{};
            spawned_actor_name.clear();
            last_attempt_at = {};
            verification_not_before = {};
            verification_complete = false;
            verification_retry_armed = false;
            verification_retry_attempted = false;
            context_entered_logged = false;
            map_ready_wait_logged = false;
            class_load_request_count = 0;
            class_load_failed_closed = false;
            last_ineligibility_reason.clear();
        }
    };
} // namespace kh3ap
