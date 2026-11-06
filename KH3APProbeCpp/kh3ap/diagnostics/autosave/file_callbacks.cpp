#include "kh3ap/diagnostics/kh3_autosave_probe.h"
#include "kh3ap/diagnostics/autosave/support.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/platform/steam_identity.h"
#include <intrin.h>
#include <cstdint>
#include <optional>
#include <string>
#include <UnrealDef.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    using namespace autosave_detail;
    void* __stdcall Kh3AutosaveProbe::hooked_create_file_w(
        const wchar_t* file_name, uint32_t desired_access, uint32_t share_mode,
        void* security_attributes, uint32_t creation_disposition, uint32_t flags_and_attributes,
        void* template_file)
    {
        Kh3AutosaveProbe* const owner = s_save_file_open_probe_owner;
        const std::optional<std::wstring> redirected_file_name =
            owner && owner->steam_shared_save_path_redirect_enabled_ && file_name
                ? redirected_kh3_steam_save_path(file_name)
                : std::nullopt;
        const wchar_t* const effective_file_name =
            redirected_file_name.has_value() ? redirected_file_name->c_str() : file_name;
        const bool is_vanilla_save_file =
            owner && path_is_vanilla_kh3_save_file(effective_file_name);
        const bool is_olympus_coliseum_progress_save =
            owner && path_is_olympus_coliseum_progress_save_file(effective_file_name);
        SaveFileCallstackSnapshot callstack{};
        if (is_vanilla_save_file)
        {
            callstack = capture_save_file_callstack(reinterpret_cast<uintptr_t>(_ReturnAddress()));
        }

        HANDLE result = INVALID_HANDLE_VALUE;
        if (owner && owner->create_file_w_trampoline_ != 0)
        {
            result = reinterpret_cast<CreateFileWFn>(owner->create_file_w_trampoline_)(
                effective_file_name, desired_access, share_mode,
                reinterpret_cast<LPSECURITY_ATTRIBUTES>(security_attributes), creation_disposition,
                flags_and_attributes, reinterpret_cast<HANDLE>(template_file));
        }
        else
        {
            result = CreateFileW(effective_file_name, desired_access, share_mode,
                                 reinterpret_cast<LPSECURITY_ATTRIBUTES>(security_attributes),
                                 creation_disposition, flags_and_attributes,
                                 reinterpret_cast<HANDLE>(template_file));
        }

        const DWORD original_error = GetLastError();
        uint64_t open_event_index = 0;
        if (is_vanilla_save_file && result != INVALID_HANDLE_VALUE)
        {
            const uint64_t observed_filetime_100ns = current_windows_filetime_100ns();
            open_event_index = owner->record_save_file_open(
                effective_file_name, observed_filetime_100ns, callstack, desired_access, share_mode,
                creation_disposition, flags_and_attributes);
        }
        else if (is_vanilla_save_file)
        {
            // A failed open is a terminal event for this save attempt too. Do
            // not leave its request snapshot available to an unrelated write.
            reconciliation::file_open(effective_file_name, desired_access, 0,
                                      current_windows_filetime_100ns(), false);
        }
        if ((is_vanilla_save_file || is_olympus_coliseum_progress_save) &&
            owner->config_.manual_save_close_tracking_enabled)
        {
            owner->track_manual_save_handle(effective_file_name, desired_access, result,
                                            open_event_index);
        }
        SetLastError(original_error);
        return result;
    }

    auto Kh3AutosaveProbe::hooked_write_file(void* handle, const void* buffer, uint32_t size,
                                             uint32_t* written, void* overlapped) -> int
    {
        auto* owner = s_save_file_open_probe_owner;
        using WriteFn = BOOL(WINAPI*)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
        const auto fn = owner && owner->write_file_trampoline_
                            ? reinterpret_cast<WriteFn>(owner->write_file_trampoline_)
                            : &WriteFile;
        const BOOL result =
            fn(reinterpret_cast<HANDLE>(handle), buffer, size, reinterpret_cast<LPDWORD>(written),
               reinterpret_cast<LPOVERLAPPED>(overlapped));
        const DWORD error = GetLastError();
        if (owner)
        {
            const bool complete = result && written && *written == size;
            owner->record_save_write(handle, complete, complete ? *written : 0);
        }
        SetLastError(error);
        return result;
    }

    int __stdcall Kh3AutosaveProbe::hooked_create_directory_w(const wchar_t* path,
                                                              void* security_attributes)
    {
        Kh3AutosaveProbe* const owner = s_save_file_open_probe_owner;
        const std::optional<std::wstring> redirected =
            owner && owner->steam_shared_save_path_redirect_enabled_
                ? redirected_kh3_steam_save_path(path)
                : std::nullopt;
        const wchar_t* const effective_path = redirected.has_value() ? redirected->c_str() : path;
        if (owner && owner->create_directory_w_trampoline_ != 0)
        {
            return reinterpret_cast<CreateDirectoryWFn>(owner->create_directory_w_trampoline_)(
                effective_path, reinterpret_cast<LPSECURITY_ATTRIBUTES>(security_attributes));
        }
        return CreateDirectoryW(effective_path,
                                reinterpret_cast<LPSECURITY_ATTRIBUTES>(security_attributes));
    }

    int __stdcall Kh3AutosaveProbe::hooked_delete_file_w(const wchar_t* path)
    {
        Kh3AutosaveProbe* const owner = s_save_file_open_probe_owner;
        const std::optional<std::wstring> redirected =
            owner && owner->steam_shared_save_path_redirect_enabled_
                ? redirected_kh3_steam_save_path(path)
                : std::nullopt;
        const wchar_t* const effective_path = redirected.has_value() ? redirected->c_str() : path;
        if (owner && owner->delete_file_w_trampoline_ != 0)
        {
            return reinterpret_cast<DeleteFileWFn>(owner->delete_file_w_trampoline_)(
                effective_path);
        }
        return DeleteFileW(effective_path);
    }

    void* __stdcall Kh3AutosaveProbe::hooked_find_first_file_w(const wchar_t* path, void* find_data)
    {
        Kh3AutosaveProbe* const owner = s_save_file_open_probe_owner;
        const std::optional<std::wstring> redirected =
            owner && owner->steam_shared_save_path_redirect_enabled_
                ? redirected_kh3_steam_save_path(path)
                : std::nullopt;
        const wchar_t* const effective_path = redirected.has_value() ? redirected->c_str() : path;
        if (owner && owner->find_first_file_w_trampoline_ != 0)
        {
            return reinterpret_cast<FindFirstFileWFn>(owner->find_first_file_w_trampoline_)(
                effective_path, reinterpret_cast<LPWIN32_FIND_DATAW>(find_data));
        }
        return FindFirstFileW(effective_path, reinterpret_cast<LPWIN32_FIND_DATAW>(find_data));
    }

    uint32_t __stdcall Kh3AutosaveProbe::hooked_get_file_attributes_w(const wchar_t* path)
    {
        Kh3AutosaveProbe* const owner = s_save_file_open_probe_owner;
        const std::optional<std::wstring> redirected =
            owner && owner->steam_shared_save_path_redirect_enabled_
                ? redirected_kh3_steam_save_path(path)
                : std::nullopt;
        const wchar_t* const effective_path = redirected.has_value() ? redirected->c_str() : path;
        if (owner && owner->get_file_attributes_w_trampoline_ != 0)
        {
            return reinterpret_cast<GetFileAttributesWFn>(owner->get_file_attributes_w_trampoline_)(
                effective_path);
        }
        return GetFileAttributesW(effective_path);
    }

    int __stdcall Kh3AutosaveProbe::hooked_get_file_attributes_ex_w(const wchar_t* path,
                                                                    int info_level_id,
                                                                    void* file_information)
    {
        Kh3AutosaveProbe* const owner = s_save_file_open_probe_owner;
        const std::optional<std::wstring> redirected =
            owner && owner->steam_shared_save_path_redirect_enabled_
                ? redirected_kh3_steam_save_path(path)
                : std::nullopt;
        const wchar_t* const effective_path = redirected.has_value() ? redirected->c_str() : path;
        const auto info_level = static_cast<GET_FILEEX_INFO_LEVELS>(info_level_id);
        if (owner && owner->get_file_attributes_ex_w_trampoline_ != 0)
        {
            return reinterpret_cast<GetFileAttributesExWFn>(
                owner->get_file_attributes_ex_w_trampoline_)(effective_path, info_level,
                                                             file_information);
        }
        return GetFileAttributesExW(effective_path, info_level, file_information);
    }

    int __stdcall Kh3AutosaveProbe::hooked_move_file_w(const wchar_t* existing_path,
                                                       const wchar_t* new_path)
    {
        Kh3AutosaveProbe* const owner = s_save_file_open_probe_owner;
        const bool redirect_enabled = owner && owner->steam_shared_save_path_redirect_enabled_;
        const std::optional<std::wstring> redirected_existing =
            redirect_enabled ? redirected_kh3_steam_save_path(existing_path) : std::nullopt;
        const std::optional<std::wstring> redirected_new =
            redirect_enabled ? redirected_kh3_steam_save_path(new_path) : std::nullopt;
        const wchar_t* const effective_existing =
            redirected_existing.has_value() ? redirected_existing->c_str() : existing_path;
        const wchar_t* const effective_new =
            redirected_new.has_value() ? redirected_new->c_str() : new_path;
        if (owner && owner->move_file_w_trampoline_ != 0)
        {
            return reinterpret_cast<MoveFileWFn>(owner->move_file_w_trampoline_)(effective_existing,
                                                                                 effective_new);
        }
        return MoveFileW(effective_existing, effective_new);
    }

    int __stdcall Kh3AutosaveProbe::hooked_remove_directory_w(const wchar_t* path)
    {
        Kh3AutosaveProbe* const owner = s_save_file_open_probe_owner;
        const std::optional<std::wstring> redirected =
            owner && owner->steam_shared_save_path_redirect_enabled_
                ? redirected_kh3_steam_save_path(path)
                : std::nullopt;
        const wchar_t* const effective_path = redirected.has_value() ? redirected->c_str() : path;
        if (owner && owner->remove_directory_w_trampoline_ != 0)
        {
            return reinterpret_cast<RemoveDirectoryWFn>(owner->remove_directory_w_trampoline_)(
                effective_path);
        }
        return RemoveDirectoryW(effective_path);
    }

    int __stdcall Kh3AutosaveProbe::hooked_set_file_attributes_w(const wchar_t* path,
                                                                 uint32_t file_attributes)
    {
        Kh3AutosaveProbe* const owner = s_save_file_open_probe_owner;
        const std::optional<std::wstring> redirected =
            owner && owner->steam_shared_save_path_redirect_enabled_
                ? redirected_kh3_steam_save_path(path)
                : std::nullopt;
        const wchar_t* const effective_path = redirected.has_value() ? redirected->c_str() : path;
        if (owner && owner->set_file_attributes_w_trampoline_ != 0)
        {
            return reinterpret_cast<SetFileAttributesWFn>(owner->set_file_attributes_w_trampoline_)(
                effective_path, file_attributes);
        }
        return SetFileAttributesW(effective_path, file_attributes);
    }

    int __stdcall Kh3AutosaveProbe::hooked_close_handle(void* handle)
    {
        Kh3AutosaveProbe* const owner = s_save_file_open_probe_owner;
        BOOL result = FALSE;
        if (owner && owner->close_handle_trampoline_ != 0)
        {
            result = reinterpret_cast<CloseHandleFn>(owner->close_handle_trampoline_)(
                reinterpret_cast<HANDLE>(handle));
        }
        else
        {
            result = CloseHandle(reinterpret_cast<HANDLE>(handle));
        }
        const DWORD original_error = GetLastError();
        if (result && owner)
        {
            owner->record_manual_save_handle_closed(handle);
        }
        SetLastError(original_error);
        return result;
    }
} // namespace kh3ap
