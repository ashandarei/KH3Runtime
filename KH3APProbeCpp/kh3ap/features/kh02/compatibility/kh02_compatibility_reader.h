#pragma once
#include "kh02_compatibility_protocol.h"
#include <cstring>
#include <string>
#include <type_traits>

namespace kh3ap::kh02compat
{
    // UE4's loaded script contains resolved 64-bit references. Serialized package
    // expressions use 32-bit indices and are audited separately by the catalog.
    class BytecodeReader
    {
        uint8_t*& cursor;
        const uint8_t* limit;

      public:
        static constexpr size_t literal_limit = 256;

        BytecodeReader(uint8_t*& position, const uint8_t* end) : cursor(position), limit(end)
        {
            if (!position || !end || position > end)
            {
                throw std::invalid_argument("kh02_compat_cursor");
            }
        }

        void require(size_t count, const char* error) const
        {
            if (cursor > limit || static_cast<size_t>(limit - cursor) < count)
            {
                throw std::runtime_error(error);
            }
        }

        void expect(uint8_t expected, const char* error)
        {
            require(1, error);
            if (*cursor != expected)
            {
                throw std::runtime_error(error);
            }
            ++cursor;
        }

        template <class Value> auto value(const char* error) -> Value
        {
            static_assert(std::is_trivially_copyable_v<Value>);
            require(sizeof(Value), error);
            Value result;
            std::memcpy(&result, cursor, sizeof(result));
            cursor += sizeof(result);
            return result;
        }

        auto integer(const char* error) -> int32_t
        {
            require(1 + sizeof(int32_t), error);
            expect(opcode::integer_constant, error);
            return value<int32_t>(error);
        }

        auto number(const char* error) -> float
        {
            require(1 + sizeof(float), error);
            expect(opcode::float_constant, error);
            return value<float>(error);
        }

        auto literal_string() -> std::wstring
        {
            constexpr uint8_t first_printable_ascii = 32;
            constexpr uint8_t last_printable_ascii = 126;
            expect(opcode::string_constant, "kh02_compat_movie_string");
            std::wstring result;
            while (cursor < limit && *cursor && result.size() < literal_limit)
            {
                if (*cursor < first_printable_ascii || *cursor > last_printable_ascii)
                {
                    throw std::runtime_error("kh02_compat_movie_string_ascii");
                }
                result.push_back(*cursor);
                ++cursor;
            }
            expect(0, "kh02_compat_movie_string_end");
            return result;
        }
    };
} // namespace kh3ap::kh02compat
