#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/hooks/patches/kh3_sqex_translucent_slice_view_guard_patch.h"

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
#include <initializer_list>

namespace kh3ap
{
    namespace
    {
        // FDeferredPixelShaderParameters::Set reads View+0x2919 even though
        // its contract accepts a base FSceneView. Two captured failures used
        // valid 0x1550-byte base views with bIsViewInfo=false at +0xB31. The
        // subclass-only read crossed into an uncommitted allocator page.
        constexpr uint32_t s_b_is_view_info_offset = 0xB31;
        constexpr uint32_t s_sqex_view_info_flag_offset = 0x2919;
        constexpr size_t s_patch_site_count = 10;
        constexpr size_t s_max_site_patch_size = 17;

        enum class ViewLoadKind : uint8_t
        {
            RbpDisp8,
            RspDisp32,
        };

        struct PatchProfile
        {
            uintptr_t patch_rva{};
            ViewLoadKind view_load_kind{};
            uint8_t view_stack_offset{};
            uint8_t site_patch_size{};
            uintptr_t continuation_delta{};
            uintptr_t fallback_delta{};
        };

        struct BuildProfile
        {
            std::wstring_view storefront{};
            size_t image_size{};
            uint32_t image_timestamp{};
            uintptr_t view_info_discriminator_write_rva{};
            uintptr_t sqex_capture_cvar_pointer_rva{};
            uintptr_t fallback_texture_pointer_rva{};
            uintptr_t set_texture_parameter_rva{};
            std::array<PatchProfile, s_patch_site_count> patch_profiles{};
        };

        // KINGDOM HEARTS III 1.0.0 CL 1065258, Epic Games Store. LTCG emitted
        // ten copies of the shader specialization. Six keep View at [rbp+60]
        // and four keep it at [rbp+58].
        constexpr BuildProfile s_egs_profile{
                L"egs",
                kh3ap::native_build::egs.image_size,
                kh3ap::native_build::egs.timestamp,
                0x3A9AB11,
                0x9CAFB90,
                0x9CD8130,
                0x36D6100,
                std::array<PatchProfile, s_patch_site_count>{
                        PatchProfile{0x3982EC9, ViewLoadKind::RbpDisp8, 0x58, 17, 0x11, 0x13E},
                        PatchProfile{0x399B157, ViewLoadKind::RbpDisp8, 0x60, 17, 0x11, 0x13E},
                        PatchProfile{0x399CFF7, ViewLoadKind::RbpDisp8, 0x60, 17, 0x11, 0x13E},
                        PatchProfile{0x399EE97, ViewLoadKind::RbpDisp8, 0x60, 17, 0x11, 0x13E},
                        PatchProfile{0x39A0D37, ViewLoadKind::RbpDisp8, 0x60, 17, 0x11, 0x13E},
                        PatchProfile{0x39A2BD7, ViewLoadKind::RbpDisp8, 0x60, 17, 0x11, 0x13E},
                        PatchProfile{0x39A4A77, ViewLoadKind::RbpDisp8, 0x60, 17, 0x11, 0x13E},
                        PatchProfile{0x39A6879, ViewLoadKind::RbpDisp8, 0x58, 17, 0x11, 0x13E},
                        PatchProfile{0x39A8689, ViewLoadKind::RbpDisp8, 0x58, 17, 0x11, 0x13E},
                        PatchProfile{0x39AA499, ViewLoadKind::RbpDisp8, 0x58, 17, 0x11, 0x13E},
                },
        };

        // Steam 1.0.0 (PE timestamp 2024-06-20) emits the same ten unsafe
        // shader specializations with View at [rsp+B8/C8]. A separate eleventh
        // +0x2919 read at RVA 0x362BE09 is a legitimate FViewInfo-only path and
        // is deliberately excluded.
        constexpr BuildProfile s_steam_profile{
                L"steam",
                kh3ap::native_build::steam.image_size,
                kh3ap::native_build::steam.timestamp,
                0x35A45FD,
                0x9CDA5D0,
                0x9D28160,
                0x320BE10,
                std::array<PatchProfile, s_patch_site_count>{
                        PatchProfile{0x346BE36, ViewLoadKind::RspDisp32, 0xB8, 15, 0x11, 0x2B},
                        PatchProfile{0x346F2B6, ViewLoadKind::RspDisp32, 0xC8, 15, 0x15, 0x9D},
                        PatchProfile{0x3471D86, ViewLoadKind::RspDisp32, 0xB8, 15, 0x11, 0x2B},
                        PatchProfile{0x3475206, ViewLoadKind::RspDisp32, 0xC8, 15, 0x15, 0x9D},
                        PatchProfile{0x3477DE6, ViewLoadKind::RspDisp32, 0xC8, 15, 0x15, 0x9D},
                        PatchProfile{0x347A9C6, ViewLoadKind::RspDisp32, 0xC8, 15, 0x15, 0x9D},
                        PatchProfile{0x347D57D, ViewLoadKind::RspDisp32, 0xC8, 15, 0x15, 0x9F},
                        PatchProfile{0x3480046, ViewLoadKind::RspDisp32, 0xB8, 15, 0x11, 0x2B},
                        PatchProfile{0x34833ED, ViewLoadKind::RspDisp32, 0xC8, 15, 0x15, 0x9F},
                        PatchProfile{0x3485EB6, ViewLoadKind::RspDisp32, 0xB8, 15, 0x11, 0x2B},
                },
        };

        constexpr std::array<uint8_t, 7>
                s_expected_view_info_discriminator_write{
                        0xC6, 0x83, 0x31, 0x0B, 0x00, 0x00, 0x01,
                };

        // mov rax,[rbp/rsp+ViewStackOffset]
        // cmp byte ptr [rax+B31],0
        // jne check_view_info_flag
        // mov rax,<fallback>; jmp rax
        // check_view_info_flag:
        // cmp byte ptr [rax+2919],0
        // jne continue_original
        // mov rax,<fallback>; jmp rax
        // continue_original:
        // mov rax,<continuation>; jmp rax
        constexpr size_t s_trampoline_size = 62;

        struct ModuleMetadata
        {
            size_t image_size{};
            uint32_t image_timestamp{};
        };

        auto module_metadata_from_pe_header(HMODULE module) -> ModuleMetadata
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
            const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                    base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE ||
                nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            {
                return {};
            }
            return ModuleMetadata{
                    static_cast<size_t>(nt->OptionalHeader.SizeOfImage),
                    nt->FileHeader.TimeDateStamp,
            };
        }

        auto profile_for_module(const ModuleMetadata& metadata)
                -> const BuildProfile*
        {
            for (const BuildProfile* const profile :
                 {&s_egs_profile, &s_steam_profile})
            {
                if (metadata.image_size == profile->image_size &&
                    metadata.image_timestamp == profile->image_timestamp)
                {
                    return profile;
                }
            }
            return nullptr;
        }

        auto executable_range_is_committed(uintptr_t address, size_t size) -> bool
        {
            MEMORY_BASIC_INFORMATION memory_info{};
            if (address == 0 || size == 0 ||
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
            const bool executable = protection == PAGE_EXECUTE ||
                                    protection == PAGE_EXECUTE_READ ||
                                    protection == PAGE_EXECUTE_READWRITE ||
                                    protection == PAGE_EXECUTE_WRITECOPY;
            if (!executable)
            {
                return false;
            }
            const uintptr_t region_base =
                    reinterpret_cast<uintptr_t>(memory_info.BaseAddress);
            const uintptr_t region_end = region_base + memory_info.RegionSize;
            return address >= region_base && address < region_end &&
                   size <= region_end - address;
        }

        auto bytes_match(
                uintptr_t address,
                const uint8_t* expected,
                size_t size) -> bool
        {
            return expected && executable_range_is_committed(address, size) &&
                   std::memcmp(
                           reinterpret_cast<const void*>(address),
                           expected,
                           size) == 0;
        }

        template <size_t Size>
        auto bytes_match(
                uintptr_t address,
                const std::array<uint8_t, Size>& expected) -> bool
        {
            return bytes_match(address, expected.data(), expected.size());
        }

        auto relative_target(
                uintptr_t instruction_address,
                size_t instruction_size,
                size_t displacement_offset) -> uintptr_t
        {
            int32_t displacement = 0;
            std::memcpy(
                    &displacement,
                    reinterpret_cast<const void*>(
                            instruction_address + displacement_offset),
                    sizeof(displacement));
            const intptr_t next_instruction = static_cast<intptr_t>(
                    instruction_address + instruction_size);
            return static_cast<uintptr_t>(next_instruction + displacement);
        }

        auto conditional_jump_target_matches(
                uintptr_t instruction_address,
                uintptr_t expected_target) -> bool
        {
            if (!executable_range_is_committed(instruction_address, 6))
            {
                return false;
            }

            const auto* const bytes = reinterpret_cast<const uint8_t*>(
                    instruction_address);
            if (bytes[0] == 0x74)
            {
                const auto displacement = static_cast<int8_t>(bytes[1]);
                return static_cast<uintptr_t>(
                               static_cast<intptr_t>(instruction_address + 2) +
                               displacement) == expected_target;
            }
            return bytes[0] == 0x0F && bytes[1] == 0x84 &&
                   relative_target(instruction_address, 6, 2) ==
                           expected_target;
        }

        struct SiteBytes
        {
            std::array<uint8_t, s_max_site_patch_size> bytes{};
            size_t size{};
        };

        auto make_original_site_bytes(const PatchProfile& profile) -> SiteBytes
        {
            SiteBytes site{};
            size_t cursor = 0;
            const auto emit = [&](uint8_t value) {
                site.bytes[cursor++] = value;
            };

            if (profile.view_load_kind == ViewLoadKind::RbpDisp8)
            {
                emit(0x48);
                emit(0x8B);
                emit(0x45);
                emit(profile.view_stack_offset);
            }
            else
            {
                emit(0x48);
                emit(0x8B);
                emit(0x84);
                emit(0x24);
                emit(profile.view_stack_offset);
                emit(0x00);
                emit(0x00);
                emit(0x00);
            }

            emit(0x80);
            emit(0xB8);
            emit(0x19);
            emit(0x29);
            emit(0x00);
            emit(0x00);
            emit(0x00);

            if (profile.site_patch_size > cursor)
            {
                const uintptr_t branch_rva =
                        profile.patch_rva + cursor;
                const uintptr_t continuation_rva =
                        profile.patch_rva + profile.continuation_delta;
                const uintptr_t fallback_rva =
                        profile.patch_rva + profile.fallback_delta;
                if (continuation_rva - branch_rva == 6)
                {
                    emit(0x0F);
                    emit(0x84);
                    const int32_t displacement = static_cast<int32_t>(
                            fallback_rva - continuation_rva);
                    std::memcpy(
                            site.bytes.data() + cursor,
                            &displacement,
                            sizeof(displacement));
                    cursor += sizeof(displacement);
                }
            }

            site.size = cursor;
            return site;
        }

        auto continuation_signature_matches(
                uintptr_t continuation_address,
                uintptr_t fallback_address,
                uintptr_t module_base,
                const BuildProfile& build_profile) -> bool
        {
            constexpr std::array<uint8_t, 4> cvar_value_check{
                    0x83, 0x78, 0x04, 0x00,
            };
            if (!executable_range_is_committed(continuation_address, 17))
            {
                return false;
            }

            const auto* const bytes = reinterpret_cast<const uint8_t*>(
                    continuation_address);
            return bytes[0] == 0x48 && bytes[1] == 0x8B && bytes[2] == 0x05 &&
                   relative_target(continuation_address, 7, 3) ==
                           module_base +
                                   build_profile.
                                           sqex_capture_cvar_pointer_rva &&
                   std::memcmp(
                           bytes + 7,
                           cvar_value_check.data(),
                           cvar_value_check.size()) == 0 &&
                   conditional_jump_target_matches(
                           continuation_address + 11,
                           fallback_address);
        }

        auto fallback_signature_matches(
                uintptr_t fallback_address,
                uintptr_t module_base,
                const BuildProfile& build_profile) -> bool
        {
            if (!executable_range_is_committed(fallback_address, 16))
            {
                return false;
            }

            const auto* const bytes = reinterpret_cast<const uint8_t*>(
                    fallback_address);
            return bytes[0] == 0x48 && bytes[1] == 0x8B && bytes[2] == 0x05 &&
                   relative_target(fallback_address, 7, 3) ==
                           module_base +
                                   build_profile.fallback_texture_pointer_rva &&
                   (bytes[7] == 0x48 || bytes[7] == 0x4C) &&
                   bytes[8] == 0x8B && (bytes[9] & 0xC7) == 0x40 &&
                   bytes[10] == 0x30 &&
                   bytes[11] == 0xE8 &&
                   relative_target(fallback_address + 11, 5, 1) ==
                           module_base +
                                   build_profile.set_texture_parameter_rva;
        }

        auto replace_code_bytes(
                uintptr_t address,
                const uint8_t* expected,
                const uint8_t* replacement,
                size_t size) -> bool
        {
            if (!expected || !replacement ||
                !executable_range_is_committed(address, size))
            {
                return false;
            }

            DWORD original_protection = 0;
            if (!VirtualProtect(
                        reinterpret_cast<void*>(address),
                        size,
                        PAGE_EXECUTE_READWRITE,
                        &original_protection))
            {
                return false;
            }

            const auto restore_protection = [&]() {
                DWORD ignored = 0;
                return VirtualProtect(
                               reinterpret_cast<void*>(address),
                               size,
                               original_protection,
                               &ignored) != FALSE;
            };

            if (std::memcmp(
                        reinterpret_cast<const void*>(address),
                        expected,
                        size) != 0)
            {
                restore_protection();
                return false;
            }

            std::memcpy(reinterpret_cast<void*>(address), replacement, size);
            const bool replacement_matches =
                    std::memcmp(
                            reinterpret_cast<const void*>(address),
                            replacement,
                            size) == 0;
            const bool cache_flushed =
                    FlushInstructionCache(
                            GetCurrentProcess(),
                            reinterpret_cast<const void*>(address),
                            size) != FALSE;
            const bool protection_restored = restore_protection();
            if (replacement_matches && cache_flushed && protection_restored)
            {
                return true;
            }

            DWORD rollback_protection = 0;
            if (VirtualProtect(
                        reinterpret_cast<void*>(address),
                        size,
                        PAGE_EXECUTE_READWRITE,
                        &rollback_protection))
            {
                std::memcpy(reinterpret_cast<void*>(address), expected, size);
                FlushInstructionCache(
                        GetCurrentProcess(),
                        reinterpret_cast<const void*>(address),
                        size);
                DWORD ignored = 0;
                VirtualProtect(
                        reinterpret_cast<void*>(address),
                        size,
                        original_protection,
                        &ignored);
            }
            return false;
        }

        auto make_trampoline_bytes(
                const PatchProfile& profile,
                uintptr_t fallback_address,
                uintptr_t continuation_address)
                -> std::array<uint8_t, s_trampoline_size>
        {
            static_assert(sizeof(uintptr_t) == sizeof(uint64_t));
            std::array<uint8_t, s_trampoline_size> bytes{};
            bytes.fill(0xCC);
            size_t cursor = 0;
            const auto emit = [&](uint8_t value) {
                bytes[cursor++] = value;
            };
            const auto emit_absolute_jump = [&](uintptr_t target) {
                emit(0x48);
                emit(0xB8);
                std::memcpy(
                        bytes.data() + cursor,
                        &target,
                        sizeof(target));
                cursor += sizeof(target);
                emit(0xFF);
                emit(0xE0);
            };

            if (profile.view_load_kind == ViewLoadKind::RbpDisp8)
            {
                emit(0x48);
                emit(0x8B);
                emit(0x45);
                emit(profile.view_stack_offset);
            }
            else
            {
                emit(0x48);
                emit(0x8B);
                emit(0x84);
                emit(0x24);
                emit(profile.view_stack_offset);
                emit(0x00);
                emit(0x00);
                emit(0x00);
            }

            // Reject base FSceneView before touching the subclass-only field.
            emit(0x80);
            emit(0xB8);
            emit(0x31);
            emit(0x0B);
            emit(0x00);
            emit(0x00);
            emit(0x00);
            emit(0x75);
            emit(0x0C);
            emit_absolute_jump(fallback_address);

            // Preserve the original Square flag check for real FViewInfo.
            emit(0x80);
            emit(0xB8);
            emit(0x19);
            emit(0x29);
            emit(0x00);
            emit(0x00);
            emit(0x00);
            emit(0x75);
            emit(0x0C);
            emit_absolute_jump(fallback_address);
            emit_absolute_jump(continuation_address);
            return bytes;
        }

        auto make_site_patch(uintptr_t trampoline_address)
                -> std::array<uint8_t, s_max_site_patch_size>
        {
            std::array<uint8_t, s_max_site_patch_size> bytes{};
            bytes.fill(0x90);
            bytes[0] = 0x48;
            bytes[1] = 0xB8;
            std::memcpy(
                    bytes.data() + 2,
                    &trampoline_address,
                    sizeof(trampoline_address));
            bytes[10] = 0xFF;
            bytes[11] = 0xE0;
            return bytes;
        }
    }

    Kh3SqexTranslucentSliceViewGuardPatch::
            ~Kh3SqexTranslucentSliceViewGuardPatch()
    {
        uninstall(STR("member_destructor"));
    }

    auto Kh3SqexTranslucentSliceViewGuardPatch::install(
            std::wstring_view build_stamp) -> bool
    {
        static_assert(patch_site_count == s_patch_site_count);
        static_assert(max_site_patch_size == s_max_site_patch_size);
        if (installed_)
        {
            return true;
        }

        build_stamp_ = build_stamp;
        HMODULE const game_module = GetModuleHandleW(nullptr);
        const uintptr_t module_base = reinterpret_cast<uintptr_t>(game_module);
        const ModuleMetadata module_metadata =
                module_metadata_from_pe_header(game_module);
        const BuildProfile* const build_profile =
                profile_for_module(module_metadata);
        if (module_base == 0 || !build_profile)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] SQEX translucent-slice view guard skipped reason=unsupported_image image_size=0x{:X} image_timestamp=0x{:X} egs_expected_size=0x{:X} egs_expected_timestamp=0x{:X} steam_expected_size=0x{:X} steam_expected_timestamp=0x{:X} build_stamp={}\n"),
                    module_metadata.image_size,
                    module_metadata.image_timestamp,
                    s_egs_profile.image_size,
                    s_egs_profile.image_timestamp,
                    s_steam_profile.image_size,
                    s_steam_profile.image_timestamp,
                    build_stamp_);
            return false;
        }
        storefront_ = std::wstring(build_profile->storefront);

        const uintptr_t discriminator_write_address =
                module_base +
                build_profile->view_info_discriminator_write_rva;
        if (!bytes_match(
                    discriminator_write_address,
                    s_expected_view_info_discriminator_write))
        {
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] SQEX translucent-slice view guard failed reason=discriminator_signature_mismatch storefront={} discriminator_rva=0x{:X} build_stamp={}\n"),
                    storefront_,
                    build_profile->view_info_discriminator_write_rva,
                    build_stamp_);
            return false;
        }

        std::array<std::array<uint8_t, s_max_site_patch_size>,
                   patch_site_count>
                original_site_bytes{};
        std::array<size_t, patch_site_count> site_patch_sizes{};
        for (size_t site_index = 0;
             site_index < build_profile->patch_profiles.size();
             ++site_index)
        {
            const auto& site_profile =
                    build_profile->patch_profiles[site_index];
            const uintptr_t patch_address =
                    module_base + site_profile.patch_rva;
            const uintptr_t continuation_address =
                    patch_address + site_profile.continuation_delta;
            const uintptr_t fallback_address =
                    patch_address + site_profile.fallback_delta;
            const size_t view_load_size =
                    site_profile.view_load_kind == ViewLoadKind::RbpDisp8
                            ? 4
                            : 8;
            const uintptr_t original_branch_address =
                    patch_address + view_load_size + 7;
            const SiteBytes original_site =
                    make_original_site_bytes(site_profile);
            original_site_bytes[site_index] = original_site.bytes;
            site_patch_sizes[site_index] = original_site.size;
            const bool generated_size_matches =
                    original_site.size == site_profile.site_patch_size;
            const bool site_matches =
                    generated_size_matches &&
                    bytes_match(
                            patch_address,
                            original_site.bytes.data(),
                            original_site.size);
            const bool branch_matches = conditional_jump_target_matches(
                    original_branch_address,
                    fallback_address);
            const bool continuation_matches = continuation_signature_matches(
                    continuation_address,
                    fallback_address,
                    module_base,
                    *build_profile);
            const bool fallback_matches = fallback_signature_matches(
                    fallback_address,
                    module_base,
                    *build_profile);
            if (site_matches && branch_matches && continuation_matches &&
                fallback_matches)
            {
                continue;
            }

            RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] SQEX translucent-slice view guard failed reason=site_signature_mismatch storefront={} site={} patch_rva=0x{:X} stack_offset=0x{:X} load_kind={} expected_patch_size={} generated_patch_size={} site_match={} branch_match={} continuation_match={} fallback_match={} build_stamp={}\n"),
                    storefront_,
                    site_index,
                    site_profile.patch_rva,
                    site_profile.view_stack_offset,
                    static_cast<uint32_t>(site_profile.view_load_kind),
                    site_profile.site_patch_size,
                    original_site.size,
                    site_matches,
                    branch_matches,
                    continuation_matches,
                    fallback_matches,
                    build_stamp_);
            return false;
        }

        constexpr size_t trampoline_block_size =
                s_trampoline_size * patch_site_count;
        void* const trampoline_block = VirtualAlloc(
                nullptr,
                trampoline_block_size,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_READWRITE);
        if (!trampoline_block)
        {
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] SQEX translucent-slice view guard failed reason=trampoline_allocation storefront={} site_count={} build_stamp={}\n"),
                    storefront_,
                    patch_site_count,
                    build_stamp_);
            return false;
        }

        auto* const trampoline_base = static_cast<uint8_t*>(trampoline_block);
        std::array<std::array<uint8_t, s_max_site_patch_size>,
                   patch_site_count>
                site_patch_bytes{};
        for (size_t site_index = 0;
             site_index < build_profile->patch_profiles.size();
             ++site_index)
        {
            const auto& site_profile =
                    build_profile->patch_profiles[site_index];
            const uintptr_t patch_address =
                    module_base + site_profile.patch_rva;
            const uintptr_t continuation_address =
                    patch_address + site_profile.continuation_delta;
            const uintptr_t fallback_address =
                    patch_address + site_profile.fallback_delta;
            const auto trampoline_bytes = make_trampoline_bytes(
                    site_profile,
                    fallback_address,
                    continuation_address);
            uint8_t* const trampoline_address =
                    trampoline_base + (site_index * s_trampoline_size);
            std::memcpy(
                    trampoline_address,
                    trampoline_bytes.data(),
                    trampoline_bytes.size());
            site_patch_bytes[site_index] = make_site_patch(
                    reinterpret_cast<uintptr_t>(trampoline_address));
        }

        DWORD previous_protection = 0;
        const bool trampoline_protected =
                VirtualProtect(
                        trampoline_block,
                        trampoline_block_size,
                        PAGE_EXECUTE_READ,
                        &previous_protection) != FALSE;
        const bool trampoline_cache_flushed =
                trampoline_protected &&
                FlushInstructionCache(
                        GetCurrentProcess(),
                        trampoline_block,
                        trampoline_block_size) != FALSE;
        if (!trampoline_protected || !trampoline_cache_flushed)
        {
            VirtualFree(trampoline_block, 0, MEM_RELEASE);
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] SQEX translucent-slice view guard failed reason=trampoline_finalize storefront={} protection={} cache_flush={} build_stamp={}\n"),
                    storefront_,
                    trampoline_protected,
                    trampoline_cache_flushed,
                    build_stamp_);
            return false;
        }

        std::array<bool, patch_site_count> applied_sites{};
        for (size_t site_index = 0;
             site_index < build_profile->patch_profiles.size();
             ++site_index)
        {
            const uintptr_t patch_address =
                    module_base +
                    build_profile->patch_profiles[site_index].patch_rva;
            if (replace_code_bytes(
                        patch_address,
                        original_site_bytes[site_index].data(),
                        site_patch_bytes[site_index].data(),
                        site_patch_sizes[site_index]))
            {
                applied_sites[site_index] = true;
                continue;
            }

            bool rollback_ok = true;
            for (size_t rollback = site_index; rollback > 0; --rollback)
            {
                const size_t applied_index = rollback - 1;
                if (!applied_sites[applied_index])
                {
                    continue;
                }
                const uintptr_t applied_address =
                        module_base +
                        build_profile->
                                patch_profiles[applied_index].patch_rva;
                if (replace_code_bytes(
                            applied_address,
                            site_patch_bytes[applied_index].data(),
                            original_site_bytes[applied_index].data(),
                            site_patch_sizes[applied_index]))
                {
                    applied_sites[applied_index] = false;
                }
                else
                {
                    rollback_ok = false;
                }
            }

            if (rollback_ok)
            {
                VirtualFree(trampoline_block, 0, MEM_RELEASE);
            }
            else
            {
                trampoline_ = trampoline_block;
                for (size_t applied_index = 0;
                     applied_index < patch_site_count;
                     ++applied_index)
                {
                    if (!applied_sites[applied_index])
                    {
                        continue;
                    }
                    patch_addresses_[applied_index] =
                            module_base +
                            build_profile->
                                    patch_profiles[applied_index].patch_rva;
                    installed_patch_bytes_[applied_index] =
                            site_patch_bytes[applied_index];
                    original_site_bytes_[applied_index] =
                            original_site_bytes[applied_index];
                    installed_patch_sizes_[applied_index] =
                            site_patch_sizes[applied_index];
                    site_installed_[applied_index] = true;
                    installed_ = true;
                }
            }

            RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] SQEX translucent-slice view guard failed reason=site_write storefront={} failed_site={} patch_rva=0x{:X} rollback_ok={} residual_patch_active={} build_stamp={}\n"),
                    storefront_,
                    site_index,
                    build_profile->patch_profiles[site_index].patch_rva,
                    rollback_ok,
                    installed_,
                    build_stamp_);
            return false;
        }

        trampoline_ = trampoline_block;
        for (size_t site_index = 0;
             site_index < patch_site_count;
             ++site_index)
        {
            patch_addresses_[site_index] =
                    module_base +
                    build_profile->patch_profiles[site_index].patch_rva;
            installed_patch_bytes_[site_index] = site_patch_bytes[site_index];
            original_site_bytes_[site_index] =
                    original_site_bytes[site_index];
            installed_patch_sizes_[site_index] = site_patch_sizes[site_index];
            site_installed_[site_index] = true;
        }
        installed_ = true;
        RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] SQEX translucent-slice view guard installed storefront={} image_size=0x{:X} image_timestamp=0x{:X} site_count={} first_patch_rva=0x{:X} last_patch_rva=0x{:X} b_is_view_info_offset=0x{:X} guarded_field_offset=0x{:X} trampoline=0x{:X} build_stamp={}\n"),
                storefront_,
                build_profile->image_size,
                build_profile->image_timestamp,
                patch_site_count,
                build_profile->patch_profiles.front().patch_rva,
                build_profile->patch_profiles.back().patch_rva,
                s_b_is_view_info_offset,
                s_sqex_view_info_flag_offset,
                reinterpret_cast<uintptr_t>(trampoline_),
                build_stamp_);
        return true;
    }

    auto Kh3SqexTranslucentSliceViewGuardPatch::uninstall(
            std::wstring_view reason) -> void
    {
        if (!installed_)
        {
            return;
        }

        bool restore_failed = false;
        size_t restored_count = 0;
        for (size_t remaining = patch_site_count; remaining > 0; --remaining)
        {
            const size_t site_index = remaining - 1;
            if (!site_installed_[site_index] ||
                patch_addresses_[site_index] == 0)
            {
                continue;
            }
            const size_t patch_size = installed_patch_sizes_[site_index];
            if (patch_size == 0 || patch_size > max_site_patch_size)
            {
                restore_failed = true;
                RC::Output::send<RC::LogLevel::Error>(
                        STR("[KH3APProbeCpp] SQEX translucent-slice view guard restore failed reason={} storefront={} site={} patch_address=0x{:X} invalid_patch_size={} build_stamp={}\n"),
                        reason,
                        storefront_,
                        site_index,
                        patch_addresses_[site_index],
                        patch_size,
                        build_stamp_);
                continue;
            }
            if (replace_code_bytes(
                        patch_addresses_[site_index],
                        installed_patch_bytes_[site_index].data(),
                        original_site_bytes_[site_index].data(),
                        patch_size))
            {
                site_installed_[site_index] = false;
                patch_addresses_[site_index] = 0;
                ++restored_count;
                continue;
            }

            restore_failed = true;
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[KH3APProbeCpp] SQEX translucent-slice view guard restore failed reason={} storefront={} site={} patch_address=0x{:X} build_stamp={}\n"),
                    reason,
                    storefront_,
                    site_index,
                    patch_addresses_[site_index],
                    build_stamp_);
        }
        if (restore_failed)
        {
            return;
        }

        // A render thread may already be inside one of the tiny standalone
        // trampolines while teardown restores the sites. Retain this one
        // allocation until process exit rather than risk an unload-time UAF.
        RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] SQEX translucent-slice view guard uninstalled reason={} storefront={} restored_site_count={} trampoline_retained_until_process_exit=0x{:X} build_stamp={}\n"),
                reason,
                storefront_,
                restored_count,
                reinterpret_cast<uintptr_t>(trampoline_),
                build_stamp_);
        installed_ = false;
        patch_addresses_ = {};
        installed_patch_bytes_ = {};
        original_site_bytes_ = {};
        installed_patch_sizes_ = {};
        site_installed_ = {};
        storefront_.clear();
    }

    auto Kh3SqexTranslucentSliceViewGuardPatch::installed() const -> bool
    {
        return installed_;
    }
}
