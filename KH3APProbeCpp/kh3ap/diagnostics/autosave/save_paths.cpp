#include "kh3ap/diagnostics/kh3_autosave_probe.h"
#include "kh3ap/diagnostics/autosave/support.h"
#include "kh3ap/core/platform/steam_identity.h"
#include <cstdint>
#include <cwchar>
#include <optional>
#include <string>
#include <string_view>
#include <UnrealDef.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        constexpr auto max_observed_slot_number = 1000;
    }

    using namespace autosave_detail;
    namespace autosave_detail
    {
        auto path_is_vanilla_kh3_save_file(const wchar_t* path) -> bool
        {
            if (!path)
            {
                return false;
            }
            return std::wcsstr(path, L"KHIII_slot") != nullptr ||
                   std::wcsstr(path, L"KHIII_system") != nullptr;
        }

        auto redirected_kh3_steam_save_path(const wchar_t* path) -> std::optional<std::wstring>
        {
            if (!path)
            {
                return std::nullopt;
            }

            // These hooks see every file operation in the main executable.
            // Avoid allocating/normalizing for unrelated game assets.
            constexpr std::wstring_view game_name = L"kingdom hearts iii";
            const std::wstring_view value{path};
            bool possible_kh3_path = false;
            for (size_t at = 0; at + game_name.size() <= value.size(); ++at)
            {
                if (_wcsnicmp(value.data() + at, game_name.data(), game_name.size()) == 0)
                {
                    possible_kh3_path = true;
                    break;
                }
            }
            if (!possible_kh3_path)
            {
                return std::nullopt;
            }

            return kh3_steam_save_path_with_shared_account(value);
        }

        auto path_is_olympus_coliseum_progress_save_file(const wchar_t* path) -> bool
        {
            if (!path)
            {
                return false;
            }
            const wchar_t* filename = path;
            for (const wchar_t* cursor = path; *cursor != L'\0'; ++cursor)
            {
                if (*cursor == L'\\' || *cursor == L'/')
                {
                    filename = cursor + 1;
                }
            }

            constexpr std::wstring_view prefix = STR("ColiseumMod_Slot");
            if (_wcsnicmp(filename, prefix.data(), prefix.size()) != 0)
            {
                return false;
            }
            const wchar_t* cursor = filename + prefix.size();
            const wchar_t* const first_digit = cursor;
            while (*cursor >= L'0' && *cursor <= L'9')
            {
                ++cursor;
            }
            return cursor != first_digit && _wcsicmp(cursor, L".sav") == 0;
        }

        auto try_parse_manual_kh3_save_slot(std::wstring_view save_file_path,
                                            uint32_t desired_access) -> std::optional<int32_t>
        {
            constexpr uint32_t generic_write = 0x40000000u;
            if ((desired_access & generic_write) == 0)
            {
                return std::nullopt;
            }

            std::wstring normalized{save_file_path};
            for (wchar_t& character : normalized)
            {
                if (character >= L'A' && character <= L'Z')
                {
                    character = static_cast<wchar_t>(character - L'A' + L'a');
                }
            }

            constexpr std::wstring_view marker = STR("khiii_slot");
            const size_t marker_at = normalized.rfind(marker);
            if (marker_at == std::wstring::npos)
            {
                return std::nullopt;
            }

            size_t cursor = marker_at + marker.size();
            const size_t first_digit = cursor;
            int32_t slot_number = 0;
            while (cursor < normalized.size() && normalized[cursor] >= L'0' &&
                   normalized[cursor] <= L'9')
            {
                slot_number = (slot_number * 10) + static_cast<int32_t>(normalized[cursor] - L'0');
                if (slot_number > max_observed_slot_number)
                {
                    return std::nullopt;
                }
                ++cursor;
            }

            if (cursor == first_digit ||
                normalized.substr(cursor, std::wstring_view{L".bin"}.size()) != STR(".bin"))
            {
                return std::nullopt;
            }
            return slot_number;
        }
    } // namespace autosave_detail

} // namespace kh3ap
