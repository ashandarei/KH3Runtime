#include "kh3ap/features/grants/keyblades/keyblade_enhancement_profile.h"

#include <array>
#include <cassert>
#include <cstdint>

int main()
{
    using kh3ap::native_keyblade_enhancement_module_image_size;
    using kh3ap::native_keyblade_enhancement_profile_for_image_size;

    assert(native_keyblade_enhancement_module_image_size(nullptr) == 0);
    assert(native_keyblade_enhancement_profile_for_image_size(0) == nullptr);
    assert(native_keyblade_enhancement_profile_for_image_size(0x12345000) == nullptr);

    const auto* const steam = native_keyblade_enhancement_profile_for_image_size(0xA969000);
    assert(steam != nullptr);
    assert(steam->storefront == L"steam");
    assert(steam->flag_write_kind ==
           kh3ap::NativeKeybladeEnhancementFlagWriteKind::NativeSetter);
    assert(steam->save_accessor_rva == 0x1522B30);
    assert(steam->enhancement_flag_write_rva == 0x0E97FE0);
    assert(steam->refresh_item_manager_rva == 0x1139A00);
    assert(steam->enhancement_flag_setter_owner_offset_from_save_data == 0x13280);
    assert(steam->enhancement_flag_words_offset_from_save_data == 0x13938);
    assert(steam->save_accessor_signature.size == 9);
    assert(steam->enhancement_flag_write_signature.size == 54);
    assert(steam->enhancement_flag_write_signature.bytes[49] == 0xB8);
    assert(steam->refresh_item_manager_signature.size == 42);

    const auto* const egs = native_keyblade_enhancement_profile_for_image_size(0xAEBF000);
    assert(egs != nullptr);
    assert(egs->storefront == L"egs");
    assert(egs->flag_write_kind ==
           kh3ap::NativeKeybladeEnhancementFlagWriteKind::DirectWordOr);
    assert(egs->save_accessor_rva == 0x1877CB0);
    assert(egs->enhancement_flag_write_rva == 0x13EF0FE);
    assert(egs->refresh_item_manager_rva == 0x13E9660);
    assert(egs->enhancement_flag_setter_owner_offset_from_save_data == 0);
    assert(egs->enhancement_flag_words_offset_from_save_data == 0x13938);
    assert(egs->save_accessor_signature.size == 9);
    assert(egs->enhancement_flag_write_signature.size == 7);
    assert(egs->enhancement_flag_write_signature.bytes[0] == 0x09);
    assert(egs->enhancement_flag_write_signature.bytes[3] == 0x38);
    assert(egs->enhancement_flag_write_signature.bytes[4] == 0x39);
    assert(egs->enhancement_flag_write_signature.bytes[5] == 0x01);
    assert(egs->enhancement_flag_write_signature.bytes[6] == 0x00);
    assert(egs->refresh_item_manager_signature.size == 41);

    std::array<uint32_t, 8> direct_words{};
    const uint64_t direct_words_address = reinterpret_cast<uint64_t>(direct_words.data());
    assert(kh3ap::try_write_native_keyblade_enhancement_flag(
            *egs, 0, 0, direct_words_address, 0));
    assert(direct_words[0] == 1);
    assert(kh3ap::try_write_native_keyblade_enhancement_flag(
            *egs, 0, 0, direct_words_address, 63));
    assert(direct_words[1] == 0x80000000u);
    assert(!kh3ap::try_write_native_keyblade_enhancement_flag(
            *egs, 0, 0, direct_words_address, -1));
    assert(!kh3ap::try_write_native_keyblade_enhancement_flag(
            *egs, 0, 0, direct_words_address, 256));
    return 0;
}
