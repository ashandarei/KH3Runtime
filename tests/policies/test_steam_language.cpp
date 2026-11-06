#include "kh3ap/core/platform/steam_language.h"

#include <cassert>
#include <string_view>

int main()
{
    using kh3ap::steam_app_language_from_manifest;
    using kh3ap::steam_language_to_unreal_culture;

    assert(steam_language_to_unreal_culture("english") == L"en");
    assert(steam_language_to_unreal_culture("  ENGLISH \t") == L"en");
    assert(steam_language_to_unreal_culture("japanese") == L"ja");
    assert(steam_language_to_unreal_culture("schinese") == L"zh-Hans");
    assert(steam_language_to_unreal_culture("tchinese") == L"zh-Hant");
    assert(steam_language_to_unreal_culture("brazilian") == L"pt-BR");
    assert(steam_language_to_unreal_culture("latam") == L"es-419");
    assert(!steam_language_to_unreal_culture("").has_value());
    assert(!steam_language_to_unreal_culture("not-a-steam-language").has_value());

    constexpr std::string_view standard_manifest = R"vdf(
        "AppState"
        {
            "appid" "2552450"
            "language" "ignored-at-root"
            "UserConfig"
            {
                "language" "english"
            }
            "MountedConfig"
            {
                "language" "japanese"
            }
        }
    )vdf";
    assert(steam_app_language_from_manifest(standard_manifest) == "english");

    constexpr std::string_view mounted_only_manifest = R"vdf(
        // Steam may omit UserConfig when no explicit choice was stored.
        "AppState" { "MountedConfig" { "language" "brazilian" } }
    )vdf";
    assert(steam_app_language_from_manifest(mounted_only_manifest) == "brazilian");

    constexpr std::string_view unrelated_language = R"vdf(
        "AppState" { "OtherConfig" { "language" "english" } }
    )vdf";
    assert(!steam_app_language_from_manifest(unrelated_language).has_value());
    assert(!steam_app_language_from_manifest(
                    R"vdf("AppState" { "UserConfig" { "language")vdf")
                    .has_value());

    return 0;
}
