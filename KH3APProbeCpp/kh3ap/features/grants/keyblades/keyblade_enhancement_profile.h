#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kh3ap
{
    enum class NativeKeybladeEnhancementFlagWriteKind : uint8_t
    {
        NativeSetter,
        DirectWordOr,
    };

    struct NativeKeybladeEnhancementSignature
    {
        const uint8_t* bytes{};
        size_t size{};
    };

    struct NativeKeybladeEnhancementProfile
    {
        std::wstring_view storefront{};
        size_t image_size{};
        NativeKeybladeEnhancementFlagWriteKind flag_write_kind{
                NativeKeybladeEnhancementFlagWriteKind::NativeSetter};
        size_t save_accessor_rva{};
        // NativeSetter profiles call this RVA. DirectWordOr profiles use it
        // only as a byte-signature guard for the game's equivalent inline OR.
        size_t enhancement_flag_write_rva{};
        size_t refresh_item_manager_rva{};
        size_t enhancement_flag_setter_owner_offset_from_save_data{};
        size_t enhancement_flag_words_offset_from_save_data{};
        NativeKeybladeEnhancementSignature save_accessor_signature{};
        NativeKeybladeEnhancementSignature enhancement_flag_write_signature{};
        NativeKeybladeEnhancementSignature refresh_item_manager_signature{};
    };

    auto native_keyblade_enhancement_module_image_size(HMODULE module) noexcept -> size_t;

    auto native_keyblade_enhancement_profile_for_image_size(size_t image_size) noexcept
            -> const NativeKeybladeEnhancementProfile*;

    auto native_keyblade_enhancement_profile_for_module(HMODULE module) noexcept
            -> const NativeKeybladeEnhancementProfile*;

    auto try_write_native_keyblade_enhancement_flag(
            const NativeKeybladeEnhancementProfile& profile,
            uint64_t module_base,
            uint64_t setter_owner,
            uint64_t flag_words_address,
            int32_t flag_index) noexcept -> bool;
}
