#include "kh3ap/features/player/context/context_resolver.h"

#include "kh3ap/bridge/utilities.h"

#include <format>
#include "kh3ap/bridge/serialization/json_fields.h"
#include <sstream>

namespace kh3ap
{
    namespace
    {
        constexpr std::wstring_view s_levels_prefix{L"/Game/Levels/"};
        constexpr std::wstring_view s_maps_prefix{L"/Game/Maps/"};

        auto make_snapshot_from_package_path(
                std::wstring_view package_path,
                std::wstring_view hook_name,
                std::wstring_view object_full_name = {}) -> std::optional<RuntimeContextSnapshot>
        {
            if (!package_path.starts_with(s_levels_prefix) && !package_path.starts_with(s_maps_prefix))
            {
                return std::nullopt;
            }

            const size_t slash_pos = package_path.find_last_of(L'/');
            const std::wstring_view world_name =
                    (slash_pos == std::wstring_view::npos) ? package_path : package_path.substr(slash_pos + 1);
            if (world_name.empty())
            {
                return std::nullopt;
            }

            RuntimeContextSnapshot snapshot{};
            snapshot.hook = std::wstring{hook_name.empty() ? std::wstring_view{L"<unknown>"} : hook_name};
            snapshot.world = std::format(L"World {}.{}", package_path, world_name);
            snapshot.outer = std::format(L"Package {}", package_path);
            snapshot.object = std::wstring{object_full_name};
            const auto timestamp = now_iso8601_utc();
            snapshot.opened_at.assign(timestamp.begin(), timestamp.end());
            return snapshot;
        }
    }

    auto RuntimeContextResolver::parse_snapshot(std::string_view context_text) -> std::optional<RuntimeContextSnapshot>
    {
        const JsonObject object{context_text};
        if (!object.valid()) return std::nullopt;
        RuntimeContextSnapshot snapshot{};
        snapshot.hook = object.string_field("hook").value_or(L"");
        snapshot.world = object.string_field("world").value_or(L"");
        snapshot.outer = object.string_field("outer").value_or(L"");
        snapshot.object = object.string_field("object").value_or(L"");
        snapshot.pawn_source = object.string_field("pawn_source").value_or(L"");
        snapshot.opened_at = object.string_field("opened_at").value_or(L"");
        snapshot.sora_level = object.int64_field("sora_level");

        if (snapshot.hook.empty() && snapshot.world.empty() && snapshot.outer.empty())
        {
            return std::nullopt;
        }

        return snapshot;
    }

    auto RuntimeContextResolver::summarize(const RuntimeContextSnapshot& snapshot) -> std::wstring
    {
        std::wostringstream summary;
        summary << L"hook=" << (snapshot.hook.empty() ? std::wstring{L"<none>"} : snapshot.hook)
                << L" | world=" << (snapshot.world.empty() ? std::wstring{L"<none>"} : snapshot.world)
                << L" | outer=" << (snapshot.outer.empty() ? std::wstring{L"<none>"} : snapshot.outer);
        if (!snapshot.object.empty())
        {
            summary << L" | object=" << snapshot.object;
        }
        if (!snapshot.pawn_source.empty())
        {
            summary << L" | pawn_source=" << snapshot.pawn_source;
        }
        if (snapshot.sora_level.has_value())
        {
            summary << L" | sora_level=" << *snapshot.sora_level;
        }
        return summary.str();
    }

    auto RuntimeContextResolver::is_object_not_real(std::wstring_view summary) -> bool
    {
        return summary.find(L"real=false") != std::wstring_view::npos;
    }

    auto RuntimeContextResolver::is_cutscene_or_camera_context(const RuntimeContextSnapshot& snapshot) -> bool
    {
        const std::wstring context_text = snapshot.world + L" " + snapshot.outer;
        return context_text.find(L"_camera") != std::wstring::npos ||
               context_text.find(L"camera_") != std::wstring::npos ||
               context_text.find(L"/camera") != std::wstring::npos ||
               context_text.find(L"_cutscene") != std::wstring::npos ||
               context_text.find(L"cutscene_") != std::wstring::npos ||
               context_text.find(L"/cutscene") != std::wstring::npos;
    }

    auto RuntimeContextResolver::is_twilight_town_context(const RuntimeContextSnapshot& snapshot) -> bool
    {
        const auto contains_tt_marker = [](std::wstring_view value) {
            return value.find(L"/tt/") != std::wstring_view::npos ||
                   value.find(L"/TT/") != std::wstring_view::npos ||
                   value.find(L"_tt_") != std::wstring_view::npos ||
                   value.find(L"_TT_") != std::wstring_view::npos ||
                   value.find(L"TT_TRM2_EVENT02") != std::wstring_view::npos;
        };

        return contains_tt_marker(snapshot.world) ||
               contains_tt_marker(snapshot.outer) ||
               contains_tt_marker(snapshot.object) ||
               contains_tt_marker(snapshot.hook);
    }

    auto RuntimeContextResolver::derive_from_frame_full_name(
            std::wstring_view frame_full_name,
            std::wstring_view hook_name) -> std::optional<RuntimeContextSnapshot>
    {
        constexpr std::wstring_view function_prefix{L"Function "};
        std::wstring_view remaining = frame_full_name;
        if (remaining.starts_with(function_prefix))
        {
            remaining.remove_prefix(function_prefix.size());
        }

        const size_t colon_pos = remaining.find(L':');
        const std::wstring_view class_path = (colon_pos == std::wstring_view::npos) ? remaining : remaining.substr(0, colon_pos);
        const size_t dot_pos = class_path.find(L'.');
        const std::wstring_view package_path = (dot_pos == std::wstring_view::npos) ? class_path : class_path.substr(0, dot_pos);

        return make_snapshot_from_package_path(package_path, hook_name);
    }

    auto RuntimeContextResolver::derive_from_object_full_name(
            std::wstring_view object_full_name,
            std::wstring_view hook_name) -> std::optional<RuntimeContextSnapshot>
    {
        size_t begin = object_full_name.find(s_levels_prefix);
        if (begin == std::wstring_view::npos)
        {
            begin = object_full_name.find(s_maps_prefix);
        }
        if (begin == std::wstring_view::npos)
        {
            return std::nullopt;
        }

        size_t end = object_full_name.find(L':', begin);
        if (end == std::wstring_view::npos)
        {
            end = object_full_name.find(L' ', begin);
        }
        const std::wstring_view class_path =
                (end == std::wstring_view::npos || end <= begin) ? object_full_name.substr(begin) : object_full_name.substr(begin, end - begin);
        const size_t dot_pos = class_path.find(L'.');
        const std::wstring_view package_path = (dot_pos == std::wstring_view::npos) ? class_path : class_path.substr(0, dot_pos);

        return make_snapshot_from_package_path(package_path, hook_name, object_full_name);
    }
}
