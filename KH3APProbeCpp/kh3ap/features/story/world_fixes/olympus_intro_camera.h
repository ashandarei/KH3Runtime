#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RC::Unreal { class UObject; }

namespace kh3ap
{
    inline constexpr auto runtime_olympus_intro_camera_class = L"he_03_camera_ms010_C";
    using OlympusIntroScriptName = std::array<uint32_t, 3>;
    enum class OlympusIntroCameraResult { rejected, applied, already_applied };

    inline auto patch_olympus_intro_camera(
            uint8_t* entry, size_t entry_size, const uint8_t* graph, size_t graph_size,
            const OlympusIntroScriptName& graph_name,
            const OlympusIntroScriptName& completion_name) -> OlympusIntroCameraResult
    {
        // Reviewed retail ms010: the entry plays a six-second Matinee, whose
        // Finished path resets the camera and blends a temporary back_camera.
        // Enter its final RemoteEvent directly. The gameflow still owns tutorial
        // flags, cinematic cleanup and autosave; no camera actor is started.
        constexpr uint32_t play_entry = 1044;
        constexpr uint32_t completion_entry = 987;
        if (!entry || !graph || entry_size != 22 || graph_size != 1151 ||
            !graph_name[0] || !completion_name[0] ||
            entry[0] != 0x1B || entry[13] != 0x1D || entry[18] != 0x16 ||
            entry[19] != 0x04 || entry[20] != 0x0B || entry[21] != 0x53 ||
            std::memcmp(entry + 1, graph_name.data(), sizeof(graph_name)) != 0)
        {
            return OlympusIntroCameraResult::rejected;
        }
        // Validate the complete callback shape and its linked FName before
        // retargeting the event. Unknown game/asset layouts remain untouched.
        const auto* callback = graph + completion_entry;
        uint32_t context_size{};
        std::memcpy(&context_size, callback + 20, sizeof(context_size));
        if (callback[0] != 0x14 || callback[1] != 0x00 ||
            callback[10] != 0x19 || callback[11] != 0x20 ||
            context_size != 24 || callback[32] != 0x1C ||
            callback[41] != 0x17 || callback[42] != 0x21 ||
            std::memcmp(callback + 43, completion_name.data(), sizeof(completion_name)) != 0 ||
            callback[55] != 0x16 || callback[56] != 0x4D ||
            graph[play_entry] != 0x14)
        {
            return OlympusIntroCameraResult::rejected;
        }
        uint32_t destination{};
        std::memcpy(&destination, entry + 14, sizeof(destination));
        if (destination == completion_entry)
        {
            return OlympusIntroCameraResult::already_applied;
        }
        if (destination != play_entry)
        {
            return OlympusIntroCameraResult::rejected;
        }
        std::memcpy(entry + 14, &completion_entry, sizeof(completion_entry));
        return OlympusIntroCameraResult::applied;
    }

    auto try_apply_olympus_intro_camera(RC::Unreal::UObject* object) -> bool;
}
