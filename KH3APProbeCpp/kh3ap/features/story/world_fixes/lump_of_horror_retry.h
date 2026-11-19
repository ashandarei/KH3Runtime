#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>

namespace RC::Unreal { class UObject; }

namespace kh3ap
{
    enum class LumpRetryPatch { rejected, applied, already_applied };

    inline auto patch_lump_retry_skip(std::span<uint8_t> graph, uintptr_t lump_class,
                                      uintptr_t on_retry_skip) -> LumpRetryPatch
    {
        // mi_04_camera_ms170_REvt_Retry02_End_skip in the retail mi_04 gameflow.
        // Its failed cast used to return before the shared battle restart.
        constexpr size_t cast = 304357, guard = 304404, call_context = 304414;
        constexpr uint8_t nothing = 0x0b, context_fail_silent = 0x1a;
        if (graph.size() != 304545 || !lump_class || !on_retry_skip)
        {
            return LumpRetryPatch::rejected;
        }
        const auto matches = [&](size_t at, const auto& value) {
            return std::memcmp(graph.data() + at, &value, sizeof(value)) == 0;
        };
        // Check the cast, the cast-result local used by the method receiver,
        // and the whole no-argument Context expression including its native
        // function pointer and the jump to the original battle continuation.
        if (graph[cast] != 0x2e || !matches(cast + 1, lump_class) ||
            graph[304348] != 0x00 || graph[call_context + 1] != 0x00 ||
            std::memcmp(&graph[304349], &graph[call_context + 2], sizeof(uintptr_t)) != 0 ||
            !matches(call_context + 10, uint32_t{10}) ||
            !matches(call_context + 14, uintptr_t{0}) || graph[call_context + 22] != 0x1c ||
            !matches(call_context + 23, on_retry_skip) || graph[call_context + 31] != 0x16 ||
            graph[call_context + 32] != 0x06 || !matches(call_context + 33, uint32_t{292842}))
        {
            return LumpRetryPatch::rejected;
        }
        auto early_return = graph.subspan(guard, call_context - guard);
        if (graph[call_context] == context_fail_silent &&
            std::all_of(early_return.begin(), early_return.end(), [](uint8_t b) { return b == nothing; }))
        {
            return LumpRetryPatch::already_applied;
        }
        if (graph[304393] != 0x38 || graph[304394] != 0x47 || graph[304395] != 0x00 ||
            std::memcmp(&graph[304396], &graph[304349], sizeof(uintptr_t)) != 0 ||
            graph[304384] != 0x00 || graph[guard] != 0x4f || graph[guard + 1] != 0x00 ||
            std::memcmp(&graph[304385], &graph[guard + 2], sizeof(uintptr_t)) != 0 ||
            graph[call_context] != 0x19)
        {
            return LumpRetryPatch::rejected;
        }
        // A silent Context skips exactly OnRetrySkip when the cast yielded
        // null. A native Lump still receives the call. Both take the authored
        // continuation and all latent and jump offsets stay in place.
        graph[call_context] = context_fail_silent;
        std::fill(early_return.begin(), early_return.end(), nothing);
        return LumpRetryPatch::applied;
    }

    auto try_apply_lump_of_horror_retry(RC::Unreal::UObject* object) -> bool;
}
