#pragma once
#include "kh3ap/diagnostics/kh3_autosave_probe.h"
#include "kh3ap/diagnostics/autosave/profiles.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace kh3ap::autosave_detail
{
    // Native entrypoints share the original process-lifetime owner slots.
    // Kh3AutosaveProbe retains every detour and trampoline; these slots borrow it.
    extern Kh3AutosaveProbe* s_save_file_open_probe_owner;
    extern Kh3AutosaveProbe* s_native_save_request_probe_owner;
    extern Kh3AutosaveProbe* s_steam_save_startup_guard_owner;
    extern Kh3AutosaveProbe* s_save_integrity_validation_probe_owner;
    extern Kh3AutosaveProbe* s_save_payload_version_status_probe_owner;
    extern Kh3AutosaveProbe* s_message_box_w_probe_owner;

    using SaveIntegrityKeyRefreshFn = bool(__fastcall*)(void*, void*);
    using SaveIntegrityValidationFn = bool(__fastcall*)(void*, const wchar_t*, const wchar_t*,
                                                        void*);
    using SavePayloadVersionStatusFn = int(__fastcall*)(void*, int*);
    using MessageBoxWFn = int(WINAPI*)(HWND, LPCWSTR, LPCWSTR, UINT);
    using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD,
                                          DWORD, HANDLE);
    using CreateDirectoryWFn = BOOL(WINAPI*)(LPCWSTR, LPSECURITY_ATTRIBUTES);
    using DeleteFileWFn = BOOL(WINAPI*)(LPCWSTR);
    using FindFirstFileWFn = HANDLE(WINAPI*)(LPCWSTR, LPWIN32_FIND_DATAW);
    using GetFileAttributesWFn = DWORD(WINAPI*)(LPCWSTR);
    using GetFileAttributesExWFn = BOOL(WINAPI*)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
    using MoveFileWFn = BOOL(WINAPI*)(LPCWSTR, LPCWSTR);
    using RemoveDirectoryWFn = BOOL(WINAPI*)(LPCWSTR);
    using SetFileAttributesWFn = BOOL(WINAPI*)(LPCWSTR, DWORD);
    using FStringCopyAssignFn = void*(__fastcall*)(void*, const void*);
    using CloseHandleFn = BOOL(WINAPI*)(HANDLE);
    using NativeSaveRequestFn = void(__fastcall*)(void*, void*, void*, void*);

    struct FStringHeader
    {
        uintptr_t data{0};
        int32_t num{0};
        int32_t max{0};
    };

    struct MainModuleImageRange
    {
        uintptr_t base{0};
        size_t size{0};
        uint32_t timestamp{0};
    };

    template <typename T> auto read_process_value(uintptr_t address, T& value) -> bool
    {
        SIZE_T bytes_read = 0;
        return address != 0 &&
               ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                                 &value, sizeof(value), &bytes_read) &&
               bytes_read == sizeof(value);
    }

    auto read_process_bytes(uintptr_t address, void* destination, size_t size) -> bool;

    auto read_fstring_text(uintptr_t address, int32_t max_characters)
        -> std::optional<std::wstring>;

    auto read_bounded_wstring(const wchar_t* value) -> std::wstring;

    auto current_process_creation_filetime_100ns() -> uint64_t;

    auto try_get_main_module_image_range() -> std::optional<MainModuleImageRange>;

    auto native_save_request_profile_for_image(const MainModuleImageRange& image)
        -> const NativeSaveRequestProfile*;

    auto find_main_module_import_slot(const char* imported_dll, const char* api_name) -> void**;

    auto replace_import_slot_if_matches(void** slot, void* expected, void* replacement) -> bool;

    auto path_is_vanilla_kh3_save_file(const wchar_t* path) -> bool;

    auto redirected_kh3_steam_save_path(const wchar_t* path) -> std::optional<std::wstring>;

    auto path_is_olympus_coliseum_progress_save_file(const wchar_t* path) -> bool;

    auto try_parse_manual_kh3_save_slot(std::wstring_view save_file_path, uint32_t desired_access)
        -> std::optional<int32_t>;

    auto phase_name(HookPhase phase) -> std::wstring_view;

    auto json_field(std::ostringstream& stream, std::string_view name, std::wstring_view value)
        -> void;

    auto json_bool_field(std::ostringstream& stream, std::string_view name, bool value) -> void;
} // namespace kh3ap::autosave_detail
