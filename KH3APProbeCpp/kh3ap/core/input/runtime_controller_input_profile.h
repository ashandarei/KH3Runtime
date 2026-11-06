#pragma once
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstring>
#include <stdexcept>

namespace kh3ap::controller_input
{
    using namespace RC::Unreal;

    template <class T> bool read(uintptr_t address, T& value) noexcept
    {
        __try
        {
            std::memcpy(&value, reinterpret_cast<void*>(address), sizeof(T));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    template <class T> T field(const void* object, size_t offset)
    {
        T value{};
        if (!object || !read(reinterpret_cast<uintptr_t>(object) + offset, value))
        {
            throw std::runtime_error("unreadable_ui_field");
        }
        return value;
    }

    inline bool signature(uintptr_t address, std::initializer_list<uint8_t> bytes)
    {
        for (auto expected : bytes)
        {
            uint8_t actual{};
            if (!read(address++, actual) || actual != expected)
            {
                return false;
            }
        }
        return true;
    }

    struct DeathUiProfile
    {
        bool steam{};
        uintptr_t manager_vtable{0x7c427f8}, pause_vtable{0x7c63c28}, game_over_vtable{0x7c633b8};
        uintptr_t retry_handler{0x1a7f140}, enabled_handler{0x189aa00}, action_table{0x66ea750};
        uintptr_t open_pause{0x19012f0}, pause_decide{0x1a81330};
        int manager_pause{0x288}, manager_instance{0x140};
    };

    inline bool supported(uintptr_t base, DeathUiProfile& profile)
    {
        IMAGE_DOS_HEADER dos{};
        IMAGE_NT_HEADERS64 nt{};
        profile = {};
        if (!read(base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew >= 0x1000 ||
            !read(base + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
            nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
        {
            return false;
        }
        if (kh3ap::native_build::steam.matches(nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage))
        {
            // Steam shipping build: live reflected owners/layouts and vtable
            // slot 78 mapped to the semantic GameOver Decide handler.
            profile.steam = true;
            profile.manager_vtable = 0x6a6cec8;
            profile.pause_vtable = 0x6b42c38;
            profile.game_over_vtable = 0x6b421a8;
            profile.retry_handler = 0x17080e0;
            profile.enabled_handler = 0x153eae0;
            profile.action_table = 0x6b37470;
            profile.manager_pause = 0x2c8;
            profile.manager_instance = 0x180;
            profile.open_pause = 0x15a2e10;
            profile.pause_decide = 0x1708b30;
            return signature(base + profile.retry_handler,
                             {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x83, 0x7a, 0x10, 0x07, 0x48, 0x8b, 0xda,
                              0x75, 0x10, 0x44, 0x8b, 0x42, 0x14, 0x8b, 0x52, 0x18, 0xe8, 0x75, 0xae, 0xff,
                              0xff, 0x83, 0x4b, 0x24, 0x02, 0xb0, 0x01, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3}) &&
                   signature(base + profile.enabled_handler, {0x40, 0x53, 0x48, 0x83, 0xec, 0x60, 0x48, 0xc7, 0x44,
                                                              0x24, 0x20, 0xfe, 0xff, 0xff, 0xff}) &&
                   signature(base + profile.open_pause, {0x48, 0x8b, 0xc4, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d,
                                                         0x68, 0xa1, 0x48, 0x81, 0xec, 0xa0, 0x00, 0x00, 0x00}) &&
                   signature(base + profile.open_pause + 0x35,
                             {0x44, 0x0f, 0xb6, 0xfa, 0x48, 0x8b, 0xd9, 0x33, 0xf6, 0x89, 0x75, 0xe7, 0x48, 0x39, 0xb1,
                              0xc8, 0x02, 0x00, 0x00}) &&
                   signature(base + profile.pause_decide, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10,
                                                           0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x40, 0x32,
                                                           0xf6, 0x8b, 0x4a, 0x10, 0x48, 0x8b, 0xfa, 0x83, 0xe9, 0x07});
        }
        return kh3ap::native_build::egs.matches(nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage) &&
               signature(base + 0x19012f0, {0x48, 0x8b, 0xc4, 0x55, 0x41, 0x56, 0x41, 0x57}) &&
               signature(base + 0x1a81330, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10}) &&
               signature(base + 0x189aa00, {0x40, 0x53, 0x48, 0x83, 0xec, 0x60});
    }

    inline void offset(UObject* object, const wchar_t* name, int expected)
    {
        auto* p = is_uobject_valid_fast(object) ? object->GetPropertyByNameInChain(name) : nullptr;
        if (!p || p->GetOffset_Internal() != expected)
        {
            throw std::runtime_error("ui_reflected_layout_mismatch");
        }
    }
} // namespace kh3ap::controller_input
