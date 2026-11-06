#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>

namespace kh3ap::loading_runtime
{
    // EGS 1.0.0 CL1065258. All instructions through this boundary are
    // position-independent; the first RIP-relative instruction follows it.
    inline constexpr std::array<uint8_t, 31> native_load_gate_prologue{
        0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0xe9,
        0x48, 0x81, 0xec, 0xb0, 0x00, 0x00, 0x00, 0x48, 0xc7, 0x45, 0xb7, 0xfe, 0xff, 0xff, 0xff};

    inline constexpr std::array<uint8_t, 31> steam_load_gate_prologue{
        0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0xe0,
        0x48, 0x81, 0xec, 0x20, 0x01, 0x00, 0x00, 0x48, 0xc7, 0x45, 0x80, 0xfe, 0xff, 0xff, 0xff};

    inline auto native_load_gate_expected_prologue(bool steam) -> const std::array<uint8_t, 31>&
    {
        return steam ? steam_load_gate_prologue : native_load_gate_prologue;
    }

    inline auto native_load_gate_stolen_size_valid(size_t size) -> bool
    {
        // Instruction boundaries at or beyond PolyHook's six-byte minimum.
        constexpr std::array<size_t, 6> boundaries{7, 9, 11, 16, 23, 31};
        return std::find(boundaries.begin(), boundaries.end(), size) != boundaries.end();
    }

    inline constexpr size_t native_load_gate_indirect_jump_size = 6;

    inline auto native_load_gate_jump_holder_offset(size_t stolen_size, size_t trampoline_size,
                                                    std::span<const uint8_t> jump) -> std::optional<size_t>
    {
        // The pinned detour stores its allocation extent as uint16_t. Decode
        // only its RIP-indirect jump, with the destination inside that extent.
        if (!native_load_gate_stolen_size_valid(stolen_size) ||
            trampoline_size > std::numeric_limits<uint16_t>::max() ||
            trampoline_size < stolen_size + native_load_gate_indirect_jump_size + sizeof(uint64_t) ||
            jump.size() != native_load_gate_indirect_jump_size || jump[0] != 0xff || jump[1] != 0x25)
        {
            return std::nullopt;
        }
        int32_t displacement{};
        std::memcpy(&displacement, jump.data() + 2, sizeof(displacement));
        const auto holder = static_cast<int64_t>(stolen_size + native_load_gate_indirect_jump_size) + displacement;
        if (holder < 0 || static_cast<size_t>(holder) > trampoline_size - sizeof(uint64_t))
        {
            return std::nullopt;
        }
        return static_cast<size_t>(holder);
    }

    inline auto native_load_gate_bytes_match(size_t stolen_size, std::span<const uint8_t> current_patch,
                                             std::span<const uint8_t> installed_patch,
                                             std::span<const uint8_t> trampoline_prefix, uintptr_t resume_target,
                                             uintptr_t original_entry,
                                             std::span<const uint8_t> expected = native_load_gate_prologue) -> bool
    {
        if (!native_load_gate_stolen_size_valid(stolen_size) ||
            current_patch.size() != expected.size() || expected.size() != native_load_gate_prologue.size() ||
            installed_patch.size() != current_patch.size() || trampoline_prefix.size() < stolen_size ||
            resume_target != original_entry + stolen_size ||
            !std::equal(current_patch.begin(), current_patch.end(), installed_patch.begin()))
        {
            return false;
        }
        return std::equal(trampoline_prefix.begin(), trampoline_prefix.begin() + stolen_size,
                          expected.begin()) &&
               std::equal(current_patch.begin() + stolen_size, current_patch.end(),
                          expected.begin() + stolen_size);
    }
} // namespace kh3ap::loading_runtime
