#pragma once

#include "kh3ap/core/types.h"

#include <optional>
#include <string>
#include <string_view>

namespace kh3ap
{
    class RuntimeContextResolver
    {
    public:
        static auto parse_snapshot(std::string_view context_text) -> std::optional<RuntimeContextSnapshot>;
        static auto summarize(const RuntimeContextSnapshot& snapshot) -> std::wstring;
        static auto is_object_not_real(std::wstring_view summary) -> bool;
        static auto is_cutscene_or_camera_context(const RuntimeContextSnapshot& snapshot) -> bool;
        static auto is_twilight_town_context(const RuntimeContextSnapshot& snapshot) -> bool;

        static auto derive_from_frame_full_name(
                std::wstring_view frame_full_name,
                std::wstring_view hook_name) -> std::optional<RuntimeContextSnapshot>;

        static auto derive_from_object_full_name(
                std::wstring_view object_full_name,
                std::wstring_view hook_name) -> std::optional<RuntimeContextSnapshot>;
    };
}
