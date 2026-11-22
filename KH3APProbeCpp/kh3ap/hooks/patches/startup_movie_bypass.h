#pragma once

#include "kh3ap/app/composition/detour.h"
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/hooks/native_detour_lifetime.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <span>

namespace kh3ap
{
    namespace startup_movie_detail
    {
        constexpr uintptr_t playlist_append_rva = 0x317526B;
        constexpr uintptr_t startup_wait_rva = 0x3DDA090;
        constexpr uintptr_t movie_player_vtable_rva = 0x7DAA740;
        constexpr uintptr_t startup_finished_rva = 0x92A2D20;
        constexpr size_t slate_widget_offset = 0x80;
        constexpr size_t movie_count_offset = 0x98;
        constexpr size_t minimum_display_time_offset = 0xA0;
        constexpr size_t last_play_time_offset = 0x200;
        constexpr double platform_time_epoch = 16777216.0;
        constexpr float input_warmup_seconds = 0.25F;

        constexpr std::array<uint8_t, 16> playlist_append{
            0x48, 0x63, 0x5C, 0x24, 0x70, 0x8D, 0x43, 0x01, 0x89, 0x44, 0x24, 0x70, 0x3B, 0x44, 0x24, 0x74};
        constexpr std::array<uint8_t, 5> omit_playlist_entry{0xEB, 0x5D, 0x90, 0x90, 0x90}; // -> 0x31752CA
        constexpr std::array<uint8_t, 19> wait_prologue{
            0x48, 0x8B, 0xC4, 0x48, 0x89, 0x48, 0x08, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
            0x41, 0x56, 0x41, 0x57};
        constexpr std::array<uint8_t, 15> wait_clock_fields{
            0xF2, 0x0F, 0x5C, 0x8F, 0x00, 0x02, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x87, 0xA0, 0x00, 0x00};
        constexpr std::array<uint8_t, 15> wait_input_pump{
            0xB1, 0x01, 0xE8, 0xF8, 0x12, 0x4A, 0xFF, 0x49, 0x8B, 0xCF, 0xE8, 0xC0, 0xA9, 0x77, 0xFF};

        inline uint8_t* game_image{};
        inline uint64_t wait_original{};
        inline std::unique_ptr<PLH::x64Detour> wait_detour;
        inline std::atomic_bool startup_widget_pending{};

        inline bool replace_bytes(uint8_t* address, std::span<const uint8_t> expected,
                                  std::span<const uint8_t> replacement)
        {
            if (std::memcmp(address, expected.data(), expected.size()) != 0)
            {
                return false;
            }

            DWORD protection{};
            if (!VirtualProtect(address, replacement.size(), PAGE_EXECUTE_READWRITE, &protection))
            {
                return false;
            }

            std::memcpy(address, replacement.data(), replacement.size());
            const bool flushed = FlushInstructionCache(GetCurrentProcess(), address, replacement.size()) != FALSE;
            if (!flushed)
            {
                std::memcpy(address, expected.data(), replacement.size());
                FlushInstructionCache(GetCurrentProcess(), address, replacement.size());
            }
            DWORD ignored{};
            VirtualProtect(address, replacement.size(), protection, &ignored);
            return flushed;
        }

        inline void wait_for_startup_screen(uint8_t* movie_player)
        {
            using Wait = void (*)(uint8_t*);
            const auto original = reinterpret_cast<Wait>(wait_original);
            if (startup_widget_pending.load(std::memory_order_acquire) && movie_player &&
                *reinterpret_cast<uint8_t**>(movie_player) == game_image + movie_player_vtable_rva &&
                game_image[startup_finished_rva] == 0 &&
                *reinterpret_cast<void**>(movie_player + slate_widget_offset) &&
                *reinterpret_cast<int32_t*>(movie_player + movie_count_offset) == 0 &&
                startup_widget_pending.exchange(false, std::memory_order_acq_rel))
            {
                LARGE_INTEGER counter{}, frequency{};
                if (QueryPerformanceCounter(&counter) && QueryPerformanceFrequency(&frequency))
                {
                    // A movie-free screen otherwise exits before the first
                    // native message pump and game-device poll. Give the
                    // original wait loop a short window starting at handoff,
                    // after the engine and viewport have initialized.
                    *reinterpret_cast<double*>(movie_player + last_play_time_offset) =
                        platform_time_epoch + static_cast<double>(counter.QuadPart) /
                                                  static_cast<double>(frequency.QuadPart);
                    *reinterpret_cast<float*>(movie_player + minimum_display_time_offset) = input_warmup_seconds;
                    RC::Output::send<RC::LogLevel::Normal>(
                        STR("[KH3APProbeCpp] Copyright playback omitted; running startup input handoff\n"));
                }
            }
            original(movie_player);
        }
    }

    inline void install_startup_movie_bypass()
    {
        using namespace startup_movie_detail;
        auto* const image = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        if (!image || wait_detour)
        {
            return;
        }
        const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew >= 0x1000)
        {
            return;
        }
        const auto* const pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
        if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            !native_build::egs.matches(pe->FileHeader.TimeDateStamp, pe->OptionalHeader.SizeOfImage))
        {
            return;
        }

        if (std::memcmp(image + playlist_append_rva, playlist_append.data(), playlist_append.size()) != 0 ||
            std::memcmp(image + startup_wait_rva, wait_prologue.data(), wait_prologue.size()) != 0 ||
            std::memcmp(image + 0x3DDA3A0, wait_clock_fields.data(), wait_clock_fields.size()) != 0 ||
            std::memcmp(image + 0x3DDA421, wait_input_pump.data(), wait_input_pump.size()) != 0)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] Copyright movie bypass signature mismatch\n"));
            return;
        }

        game_image = image;
        wait_detour = std::make_unique<PLH::x64Detour>(
            reinterpret_cast<uint64_t>(image + startup_wait_rva),
            reinterpret_cast<uint64_t>(&wait_for_startup_screen), &wait_original);
        if (!wait_detour->hook() ||
            !replace_bytes(image + playlist_append_rva, playlist_append, omit_playlist_entry))
        {
            release_native_detour(wait_detour);
            RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Copyright movie bypass unavailable\n"));
            return;
        }
        startup_widget_pending.store(true, std::memory_order_release);
    }

    inline void uninstall_startup_movie_bypass()
    {
        using namespace startup_movie_detail;
        startup_widget_pending.store(false, std::memory_order_release);
        if (game_image)
        {
            replace_bytes(game_image + playlist_append_rva, omit_playlist_entry,
                          std::span{playlist_append}.first<omit_playlist_entry.size()>());
        }
        release_native_detour(wait_detour);
    }
}
