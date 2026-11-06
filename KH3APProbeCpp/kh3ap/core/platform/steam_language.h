#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace kh3ap
{
    // Steam exposes language names such as "english", "schinese", and
    // "brazilian". Unreal's internationalization APIs expect culture names.
    // Keep this conversion independent of the active account and machine.
    auto steam_language_to_unreal_culture(std::string_view steam_language)
            -> std::optional<std::wstring>;

    // Extracts the effective per-game language from a Steam appmanifest.
    // UserConfig is authoritative; MountedConfig is accepted as a fallback.
    auto steam_app_language_from_manifest(std::string_view manifest)
            -> std::optional<std::string>;
}
