#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kh3ap
{
    // Public-universe, individual-account, desktop-instance SteamID64 base.
    // The user's 32-bit account id is added at runtime; this is not an
    // account-specific Steam id.
    constexpr uint64_t steam_individual_account_id64_base = 76561197960265728ULL;
    constexpr std::wstring_view kh3_steam_shared_save_account_id =
            L"00000000000000000000000000000000";

    auto is_standard_steam_id64(std::wstring_view value) noexcept -> bool;
    auto is_kh3_steam_shared_save_account_id(std::wstring_view value) noexcept
            -> bool;
    auto is_kh3_steam_save_account_component(std::wstring_view value) noexcept
            -> bool;
    auto kh3_steam_save_root_with_account(
            std::wstring_view current_save_root,
            std::wstring_view target_account) -> std::optional<std::wstring>;
    auto kh3_steam_save_path_with_account(
            std::wstring_view current_save_path,
            std::wstring_view target_account) -> std::optional<std::wstring>;
    auto kh3_steam_save_root_with_shared_account(
            std::wstring_view current_save_root) -> std::optional<std::wstring>;
    auto kh3_steam_save_path_with_shared_account(
            std::wstring_view current_save_path) -> std::optional<std::wstring>;
}
