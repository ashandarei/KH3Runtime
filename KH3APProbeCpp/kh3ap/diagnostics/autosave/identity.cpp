#include "kh3ap/diagnostics/kh3_autosave_probe.h"
#include "kh3ap/diagnostics/autosave/support.h"
#include "kh3ap/core/platform/steam_identity.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <UnrealDef.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        constexpr auto minimum_identity_buffer_chars = 2; // One character and a terminator.
        constexpr auto identity_owner_ready_offset = 0x70;
        constexpr auto identity_source_key_offset = 0x88;
        constexpr auto identity_refreshed_key_offset = 0x98;
        constexpr auto identity_save_root_offset = 0xa8;
        constexpr auto max_steam_identity_chars = 64;
        constexpr auto initial_save_root_redirect_logs = 8;
        constexpr auto save_root_redirect_log_interval = 120;

        constexpr auto max_save_identity_buffer_chars = 1024;
    } // namespace

    using namespace autosave_detail;
    auto Kh3AutosaveProbe::redirect_save_root_to_account(uintptr_t fstring_address) -> bool
    {
        if (fstring_address == 0 || steam_save_root_fstring_copy_assign_address_ == 0)
        {
            return false;
        }

        FStringHeader current_header{};
        if (!read_process_value(fstring_address, current_header) || current_header.data == 0 ||
            current_header.num < minimum_identity_buffer_chars ||
            current_header.num > max_save_identity_buffer_chars ||
            current_header.max < current_header.num ||
            current_header.max > max_save_identity_buffer_chars)
        {
            return false;
        }

        std::vector<wchar_t> current_text(static_cast<size_t>(current_header.num));
        if (!read_process_bytes(current_header.data, current_text.data(),
                                current_text.size() * sizeof(wchar_t)) ||
            current_text.back() != L'\0')
        {
            return false;
        }

        const std::wstring current_root{current_text.data(), current_text.size() - 1};
        const std::optional<std::wstring> redirected_root =
            kh3_steam_save_root_with_shared_account(current_root);
        if (!redirected_root.has_value())
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] SteamSaveRootRedirect root rejected source_root={} "
                    "target_account={} reason=unrecognized_root_or_invalid_target\n"),
                current_root, kh3_steam_shared_save_account_id);
            return false;
        }
        if (*redirected_root == current_root)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] SteamSaveRootRedirect root verified source_root={} "
                    "target_root={} target_account={} changed=false verification=true\n"),
                current_root, *redirected_root, kh3_steam_shared_save_account_id);
            return true;
        }

        const auto copy_assign =
            reinterpret_cast<FStringCopyAssignFn>(steam_save_root_fstring_copy_assign_address_);
        const auto assign_text = [&](const std::wstring& value)
        {
            const int32_t num = static_cast<int32_t>(value.size() + 1);
            const FStringHeader source{reinterpret_cast<uintptr_t>(value.c_str()), num, num};
            return copy_assign(reinterpret_cast<void*>(fstring_address), &source) ==
                   reinterpret_cast<void*>(fstring_address);
        };
        const auto matches_text = [&](const std::wstring& expected)
        {
            FStringHeader verified_header{};
            const int32_t expected_num = static_cast<int32_t>(expected.size() + 1);
            if (!read_process_value(fstring_address, verified_header) ||
                verified_header.data == 0 || verified_header.num != expected_num ||
                verified_header.max < verified_header.num ||
                verified_header.max > max_save_identity_buffer_chars)
            {
                return false;
            }
            std::vector<wchar_t> verified_text(static_cast<size_t>(verified_header.num));
            return read_process_bytes(verified_header.data, verified_text.data(),
                                      verified_text.size() * sizeof(wchar_t)) &&
                   verified_text.back() == L'\0' &&
                   std::wstring_view{verified_text.data(), verified_text.size() - 1} == expected;
        };

        const bool redirected = assign_text(*redirected_root) && matches_text(*redirected_root);
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] SteamSaveRootRedirect root commit source_root={} target_root={} "
                "target_account={} changed=true verification={}\n"),
            current_root, *redirected_root, kh3_steam_shared_save_account_id, redirected);
        if (!redirected)
        {
            assign_text(current_root);
        }
        return redirected;
    }

    auto Kh3AutosaveProbe::redirect_steam_save_root_after_identity_refresh(void* owner_context,
                                                                           void* output_key) -> bool
    {
        if (!owner_context || !output_key)
        {
            return false;
        }

        std::scoped_lock redirect_lock{steam_save_root_redirect_mutex_};

        HMODULE const main_module = GetModuleHandleW(nullptr);
        const uintptr_t main_base = reinterpret_cast<uintptr_t>(main_module);
        const uintptr_t expected_owner =
            main_base != 0 ? main_base + kh3_save_validation_owner_singleton_rva : 0;
        const uintptr_t owner = reinterpret_cast<uintptr_t>(owner_context);
        if (owner == 0 || owner != expected_owner)
        {
            return false;
        }

        uint8_t owner_ready = 0;
        const uintptr_t output_key_address = reinterpret_cast<uintptr_t>(output_key);
        const std::optional<std::wstring> source_key =
            read_fstring_text(owner + identity_source_key_offset, max_steam_identity_chars);
        const std::optional<std::wstring> refreshed_key =
            read_fstring_text(owner + identity_refreshed_key_offset, max_steam_identity_chars);
        const std::optional<std::wstring> output_key_text =
            output_key_address == owner + identity_refreshed_key_offset
                ? refreshed_key
                : read_fstring_text(output_key_address, max_steam_identity_chars);
        const bool source_valid = source_key.has_value() && is_standard_steam_id64(*source_key);
        const bool refreshed_valid =
            refreshed_key.has_value() && is_standard_steam_id64(*refreshed_key);
        const bool output_valid =
            output_key_text.has_value() && is_standard_steam_id64(*output_key_text);
        const bool identity_consistent = source_valid && refreshed_valid && output_valid &&
                                         *source_key == *refreshed_key &&
                                         *source_key == *output_key_text;
        if (!read_process_value(owner + identity_owner_ready_offset, owner_ready) ||
            owner_ready != 1 || !identity_consistent)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] SteamSaveRootRedirect skipped owner_ready={} source_valid={} "
                    "refreshed_valid={} output_valid={} identity_consistent={} "
                    "output_is_refreshed={} identity_mutation=false\n"),
                owner_ready == 1, source_valid, refreshed_valid, output_valid, identity_consistent,
                output_key_address == owner + identity_refreshed_key_offset);
            return false;
        }

        // Vanilla Steam is the sole identity authority. The source, refreshed,
        // and output FStrings above are read-only prerequisites. Only the
        // separate resolved SaveGames root at +0xA8 may be redirected.
        const bool save_root_redirected =
            redirect_save_root_to_account(owner + identity_save_root_offset);
        if (!save_root_redirected)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] SteamSaveRootRedirect incomplete save_root_redirected={} "
                    "output_is_refreshed={} fstring_copy_assign_available={} "
                    "identity_authority=vanilla_steam identity_mutation=false\n"),
                save_root_redirected, output_key_address == owner + identity_refreshed_key_offset,
                steam_save_root_fstring_copy_assign_address_ != 0);
            return false;
        }

        ++steam_save_root_redirect_count_;
        if (steam_save_root_redirect_count_ <= initial_save_root_redirect_logs ||
            steam_save_root_redirect_count_ % save_root_redirect_log_interval == 0)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] SteamSaveRootRedirect applied after vanilla identity refresh "
                    "count={} identity_consistent=true identity_authority=vanilla_steam "
                    "identity_mutation=false save_root_account={}\n"),
                steam_save_root_redirect_count_, kh3_steam_shared_save_account_id);
        }
        return true;
    }
} // namespace kh3ap
