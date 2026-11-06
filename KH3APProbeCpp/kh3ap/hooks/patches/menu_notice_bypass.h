#pragma once
#include "kh3ap/core/platform/game_build.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>

namespace kh3ap
{
    // EGS CL 1065258 and Steam timestamp 0x66743F34. Installed before menus run, with exact image/code guards.
    // Camp's four-notice selector returns false; its callers use their normal
    // no-notice path. CodeMenu state 5 still pops/frees each queued notice and
    // records it seen, then jumps past text/dialog construction to the tick tail.
    // The next tick drains the next notice; the empty queue resumes normally.
    class RuntimeMenuNoticeBypass
    {
        inline static constexpr std::array<uint8_t, 33> camp_bytes_{
            0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0x41, 0x70, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0x98, 0x40, 0x01, 0x00, 0x00, 0x48, 0x85, 0xDB, 0x0F, 0x84, 0xD4, 0x00, 0x00, 0x00};
        inline static constexpr std::array<uint8_t, 32> camp_tail_bytes_{
            0xC7, 0x07, 0x01, 0x00, 0x00, 0x00, 0xB0, 0x01, 0x48, 0x8B, 0x5C, 0x24, 0x48, 0x48, 0x83, 0xC4, 0x30, 0x5F, 0xC3, 0x48, 0x8B, 0x5C, 0x24, 0x48, 0x32, 0xC0, 0x48, 0x83, 0xC4, 0x30, 0x5F, 0xC3};
        inline static constexpr std::array<uint8_t, 63> notice_bytes_{
            0x48, 0x8B, 0x47, 0x70, 0x48, 0x85, 0xC0, 0x74, 0x09, 0x48, 0x8B, 0x80, 0x40, 0x01, 0x00, 0x00, 0xEB, 0x03, 0x48, 0x8B, 0xC6, 0x48, 0x85, 0xC0, 0x74, 0x15, 0x48, 0x8D, 0x88, 0xB8, 0x45, 0x01, 0x00, 0x85, 0xDB, 0x78, 0x0A, 0x41, 0xB0, 0x01, 0x8B, 0xD3, 0xE8, 0x7B, 0x56, 0x7B, 0xFF, 0x8B, 0xCE, 0x39, 0xB7, 0x98, 0x04, 0x00, 0x00, 0x7E, 0x2B, 0x48, 0x8B, 0x87, 0x90, 0x04, 0x00};
        inline static constexpr std::array<uint8_t, 14> end_bytes_{
            0x0F, 0xB6, 0x97, 0xF1, 0x04, 0x00, 0x00, 0x80, 0xFA, 0x40, 0x74, 0x4E, 0x48, 0x8B};

        inline static constexpr std::array<uint8_t, 5> camp_original_{0x48, 0x89, 0x5C, 0x24, 0x10};
        inline static constexpr std::array<uint8_t, 5> camp_patch_{0x32, 0xC0, 0xC3, 0x90, 0x90};
        inline static constexpr std::array<uint8_t, 8> notice_original_{0x8B, 0xCE, 0x39, 0xB7, 0x98, 0x04, 0x00, 0x00};
        inline static constexpr std::array<uint8_t, 8> notice_patch_{0xE9, 0xEC, 0x09, 0x00, 0x00, 0x90, 0x90, 0x90};
        inline static constexpr std::array<uint8_t, 51> steam_camp_bytes_{
            0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0xC7, 0x02, 0x04, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0x41, 0x70, 0x4C, 0x8B, 0xF1, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0xBE, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xA8, 0x80, 0x01, 0x00, 0x00, 0x48};
        inline static constexpr std::array<uint8_t, 31> steam_camp_tail_bytes_{
            0xC7, 0x03, 0x01, 0x00, 0x00, 0x00, 0xB0, 0x01, 0xEB, 0x02, 0x32, 0xC0, 0x48, 0x8B, 0x5C, 0x24, 0x48, 0x48, 0x8B, 0x6C, 0x24, 0x50, 0x48, 0x83, 0xC4, 0x20, 0x41, 0x5E, 0x5F, 0x5E, 0xC3};
        inline static constexpr std::array<uint8_t, 56> steam_notice_bytes_{
            0x48, 0x8B, 0x4B, 0x70, 0x48, 0x85, 0xC9, 0x74, 0x09, 0x48, 0x8B, 0x89, 0x80, 0x01, 0x00, 0x00, 0xEB, 0x03, 0x48, 0x8B, 0xCF, 0x48, 0x85, 0xC9, 0x74, 0x16, 0xE8, 0xD4, 0x86, 0x8B, 0xFF, 0x48, 0x8D, 0x88, 0x48, 0x3D, 0x01, 0x00, 0x41, 0xB0, 0x01, 0x8B, 0xD6, 0xE8, 0xE3, 0x58, 0x8A, 0xFF, 0x8B, 0xCF, 0x39, 0xBB, 0x98, 0x04, 0x00, 0x00};
        inline static constexpr std::array<uint8_t, 14> steam_end_bytes_{
            0x0F, 0xB6, 0x93, 0xF1, 0x04, 0x00, 0x00, 0x80, 0xFA, 0x40, 0x74, 0x51, 0x48, 0x8B};
        inline static constexpr std::array<uint8_t, 8> steam_notice_original_{
            0x8B, 0xCF, 0x39, 0xBB, 0x98, 0x04, 0x00, 0x00};
        inline static constexpr std::array<uint8_t, 8> steam_notice_patch_{0xE9, 0xA4, 0x08, 0x00, 0x00, 0x90, 0x90, 0x90};
        std::array<uint8_t, 8> active_notice_original_{notice_original_};
        std::array<uint8_t, 8> active_notice_patch_{notice_patch_};
        uintptr_t camp_rva_{0x19257A0};
        uintptr_t notice_rva_{0x195A875};
        uint8_t* base_{};
        bool camp_installed_{};
        bool notice_installed_{};

        template<size_t N>
        static bool matches(const uint8_t* address, const std::array<uint8_t, N>& bytes)
        {
            MEMORY_BASIC_INFORMATION info{};
            const auto start = reinterpret_cast<uintptr_t>(address);
            return VirtualQuery(address, &info, sizeof(info)) == sizeof(info) &&
                info.State == MEM_COMMIT && !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                start + N <= reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize &&
                std::memcmp(address, bytes.data(), N) == 0;
        }

        template<size_t N>
        static bool replace(uint8_t* address, const std::array<uint8_t, N>& expected,
                            const std::array<uint8_t, N>& replacement)
        {
            if (!matches(address, expected)) return false;
            DWORD protection{};
            if (!VirtualProtect(address, N, PAGE_EXECUTE_READWRITE, &protection)) return false;
            std::memcpy(address, replacement.data(), N);
            const bool flushed = FlushInstructionCache(GetCurrentProcess(), address, N) != FALSE;
            DWORD ignored{};
            const bool restored = VirtualProtect(address, N, protection, &ignored) != FALSE;
            if (flushed && restored && matches(address, replacement)) return true;
            DWORD rollback{};
            if (VirtualProtect(address, N, PAGE_EXECUTE_READWRITE, &rollback))
            {
                std::memcpy(address, expected.data(), N);
                FlushInstructionCache(GetCurrentProcess(), address, N);
                VirtualProtect(address, N, protection, &ignored);
            }
            return false;
        }

    public:
        void install()
        {
            if (base_) return;
            auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
            if (!base) return;
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
            const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            const bool steam = kh3ap::native_build::steam.matches(pe->FileHeader.TimeDateStamp, pe->OptionalHeader.SizeOfImage);
            const bool egs = kh3ap::native_build::egs.matches(pe->FileHeader.TimeDateStamp, pe->OptionalHeader.SizeOfImage);
            const bool signatures = steam
                ? matches(base + 0x15BFEA0, steam_camp_bytes_) &&
                  matches(base + 0x15BFF7F, steam_camp_tail_bytes_) &&
                  matches(base + 0x15EFF2D, steam_notice_bytes_) &&
                  matches(base + 0x15F0806, steam_end_bytes_) &&
                  matches(base + 0x15EFF5D, steam_notice_original_)
                : egs && matches(base + 0x19257A0, camp_bytes_) &&
                  matches(base + 0x1925882, camp_tail_bytes_) &&
                  matches(base + 0x195A846, notice_bytes_) &&
                  matches(base + 0x195B266, end_bytes_) &&
                  matches(base + 0x195A875, notice_original_);
            if (pe->Signature != IMAGE_NT_SIGNATURE ||
                pe->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || !signatures)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Menu notice bypass rejected: unsupported image or signature\n"));
                return;
            }
            camp_rva_ = steam ? 0x15BFEA0 : 0x19257A0;
            notice_rva_ = steam ? 0x15EFF5D : 0x195A875;
            active_notice_original_ = steam ? steam_notice_original_ : notice_original_;
            active_notice_patch_ = steam ? steam_notice_patch_ : notice_patch_;
            base_ = base;
            camp_installed_ = replace(base_ + camp_rva_, camp_original_, camp_patch_);
            if (camp_installed_)
                notice_installed_ = replace(base_ + notice_rva_, active_notice_original_, active_notice_patch_);
            if (!camp_installed_ || !notice_installed_)
            {
                uninstall();
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] Menu notice bypass failed: code write\n"));
                return;
            }
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] Menu notice bypass installed: camp_notices=4 premium_information=quiet_queue_drain storefront={}\n"), steam ? STR("steam") : STR("egs"));
        }

        void uninstall()
        {
            if (!base_) return;
            if (notice_installed_ && !replace(base_ + notice_rva_, active_notice_patch_, active_notice_original_))
                RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Premium notice bypass restore failed\n"));
            if (camp_installed_ && !replace(base_ + camp_rva_, camp_patch_, camp_original_))
                RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Camp notice bypass restore failed\n"));
            base_ = nullptr;
            camp_installed_ = notice_installed_ = false;
        }
    };
}
