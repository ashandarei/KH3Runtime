#include "kh02_damage_event_decoder.h"
#include "kh02_challenge_hooks.h"
#include "kh3ap/features/grants/abilities/ability_snapshot_helpers.h"
#include <Unreal/NameTypes.hpp>
#define NOMINMAX
#include <Windows.h>
#include <cstring>

namespace kh3ap
{
    namespace
    {
        constexpr int point_damage_event_type = 1'000;
        constexpr uintptr_t damage_type_test_vtable_offset = 0x10;
        constexpr uintptr_t point_damage_info_offset = 0xa8;
        constexpr std::size_t damage_info_bytes = 104;
        constexpr std::size_t damage_command_offset = 8;
        constexpr std::size_t damage_element_offset = 11;
        constexpr std::size_t damage_magic_flags_offset = 80;

        template <class Value> auto read(uintptr_t address, Value& value) -> bool
        {
            const auto bytes = try_read_process_memory_bytes(address, sizeof(value));
            if (!bytes)
            {
                return false;
            }
            std::memcpy(&value, bytes->data(), sizeof(value));
            return true;
        }
    } // namespace

    auto kh02_decode_damage_event(const void* event) -> std::optional<Kh02DamageEventMetadata>
    {
        // Validate the native type test before reading FTresDamageInfo, matching
        // the original IsOfType(1000) gate at FTresPointDamageEvent + 0xa8.
        const auto image_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        uintptr_t vtable{};
        uintptr_t type_test{};
        if (!event || !read(reinterpret_cast<uintptr_t>(event), vtable) ||
            !Kh02ChallengeHooks::contains_image_address(image_base, vtable) ||
            !read(vtable + damage_type_test_vtable_offset, type_test) ||
            !Kh02ChallengeHooks::contains_image_address(image_base, type_test) ||
            !reinterpret_cast<bool (*)(const void*, int32_t)>(type_test)(event, point_damage_event_type))
        {
            return std::nullopt;
        }
        const auto info = try_read_process_memory_bytes(reinterpret_cast<uintptr_t>(event) + point_damage_info_offset,
                                                        damage_info_bytes);
        if (!info)
        {
            return std::nullopt;
        }
        const auto* data = reinterpret_cast<const uint8_t*>(info->data());
        RC::Unreal::FName attack_name;
        std::memcpy(&attack_name, data, sizeof(attack_name));
        return Kh02DamageEventMetadata{data[damage_command_offset], data[damage_element_offset],
                                       data[damage_magic_flags_offset], attack_name.ToString()};
    }
} // namespace kh3ap
