#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/features/world/save_slot_policy.h"
#include "kh3ap/hooks/patches/kh3_expanded_save_slots_patch.h"

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
        // UTresUIP_SaveLoad has a distinct autosave widget. These constants
        // size only the manual-slot list and point its wrap navigation at the
        // final manual row; they do not turn autosave into a manual slot.
        constexpr size_t s_egs_image_size = kh3ap::native_build::egs.image_size;
        constexpr size_t s_steam_image_size = kh3ap::native_build::steam.image_size;
        constexpr uint32_t s_manual_save_slot_count = save_slots::manual_count;
        constexpr uint32_t s_last_manual_save_slot_index =
                save_slots::last_manual_list_index;
        constexpr size_t s_max_signature_count = 5;
        constexpr size_t s_max_patch_site_count = 5;

        // KINGDOM HEARTS III 1.0.0 CL 1065258, Epic Games Store.
        constexpr std::array<uint8_t, 24> s_egs_scroll_range_signature{
                0x85, 0xF6, 0x75, 0x05, 0x45, 0x8B, 0xF4, 0xEB,
                0x0F, 0xB8, 0x09, 0x00, 0x00, 0x00, 0x2B, 0xC6,
                0x83, 0xF8, 0x04, 0x7D, 0x03, 0x41, 0xFF, 0xCE,
        };
        constexpr std::array<uint8_t, 31> s_egs_list_count_signature{
                0x8D, 0x71, 0xFF, 0x85, 0xC9, 0x0F, 0x44, 0xF1,
                0xC7, 0x82, 0x30, 0x01, 0x00, 0x00, 0x09, 0x00,
                0x00, 0x00, 0xBA, 0x09, 0x00, 0x00, 0x00, 0x49,
                0x8B, 0x8C, 0x24, 0xC0, 0x00, 0x00, 0x00,
        };
        constexpr std::array<uint8_t, 34> s_egs_wrap_index_signature{
                0x8B, 0x46, 0x18, 0x83, 0xF8, 0x09, 0x75, 0x44,
                0xBB, 0x08, 0x00, 0x00, 0x00, 0x44, 0x39, 0x76,
                0x20, 0x41, 0x0F, 0x45, 0xDE, 0x48, 0x8B, 0x89,
                0xE0, 0x04, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x74,
                0x69, 0x8B,
        };

        // KINGDOM HEARTS III 1.0.0 CL 1065258, Steam. Steam calls the
        // list-count setter instead of inlining its field write, so the second
        // immediate updates both the saved count and the list widget.
        constexpr std::array<uint8_t, 24> s_steam_scroll_range_signature{
                0x85, 0xF6, 0x75, 0x05, 0x45, 0x8B, 0xF4, 0xEB,
                0x0F, 0xB8, 0x09, 0x00, 0x00, 0x00, 0x2B, 0xC6,
                0x41, 0x3B, 0xC5, 0x7D, 0x03, 0x41, 0xFF, 0xCE,
        };
        constexpr std::array<uint8_t, 31> s_steam_list_count_signature{
                0x8D, 0x73, 0xFF, 0x85, 0xDB, 0x0F, 0x44, 0xF3,
                0xBA, 0x09, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xCE,
                0xE8, 0x8C, 0x5D, 0xE4, 0xFF, 0x8B, 0xD6, 0x49,
                0x8B, 0xCE, 0xE8, 0xF2, 0x4D, 0xE4, 0xFF,
        };
        constexpr std::array<uint8_t, 34> s_steam_wrap_index_signature{
                0x41, 0x8B, 0x46, 0x18, 0x83, 0xF8, 0x09, 0x75,
                0x35, 0xBB, 0x08, 0x00, 0x00, 0x00, 0x41, 0x39,
                0x76, 0x20, 0x0F, 0x45, 0xDE, 0x48, 0x8B, 0x89,
                0xE0, 0x04, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x74,
                0x4E, 0x8B,
        };
        constexpr std::array<uint8_t, 26> s_steam_save_event_signature{
                0x48, 0x05, 0x28, 0x04, 0x00, 0x00, 0x8B, 0x70,
                0x04, 0x44, 0x8B, 0x78, 0x08, 0xBA, 0x09, 0x00,
                0x00, 0x00, 0x48, 0x8B, 0xCB, 0xE8, 0x30, 0xFB,
                0xE3, 0xFF,
        };
        constexpr std::array<uint8_t, 27> s_steam_save_open_signature{
                0x44, 0x8B, 0xB0, 0x2C, 0x04, 0x00, 0x00, 0x8B,
                0xB0, 0x30, 0x04, 0x00, 0x00, 0xBA, 0x09, 0x00,
                0x00, 0x00, 0x48, 0x8B, 0xCF, 0xE8, 0x73, 0x52,
                0xE3, 0xFF, 0x41,
        };

        struct SignatureWindow
        {
            const wchar_t* name;
            uintptr_t rva;
            const uint8_t* bytes;
            size_t size;
        };

        struct ImmediatePatchSite
        {
            const wchar_t* name;
            uintptr_t rva;
            uint32_t original_value;
            uint32_t replacement_value;
        };

        struct BuildProfile
        {
            const wchar_t* storefront;
            size_t image_size;
            std::array<SignatureWindow, s_max_signature_count> signatures;
            size_t signature_count;
            std::array<ImmediatePatchSite, s_max_patch_site_count> patch_sites;
            size_t patch_site_count;
        };

        constexpr std::array<SignatureWindow, s_max_signature_count>
                s_egs_signatures{
                        SignatureWindow{
                                L"scroll_range",
                                0x1A90EA3,
                                s_egs_scroll_range_signature.data(),
                                s_egs_scroll_range_signature.size(),
                        },
                        SignatureWindow{
                                L"list_count_fields",
                                0x1A922D2,
                                s_egs_list_count_signature.data(),
                                s_egs_list_count_signature.size(),
                        },
                        SignatureWindow{
                                L"load_wrap",
                                0x1A8BC33,
                                s_egs_wrap_index_signature.data(),
                                s_egs_wrap_index_signature.size(),
                        },
                        SignatureWindow{},
                        SignatureWindow{},
                };
        constexpr std::array<ImmediatePatchSite, s_max_patch_site_count>
                s_egs_patch_sites{
                        ImmediatePatchSite{
                                L"scroll_range_count",
                                0x1A90EAD,
                                9,
                                s_manual_save_slot_count,
                        },
                        ImmediatePatchSite{
                                L"list_state_count",
                                0x1A922E0,
                                9,
                                s_manual_save_slot_count,
                        },
                        ImmediatePatchSite{
                                L"list_widget_count",
                                0x1A922E5,
                                9,
                                s_manual_save_slot_count,
                        },
                        ImmediatePatchSite{
                                L"load_wrap_last_index",
                                0x1A8BC3C,
                                8,
                                s_last_manual_save_slot_index,
                        },
                        ImmediatePatchSite{},
                };
        constexpr BuildProfile s_egs_profile{
                L"egs",
                s_egs_image_size,
                s_egs_signatures,
                3,
                s_egs_patch_sites,
                4,
        };

        constexpr std::array<SignatureWindow, s_max_signature_count>
                s_steam_signatures{
                        SignatureWindow{
                                L"scroll_range",
                                0x1704466,
                                s_steam_scroll_range_signature.data(),
                                s_steam_scroll_range_signature.size(),
                        },
                        SignatureWindow{
                                L"load_list_count",
                                0x1702F0F,
                                s_steam_list_count_signature.data(),
                                s_steam_list_count_signature.size(),
                        },
                        SignatureWindow{
                                L"load_wrap",
                                0x1709090,
                                s_steam_wrap_index_signature.data(),
                                s_steam_wrap_index_signature.size(),
                        },
                        SignatureWindow{
                                L"save_event_list_count",
                                0x1709166,
                                s_steam_save_event_signature.data(),
                                s_steam_save_event_signature.size(),
                        },
                        SignatureWindow{
                                L"save_open_list_count",
                                0x1713A23,
                                s_steam_save_open_signature.data(),
                                s_steam_save_open_signature.size(),
                        },
                };
        constexpr std::array<ImmediatePatchSite, s_max_patch_site_count>
                s_steam_patch_sites{
                        ImmediatePatchSite{
                                L"scroll_range_count",
                                0x1704470,
                                9,
                                s_manual_save_slot_count,
                        },
                        ImmediatePatchSite{
                                L"load_list_count",
                                0x1702F18,
                                9,
                                s_manual_save_slot_count,
                        },
                        ImmediatePatchSite{
                                L"save_event_list_count",
                                0x1709174,
                                9,
                                s_manual_save_slot_count,
                        },
                        ImmediatePatchSite{
                                L"save_open_list_count",
                                0x1713A31,
                                9,
                                s_manual_save_slot_count,
                        },
                        ImmediatePatchSite{
                                L"load_wrap_last_index",
                                0x170909A,
                                8,
                                s_last_manual_save_slot_index,
                        },
                };
        constexpr BuildProfile s_steam_profile{
                L"steam",
                s_steam_image_size,
                s_steam_signatures,
                5,
                s_steam_patch_sites,
                5,
        };

        auto find_build_profile(size_t image_size) -> const BuildProfile*
        {
            if (image_size == s_egs_profile.image_size)
            {
                return &s_egs_profile;
            }
            if (image_size == s_steam_profile.image_size)
            {
                return &s_steam_profile;
            }
            return nullptr;
        }
        constexpr auto immediate_bytes(uint32_t value)
                -> std::array<uint8_t, 4>
        {
            return {
                    static_cast<uint8_t>(value & 0xFF),
                    static_cast<uint8_t>((value >> 8) & 0xFF),
                    static_cast<uint8_t>((value >> 16) & 0xFF),
                    static_cast<uint8_t>((value >> 24) & 0xFF),
            };
        }

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
            const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                    base + dos->e_lfanew);
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
            if (memory_info.State != MEM_COMMIT ||
                (memory_info.Protect & PAGE_GUARD) != 0)
            {
                return false;
            }
            const DWORD protection = memory_info.Protect & 0xFF;
            return protection == PAGE_EXECUTE ||
                   protection == PAGE_EXECUTE_READ ||
                   protection == PAGE_EXECUTE_READWRITE ||
                   protection == PAGE_EXECUTE_WRITECOPY;
        }

        auto bytes_match(
                uintptr_t address,
                const uint8_t* expected,
                size_t expected_size) -> bool
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
                const std::array<uint8_t, 4>& expected,
                const std::array<uint8_t, 4>& replacement) -> bool
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

            std::memcpy(
                    reinterpret_cast<void*>(address),
                    replacement.data(),
                    replacement.size());
            const bool replacement_matches =
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
            if (replacement_matches && cache_flushed && protection_restored)
            {
                return true;
            }

            DWORD rollback_protection = 0;
            if (VirtualProtect(
                        reinterpret_cast<void*>(address),
                        expected.size(),
                        PAGE_EXECUTE_READWRITE,
                        &rollback_protection))
            {
                std::memcpy(
                        reinterpret_cast<void*>(address),
                        expected.data(),
                        expected.size());
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

    Kh3ExpandedSaveSlotsPatch::~Kh3ExpandedSaveSlotsPatch()
    {
        uninstall(STR("member_destructor"));
    }

    auto Kh3ExpandedSaveSlotsPatch::install(std::wstring_view build_stamp)
            -> bool
    {
        if (installed_)
        {
            return true;
        }

        build_stamp_ = build_stamp;
        HMODULE const game_module = GetModuleHandleW(nullptr);
        const uintptr_t module_base = reinterpret_cast<uintptr_t>(game_module);
        const size_t module_size = module_size_from_pe_header(game_module);
        const BuildProfile* const profile = find_build_profile(module_size);
        if (module_base == 0 || !profile)
        {
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] Expanded save slots patch failed reason=unsupported_image image_size=0x{:X} egs_expected=0x{:X} steam_expected=0x{:X} build_stamp={}\n"),
                    module_size,
                    s_egs_image_size,
                    s_steam_image_size,
                    build_stamp_);
            return false;
        }

        for (size_t signature_index = 0;
             signature_index < profile->signature_count;
             ++signature_index)
        {
            const auto& signature = profile->signatures[signature_index];
            if (!bytes_match(
                        module_base + signature.rva,
                        signature.bytes,
                        signature.size))
            {
                RC::Output::send<RC::LogLevel::Error>(
                        STR("[KH3APProbeCpp] Expanded save slots patch failed reason=function_signature_mismatch storefront={} signature={} signature_name={} image_offset=0x{:X} build_stamp={}\n"),
                        profile->storefront,
                        signature_index,
                        signature.name,
                        signature.rva,
                        build_stamp_);
                return false;
            }
        }

        std::array<uintptr_t, max_patch_site_count> resolved_addresses{};
        std::array<std::array<uint8_t, immediate_size>, max_patch_site_count>
                expected_bytes{};
        std::array<std::array<uint8_t, immediate_size>, max_patch_site_count>
                replacement_bytes{};
        for (size_t site_index = 0;
             site_index < profile->patch_site_count;
             ++site_index)
        {
            const auto& patch_site = profile->patch_sites[site_index];
            resolved_addresses[site_index] =
                    module_base + patch_site.rva;
            expected_bytes[site_index] =
                    immediate_bytes(patch_site.original_value);
            replacement_bytes[site_index] =
                    immediate_bytes(patch_site.replacement_value);
            if (!bytes_match(
                        resolved_addresses[site_index],
                        expected_bytes[site_index].data(),
                        expected_bytes[site_index].size()))
            {
                RC::Output::send<RC::LogLevel::Error>(
                        STR("[KH3APProbeCpp] Expanded save slots patch failed reason=immediate_mismatch storefront={} site={} site_name={} image_offset=0x{:X} expected={} replacement={} build_stamp={}\n"),
                        profile->storefront,
                        site_index,
                        patch_site.name,
                        patch_site.rva,
                        patch_site.original_value,
                        patch_site.replacement_value,
                        build_stamp_);
                return false;
            }
        }

        size_t applied_site_count = 0;
        for (;
             applied_site_count < profile->patch_site_count;
             ++applied_site_count)
        {
            if (replace_code_bytes(
                        resolved_addresses[applied_site_count],
                        expected_bytes[applied_site_count],
                        replacement_bytes[applied_site_count]))
            {
                continue;
            }

            const size_t failed_site_index = applied_site_count;
            bool rollback_ok = true;
            while (applied_site_count > 0)
            {
                --applied_site_count;
                rollback_ok = replace_code_bytes(
                                      resolved_addresses[applied_site_count],
                                      replacement_bytes[applied_site_count],
                                      expected_bytes[applied_site_count]) &&
                              rollback_ok;
            }
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] Expanded save slots patch failed reason=write storefront={} failed_site={} site_name={} image_offset=0x{:X} rollback_ok={} build_stamp={}\n"),
                    profile->storefront,
                    failed_site_index,
                    profile->patch_sites[failed_site_index].name,
                    profile->patch_sites[failed_site_index].rva,
                    rollback_ok,
                    build_stamp_);
            return false;
        }

        patch_addresses_ = resolved_addresses;
        original_bytes_ = expected_bytes;
        patched_bytes_ = replacement_bytes;
        for (size_t site_index = 0;
             site_index < profile->patch_site_count;
             ++site_index)
        {
            patch_site_names_[site_index] =
                    profile->patch_sites[site_index].name;
        }
        storefront_ = profile->storefront;
        active_patch_site_count_ = profile->patch_site_count;
        installed_ = true;
        RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] Expanded save slots patch installed storefront={} manual_slots={} autosave=preserved wrap_last_index={} site_count={} build_stamp={}\n"),
                storefront_,
                s_manual_save_slot_count,
                s_last_manual_save_slot_index,
                active_patch_site_count_,
                build_stamp_);
        return true;
    }

    auto Kh3ExpandedSaveSlotsPatch::uninstall(std::wstring_view reason) -> void
    {
        if (!installed_)
        {
            return;
        }

        bool restore_failed = false;
        for (size_t remaining = active_patch_site_count_;
             remaining > 0;
             --remaining)
        {
            const size_t site_index = remaining - 1;
            if (patch_addresses_[site_index] == 0)
            {
                continue;
            }
            if (replace_code_bytes(
                        patch_addresses_[site_index],
                        patched_bytes_[site_index],
                        original_bytes_[site_index]))
            {
                patch_addresses_[site_index] = 0;
                continue;
            }
            restore_failed = true;
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] Expanded save slots patch restore failed storefront={} reason={} site={} site_name={} address=0x{:X} build_stamp={}\n"),
                    storefront_,
                    reason,
                    site_index,
                    patch_site_names_[site_index],
                    patch_addresses_[site_index],
                    build_stamp_);
        }
        if (restore_failed)
        {
            return;
        }

        RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] Expanded save slots patch uninstalled storefront={} reason={} build_stamp={}\n"),
                storefront_,
                reason,
                build_stamp_);
        installed_ = false;
        active_patch_site_count_ = 0;
        storefront_.clear();
        patch_site_names_ = {};
        patch_addresses_ = {};
        original_bytes_ = {};
        patched_bytes_ = {};
    }

    auto Kh3ExpandedSaveSlotsPatch::installed() const -> bool
    {
        return installed_;
    }
}
