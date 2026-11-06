#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/core/memory/memory_protection_bits.h"
#include "kh3ap/features/enemies/experience_multiplier_service.h"

#include "kh3ap/features/enemies/experience_multiplier_math.h"
#include "kh3ap/core/settings/goa_settings_service.h"

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
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace kh3ap
{
    using namespace RC;

    namespace
    {
        constexpr auto experience_award_cap_bytes = 12;
        constexpr auto experience_award_result_register_byte = 11;
        constexpr uint8_t steam_award_result_register_opcode = 0xef;
        constexpr auto steam_award_cap_unwind_begin_rva = 0x632d5d;
        constexpr auto egs_award_cap_unwind_begin_rva = 0x7d90ad;
        constexpr auto vanilla_experience_award_cap = 999;
        constexpr auto expanded_experience_award_cap = 9999999;

        // This function is the common Sora EXP award path. EDX contains the
        // vanilla base award, R8D selects the progression lane, and the
        // function applies KH3's ordinary difficulty/ability modifiers before
        // updating total EXP and level-up presentation. Storefront profiles
        // fail closed on the exact image layout, timestamp, RVA, unwind entry,
        // and prologue validated against each current executable.

        constexpr std::array<uint8_t, 24> steam_experience_award_prologue{
            0x85, 0xD2, 0x0F, 0x84, 0x91, 0x01, 0x00, 0x00, 0x55, 0x56, 0x41, 0x54,
            0x48, 0x83, 0xEC, 0x30, 0x4D, 0x8B, 0xE1, 0x8B, 0xF2, 0x48, 0x8B, 0xE9,
        };

        constexpr std::array<uint8_t, 30> egs_experience_award_prologue{
            0x45, 0x85, 0xC0, 0x0F, 0x85, 0xD8, 0x02, 0x00, 0x00, 0x4C,
            0x89, 0x4C, 0x24, 0x20, 0x55, 0x56, 0x41, 0x56, 0x48, 0x83,
            0xEC, 0x30, 0x4D, 0x8B, 0xF1, 0x8B, 0xF2, 0x48, 0x8B, 0xE9,
        };

        struct NativeExperienceAwardProfile
        {
            std::wstring_view storefront{};
            std::size_t image_size{};
            uint32_t image_timestamp{};
            uintptr_t award_rva{};
            const uint8_t* prologue{};
            std::size_t prologue_size{};
        };

        constexpr std::array<NativeExperienceAwardProfile, 2> experience_award_profiles{{
            {
                STR("steam"),
                kh3ap::native_build::steam.image_size,
                kh3ap::native_build::steam.timestamp,
                0xE9E830,
                steam_experience_award_prologue.data(),
                steam_experience_award_prologue.size(),
            },
            {
                STR("egs"),
                kh3ap::native_build::egs.image_size,
                kh3ap::native_build::egs.timestamp,
                0x1135CC0,
                egs_experience_award_prologue.data(),
                egs_experience_award_prologue.size(),
            },
        }};

        struct ModuleMetadata
        {
            std::size_t image_size{};
            uint32_t image_timestamp{};
        };

        [[nodiscard]] auto module_metadata_from_pe_header(HMODULE module) -> ModuleMetadata
        {
            if (!module)
            {
                return {};
            }
            const auto* const base = reinterpret_cast<const std::byte*>(module);
            const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
            {
                return {};
            }
            const auto* const nt =
                reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE ||
                nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            {
                return {};
            }
            return ModuleMetadata{
                .image_size = static_cast<std::size_t>(nt->OptionalHeader.SizeOfImage),
                .image_timestamp = nt->FileHeader.TimeDateStamp,
            };
        }

        [[nodiscard]] auto address_is_executable(uintptr_t address) -> bool
        {
            MEMORY_BASIC_INFORMATION memory_info{};
            if (address == 0 ||
                VirtualQuery(reinterpret_cast<const void*>(address), &memory_info,
                             sizeof(memory_info)) != sizeof(memory_info) ||
                memory_info.State != MEM_COMMIT || (memory_info.Protect & PAGE_GUARD) != 0)
            {
                return false;
            }
            const DWORD protection = memory_info.Protect & memory_protection::base_mode_mask;
            return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
                   protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        }

        [[nodiscard]] auto find_experience_award_profile(const ModuleMetadata& metadata)
            -> const NativeExperienceAwardProfile*
        {
            for (const auto& profile : experience_award_profiles)
            {
                if (profile.image_size == metadata.image_size &&
                    profile.image_timestamp == metadata.image_timestamp)
                {
                    return &profile;
                }
            }
            return nullptr;
        }

        [[nodiscard]] auto has_expected_prologue(uintptr_t target,
                                                 const NativeExperienceAwardProfile& profile)
            -> bool
        {
            return address_is_executable(target) && profile.prologue && profile.prologue_size > 0 &&
                   std::memcmp(reinterpret_cast<const void*>(target), profile.prologue,
                               profile.prologue_size) == 0;
        }

        [[nodiscard]] auto multiplier_text(int32_t half_steps) -> std::wstring
        {
            return half_steps % experience_half_steps_per_unit == 0
                       ? std::format(STR("{}x"), half_steps / experience_half_steps_per_unit)
                       : std::format(STR("{}.5x"), half_steps / experience_half_steps_per_unit);
        }

        // EGS CL 1065258: final difficulty/EXP Boost result, before accumulation.
        // Retain the signed clamp but use the game's total EXP ceiling. Even
        // adding this to the largest legal total cannot overflow int32_t.
        constexpr uintptr_t egs_award_cap_rva = 0x7D91A1;
        constexpr uintptr_t steam_award_cap_rva = 0x632E37;

        constexpr std::array<uint8_t, experience_award_cap_bytes> vanilla_award_cap{
            0xB8, 0xE7, 0x03, 0x00, 0x00, // mov eax, 999
            0x3B, 0xF8,                   // cmp edi, eax
            0x0F, 0x4F, 0xF8,             // cmovg edi, eax
            0x8B, 0xF7};
        // mov esi, edi

        constexpr std::array<uint8_t, experience_award_cap_bytes> expanded_award_cap{
            0xB8, 0x7F, 0x96, 0x98, 0x00, // mov eax, 9999999
            0x3B, 0xF8, 0x0F, 0x4F, 0xF8, 0x8B, 0xF7};
        constexpr auto steam_vanilla_award_cap = []
        {
            auto bytes = vanilla_award_cap;
            bytes[experience_award_result_register_byte] =
                steam_award_result_register_opcode; // Steam keeps the result in EBP, EGS in ESI.
            return bytes;
        }();
        constexpr auto steam_expanded_award_cap = []
        {
            auto bytes = expanded_award_cap;
            bytes[experience_award_result_register_byte] = steam_award_result_register_opcode;
            return bytes;
        }();

        auto replace_award_cap(uintptr_t address,
                               const std::array<uint8_t, experience_award_cap_bytes>& expected,
                               const std::array<uint8_t, experience_award_cap_bytes>& replacement)
            -> bool
        {
            if (!address_is_executable(address) ||
                std::memcmp(reinterpret_cast<const void*>(address), expected.data(),
                            expected.size()) != 0)
            {
                return false;
            }
            DWORD protection = 0;
            if (!VirtualProtect(reinterpret_cast<void*>(address), expected.size(),
                                PAGE_EXECUTE_READWRITE, &protection))
            {
                return false;
            }
            std::memcpy(reinterpret_cast<void*>(address), replacement.data(), replacement.size());
            const bool flushed =
                FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                                      replacement.size()) != FALSE;
            DWORD ignored = 0;
            const bool restored = VirtualProtect(reinterpret_cast<void*>(address), expected.size(),
                                                 protection, &ignored) != FALSE;
            if (flushed && restored)
            {
                return true;
            }
            DWORD rollback = 0;
            if (VirtualProtect(reinterpret_cast<void*>(address), expected.size(),
                               PAGE_EXECUTE_READWRITE, &rollback))
            {
                std::memcpy(reinterpret_cast<void*>(address), expected.data(), expected.size());
                FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                                      expected.size());
                VirtualProtect(reinterpret_cast<void*>(address), expected.size(), protection,
                               &ignored);
            }
            return false;
        }
    } // namespace

    struct RuntimeExperienceMultiplierService::Impl
    {
        using ExperienceAwardFn = void (*)(void* save_owner, int32_t base_experience,
                                           int32_t progression_lane, void* presentation_context);

        explicit Impl(RuntimeExperienceMultiplierConfig config) : config_(std::move(config))
        {
        }

        ~Impl()
        {
            uninstall_native_award_hook();
        }

        auto sync_multiplier_from_settings() -> void
        {
            const RuntimeGoaSettingsStatus settings = config_.goa_settings.status();
            const int32_t runtime_half_steps = is_supported_experience_multiplier_half_steps(
                                                   settings.experience_multiplier_half_steps)
                                                   ? settings.experience_multiplier_half_steps
                                                   : 2;
            runtime_multiplier_half_steps_.store(runtime_half_steps, std::memory_order_release);
        }

        [[nodiscard]] auto install_native_award_hook() -> bool
        {
            if (hook_installed_)
            {
                return true;
            }

            HMODULE const game_module = GetModuleHandleW(nullptr);
            const uintptr_t module_base = reinterpret_cast<uintptr_t>(game_module);
            const ModuleMetadata module_metadata = module_metadata_from_pe_header(game_module);
            const NativeExperienceAwardProfile* const profile =
                find_experience_award_profile(module_metadata);
            if (module_base == 0 || !profile)
            {
                Output::send<LogLevel::Error>(
                    STR("[KH3APProbeCpp] Runtime EXP award hook failed "
                        "reason=unsupported_image image_size=0x{:X} "
                        "image_timestamp=0x{:X} "
                        "steam_expected_size=0x{:X} "
                        "steam_expected_timestamp=0x{:X} "
                        "egs_expected_size=0x{:X} "
                        "egs_expected_timestamp=0x{:X} "
                        "build_stamp={}\n"),
                    module_metadata.image_size, module_metadata.image_timestamp,
                    experience_award_profiles[0].image_size,
                    experience_award_profiles[0].image_timestamp,
                    experience_award_profiles[1].image_size,
                    experience_award_profiles[1].image_timestamp, config_.build_stamp);
                return false;
            }

            target_ = module_base + profile->award_rva;
            if (!has_expected_prologue(target_, *profile))
            {
                Output::send<LogLevel::Error>(STR("[KH3APProbeCpp] Runtime EXP award hook failed "
                                                  "reason=signature_mismatch storefront={} "
                                                  "target_offset=0x{:X} build_stamp={}\n"),
                                              profile->storefront, profile->award_rva,
                                              config_.build_stamp);
                target_ = 0;
                return false;
            }

            DWORD64 runtime_image_base = 0;
            const PRUNTIME_FUNCTION runtime_function =
                RtlLookupFunctionEntry(static_cast<DWORD64>(target_), &runtime_image_base, nullptr);
            if (!runtime_function || runtime_image_base != static_cast<DWORD64>(module_base) ||
                runtime_function->BeginAddress != profile->award_rva ||
                runtime_function->EndAddress <
                    runtime_function->BeginAddress + profile->prologue_size)
            {
                Output::send<LogLevel::Error>(STR("[KH3APProbeCpp] Runtime EXP award hook failed "
                                                  "reason=runtime_function_mismatch storefront={} "
                                                  "target_offset=0x{:X} build_stamp={}\n"),
                                              profile->storefront, profile->award_rva,
                                              config_.build_stamp);
                target_ = 0;
                return false;
            }

            trampoline_ = 0;
            detour_ = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(target_),
                reinterpret_cast<uint64_t>(&Impl::experience_award_detour), &trampoline_);
            const bool hook_result = detour_->hook();
            if (!hook_result || trampoline_ == 0)
            {
                if (hook_result)
                {
                    detour_->unHook();
                }
                detour_.reset();
                trampoline_ = 0;
                target_ = 0;
                Output::send<LogLevel::Error>(STR("[KH3APProbeCpp] Runtime EXP award hook failed "
                                                  "reason=polyhook_install storefront={} "
                                                  "build_stamp={}\n"),
                                              profile->storefront, config_.build_stamp);
                return false;
            }

            original_award_.store(static_cast<uintptr_t>(trampoline_), std::memory_order_release);
            hook_owner_.store(this, std::memory_order_release);
            hook_installed_ = true;
            storefront_ = profile->storefront;
            if (profile->storefront == STR("egs") || profile->storefront == STR("steam"))
            {
                const bool steam = profile->storefront == STR("steam");
                const uintptr_t cap_rva = steam ? steam_award_cap_rva : egs_award_cap_rva;
                const auto& original = steam ? steam_vanilla_award_cap : vanilla_award_cap;
                const auto& replacement = steam ? steam_expanded_award_cap : expanded_award_cap;
                const uintptr_t cap = module_base + cap_rva;
                DWORD64 cap_image_base = 0;
                const PRUNTIME_FUNCTION cap_function =
                    RtlLookupFunctionEntry(static_cast<DWORD64>(cap), &cap_image_base, nullptr);
                if (cap_function && cap_image_base == module_base &&
                    // This routine has chained unwind fragments; the clamp
                    // belongs to the modifier body, not its prologue fragment.
                    cap_function->BeginAddress == (steam ? steam_award_cap_unwind_begin_rva
                                                         : egs_award_cap_unwind_begin_rva) &&
                    cap_function->EndAddress >= cap_rva + original.size() &&
                    replace_award_cap(cap, original, replacement))
                {
                    award_cap_address_ = cap;
                }
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Runtime EXP award cap expanded={} limit={} storefront={} "
                        "build_stamp={}\n"),
                    award_cap_address_ != 0,
                    award_cap_address_ ? expanded_experience_award_cap
                                       : vanilla_experience_award_cap,
                    profile->storefront, config_.build_stamp);
            }
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Runtime EXP award hook installed "
                    "storefront={} image_size=0x{:X} "
                    "image_timestamp=0x{:X} target_offset=0x{:X} "
                    "unwind_fragment_size=0x{:X} build_stamp={}\n"),
                profile->storefront, profile->image_size, profile->image_timestamp,
                profile->award_rva, runtime_function->EndAddress - runtime_function->BeginAddress,
                config_.build_stamp);
            return true;
        }

        auto uninstall_native_award_hook() -> void
        {
            if (award_cap_address_)
            {
                const bool steam = storefront_ == STR("steam");
                if (!replace_award_cap(award_cap_address_,
                                       steam ? steam_expanded_award_cap : expanded_award_cap,
                                       steam ? steam_vanilla_award_cap : vanilla_award_cap))
                {
                    Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] Runtime EXP cap restore failed\n"));
                }
                award_cap_address_ = 0;
            }
            Impl* expected_owner = this;
            hook_owner_.compare_exchange_strong(expected_owner, nullptr, std::memory_order_acq_rel);
            if (detour_)
            {
                if (hook_installed_)
                {
                    detour_->unHook();
                }
                detour_.reset();
            }
            original_award_.store(0, std::memory_order_release);
            trampoline_ = 0;
            target_ = 0;
            hook_installed_ = false;
        }

        static auto experience_award_detour(void* save_owner, int32_t base_experience,
                                            int32_t progression_lane,
                                            void* presentation_context) noexcept -> void
        {
            const uintptr_t original_address = original_award_.load(std::memory_order_acquire);
            if (original_address == 0)
            {
                return;
            }

            int32_t forwarded_experience = base_experience;
            Impl* const owner = hook_owner_.load(std::memory_order_acquire);
            if (owner && base_experience > 0 && progression_lane == 0)
            {
                const int32_t runtime_half_steps =
                    owner->runtime_multiplier_half_steps_.load(std::memory_order_acquire);
                if (const auto scaled =
                        scale_experience_for_runtime_award(base_experience, runtime_half_steps))
                {
                    forwarded_experience = *scaled;
                }

                owner->last_base_experience_.store(base_experience, std::memory_order_relaxed);
                owner->last_forwarded_experience_.store(forwarded_experience,
                                                        std::memory_order_relaxed);
                owner->last_runtime_multiplier_half_steps_.store(runtime_half_steps,
                                                                 std::memory_order_relaxed);
                owner->observed_award_count_.fetch_add(1, std::memory_order_release);
            }

            if (Kh02Story::award_shared_experience(save_owner, forwarded_experience, progression_lane))
            {
                return;
            }
            reinterpret_cast<ExperienceAwardFn>(original_address)(
                save_owner, forwarded_experience, progression_lane, presentation_context);
        }

        auto install_once() -> void
        {
            if (installed_)
            {
                return;
            }
            installed_ = true;
            sync_multiplier_from_settings();
            const bool hook_installed = install_native_award_hook();
            const RuntimeExperienceMultiplierStatus current = status();
            Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Runtime EXP multiplier initialized "
                                               "selected={} seed_default={} applied={} "
                                               "route=native_award_before_game_modifiers "
                                               "build_stamp={}\n"),
                                           multiplier_text(current.multiplier_half_steps),
                                           multiplier_text(current.seed_multiplier_half_steps),
                                           hook_installed, config_.build_stamp);
        }

        auto tick() -> void
        {
            const uint64_t observed = observed_award_count_.load(std::memory_order_acquire);
            if (observed == reported_award_count_)
            {
                return;
            }

            const uint64_t new_events = observed - reported_award_count_;
            reported_award_count_ = observed;
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Runtime EXP award intercepted "
                    "events={} total_events={} base_exp={} "
                    "forwarded_exp={} multiplier={} "
                    "storefront={} "
                    "route=native_award_before_game_modifiers\n"),
                new_events, observed, last_base_experience_.load(std::memory_order_relaxed),
                last_forwarded_experience_.load(std::memory_order_relaxed),
                multiplier_text(
                    last_runtime_multiplier_half_steps_.load(std::memory_order_relaxed)),
                storefront_.empty() ? std::wstring_view{STR("<none>")} : storefront_);
        }

        [[nodiscard]] auto status() const -> RuntimeExperienceMultiplierStatus
        {
            const RuntimeGoaSettingsStatus settings = config_.goa_settings.status();
            const int32_t active_runtime =
                runtime_multiplier_half_steps_.load(std::memory_order_acquire);
            return RuntimeExperienceMultiplierStatus{
                .multiplier_half_steps = settings.experience_multiplier_half_steps,
                .seed_multiplier_half_steps = settings.seed_experience_multiplier_half_steps,
                .applied =
                    hook_installed_ && active_runtime == settings.experience_multiplier_half_steps,
            };
        }

        auto set_multiplier_half_steps(int32_t half_steps) -> bool
        {
            if (!is_supported_experience_multiplier_half_steps(half_steps))
            {
                return false;
            }
            const RuntimeExperienceMultiplierStatus previous = status();
            const bool persisted =
                config_.goa_settings.set_experience_multiplier_half_steps(half_steps);
            sync_multiplier_from_settings();
            Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] GoA setting changed "
                                               "kind=exp_multiplier previous={} multiplier={} "
                                               "persisted={} applied={} "
                                               "apply=future_exp_awards_immediately "
                                               "existing_total_unchanged=true\n"),
                                           multiplier_text(previous.multiplier_half_steps),
                                           multiplier_text(half_steps), persisted, hook_installed_);
            return true;
        }

        RuntimeExperienceMultiplierConfig config_;
        std::unique_ptr<PLH::x64Detour> detour_{};
        std::atomic_int32_t runtime_multiplier_half_steps_{experience_half_steps_per_unit};
        uintptr_t award_cap_address_{};
        std::atomic_uint64_t observed_award_count_{0};
        std::atomic_int32_t last_base_experience_{0};
        std::atomic_int32_t last_forwarded_experience_{0};
        std::atomic_int32_t last_runtime_multiplier_half_steps_{experience_half_steps_per_unit};
        uint64_t reported_award_count_{};
        uint64_t trampoline_{};
        uintptr_t target_{};
        std::wstring_view storefront_{};
        bool installed_{};
        bool hook_installed_{};

        inline static std::atomic<Impl*> hook_owner_{nullptr};
        inline static std::atomic_uintptr_t original_award_{0};
    };

    RuntimeExperienceMultiplierService::RuntimeExperienceMultiplierService(
        RuntimeExperienceMultiplierConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeExperienceMultiplierService::~RuntimeExperienceMultiplierService() = default;
    RuntimeExperienceMultiplierService::RuntimeExperienceMultiplierService(
        RuntimeExperienceMultiplierService&&) noexcept = default;
    auto
    RuntimeExperienceMultiplierService::operator=(RuntimeExperienceMultiplierService&&) noexcept
        -> RuntimeExperienceMultiplierService& = default;

    auto RuntimeExperienceMultiplierService::install_once() -> void
    {
        impl_->install_once();
    }

    auto RuntimeExperienceMultiplierService::tick() -> void
    {
        impl_->tick();
    }

    auto RuntimeExperienceMultiplierService::status() const -> RuntimeExperienceMultiplierStatus
    {
        return impl_->status();
    }

    auto RuntimeExperienceMultiplierService::set_multiplier_half_steps(int32_t half_steps) -> bool
    {
        return impl_->set_multiplier_half_steps(half_steps);
    }
} // namespace kh3ap
