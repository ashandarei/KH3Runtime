#include "kh3ap/core/platform/steam_language.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <utility>
#include <vector>

namespace kh3ap
{
    namespace
    {
        struct SteamLanguageCulture
        {
            std::string_view steam_language{};
            std::wstring_view unreal_culture{};
        };

        constexpr std::array s_steam_language_cultures{
                SteamLanguageCulture{"arabic", L"ar"},
                SteamLanguageCulture{"bulgarian", L"bg"},
                SteamLanguageCulture{"schinese", L"zh-Hans"},
                SteamLanguageCulture{"tchinese", L"zh-Hant"},
                SteamLanguageCulture{"czech", L"cs"},
                SteamLanguageCulture{"danish", L"da"},
                SteamLanguageCulture{"dutch", L"nl"},
                SteamLanguageCulture{"english", L"en"},
                SteamLanguageCulture{"finnish", L"fi"},
                SteamLanguageCulture{"french", L"fr"},
                SteamLanguageCulture{"german", L"de"},
                SteamLanguageCulture{"greek", L"el"},
                SteamLanguageCulture{"hungarian", L"hu"},
                SteamLanguageCulture{"indonesian", L"id"},
                SteamLanguageCulture{"italian", L"it"},
                SteamLanguageCulture{"japanese", L"ja"},
                SteamLanguageCulture{"koreana", L"ko"},
                SteamLanguageCulture{"korean", L"ko"},
                SteamLanguageCulture{"norwegian", L"no"},
                SteamLanguageCulture{"polish", L"pl"},
                SteamLanguageCulture{"portuguese", L"pt-PT"},
                SteamLanguageCulture{"brazilian", L"pt-BR"},
                SteamLanguageCulture{"romanian", L"ro"},
                SteamLanguageCulture{"russian", L"ru"},
                SteamLanguageCulture{"spanish", L"es"},
                SteamLanguageCulture{"latam", L"es-419"},
                SteamLanguageCulture{"spanish-latin-america", L"es-419"},
                SteamLanguageCulture{"swedish", L"sv"},
                SteamLanguageCulture{"thai", L"th"},
                SteamLanguageCulture{"turkish", L"tr"},
                SteamLanguageCulture{"ukrainian", L"uk"},
                SteamLanguageCulture{"vietnamese", L"vi"},
        };

        auto normalized_steam_language(std::string_view value) -> std::string
        {
            const auto first = std::find_if_not(
                    value.begin(),
                    value.end(),
                    [](unsigned char character) {
                        return std::isspace(character) != 0;
                    });
            const auto last = std::find_if_not(
                    value.rbegin(),
                    value.rend(),
                    [](unsigned char character) {
                        return std::isspace(character) != 0;
                    }).base();
            if (first >= last)
            {
                return {};
            }

            std::string normalized(first, last);
            std::transform(
                    normalized.begin(),
                    normalized.end(),
                    normalized.begin(),
                    [](unsigned char character) {
                        return static_cast<char>(std::tolower(character));
                    });
            return normalized;
        }

        enum class KeyValuesTokenKind
        {
            string,
            open_brace,
            close_brace,
            end,
            invalid,
        };

        struct KeyValuesToken
        {
            KeyValuesTokenKind kind{KeyValuesTokenKind::invalid};
            std::string text{};
        };

        class KeyValuesTokenizer
        {
        public:
            explicit KeyValuesTokenizer(std::string_view input) : input_(input) {}

            auto next() -> KeyValuesToken
            {
                skip_ignored();
                if (offset_ >= input_.size())
                {
                    return {KeyValuesTokenKind::end, {}};
                }

                const char character = input_[offset_++];
                if (character == '{')
                {
                    return {KeyValuesTokenKind::open_brace, {}};
                }
                if (character == '}')
                {
                    return {KeyValuesTokenKind::close_brace, {}};
                }
                if (character != '"')
                {
                    return {KeyValuesTokenKind::invalid, {}};
                }

                std::string value{};
                while (offset_ < input_.size())
                {
                    const char current = input_[offset_++];
                    if (current == '"')
                    {
                        return {KeyValuesTokenKind::string, std::move(value)};
                    }
                    if (current == '\\' && offset_ < input_.size())
                    {
                        const char escaped = input_[offset_];
                        if (escaped == '\\' || escaped == '"')
                        {
                            value.push_back(escaped);
                            ++offset_;
                            continue;
                        }
                    }
                    value.push_back(current);
                }
                return {KeyValuesTokenKind::invalid, {}};
            }

        private:
            auto skip_ignored() -> void
            {
                while (offset_ < input_.size())
                {
                    if (std::isspace(
                                static_cast<unsigned char>(input_[offset_])) != 0)
                    {
                        ++offset_;
                        continue;
                    }
                    if (input_[offset_] == '/' &&
                        offset_ + 1 < input_.size() &&
                        input_[offset_ + 1] == '/')
                    {
                        offset_ += 2;
                        while (offset_ < input_.size() &&
                               input_[offset_] != '\n')
                        {
                            ++offset_;
                        }
                        continue;
                    }
                    break;
                }
            }

            std::string_view input_{};
            size_t offset_{0};
        };

        auto ascii_lower(std::string_view value) -> std::string
        {
            std::string result{value};
            std::transform(
                    result.begin(),
                    result.end(),
                    result.begin(),
                    [](unsigned char character) {
                        return static_cast<char>(std::tolower(character));
                    });
            return result;
        }
    }

    auto steam_language_to_unreal_culture(std::string_view steam_language)
            -> std::optional<std::wstring>
    {
        const std::string normalized = normalized_steam_language(steam_language);
        for (const SteamLanguageCulture& entry : s_steam_language_cultures)
        {
            if (entry.steam_language == normalized)
            {
                return std::wstring{entry.unreal_culture};
            }
        }
        return std::nullopt;
    }

    auto steam_app_language_from_manifest(std::string_view manifest)
            -> std::optional<std::string>
    {
        KeyValuesTokenizer tokenizer{manifest};
        std::vector<std::string> sections{};
        std::optional<std::string> mounted_language{};

        for (;;)
        {
            KeyValuesToken key = tokenizer.next();
            if (key.kind == KeyValuesTokenKind::end)
            {
                break;
            }
            if (key.kind == KeyValuesTokenKind::close_brace)
            {
                if (sections.empty())
                {
                    return std::nullopt;
                }
                sections.pop_back();
                continue;
            }
            if (key.kind != KeyValuesTokenKind::string)
            {
                return std::nullopt;
            }

            KeyValuesToken value = tokenizer.next();
            if (value.kind == KeyValuesTokenKind::open_brace)
            {
                sections.push_back(ascii_lower(key.text));
                continue;
            }
            if (value.kind != KeyValuesTokenKind::string)
            {
                return std::nullopt;
            }

            if (sections.size() == 2 && sections[0] == "appstate" &&
                ascii_lower(key.text) == "language")
            {
                const std::string section = sections[1];
                if (section == "userconfig")
                {
                    return value.text.empty()
                            ? std::nullopt
                            : std::optional<std::string>{std::move(value.text)};
                }
                if (section == "mountedconfig" && !value.text.empty())
                {
                    mounted_language = std::move(value.text);
                }
            }
        }

        return mounted_language;
    }
}
