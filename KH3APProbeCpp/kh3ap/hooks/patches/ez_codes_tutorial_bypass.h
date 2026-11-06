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
    // EGS CL 1065258 and Steam timestamp 0x66743F34: UTresUIP_CodeMenu's native tutorial queue, states 7 and 8.
    // As with the story Blueprint skips, route only these tutorial states to its
    // existing no-help continuation. The continuation advances the queue and
    // restores the menu; no help flags, code unlocks or save data are written.
    // Manual help and all other menu states stay unchanged.
    class RuntimeEzCodesTutorialBypass
    {
        uintptr_t table_rva_ = 0x195B2EC;
        uintptr_t entry_rva_ = table_rva_ + 6 * sizeof(uint32_t);
        LONG original_target_ = 0x195AFC9;
        LONG resume_target_ = 0x195A7DF;
        volatile LONG* entry_{};
        volatile LONG* pro_entry_{};
        inline static constexpr std::array<uint8_t, 40> dispatch_bytes_{
            0x8B, 0x91, 0x60, 0x05, 0x00, 0x00, 0x8D, 0x42, 0xFF, 0x83, 0xF8, 0x09, 0x0F, 0x87, 0x1E, 0x0E, 0x00, 0x00, 0x48, 0x98, 0x4C, 0x8D, 0x05, 0x70, 0x5C, 0x6A, 0xFE, 0x41, 0x8B, 0x8C, 0x80, 0xEC, 0xB2, 0x95, 0x01, 0x49, 0x03, 0xC8, 0xFF, 0xE1};
        inline static constexpr std::array<uint8_t, 40> table_bytes_{
            0x9D, 0xA3, 0x95, 0x01, 0xD0, 0xA4, 0x95, 0x01, 0xBB, 0xA5, 0x95, 0x01, 0xF4, 0xA6, 0x95, 0x01, 0xD0, 0xA7, 0x95, 0x01, 0x66, 0xB2, 0x95, 0x01, 0xC9, 0xAF, 0x95, 0x01, 0xC9, 0xAF, 0x95, 0x01, 0x66, 0xB2, 0x95, 0x01, 0xA5, 0xB0, 0x95, 0x01};
        inline static constexpr std::array<uint8_t, 15> help_bytes_{
            0x83, 0xFA, 0x07, 0x0F, 0x95, 0xC3, 0x80, 0xC3, 0x85, 0x48, 0x39, 0x77, 0x70, 0x74, 0x62};
        inline static constexpr std::array<uint8_t, 24> resume_bytes_{
            0xF3, 0x0F, 0x10, 0x0D, 0xF5, 0x0F, 0x60, 0x06, 0x0F, 0x28, 0xD1, 0x48, 0x8B, 0xCF, 0xE8, 0xBE, 0x0B, 0x00, 0x00, 0xE9, 0x6F, 0x0A, 0x00, 0x00};
        inline static constexpr std::array<uint8_t, 41> open_bytes_{
            0xC6, 0x44, 0x24, 0x28, 0x00, 0xC6, 0x44, 0x24, 0x20, 0x00, 0x45, 0x0F, 0xB6, 0xCE, 0x4C, 0x8B, 0xC0, 0x0F, 0xB6, 0xD3, 0x48, 0x8B, 0x4F, 0x70, 0xE8, 0xC0, 0x35, 0xFA, 0xFF, 0x84, 0xC0, 0x74, 0x06, 0x40, 0x0F, 0xB6, 0xDE, 0xEB, 0x02, 0x32, 0xDB};

        inline static constexpr std::array<uint8_t, 40> steam_dispatch_bytes_{
            0x8B, 0x91, 0x60, 0x05, 0x00, 0x00, 0x8D, 0x42, 0xFF, 0x83, 0xF8, 0x09, 0x0F, 0x87, 0xCD, 0x0D, 0x00, 0x00, 0x48, 0x98, 0x4C, 0x8D, 0x05, 0x6C, 0x06, 0xA1, 0xFE, 0x41, 0x8B, 0x8C, 0x80, 0x90, 0x08, 0x5F, 0x01, 0x49, 0x03, 0xC8, 0xFF, 0xE1};
        inline static constexpr std::array<uint8_t, 40> steam_table_bytes_{
            0xA1, 0xF9, 0x5E, 0x01, 0xD1, 0xFA, 0x5E, 0x01, 0xC3, 0xFB, 0x5E, 0x01, 0xF1, 0xFD, 0x5E, 0x01, 0xD4, 0xFE, 0x5E, 0x01, 0x06, 0x08, 0x5F, 0x01, 0x42, 0x05, 0x5F, 0x01, 0x42, 0x05, 0x5F, 0x01, 0x06, 0x08, 0x5F, 0x01, 0x9B, 0x06, 0x5F, 0x01};
        inline static constexpr std::array<uint8_t, 17> steam_help_bytes_{
            0x83, 0xFA, 0x07, 0x41, 0x0F, 0x95, 0xC6, 0x41, 0x80, 0xC6, 0x85, 0x48, 0x39, 0x73, 0x70, 0x74, 0x5C};
        inline static constexpr std::array<uint8_t, 24> steam_resume_bytes_{
            0xF3, 0x0F, 0x10, 0x0D, 0x15, 0x7E, 0x69, 0x04, 0x0F, 0x28, 0xD1, 0x48, 0x8B, 0xCB, 0xE8, 0xAA, 0x14, 0x00, 0x00, 0xE9, 0x0B, 0x09, 0x00, 0x00};
        inline static constexpr std::array<uint8_t, 43> steam_open_bytes_{
            0xC6, 0x44, 0x24, 0x28, 0x00, 0xC6, 0x44, 0x24, 0x20, 0x00, 0x44, 0x0F, 0xB6, 0xCE, 0x4C, 0x8B, 0xC0, 0x41, 0x0F, 0xB6, 0xD6, 0x48, 0x8B, 0x4B, 0x70, 0xE8, 0x8B, 0x1E, 0xFB, 0xFF, 0x84, 0xC0, 0x74, 0x06, 0x44, 0x0F, 0xB6, 0xF6, 0xEB, 0x03, 0x45, 0x32, 0xF6};

        template<size_t N>
        static bool matches(const uint8_t* base, uintptr_t rva,
                            const std::array<uint8_t, N>& expected)
        {
            MEMORY_BASIC_INFORMATION info{};
            const auto address = reinterpret_cast<uintptr_t>(base) + rva;
            if (VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) != sizeof(info) ||
                info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
                address + N > reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize)
                return false;
            return std::memcmp(base + rva, expected.data(), N) == 0;
        }

        static bool exchange(volatile LONG* entry, LONG expected, LONG replacement)
        {
            auto* address = const_cast<LONG*>(entry);
            DWORD protection{};
            if (!VirtualProtect(address, sizeof(LONG), PAGE_EXECUTE_READWRITE, &protection))
                return false;
            const bool wrote = InterlockedCompareExchange(entry, replacement, expected) == expected;
            const bool flushed = FlushInstructionCache(GetCurrentProcess(), address, sizeof(LONG)) != FALSE;
            DWORD ignored{};
            const bool restored = VirtualProtect(address, sizeof(LONG), protection, &ignored) != FALSE;
            if (wrote && flushed && restored) return true;
            // Restore only our own value if protection/cache restoration failed.
            if (wrote)
            {
                DWORD rollback{};
                if (VirtualProtect(address, sizeof(LONG), PAGE_EXECUTE_READWRITE, &rollback))
                {
                    InterlockedCompareExchange(entry, expected, replacement);
                    FlushInstructionCache(GetCurrentProcess(), address, sizeof(LONG));
                    VirtualProtect(address, sizeof(LONG), protection, &ignored);
                }
            }
            return false;
        }

    public:
        void install()
        {
            if (entry_) return;
            const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
            if (!base) return;
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
            const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            const bool steam = kh3ap::native_build::steam.matches(pe->FileHeader.TimeDateStamp, pe->OptionalHeader.SizeOfImage);
            const bool egs = kh3ap::native_build::egs.matches(pe->FileHeader.TimeDateStamp, pe->OptionalHeader.SizeOfImage);
            const bool signatures = steam
                ? matches(base, 0x15EF979, steam_dispatch_bytes_) &&
                  matches(base, 0x15F0890, steam_table_bytes_) &&
                  matches(base, 0x15F0562, steam_help_bytes_) &&
                  matches(base, 0x15EFEE3, steam_resume_bytes_) &&
                  matches(base, 0x15F05A7, steam_open_bytes_)
                : egs && matches(base, 0x195A375, dispatch_bytes_) &&
                  matches(base, 0x195B2EC, table_bytes_) &&
                  matches(base, 0x195AFE9, help_bytes_) &&
                  matches(base, 0x195A7DF, resume_bytes_) &&
                  matches(base, 0x195B033, open_bytes_);
            if (pe->Signature != IMAGE_NT_SIGNATURE ||
                pe->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || !signatures)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] EZ Codes tutorial bypass rejected: unsupported image or menu signature\n"));
                return;
            }
            table_rva_ = steam ? 0x15F0890 : 0x195B2EC;
            entry_rva_ = table_rva_ + 6 * sizeof(uint32_t);
            original_target_ = steam ? 0x15F0542 : 0x195AFC9;
            resume_target_ = steam ? 0x15EFEE3 : 0x195A7DF;
            auto* entry = reinterpret_cast<volatile LONG*>(const_cast<uint8_t*>(base) + entry_rva_);
            static_assert((0x15F0890 + 6 * sizeof(uint32_t)) % alignof(LONG) == 0);
            static_assert((0x195B2EC + 6 * sizeof(uint32_t)) % alignof(LONG) == 0);
            if (!exchange(entry, original_target_, resume_target_))
            {
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] EZ Codes tutorial bypass failed: jump-table write\n"));
                return;
            }
            entry_ = entry;
            if (!exchange(entry + 1, original_target_, resume_target_))
            {
                uninstall();
                RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] PRO Codes tutorial bypass failed: jump-table write\n"));
                return;
            }
            pro_entry_ = entry + 1;
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] EZ/PRO Codes tutorial bypass installed: code-menu states=7,8 continuation=normal_menu_resume storefront={}\n"), steam ? STR("steam") : STR("egs"));
        }

        void uninstall()
        {
            if (pro_entry_ && !exchange(pro_entry_, resume_target_, original_target_))
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] PRO Codes tutorial bypass restore failed or ownership changed\n"));
            pro_entry_ = nullptr;
            if (!entry_) return;
            if (!exchange(entry_, resume_target_, original_target_))
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] EZ Codes tutorial bypass restore failed or ownership changed\n"));
            entry_ = nullptr;
        }
    };
}
