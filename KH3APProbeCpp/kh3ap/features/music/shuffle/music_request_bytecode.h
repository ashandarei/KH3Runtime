#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace kh3ap
{
    namespace music_request_bytecode
    {
        inline constexpr uint8_t ex_soft_object_const = 0x67;
        inline constexpr uint8_t ex_string_const = 0x1f;
        inline constexpr uint8_t ex_end_function_parms = 0x16;
        inline constexpr std::size_t minimum_literal_bytes = 4;
        inline constexpr std::size_t asset_path_offset = 2;
        inline constexpr std::size_t maximum_literal_scan_bytes = 1024;
        inline constexpr uint8_t minimum_path_character = 0x20;
        inline constexpr uint8_t maximum_path_character = 0x7e;
    } // namespace music_request_bytecode

    // Verified KH3 gameflow SetPath call: EX_SoftObjectConst,
    // EX_StringConst, a null-terminated asset path, EX_EndFunctionParms.
    // Unknown expressions remain entirely owned by the engine.
    inline auto music_request_literal(std::span<const uint8_t> code) -> std::optional<std::wstring>
    {
        if (code.size() < music_request_bytecode::minimum_literal_bytes ||
            code[0] != music_request_bytecode::ex_soft_object_const ||
            code[1] != music_request_bytecode::ex_string_const)
        {
            return std::nullopt;
        }
        std::wstring path;
        for (size_t index = music_request_bytecode::asset_path_offset;
             index < code.size() && index < music_request_bytecode::maximum_literal_scan_bytes;
             ++index)
        {
            if (code[index] == 0)
            {
                return index + 1 < code.size() &&
                               code[index + 1] == music_request_bytecode::ex_end_function_parms &&
                               path.starts_with(L"/Game/Sound/")
                           ? std::optional{std::move(path)}
                           : std::nullopt;
            }
            if (code[index] < music_request_bytecode::minimum_path_character ||
                code[index] > music_request_bytecode::maximum_path_character)
            {
                return std::nullopt;
            }
            path.push_back(static_cast<wchar_t>(code[index]));
        }
        return std::nullopt;
    }
} // namespace kh3ap
