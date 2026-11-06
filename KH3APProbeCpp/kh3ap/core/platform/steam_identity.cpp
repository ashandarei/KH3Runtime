#include "kh3ap/core/platform/steam_identity.h"

#include <limits>

namespace kh3ap
{
    namespace
    {
        constexpr size_t steam_id64_decimal_characters = 17;

        constexpr auto max_steam_identity_characters = 32;
    }

    auto is_kh3_steam_shared_save_account_id(std::wstring_view value) noexcept
            -> bool
    {
        return value == kh3_steam_shared_save_account_id;
    }

    auto is_standard_steam_id64(std::wstring_view value) noexcept -> bool
    {
        if (value.size() != steam_id64_decimal_characters)
        {
            return false;
        }

        uint64_t parsed = 0;
        for (const wchar_t character : value)
        {
            if (character < L'0' || character > L'9')
            {
                return false;
            }
            parsed = parsed * 10 + static_cast<uint64_t>(character - L'0');
        }

        return parsed > steam_individual_account_id64_base &&
                parsed <= steam_individual_account_id64_base +
                                  std::numeric_limits<uint32_t>::max();
    }

    auto is_kh3_steam_save_account_component(std::wstring_view value) noexcept
            -> bool
    {
        if (value.empty() || value.size() > max_steam_identity_characters)
        {
            return false;
        }
        for (const wchar_t character : value)
        {
            if (character < L'0' || character > L'9')
            {
                return false;
            }
        }
        return true;
    }

    auto kh3_steam_save_root_with_account(
            std::wstring_view current_save_root,
            std::wstring_view target_account) -> std::optional<std::wstring>
    {
        if (!is_kh3_steam_save_account_component(target_account))
        {
            return std::nullopt;
        }

        std::wstring normalized{current_save_root};
        for (wchar_t& character : normalized)
        {
            if (character == L'\\')
            {
                character = L'/';
            }
            else if (character >= L'A' && character <= L'Z')
            {
                character = static_cast<wchar_t>(character - L'A' + L'a');
            }
        }

        constexpr std::wstring_view profile_marker =
                L"/kingdom hearts iii/steam/";
        const size_t marker_at = normalized.rfind(profile_marker);
        if (marker_at == std::wstring::npos)
        {
            return std::nullopt;
        }

        const size_t account_at = marker_at + profile_marker.size();
        const size_t account_end = normalized.find(L'/', account_at);
        if (account_end == std::wstring::npos)
        {
            return std::nullopt;
        }

        size_t suffix_end = normalized.size();
        while (suffix_end > account_end + 1 &&
               normalized[suffix_end - 1] == L'/')
        {
            --suffix_end;
        }
        if (normalized.substr(account_end + 1, suffix_end - account_end - 1) !=
            L"savegames")
        {
            return std::nullopt;
        }

        const std::wstring_view account = current_save_root.substr(
                account_at,
                account_end - account_at);
        if (!is_kh3_steam_save_account_component(account))
        {
            return std::nullopt;
        }

        std::wstring redirected{current_save_root};
        redirected.replace(
                account_at,
                account_end - account_at,
                target_account);
        return redirected;
    }

    auto kh3_steam_save_path_with_account(
            std::wstring_view current_save_path,
            std::wstring_view target_account) -> std::optional<std::wstring>
    {
        if (!is_kh3_steam_save_account_component(target_account))
        {
            return std::nullopt;
        }

        std::wstring normalized{current_save_path};
        for (wchar_t& character : normalized)
        {
            if (character == L'\\')
            {
                character = L'/';
            }
            else if (character >= L'A' && character <= L'Z')
            {
                character = static_cast<wchar_t>(character - L'A' + L'a');
            }
        }

        constexpr std::wstring_view profile_marker =
                L"/kingdom hearts iii/steam/";
        const size_t marker_at = normalized.rfind(profile_marker);
        if (marker_at == std::wstring::npos)
        {
            return std::nullopt;
        }

        const size_t account_at = marker_at + profile_marker.size();
        const size_t account_end = normalized.find(L'/', account_at);
        if (account_end == std::wstring::npos)
        {
            return std::nullopt;
        }

        constexpr std::wstring_view savegames_segment = L"/savegames";
        const size_t savegames_at = account_end;
        if (normalized.substr(savegames_at, savegames_segment.size()) !=
            savegames_segment)
        {
            return std::nullopt;
        }
        const size_t savegames_end = savegames_at + savegames_segment.size();
        if (savegames_end < normalized.size() &&
            normalized[savegames_end] != L'/')
        {
            return std::nullopt;
        }

        const std::wstring_view account = current_save_path.substr(
                account_at,
                account_end - account_at);
        if (!is_kh3_steam_save_account_component(account))
        {
            return std::nullopt;
        }

        std::wstring redirected{current_save_path};
        redirected.replace(
                account_at,
                account_end - account_at,
                target_account);
        return redirected;
    }

    auto kh3_steam_save_root_with_shared_account(
            std::wstring_view current_save_root) -> std::optional<std::wstring>
    {
        return kh3_steam_save_root_with_account(
                current_save_root,
                kh3_steam_shared_save_account_id);
    }

    auto kh3_steam_save_path_with_shared_account(
            std::wstring_view current_save_path) -> std::optional<std::wstring>
    {
        return kh3_steam_save_path_with_account(
                current_save_path,
                kh3_steam_shared_save_account_id);
    }
}
