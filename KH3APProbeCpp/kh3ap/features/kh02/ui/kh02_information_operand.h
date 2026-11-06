#pragma once
#include "kh3ap/features/kh02/compatibility/kh02_compatibility_protocol.h"
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace kh3ap
{
    inline constexpr std::size_t kh02_information_boolean_offset = kh02compat::vm::reference_operand_bytes;
    inline constexpr std::size_t kh02_information_end_offset = kh02_information_boolean_offset + 1;
    inline constexpr std::size_t kh02_information_operand_bytes = kh02_information_end_offset + 1;

    struct Kh02InformationOperand
    {
        uintptr_t local_property;
        bool auto_close;
    };

    inline auto kh02_information_operand(std::span<const uint8_t> bytes) -> std::optional<Kh02InformationOperand>
    {
        if (bytes.size() < kh02_information_operand_bytes || bytes[0] != kh02compat::opcode::local_variable ||
            (bytes[kh02_information_boolean_offset] != kh02compat::opcode::true_constant &&
             bytes[kh02_information_boolean_offset] != kh02compat::opcode::false_constant) ||
            bytes[kh02_information_end_offset] != kh02compat::opcode::end_parameters)
        {
            return std::nullopt;
        }
        uintptr_t property{};
        std::memcpy(&property, bytes.data() + 1, sizeof(property));
        return Kh02InformationOperand{property,
                                      bytes[kh02_information_boolean_offset] == kh02compat::opcode::true_constant};
    }
} // namespace kh3ap
