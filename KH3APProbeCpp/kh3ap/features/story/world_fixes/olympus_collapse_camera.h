#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RC::Unreal { class UObject; }

namespace kh3ap
{
    inline constexpr auto runtime_olympus_collapse_camera_class = L"he_03_camera_ms024s_C";
    inline constexpr auto runtime_olympus_collapse_gameflow_class = L"he_03_gameflow_C";
    using OlympusCollapseName = std::array<uint32_t, 3>;
    enum class OlympusCollapseCameraResult { rejected, applied, already_applied };

    struct OlympusCollapseCameraLinks
    {
        uintptr_t spawn{};
        uintptr_t end_same{};
        uintptr_t end_all{};
        std::array<OlympusCollapseName, 3> camera_names{};
        OlympusCollapseName end_normal{};
    };

    // Retail ms024s contains the first building collapse and the downhill
    // collapse, with two alternative OTS targets for the latter. Jump over only
    // their camera setup/cleanup; preserve the scenery timelines and callbacks.
    inline auto patch_olympus_collapse_camera(uint8_t* script, size_t size,
            const OlympusCollapseCameraLinks& links) -> OlympusCollapseCameraResult
    {
        constexpr std::array<uint32_t, 6> starts{2161, 2974, 4458, 4228, 6186, 11437};
        constexpr std::array<uint32_t, 6> ends{2897, 2897, 5231, 4271, 6229, 11487};
        if (!script || size != 11501 || !links.spawn || !links.end_same || !links.end_all)
            return OlympusCollapseCameraResult::rejected;
        const auto matches = [&](size_t offset, const auto& value) {
            return std::memcmp(script + offset, &value, sizeof(value)) == 0;
        };
        size_t patched = 0;
        for (size_t i = 0; i < starts.size(); ++i)
        {
            const auto start = starts[i];
            if (script[start] == 0x06 && matches(start + 1, ends[i])) ++patched;
            else if (script[start] != (i < 3 ? 0x5F : 0x19) ||
                     script[start + 1] != (i < 3 ? 0x00 : 0x20))
                return OlympusCollapseCameraResult::rejected;
        }
        // Validate the untouched operands even on repeat application. Do not
        // accept partially patched graphs or write anything before all checks.
        if (patched != 0 && patched != starts.size()) return OlympusCollapseCameraResult::rejected;
        for (size_t i = 0; i < 3; ++i)
        {
            const auto s = starts[i];
            if (!links.camera_names[i][0] || script[s + 10] != 0x19 || script[s + 11] != 0x20 ||
                !matches(s + 20, uint32_t{33}) || script[s + 32] != 0x1C ||
                !matches(s + 33, links.spawn) || script[s + 41] != 0x17 ||
                script[s + 42] != 0x20 || script[s + 51] != 0x21 ||
                !matches(s + 52, links.camera_names[i]) || script[s + 64] != 0x16)
                return OlympusCollapseCameraResult::rejected;
        }
        for (size_t i = 3; i < 5; ++i)
        {
            const auto s = starts[i];
            if (!matches(s + 10, uint32_t{21}) || script[s + 22] != 0x1C ||
                !matches(s + 23, i == 3 ? links.end_same : links.end_all) ||
                script[s + 31] != 0x17 || script[s + 32] != 0x28 ||
                script[s + 33] != 0x00 || script[s + 42] != 0x16)
                return OlympusCollapseCameraResult::rejected;
        }
        if (!links.end_normal[0] || !matches(11447, uint32_t{28}) || script[11459] != 0x1B ||
            !matches(11460, links.end_normal) || script[11486] != 0x16 ||
            script[2897] != 0x19 || script[5231] != 0x19 || script[4271] != 0x14 ||
            script[6229] != 0x14 || script[11487] != 0x4D || script[11500] != 0x53)
            return OlympusCollapseCameraResult::rejected;
        if (patched) return OlympusCollapseCameraResult::already_applied;
        for (size_t i = 0; i < starts.size(); ++i)
        {
            script[starts[i]] = 0x06; // EX_Jump, retaining all subsequent offsets.
            std::memcpy(script + starts[i] + 1, &ends[i], sizeof(ends[i]));
        }
        return OlympusCollapseCameraResult::applied;
    }

    struct OlympusCollapseMovementLinks
    {
        uintptr_t start_cinematic{};
        uintptr_t end_cinematic{};
        uintptr_t get_pawn{};
    };

    inline auto patch_olympus_collapse_movement(uint8_t* script, size_t size,
            const OlympusCollapseMovementLinks& links) -> OlympusCollapseCameraResult
    {
        constexpr std::array<uint32_t, 7> starts{581, 7772, 8112, 10035, 4282, 6039, 1485};
        constexpr std::array<uint32_t, 7> ends{627, 7818, 8158, 10081, 4317, 6074, 1748};
        if (!script || size != 11501 || !links.start_cinematic || !links.end_cinematic || !links.get_pawn)
            return OlympusCollapseCameraResult::rejected;
        const auto matches = [&](size_t offset, const auto& value) {
            return std::memcmp(script + offset, &value, sizeof(value)) == 0;
        };
        size_t patched = 0;
        for (size_t i = 0; i < starts.size(); ++i)
        {
            const auto s = starts[i];
            if (script[s] == 0x06 && matches(s + 1, ends[i])) ++patched;
            else if (script[s] != (i == 6 ? 0x5F : 0x19) || script[s + 1] != (i == 6 ? 0x00 : 0x20))
                return OlympusCollapseCameraResult::rejected;
            if (i < 6)
            {
                if (!matches(s + 10, uint32_t{i < 4 ? 24u : 13u}) || script[s + 22] != 0x1C ||
                    !matches(s + 23, i < 4 ? links.start_cinematic : links.end_cinematic) ||
                    script[s + 31] != 0x17 || script[ends[i] - 1] != 0x16)
                    return OlympusCollapseCameraResult::rejected;
            }
        }
        // Without cinematic mode the pawn never reaches state 67. Bypass its
        // retry loop and forced rt001 animation, entering the original scenery
        // and shake sequence directly. Keep the pending on-ground checks above.
        if (!matches(1505, uint32_t{11}) || script[1495] != 0x19 || script[1496] != 0x20 ||
            script[1517] != 0x1C || !matches(1518, links.get_pawn) || script[1526] != 0x17 ||
            script[1527] != 0x16 || script[1748] != 0x4C || !matches(1749, uint32_t{3806}) ||
            script[1753] != 0x4C || !matches(1754, uint32_t{3861}) ||
            script[627] != 0x4D || script[7818] != 0x06 || script[8158] != 0x06 ||
            script[10081] != 0x4C || script[4317] != 0x19 || script[6074] != 0x19 ||
            (patched != 0 && patched != starts.size()))
            return OlympusCollapseCameraResult::rejected;
        if (patched) return OlympusCollapseCameraResult::already_applied;
        for (size_t i = 0; i < starts.size(); ++i)
        {
            script[starts[i]] = 0x06;
            std::memcpy(script + starts[i] + 1, &ends[i], sizeof(ends[i]));
        }
        return OlympusCollapseCameraResult::applied;
    }

    inline auto patch_olympus_collapse_completion(uint8_t* entry, size_t entry_size,
            const uint8_t* graph, size_t graph_size, const OlympusCollapseName& graph_name,
            const OlympusCollapseName& voice_name) -> OlympusCollapseCameraResult
    {
        // The original completion resets Sora to IdleN and waits 0.6s. Continue
        // with the original field-voice callback without touching Sora's state.
        constexpr uint32_t original = 463011;
        constexpr uint32_t resume = 19103;
        if (!entry || !graph || entry_size != 22 || graph_size != 464063 ||
            !graph_name[0] || !voice_name[0] || entry[0] != 0x1B || entry[13] != 0x1D ||
            entry[18] != 0x16 || entry[19] != 0x04 || entry[20] != 0x0B || entry[21] != 0x53 ||
            std::memcmp(entry + 1, graph_name.data(), sizeof(graph_name)) != 0)
            return OlympusCollapseCameraResult::rejected;
        const auto* callback = graph + resume;
        uint32_t context_size{};
        std::memcpy(&context_size, callback + 20, sizeof(context_size));
        if (graph[original] != 0x06 || callback[0] != 0x14 || callback[1] != 0x00 ||
            callback[10] != 0x19 || callback[11] != 0x20 || context_size != 24 ||
            callback[32] != 0x1C || callback[41] != 0x17 || callback[42] != 0x21 ||
            std::memcmp(callback + 43, voice_name.data(), sizeof(voice_name)) != 0 ||
            callback[55] != 0x16 || callback[56] != 0x4D)
            return OlympusCollapseCameraResult::rejected;
        uint32_t destination{};
        std::memcpy(&destination, entry + 14, sizeof(destination));
        if (destination == resume) return OlympusCollapseCameraResult::already_applied;
        if (destination != original) return OlympusCollapseCameraResult::rejected;
        std::memcpy(entry + 14, &resume, sizeof(resume));
        return OlympusCollapseCameraResult::applied;
    }

    auto try_apply_olympus_collapse_camera(RC::Unreal::UObject* object) -> bool;
    auto try_apply_olympus_collapse_completion(RC::Unreal::UObject* object) -> bool;
}
