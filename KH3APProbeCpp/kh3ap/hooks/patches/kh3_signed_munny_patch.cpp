#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/hooks/patches/kh3_signed_munny_patch.h"

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

namespace kh3ap
{
    namespace
    {
        // KINGDOM HEARTS III 1.0.0 CL 1065258 storefront image profiles.
        // Steam keeps AddMunny as one native routine. EGS's LTCG image inlines
        // the current/lifetime balance mutation into two authoritative paths,
        // so both EGS sites must validate before either one is patched.
        constexpr size_t s_steam_image_size = kh3ap::native_build::steam.image_size;
        constexpr size_t s_egs_image_size = kh3ap::native_build::egs.image_size;
        constexpr uintptr_t s_steam_add_munny_rva = 0xE866A0;
        constexpr size_t s_lower_bound_branch_offset = 0x37;
        constexpr std::array<uint8_t, 77> s_expected_add_munny_function{
                0x33, 0xC0, 0x44, 0x8B, 0xCA, 0x85, 0xD2, 0x44, 0x0F, 0x4E, 0xC8,
                0x44, 0x03, 0x89, 0x00, 0x86, 0x00, 0x00, 0x79, 0x05, 0x44, 0x8B,
                0xC0, 0xEB, 0x0D, 0x41, 0xB8, 0xFF, 0xC9, 0x9A, 0x3B, 0x45, 0x3B,
                0xC8, 0x45, 0x0F, 0x4C, 0xC1, 0x44, 0x89, 0x81, 0x00, 0x86, 0x00,
                0x00, 0x44, 0x8B, 0x81, 0x04, 0x7E, 0x00, 0x00, 0x41, 0x03, 0xD0,
                0x78, 0x0A, 0xB8, 0x3F, 0x42, 0x0F, 0x00, 0x3B, 0xD0, 0x0F, 0x4C,
                0xC2, 0x89, 0x81, 0x04, 0x7E, 0x00, 0x00, 0x41, 0x2B, 0xC0, 0xC3,
        };
        constexpr uintptr_t s_egs_add_munny_direct_site_rva = 0x1332F65;
        constexpr size_t s_egs_direct_lower_bound_branch_offset = 0x37;
        constexpr std::array<uint8_t, 75> s_expected_egs_add_munny_direct_site{
                0x8B, 0xC6, 0x33, 0xFF, 0x85, 0xF6, 0x0F, 0x4E, 0xC7, 0x01,
                0x81, 0x00, 0x86, 0x00, 0x00, 0x8B, 0x91, 0x00, 0x86, 0x00,
                0x00, 0x79, 0x04, 0x8B, 0xC7, 0xEB, 0x0A, 0xB8, 0xFF, 0xC9,
                0x9A, 0x3B, 0x3B, 0xD0, 0x0F, 0x4C, 0xC2, 0x89, 0x81, 0x00,
                0x86, 0x00, 0x00, 0x01, 0xB1, 0x04, 0x7E, 0x00, 0x00, 0x8B,
                0x91, 0x04, 0x7E, 0x00, 0x00, 0x78, 0x0C, 0xB8, 0x3F, 0x42,
                0x0F, 0x00, 0x3B, 0xD0, 0x0F, 0x4C, 0xC2, 0x8B, 0xF8, 0x89,
                0xB9, 0x04, 0x7E, 0x00, 0x00,
        };
        constexpr uintptr_t s_egs_add_munny_exec_site_rva = 0x25670CD;
        constexpr size_t s_egs_exec_lower_bound_branch_offset = 0x35;
        constexpr std::array<uint8_t, 73> s_expected_egs_add_munny_exec_site{
                0x85, 0xF6, 0x8B, 0xC6, 0x0F, 0x4E, 0xC7, 0x01, 0x81, 0x00,
                0x86, 0x00, 0x00, 0x8B, 0x91, 0x00, 0x86, 0x00, 0x00, 0x79,
                0x04, 0x8B, 0xC7, 0xEB, 0x0A, 0xB8, 0xFF, 0xC9, 0x9A, 0x3B,
                0x3B, 0xD0, 0x0F, 0x4C, 0xC2, 0x89, 0x81, 0x00, 0x86, 0x00,
                0x00, 0x01, 0xB1, 0x04, 0x7E, 0x00, 0x00, 0x8B, 0x91, 0x04,
                0x7E, 0x00, 0x00, 0x78, 0x0C, 0xB8, 0x3F, 0x42, 0x0F, 0x00,
                0x3B, 0xD0, 0x0F, 0x4C, 0xC2, 0x8B, 0xF8, 0x89, 0xB9, 0x04,
                0x7E, 0x00, 0x00,
        };
        constexpr std::array<uint8_t, 2> s_original_lower_bound_branch{0x78, 0x0A};
        constexpr std::array<uint8_t, 2> s_egs_original_lower_bound_branch{0x78, 0x0C};
        constexpr std::array<uint8_t, 2> s_signed_balance_patch{0x90, 0x90};

        struct ResolvedPatchSite
        {
            uintptr_t branch_address{0};
            std::array<uint8_t, 2> original_bytes{};
        };

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

        auto bytes_match(uintptr_t address, const uint8_t* expected, size_t expected_size) -> bool
        {
            return address != 0 && expected && expected_size > 0 &&
                   address_is_executable(address) &&
                   std::memcmp(
                           reinterpret_cast<const void*>(address),
                           expected,
                           expected_size) == 0;
        }

        auto replace_code_bytes(
                uintptr_t address,
                const std::array<uint8_t, 2>& expected,
                const std::array<uint8_t, 2>& replacement) -> bool
        {
            if (!address_is_executable(address))
            {
                return false;
            }

            DWORD original_protection = 0;
            if (!VirtualProtect(
                        reinterpret_cast<void*>(address),
                        replacement.size(),
                        PAGE_EXECUTE_READWRITE,
                        &original_protection))
            {
                return false;
            }

            const auto restore_protection = [&]() {
                DWORD ignored = 0;
                return VirtualProtect(
                               reinterpret_cast<void*>(address),
                               replacement.size(),
                               original_protection,
                               &ignored) != FALSE;
            };

            if (std::memcmp(
                        reinterpret_cast<const void*>(address),
                        expected.data(),
                        expected.size()) != 0)
            {
                restore_protection();
                return false;
            }

            std::memcpy(reinterpret_cast<void*>(address), replacement.data(), replacement.size());
            const bool bytes_match =
                    std::memcmp(
                            reinterpret_cast<const void*>(address),
                            replacement.data(),
                            replacement.size()) == 0;
            const bool cache_flushed =
                    FlushInstructionCache(
                            GetCurrentProcess(),
                            reinterpret_cast<const void*>(address),
                            replacement.size()) != FALSE;
            const bool protection_restored = restore_protection();
            if (bytes_match && cache_flushed && protection_restored)
            {
                return true;
            }

            // A partially completed code write is not an acceptable state.
            // Reacquire write access and put the validated original bytes back.
            DWORD rollback_protection = 0;
            if (VirtualProtect(
                        reinterpret_cast<void*>(address),
                        expected.size(),
                        PAGE_EXECUTE_READWRITE,
                        &rollback_protection))
            {
                std::memcpy(reinterpret_cast<void*>(address), expected.data(), expected.size());
                FlushInstructionCache(
                        GetCurrentProcess(),
                        reinterpret_cast<const void*>(address),
                        expected.size());
                DWORD ignored = 0;
                VirtualProtect(
                        reinterpret_cast<void*>(address),
                        expected.size(),
                        original_protection,
                        &ignored);
            }
            return false;
        }
    }

    Kh3SignedMunnyPatch::~Kh3SignedMunnyPatch()
    {
        uninstall(STR("member_destructor"));
    }

    auto Kh3SignedMunnyPatch::install(std::wstring_view build_stamp) -> bool
    {
        if (installed_)
        {
            return true;
        }

        build_stamp_ = build_stamp;
        HMODULE const game_module = GetModuleHandleW(nullptr);
        const uintptr_t module_base = reinterpret_cast<uintptr_t>(game_module);
        const size_t module_size = module_size_from_pe_header(game_module);
        if (module_base == 0 ||
            (module_size != s_steam_image_size && module_size != s_egs_image_size))
        {
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] Signed Munny patch failed reason=unsupported_image image_size=0x{:X} steam_expected=0x{:X} egs_expected=0x{:X} build_stamp={}\n"),
                    module_size,
                    s_steam_image_size,
                    s_egs_image_size,
                    build_stamp_);
            return false;
        }

        std::wstring_view storefront{};
        std::array<ResolvedPatchSite, 2> resolved_sites{};
        size_t resolved_site_count = 0;
        if (module_size == s_steam_image_size)
        {
            storefront = STR("steam");
            const uintptr_t function_address = module_base + s_steam_add_munny_rva;
            if (!bytes_match(
                        function_address,
                        s_expected_add_munny_function.data(),
                        s_expected_add_munny_function.size()))
            {
                RC::Output::send<RC::LogLevel::Error>(
                        STR("[KH3APProbeCpp] Signed Munny patch failed reason=function_signature_mismatch storefront=steam add_munny_offset=0x{:X} build_stamp={}\n"),
                        s_steam_add_munny_rva,
                        build_stamp_);
                return false;
            }
            resolved_sites[0] = {
                    function_address + s_lower_bound_branch_offset,
                    s_original_lower_bound_branch,
            };
            resolved_site_count = 1;
        }
        else
        {
            storefront = STR("egs");
            const uintptr_t direct_site_address =
                    module_base + s_egs_add_munny_direct_site_rva;
            const uintptr_t exec_site_address =
                    module_base + s_egs_add_munny_exec_site_rva;
            const bool direct_site_matches = bytes_match(
                    direct_site_address,
                    s_expected_egs_add_munny_direct_site.data(),
                    s_expected_egs_add_munny_direct_site.size());
            const bool exec_site_matches = bytes_match(
                    exec_site_address,
                    s_expected_egs_add_munny_exec_site.data(),
                    s_expected_egs_add_munny_exec_site.size());
            if (!direct_site_matches || !exec_site_matches)
            {
                RC::Output::send<RC::LogLevel::Error>(
                        STR("[KH3APProbeCpp] Signed Munny patch failed reason=function_signature_mismatch storefront=egs direct_site_match={} exec_site_match={} direct_site_offset=0x{:X} exec_site_offset=0x{:X} build_stamp={}\n"),
                        direct_site_matches,
                        exec_site_matches,
                        s_egs_add_munny_direct_site_rva,
                        s_egs_add_munny_exec_site_rva,
                        build_stamp_);
                return false;
            }
            resolved_sites[0] = {
                    direct_site_address + s_egs_direct_lower_bound_branch_offset,
                    s_egs_original_lower_bound_branch,
            };
            resolved_sites[1] = {
                    exec_site_address + s_egs_exec_lower_bound_branch_offset,
                    s_egs_original_lower_bound_branch,
            };
            resolved_site_count = 2;
        }

        size_t applied_site_count = 0;
        for (; applied_site_count < resolved_site_count; ++applied_site_count)
        {
            const auto& site = resolved_sites[applied_site_count];
            if (replace_code_bytes(
                        site.branch_address,
                        site.original_bytes,
                        s_signed_balance_patch))
            {
                continue;
            }

            const size_t failed_site_index = applied_site_count;
            bool rollback_ok = true;
            while (applied_site_count > 0)
            {
                --applied_site_count;
                const auto& applied_site = resolved_sites[applied_site_count];
                rollback_ok = replace_code_bytes(
                                      applied_site.branch_address,
                                      s_signed_balance_patch,
                                      applied_site.original_bytes) &&
                              rollback_ok;
            }
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] Signed Munny patch failed reason=branch_write storefront={} failed_site={} branch_image_offset=0x{:X} rollback_ok={} build_stamp={}\n"),
                    storefront,
                    failed_site_index,
                    site.branch_address - module_base,
                    rollback_ok,
                    build_stamp_);
            return false;
        }

        branch_count_ = resolved_site_count;
        for (size_t site_index = 0; site_index < branch_count_; ++site_index)
        {
            branch_addresses_[site_index] = resolved_sites[site_index].branch_address;
            original_branch_bytes_[site_index] = resolved_sites[site_index].original_bytes;
        }
        installed_ = true;
        RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] Signed Munny patch installed storefront={} discovery=verified_rva_signatures site_count={} branch0_image_offset=0x{:X} branch1_image_offset=0x{:X} replacement=9090 build_stamp={}\n"),
                storefront,
                branch_count_,
                branch_addresses_[0] - module_base,
                branch_count_ > 1 ? branch_addresses_[1] - module_base : 0,
                build_stamp_);
        return true;
    }

    auto Kh3SignedMunnyPatch::uninstall(std::wstring_view reason) -> void
    {
        if (!installed_)
        {
            return;
        }

        bool restore_failed = false;
        for (size_t remaining = branch_count_; remaining > 0; --remaining)
        {
            const size_t site_index = remaining - 1;
            if (branch_addresses_[site_index] == 0)
            {
                continue;
            }
            if (replace_code_bytes(
                        branch_addresses_[site_index],
                        s_signed_balance_patch,
                        original_branch_bytes_[site_index]))
            {
                branch_addresses_[site_index] = 0;
                continue;
            }
            restore_failed = true;
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] Signed Munny patch restore failed reason={} site={} branch_address=0x{:X} build_stamp={}\n"),
                    reason,
                    site_index,
                    branch_addresses_[site_index],
                    build_stamp_);
        }
        if (restore_failed)
        {
            return;
        }

        RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] Signed Munny patch uninstalled reason={} build_stamp={}\n"),
                reason,
                build_stamp_);
        installed_ = false;
        branch_count_ = 0;
        branch_addresses_ = {};
        original_branch_bytes_ = {};
    }

    auto Kh3SignedMunnyPatch::installed() const -> bool
    {
        return installed_;
    }

    auto Kh3SignedMunnyPatch::branch_address() const -> uintptr_t
    {
        for (const uintptr_t address : branch_addresses_)
        {
            if (address != 0)
            {
                return address;
            }
        }
        return 0;
    }
}
