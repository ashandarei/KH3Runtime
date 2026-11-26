#pragma once

#include "kh3ap/app/composition/detour.h"
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/hooks/native_detour_lifetime.h"
#include "kh3ap/hooks/patches/checked_code_patch.h"

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
        struct StartupMovieProfile
        {
            uintptr_t playlist_append_rva;
            uintptr_t startup_wait_rva;
            uintptr_t movie_player_vtable_rva;
            uintptr_t startup_finished_rva;
            uintptr_t clock_fields_rva;
            uintptr_t input_pump_rva;
            std::span<const uint8_t> wait_prologue;
            std::array<uint8_t, 15> input_pump;
            std::array<uint8_t, 5> omit_playlist_entry;
        };

        constexpr size_t wait_vtable_offset = 0x50;
        constexpr size_t slate_widget_offset = 0x80;
        constexpr size_t movie_count_offset = 0x98;
        constexpr size_t minimum_display_time_offset = 0xA0;
        constexpr size_t last_play_time_offset = 0x200;
        constexpr double platform_time_epoch = 16777216.0;
        constexpr float input_warmup_seconds = 0.25F;

        constexpr std::array<uint8_t, 16> playlist_append{
            0x48, 0x63, 0x5C, 0x24, 0x70, 0x8D, 0x43, 0x01, 0x89, 0x44, 0x24, 0x70, 0x3B, 0x44, 0x24, 0x74};
        constexpr std::array<uint8_t, 19> egs_wait_prologue{
            0x48, 0x8B, 0xC4, 0x48, 0x89, 0x48, 0x08, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
            0x41, 0x56, 0x41, 0x57};
        constexpr std::array<uint8_t, 20> steam_wait_prologue{
            0x48, 0x8B, 0xC4, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
            0x48, 0x8D, 0x6C, 0x24, 0xA8};
        constexpr std::array<uint8_t, 15> wait_clock_fields{
            0xF2, 0x0F, 0x5C, 0x8F, 0x00, 0x02, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x87, 0xA0, 0x00, 0x00};

        // Both builds resume at the instruction following the copyright playlist append.
        constexpr StartupMovieProfile egs_profile{
            0x317526B, 0x3DDA090, 0x7DAA740, 0x92A2D20, 0x3DDA3A0, 0x3DDA421, egs_wait_prologue,
            {0xB1, 0x01, 0xE8, 0xF8, 0x12, 0x4A, 0xFF, 0x49, 0x8B, 0xCF, 0xE8, 0xC0, 0xA9, 0x77, 0xFF},
            {0xEB, 0x5D, 0x90, 0x90, 0x90}};
        constexpr StartupMovieProfile steam_profile{
            0x2CDCA6C, 0x38B9D90, 0x7477828, 0x9C76E4C, 0x38BA145, 0x38BA1C6, steam_wait_prologue,
            {0xB1, 0x01, 0xE8, 0x23, 0xCF, 0x52, 0xFF, 0x49, 0x8B, 0xCD, 0xE8, 0x4B, 0x77, 0x80, 0xFF},
            {0xEB, 0x62, 0x90, 0x90, 0x90}};

        inline uint8_t* game_image{};
        inline const StartupMovieProfile* installed_profile{};
        inline uint64_t wait_original{};
        inline std::unique_ptr<PLH::x64Detour> wait_detour;
        inline std::atomic_bool startup_widget_pending{};

        inline void wait_for_startup_screen(uint8_t* movie_player)
        {
            using Wait = void (*)(uint8_t*);
            const auto original = reinterpret_cast<Wait>(wait_original);
            if (startup_widget_pending.load(std::memory_order_acquire) && movie_player &&
                *reinterpret_cast<uint8_t**>(movie_player) == game_image + installed_profile->movie_player_vtable_rva &&
                game_image[installed_profile->startup_finished_rva] == 0 &&
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
        if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
        {
            return;
        }

        const StartupMovieProfile* profile{};
        switch (native_build::identify(pe->FileHeader.TimeDateStamp, pe->OptionalHeader.SizeOfImage))
        {
        case native_build::Build::Egs:
            profile = &egs_profile;
            break;
        case native_build::Build::Steam:
            profile = &steam_profile;
            break;
        default:
            return;
        }

        auto* wait = image + profile->startup_wait_rva;
        auto* wait_slot = image + profile->movie_player_vtable_rva + wait_vtable_offset;
        if (std::memcmp(image + profile->playlist_append_rva, playlist_append.data(), playlist_append.size()) != 0 ||
            std::memcmp(wait, profile->wait_prologue.data(), profile->wait_prologue.size()) != 0 ||
            std::memcmp(image + profile->clock_fields_rva, wait_clock_fields.data(), wait_clock_fields.size()) != 0 ||
            std::memcmp(image + profile->input_pump_rva, profile->input_pump.data(), profile->input_pump.size()) != 0 ||
            *reinterpret_cast<uint8_t**>(wait_slot) != wait)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] Copyright movie bypass signature mismatch\n"));
            return;
        }

        game_image = image;
        wait_detour = std::make_unique<PLH::x64Detour>(
            reinterpret_cast<uint64_t>(wait),
            reinterpret_cast<uint64_t>(&wait_for_startup_screen), &wait_original);
        if (!wait_detour->hook() || !replace_checked_code_bytes(image + profile->playlist_append_rva,
                                                  playlist_append, profile->omit_playlist_entry))
        {
            release_native_detour(wait_detour);
            RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Copyright movie bypass unavailable\n"));
            return;
        }
        installed_profile = profile;
        startup_widget_pending.store(true, std::memory_order_release);
    }

    inline void uninstall_startup_movie_bypass()
    {
        using namespace startup_movie_detail;
        startup_widget_pending.store(false, std::memory_order_release);
        if (installed_profile)
        {
            const auto& patch = installed_profile->omit_playlist_entry;
            replace_checked_code_bytes(game_image + installed_profile->playlist_append_rva, patch,
                          std::span{playlist_append}.first(patch.size()));
        }
        release_native_detour(wait_detour);
        installed_profile = nullptr;
    }
}
