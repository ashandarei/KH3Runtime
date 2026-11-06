#include "kh02_file_adapter.h"
#include "kh3ap/features/kh02/core/kh02_profile.h"
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string_view>

extern "C" unsigned int kh02_async_request_size();

namespace kh3ap
{
    namespace
    {
        constexpr unsigned int async_request_bytes = 0x80;
        constexpr size_t executable_path_capacity = 32768;
    } // namespace

    void Kh02NativeFileAdapter::install(uint32_t pid, const Callbacks& callbacks)
    {
        std::lock_guard lock(enable_mutex);
        if (original)
        {
            if (bound_provider != pid)
            {
                throw std::runtime_error("remote_provider_already_bound");
            }
            return;
        }
        if (!pid || pid == GetCurrentProcessId())
        {
            throw std::runtime_error("remote_provider_pid");
        }
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, pid);
        if (!process)
        {
            throw std::runtime_error("remote_provider_not_running");
        }
        wchar_t path[executable_path_capacity]{};
        DWORD length = static_cast<DWORD>(std::size(path));
        const bool named = QueryFullProcessImageNameW(process, 0, path, &length) != 0;
        CloseHandle(process);
        if (!named || !std::wstring_view(path, length).ends_with(L"\\KINGDOM HEARTS 0.2 Birth by Sleep.exe"))
        {
            throw std::runtime_error("remote_provider_image");
        }
        auto* image = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
        using kh02_native::Site;
        if (!kh02_native::verify_sites({Site::pak_instance_getter, Site::pak_name}))
        {
            throw std::runtime_error("remote_unsupported_build");
        }
        auto* object = *reinterpret_cast<void**>(image + kh02_native::rva(Site::pak_instance));
        if (!object ||
            *static_cast<void***>(object) != reinterpret_cast<void**>(image + kh02_native::rva(Site::pak_vtable)))
        {
            throw std::runtime_error("remote_pak_instance_profile");
        }
        if (kh02_async_request_size() != async_request_bytes ||
            !kh02_native::verify_sites({Site::async_size_result, Site::async_completion}))
        {
            throw std::runtime_error("remote_async_request_abi");
        }
        callbacks.prepare_binding(pid);
        auto** table = *static_cast<void***>(object);
        std::copy_n(table, replacement.size(), replacement.begin());
        replacement[slot::exists] = reinterpret_cast<void*>(callbacks.exists);
        replacement[slot::size] = reinterpret_cast<void*>(callbacks.size);
        replacement[slot::timestamp] = reinterpret_cast<void*>(callbacks.timestamp);
        replacement[slot::stat] = reinterpret_cast<void*>(callbacks.stat);
        replacement[slot::open] = reinterpret_cast<void*>(callbacks.open);
        replacement[slot::open_no_buffering] = reinterpret_cast<void*>(callbacks.open);
        replacement[slot::open_async] = reinterpret_cast<void*>(callbacks.open_async);
        original = table;
        pak = object;
        if (InterlockedCompareExchangePointer(static_cast<void* volatile*>(object), replacement.data(), table) != table)
        {
            original = nullptr;
            pak = nullptr;
            callbacks.rollback_binding();
            throw std::runtime_error("remote_pak_identity_changed");
        }
        bound_provider = pid;
    }

    void Kh02NativeFileAdapter::restore_dispatch()
    {
        if (pak && original)
        {
            InterlockedCompareExchangePointer(static_cast<void* volatile*>(pak), original, replacement.data());
        }
    }

    auto Kh02NativeFileAdapter::forward_exists(void* self, const wchar_t* path) const -> bool
    {
        return reinterpret_cast<bool (*)(void*, const wchar_t*)>(original[slot::exists])(self, path);
    }

    auto Kh02NativeFileAdapter::forward_size(void* self, const wchar_t* path) const -> int64_t
    {
        return reinterpret_cast<int64_t (*)(void*, const wchar_t*)>(original[slot::size])(self, path);
    }

    auto Kh02NativeFileAdapter::forward_timestamp(void* self, int64_t* result, const wchar_t* path) const -> void*
    {
        return reinterpret_cast<void* (*)(void*, int64_t*, const wchar_t*)>(original[slot::timestamp])(self, result,
                                                                                                       path);
    }

    auto Kh02NativeFileAdapter::forward_stat(void* self, void* result, const wchar_t* path) const -> void*
    {
        return reinterpret_cast<void* (*)(void*, void*, const wchar_t*)>(original[slot::stat])(self, result, path);
    }

    auto Kh02NativeFileAdapter::forward_open(void* self, const wchar_t* path, bool allow_write) const -> void*
    {
        return reinterpret_cast<void* (*)(void*, const wchar_t*, bool)>(original[slot::open])(self, path, allow_write);
    }

    auto Kh02NativeFileAdapter::forward_async(void* self, const wchar_t* path) const -> void*
    {
        return reinterpret_cast<void* (*)(void*, const wchar_t*)>(original[slot::open_async])(self, path);
    }
} // namespace kh3ap
