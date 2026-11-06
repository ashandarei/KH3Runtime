#include "kh3ap/diagnostics/kh3_autosave_probe.h"
#include "kh3ap/diagnostics/autosave/support.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include <UnrealDef.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    using namespace autosave_detail;
    namespace autosave_detail
    {
        Kh3AutosaveProbe* s_save_file_open_probe_owner = nullptr;
        Kh3AutosaveProbe* s_native_save_request_probe_owner = nullptr;
        Kh3AutosaveProbe* s_steam_save_startup_guard_owner = nullptr;
        Kh3AutosaveProbe* s_save_integrity_validation_probe_owner = nullptr;
        Kh3AutosaveProbe* s_save_payload_version_status_probe_owner = nullptr;
        Kh3AutosaveProbe* s_message_box_w_probe_owner = nullptr;
        auto read_process_bytes(uintptr_t address, void* destination, size_t size) -> bool
        {
            SIZE_T bytes_read = 0;
            return address != 0 && destination != nullptr && size != 0 &&
                   ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                                     destination, size, &bytes_read) &&
                   bytes_read == size;
        }

        auto read_fstring_text(uintptr_t address, int32_t max_characters)
            -> std::optional<std::wstring>
        {
            FStringHeader header{};
            if (!read_process_value(address, header) || header.data == 0 || header.num < 2 ||
                header.num > max_characters || header.max < header.num ||
                header.max > max_characters)
            {
                return std::nullopt;
            }

            std::vector<wchar_t> text(static_cast<size_t>(header.num));
            if (!read_process_bytes(header.data, text.data(), text.size() * sizeof(wchar_t)) ||
                text.back() != L'\0')
            {
                return std::nullopt;
            }

            return std::wstring{text.data(), text.size() - 1};
        }

        auto read_bounded_wstring(const wchar_t* value) -> std::wstring
        {
            if (!value)
            {
                return STR("<null>");
            }

            constexpr size_t max_characters = 128;
            std::wstring result{};
            result.reserve(max_characters);
            const uintptr_t base = reinterpret_cast<uintptr_t>(value);
            for (size_t index = 0; index < max_characters; ++index)
            {
                wchar_t character = L'\0';
                if (!read_process_value(base + index * sizeof(wchar_t), character))
                {
                    return STR("<unreadable>");
                }
                if (character == L'\0')
                {
                    return result;
                }
                result.push_back(character >= L' ' && character != 0x7f ? character : L'?');
            }
            result.append(STR("<truncated>"));
            return result;
        }

        auto current_process_creation_filetime_100ns() -> uint64_t
        {
            FILETIME created{};
            FILETIME exited{};
            FILETIME kernel{};
            FILETIME user{};
            if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
            {
                return 0;
            }
            ULARGE_INTEGER value{};
            value.LowPart = created.dwLowDateTime;
            value.HighPart = created.dwHighDateTime;
            return value.QuadPart;
        }

        auto try_get_main_module_image_range() -> std::optional<MainModuleImageRange>
        {
            auto* const module = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
            if (!module)
            {
                return std::nullopt;
            }

            auto* const dos_header = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
            if (dos_header->e_magic != IMAGE_DOS_SIGNATURE || dos_header->e_lfanew <= 0)
            {
                return std::nullopt;
            }

            auto* const nt_headers =
                reinterpret_cast<IMAGE_NT_HEADERS*>(module + dos_header->e_lfanew);
            if (nt_headers->Signature != IMAGE_NT_SIGNATURE ||
                nt_headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
                nt_headers->OptionalHeader.SizeOfImage == 0)
            {
                return std::nullopt;
            }

            return MainModuleImageRange{
                .base = reinterpret_cast<uintptr_t>(module),
                .size = static_cast<size_t>(nt_headers->OptionalHeader.SizeOfImage),
                .timestamp = nt_headers->FileHeader.TimeDateStamp,
            };
        }

        auto native_save_request_profile_for_image(const MainModuleImageRange& image)
            -> const NativeSaveRequestProfile*
        {
            for (const NativeSaveRequestProfile& profile : kh3_native_save_request_profiles)
            {
                if (profile.image_size == image.size && profile.image_timestamp == image.timestamp)
                {
                    return &profile;
                }
            }
            return nullptr;
        }

        auto find_main_module_import_slot(const char* imported_dll, const char* api_name) -> void**
        {
            auto* const module = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
            if (!module || !imported_dll || !api_name)
            {
                return nullptr;
            }

            auto* const dos_header = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
            if (dos_header->e_magic != IMAGE_DOS_SIGNATURE)
            {
                return nullptr;
            }

            auto* const nt_headers =
                reinterpret_cast<IMAGE_NT_HEADERS*>(module + dos_header->e_lfanew);
            if (nt_headers->Signature != IMAGE_NT_SIGNATURE)
            {
                return nullptr;
            }

            const IMAGE_DATA_DIRECTORY& import_directory =
                nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
            if (import_directory.VirtualAddress == 0)
            {
                return nullptr;
            }

            auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
                module + import_directory.VirtualAddress);
            for (; descriptor->Name != 0; ++descriptor)
            {
                const char* const dll_name =
                    reinterpret_cast<const char*>(module + descriptor->Name);
                if (_stricmp(dll_name, imported_dll) != 0 || descriptor->OriginalFirstThunk == 0)
                {
                    continue;
                }

                auto* original_thunk =
                    reinterpret_cast<IMAGE_THUNK_DATA*>(module + descriptor->OriginalFirstThunk);
                auto* import_thunk =
                    reinterpret_cast<IMAGE_THUNK_DATA*>(module + descriptor->FirstThunk);
                for (; original_thunk->u1.AddressOfData != 0; ++original_thunk, ++import_thunk)
                {
                    if (IMAGE_SNAP_BY_ORDINAL(original_thunk->u1.Ordinal))
                    {
                        continue;
                    }

                    auto* const import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                        module + original_thunk->u1.AddressOfData);
                    if (_stricmp(reinterpret_cast<const char*>(import->Name), api_name) == 0)
                    {
                        return reinterpret_cast<void**>(&import_thunk->u1.Function);
                    }
                }
            }
            return nullptr;
        }

        auto replace_import_slot_if_matches(void** slot, void* expected, void* replacement) -> bool
        {
            if (!slot || !expected || !replacement)
            {
                return false;
            }

            DWORD old_protection = 0;
            if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old_protection))
            {
                return false;
            }

            bool replaced = false;
            if (*slot == expected)
            {
                *slot = replacement;
                replaced = true;
            }

            DWORD ignored_protection = 0;
            VirtualProtect(slot, sizeof(*slot), old_protection, &ignored_protection);
            if (replaced)
            {
                FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
            }
            return replaced;
        }
    } // namespace autosave_detail

} // namespace kh3ap
