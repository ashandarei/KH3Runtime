#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/core/memory/memory_protection_bits.h"
#include "kh3ap/features/grants/keyblades/keyblade_enhancement_profile.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace kh3ap
{
    namespace
    {
        constexpr auto max_keyblade_enhancement_flag_index = 255;

        // These profiles are tied to the currently supported Steam and Epic
        // executable images. Every native entry point is byte-guarded before
        // use so a future game update fails closed.
        // clang-format off: retain native signature byte grouping
        constexpr std::array<uint8_t, 9> s_save_accessor_prefix{
                0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x1D};
        // clang-format on

        // clang-format off: retain native signature byte grouping
        constexpr std::array<uint8_t, 54> s_steam_set_enhancement_flag_signature{
                0x4C, 0x8B, 0xC9, 0x81, 0xFA, 0xFF, 0x00, 0x00, 0x00,
                0x77, 0x2A, 0x8B, 0xC2, 0x99, 0x83, 0xE2, 0x1F, 0x03,
                0xC2, 0x44, 0x8B, 0xC0, 0x83, 0xE0, 0x1F, 0x2B, 0xC2,
                0x41, 0xC1, 0xF8, 0x05, 0x49, 0x63, 0xC8, 0x49, 0x8D,
                0x14, 0x89, 0x8B, 0xC8, 0xB8, 0x01, 0x00, 0x00, 0x00,
                0xD3, 0xE0, 0x09, 0x82, 0xB8, 0x06, 0x00, 0x00, 0xC3};
        // clang-format on
        // EGS inlines the authoritative Forge mutation instead of exposing a
        // callable counterpart to Steam's setter. Guard the exact OR that
        // targets save_data + 0x13938 before mirroring that operation.
        // clang-format off: retain native signature byte grouping
        constexpr std::array<uint8_t, 7> s_egs_enhancement_flag_write_signature{
                0x09, 0x84, 0x95, 0x38, 0x39, 0x01, 0x00};
        // clang-format on

        // clang-format off: retain native signature byte grouping
        constexpr std::array<uint8_t, 42> s_steam_refresh_item_manager_signature{
                0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40,
                0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF,
                0x48, 0x89, 0x5C, 0x24, 0x60, 0x48, 0x89, 0x6C, 0x24, 0x68,
                0x48, 0x89, 0x74, 0x24, 0x70, 0x48, 0x89, 0x7C, 0x24, 0x78,
                0x48, 0x8B, 0xE9};
        // clang-format on
        // clang-format off: retain native signature byte grouping
        constexpr std::array<uint8_t, 41> s_egs_refresh_item_manager_signature{
                0x48, 0x8B, 0xC4, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41,
                0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0xC7, 0x40, 0xB8,
                0xFE, 0xFF, 0xFF, 0xFF, 0x48, 0x89, 0x58, 0x10, 0x48,
                0x89, 0x68, 0x18, 0x48, 0x8B, 0xF1, 0xC7, 0x40, 0x08,
                0x00, 0x00, 0x00, 0x00};
        // clang-format on

        constexpr std::array<NativeKeybladeEnhancementProfile, 2> s_profiles{{
                {
                        L"steam",
                        kh3ap::native_build::steam.image_size,
                        NativeKeybladeEnhancementFlagWriteKind::NativeSetter,
                        0x1522B30,
                        0x0E97FE0,
                        0x1139A00,
                        0x13280,
                        0x13938,
                        {s_save_accessor_prefix.data(), s_save_accessor_prefix.size()},
                        {
                                s_steam_set_enhancement_flag_signature.data(),
                                s_steam_set_enhancement_flag_signature.size(),
                        },
                        {
                                s_steam_refresh_item_manager_signature.data(),
                                s_steam_refresh_item_manager_signature.size(),
                        },
                },
                {
                        L"egs",
                        kh3ap::native_build::egs.image_size,
                        NativeKeybladeEnhancementFlagWriteKind::DirectWordOr,
                        0x1877CB0,
                        0x13EF0FE,
                        0x13E9660,
                        0,
                        0x13938,
                        {s_save_accessor_prefix.data(), s_save_accessor_prefix.size()},
                        {
                                s_egs_enhancement_flag_write_signature.data(),
                                s_egs_enhancement_flag_write_signature.size(),
                        },
                        {
                                s_egs_refresh_item_manager_signature.data(),
                                s_egs_refresh_item_manager_signature.size(),
                        },
                },
        }};
    }

    auto native_keyblade_enhancement_module_image_size(HMODULE module) noexcept -> size_t
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

    auto native_keyblade_enhancement_profile_for_image_size(size_t image_size) noexcept
            -> const NativeKeybladeEnhancementProfile*
    {
        for (const auto& profile : s_profiles)
        {
            if (profile.image_size == image_size)
            {
                return &profile;
            }
        }
        return nullptr;
    }

    auto native_keyblade_enhancement_profile_for_module(HMODULE module) noexcept
            -> const NativeKeybladeEnhancementProfile*
    {
        return native_keyblade_enhancement_profile_for_image_size(
                native_keyblade_enhancement_module_image_size(module));
    }

    auto try_write_native_keyblade_enhancement_flag(
            const NativeKeybladeEnhancementProfile& profile,
            uint64_t module_base,
            uint64_t setter_owner,
            uint64_t flag_words_address,
            int32_t flag_index) noexcept -> bool
    {
        if (flag_index < 0 || flag_index > max_keyblade_enhancement_flag_index)
        {
            return false;
        }

        if (profile.flag_write_kind == NativeKeybladeEnhancementFlagWriteKind::NativeSetter)
        {
            if (module_base == 0 || setter_owner == 0 || profile.enhancement_flag_write_rva == 0)
            {
                return false;
            }
            using SetEnhancementFlag = void (*)(void*, int32_t);
            auto* const set_flag = reinterpret_cast<SetEnhancementFlag>(
                    module_base + profile.enhancement_flag_write_rva);
            set_flag(reinterpret_cast<void*>(setter_owner), flag_index);
            return true;
        }

        if (profile.flag_write_kind != NativeKeybladeEnhancementFlagWriteKind::DirectWordOr ||
            flag_words_address == 0)
        {
            return false;
        }

        const uint64_t word_address =
                flag_words_address + static_cast<uint64_t>(flag_index / 32) * sizeof(uint32_t);
        MEMORY_BASIC_INFORMATION memory_info{};
        if (VirtualQuery(
                    reinterpret_cast<const void*>(word_address),
                    &memory_info,
                    sizeof(memory_info)) != sizeof(memory_info) ||
            memory_info.State != MEM_COMMIT ||
            (memory_info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        {
            return false;
        }
        const DWORD protection = memory_info.Protect & memory_protection::base_mode_mask;
        if (protection != PAGE_READWRITE &&
            protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READWRITE &&
            protection != PAGE_EXECUTE_WRITECOPY)
        {
            return false;
        }
        const uint64_t region_base = reinterpret_cast<uint64_t>(memory_info.BaseAddress);
        if (word_address < region_base ||
            word_address - region_base > memory_info.RegionSize ||
            sizeof(uint32_t) > memory_info.RegionSize - (word_address - region_base))
        {
            return false;
        }

        const uint32_t bit_mask = uint32_t{1} << static_cast<uint32_t>(flag_index % 32);
        InterlockedOr(
                reinterpret_cast<volatile LONG*>(word_address),
                static_cast<LONG>(bit_mask));
        return true;
    }
}
