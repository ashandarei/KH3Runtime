#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/hooks/kh3_game_engine_tick_hook.h"
#include "kh3ap/hooks/frame_wait/kh3_frame_wait.h"
#include "kh3ap/core/performance_build.h"
#include "kh3ap/diagnostics/profiling/engine_hitch_trace.h"

#ifdef S
#pragma push_macro("S")
#undef S
#define KH3AP_RESTORE_S_MACRO_AFTER_POLYHOOK
#endif
#include <polyhook2/Detour/x64Detour.hpp>
#ifdef KH3AP_RESTORE_S_MACRO_AFTER_POLYHOOK
#pragma pop_macro("S")
#undef KH3AP_RESTORE_S_MACRO_AFTER_POLYHOOK
#endif

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace kh3ap
{
    namespace
    {
        // These UGameEngine::Tick implementations are rooted through each PE
        // exception table from the unique "Negative delta time" diagnostic
        // reference. Fail closed if a storefront updates its image layout or
        // function prologue.
        constexpr std::array<uint8_t, 29> s_steam_ugame_engine_tick_prologue{
                0x48, 0x8B, 0xC4,
                0x44, 0x88, 0x40, 0x18,
                0x55, 0x56, 0x57,
                0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                0x48, 0x8D, 0x68, 0xA1,
                0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00,
        };
        constexpr std::array<uint8_t, 34> s_egs_ugame_engine_tick_prologue{
                0x48, 0x8B, 0xC4,
                0x44, 0x88, 0x40, 0x18,
                0x48, 0x89, 0x48, 0x08,
                0x55, 0x53, 0x56, 0x57,
                0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                0x48, 0x8D, 0x68, 0xB8,
                0x48, 0x81, 0xEC, 0x08, 0x01, 0x00, 0x00,
        };

        struct GameEngineTickProfile
        {
            std::wstring_view storefront{};
            size_t image_size{};
            uintptr_t tick_rva{};
            const uint8_t* prologue{};
            size_t prologue_size{};
        };

        constexpr std::array<GameEngineTickProfile, 2> s_game_engine_tick_profiles{{
                {
                        STR("steam"),
                        kh3ap::native_build::steam.image_size,
                        0x40ED990,
                        s_steam_ugame_engine_tick_prologue.data(),
                        s_steam_ugame_engine_tick_prologue.size(),
                },
                {
                        STR("egs"),
                        kh3ap::native_build::egs.image_size,
                        0x46652C0,
                        s_egs_ugame_engine_tick_prologue.data(),
                        s_egs_ugame_engine_tick_prologue.size(),
                },
        }};

        auto module_size_from_pe_header(HMODULE module) -> size_t
        {
            if (!module)
            {
                return 0;
            }
            const auto* const base = reinterpret_cast<const std::byte*>(module);
            const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
            {
                return 0;
            }
            const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE ||
                nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            {
                return 0;
            }
            return static_cast<size_t>(nt->OptionalHeader.SizeOfImage);
        }

        auto address_is_executable(uintptr_t address) -> bool
        {
            MEMORY_BASIC_INFORMATION memory_info{};
            if (address == 0 ||
                VirtualQuery(
                        reinterpret_cast<const void*>(address),
                        &memory_info,
                        sizeof(memory_info)) != sizeof(memory_info))
            {
                return false;
            }
            if (memory_info.State != MEM_COMMIT || (memory_info.Protect & PAGE_GUARD) != 0)
            {
                return false;
            }
            const DWORD protection = memory_info.Protect & 0xFF;
            return protection == PAGE_EXECUTE ||
                   protection == PAGE_EXECUTE_READ ||
                   protection == PAGE_EXECUTE_READWRITE ||
                   protection == PAGE_EXECUTE_WRITECOPY;
        }

        auto has_expected_tick_prologue(
                uintptr_t target,
                const GameEngineTickProfile& profile) -> bool
        {
            if (!address_is_executable(target) || !profile.prologue || profile.prologue_size == 0)
            {
                return false;
            }
            return std::memcmp(
                           reinterpret_cast<const void*>(target),
                           profile.prologue,
                           profile.prologue_size) == 0;
        }
    }

    Kh3GameEngineTickHook::Kh3GameEngineTickHook() = default;

    auto Kh3GameEngineTickHook::X64DetourDeleter::operator()(
            PLH::x64Detour* detour) const noexcept -> void
    {
        delete detour;
    }

    Kh3GameEngineTickHook::~Kh3GameEngineTickHook()
    {
        uninstall();
    }

    auto Kh3GameEngineTickHook::install(
            std::function<void()> post_tick_callback,
            std::wstring_view build_stamp
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            , std::function<std::wstring()> hitch_context
#endif
            ) -> bool
    {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        hitch_context_ = std::move(hitch_context);
#endif
        if (installed_)
        {
            post_tick_callback_ = std::move(post_tick_callback);
            s_owner.store(this, std::memory_order_release);
            return true;
        }

        HMODULE const game_module = GetModuleHandleW(nullptr);
        const uintptr_t module_base = reinterpret_cast<uintptr_t>(game_module);
        const size_t module_size = module_size_from_pe_header(game_module);
        const GameEngineTickProfile* profile = nullptr;
        for (const auto& candidate : s_game_engine_tick_profiles)
        {
            if (candidate.image_size == module_size)
            {
                profile = &candidate;
                break;
            }
        }
        if (module_base == 0 || !profile)
        {
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] UGameEngine::Tick hook failed reason=unsupported_image image_size=0x{:X} steam_expected=0x{:X} egs_expected=0x{:X} build_stamp={}\n"),
                    module_size,
                    s_game_engine_tick_profiles[0].image_size,
                    s_game_engine_tick_profiles[1].image_size,
                    build_stamp);
            return false;
        }

        target_ = module_base + profile->tick_rva;
        if (!has_expected_tick_prologue(target_, *profile))
        {
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] UGameEngine::Tick hook failed reason=signature_mismatch storefront={} target_offset=0x{:X} build_stamp={}\n"),
                    profile->storefront,
                    profile->tick_rva,
                    build_stamp);
            target_ = 0;
            return false;
        }

        DWORD64 runtime_image_base = 0;
        const PRUNTIME_FUNCTION runtime_function = RtlLookupFunctionEntry(
                static_cast<DWORD64>(target_),
                &runtime_image_base,
                nullptr);
        if (!runtime_function ||
            runtime_image_base != static_cast<DWORD64>(module_base) ||
            runtime_function->BeginAddress != profile->tick_rva ||
            runtime_function->EndAddress <= runtime_function->BeginAddress + 0x400)
        {
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] UGameEngine::Tick hook failed reason=runtime_function_mismatch storefront={} target_offset=0x{:X} build_stamp={}\n"),
                    profile->storefront,
                    profile->tick_rva,
                    build_stamp);
            target_ = 0;
            return false;
        }

        post_tick_callback_ = std::move(post_tick_callback);
        trampoline_ = 0;
        detour_.reset(new PLH::x64Detour(
                static_cast<uint64_t>(target_),
                reinterpret_cast<uint64_t>(&Kh3GameEngineTickHook::game_engine_tick_detour),
                &trampoline_));
        const bool hook_result = detour_->hook();
        if (!hook_result || trampoline_ == 0)
        {
            if (hook_result)
            {
                detour_->unHook();
            }
            detour_.reset();
            post_tick_callback_ = {};
            trampoline_ = 0;
            target_ = 0;
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] UGameEngine::Tick hook failed reason=polyhook_install build_stamp={}\n"),
                    build_stamp);
            return false;
        }

        s_original_tick.store(trampoline_, std::memory_order_release);
        s_owner.store(this, std::memory_order_release);
        installed_ = true;
        RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] UGameEngine::Tick hook installed storefront={} target_offset=0x{:X} function_size=0x{:X} build_stamp={}\n"),
                profile->storefront,
                profile->tick_rva,
                runtime_function->EndAddress - runtime_function->BeginAddress,
                build_stamp);
        return true;
    }

    auto Kh3GameEngineTickHook::uninstall() -> void
    {
        Kh3GameEngineTickHook* expected_owner = this;
        s_owner.compare_exchange_strong(
                expected_owner,
                nullptr,
                std::memory_order_acq_rel);
        if (detour_)
        {
            if (installed_)
            {
                detour_->unHook();
            }
            detour_.reset();
        }
        s_original_tick.store(0, std::memory_order_release);
        post_tick_callback_ = {};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        hitch_context_ = {};
#endif
        trampoline_ = 0;
        target_ = 0;
        installed_ = false;
    }

    auto Kh3GameEngineTickHook::installed() const -> bool
    {
        return installed_;
    }

    auto Kh3GameEngineTickHook::target() const -> uintptr_t
    {
        return target_;
    }

    auto Kh3GameEngineTickHook::game_engine_tick_detour(
            void* game_engine,
            float delta_seconds,
            bool idle_mode) -> void
    {
        const uintptr_t original_tick = s_original_tick.load(std::memory_order_acquire);
        if (original_tick == 0)
        {
            return;
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        RuntimeEngineHitchScope hitch_trace{delta_seconds, idle_mode};
#endif
        reinterpret_cast<GameEngineTickFn>(original_tick)(
                game_engine,
                delta_seconds,
                idle_mode);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        hitch_trace.engine_finished();
        observe_native_frame_tick();
#endif

        Kh3GameEngineTickHook* const owner = s_owner.load(std::memory_order_acquire);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        if (owner) hitch_trace.set_context_provider(&owner->hitch_context_);
#endif
        if (!owner || !owner->post_tick_callback_)
        {
            return;
        }
        try
        {
            owner->post_tick_callback_();
        }
        catch (...)
        {
            // Never allow a C++ exception to cross the engine tick boundary.
        }
    }
}
