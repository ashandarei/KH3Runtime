#pragma once

#include <limits>
#include <optional>
#include <string_view>

namespace kh3ap::ultima_weapon_shard
{
    inline constexpr std::wstring_view token_prefix = L"KH3AP_ULTIMA_WEAPON_SHARD::";
    inline constexpr int minimum_required_count = 2;
    inline constexpr int maximum_required_count = 13;

    struct Shard
    {
        int ordinal{};
        int required_count{};

        [[nodiscard]] constexpr bool awards_weapon() const noexcept
        {
            return ordinal == required_count;
        }
    };

    enum class ParseFailure
    {
        None,
        Prefix,
        Shape,
        Count,
    };

    struct ParseResult
    {
        Shard shard{};
        ParseFailure failure{ParseFailure::None};

        explicit constexpr operator bool() const noexcept
        {
            return failure == ParseFailure::None;
        }
    };

    namespace detail
    {
        constexpr std::optional<int> positive_decimal(std::wstring_view text, int maximum) noexcept
        {
            if (text.empty())
            {
                return std::nullopt;
            }
            int value = 0;
            for (const wchar_t character : text)
            {
                if (character < L'0' || character > L'9')
                {
                    return std::nullopt;
                }
                const int digit = character - L'0';
                if (value > maximum / 10 || (value == maximum / 10 && digit > maximum % 10))
                {
                    return std::nullopt;
                }
                value = value * 10 + digit;
            }
            return value > 0 ? std::optional{value} : std::nullopt;
        }
    } // namespace detail

    [[nodiscard]] constexpr ParseResult parse(std::wstring_view token) noexcept
    {
        if (!token.starts_with(token_prefix))
        {
            return {{}, ParseFailure::Prefix};
        }
        constexpr std::wstring_view separator = L"::";
        const auto suffix = token.substr(token_prefix.size());
        const auto split = suffix.find(separator);
        if (split == std::wstring_view::npos ||
            suffix.find(separator, split + separator.size()) != std::wstring_view::npos)
        {
            return {{}, ParseFailure::Shape};
        }
        const auto ordinal =
            detail::positive_decimal(suffix.substr(0, split), std::numeric_limits<int>::max());
        const auto required = detail::positive_decimal(suffix.substr(split + separator.size()),
                                                       maximum_required_count);
        if (!ordinal || !required || *required < minimum_required_count)
        {
            return {{}, ParseFailure::Count};
        }
        // Excess receipts still grant a material; only the exact threshold
        // receipt awards the weapon. Never clamp the ordinal to the threshold.
        return {{*ordinal, *required}, ParseFailure::None};
    }

    [[nodiscard]] constexpr std::wstring_view failure_reason(ParseFailure failure) noexcept
    {
        switch (failure)
        {
        case ParseFailure::Prefix:
            return L"ultima_weapon_shard_token_prefix_invalid";
        case ParseFailure::Shape:
            return L"ultima_weapon_shard_token_shape_invalid";
        case ParseFailure::Count:
            return L"ultima_weapon_shard_token_count_invalid";
        case ParseFailure::None:
            return {};
        }
        return {};
    }
} // namespace kh3ap::ultima_weapon_shard
