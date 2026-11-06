#pragma once

#include <limits>
#include <cstdint>
#include <span>
#include <vector>

namespace kh3ap
{
    inline constexpr auto shop_movie_hash_offset_basis = 14695981039346656037ULL;
    inline constexpr auto shop_movie_hash_prime = 1099511628211ULL;

    inline auto shop_movie_fingerprint(std::span<const uint8_t> data) -> uint64_t
    {
        uint64_t hash = shop_movie_hash_offset_basis;
        for (const auto byte : data)
        {
            hash = (hash ^ byte) * shop_movie_hash_prime;
        }
        return hash;
    }

    // Decode into separate storage. Invalid versions, truncated operations,
    // overflowing ranges and an incorrect result leave the caller untouched.
    inline auto decode_shop_movie_patch(std::span<const uint8_t> original,
                                        std::span<const uint8_t> delta, uint32_t original_size,
                                        uint64_t original_hash, uint32_t result_size,
                                        uint64_t result_hash, std::vector<uint8_t>& result) -> bool
    {
        constexpr size_t maximum_movie_size = 4 * 1024 * 1024;
        if (original.size() != original_size || result_size > maximum_movie_size ||
            shop_movie_fingerprint(original) != original_hash)
        {
            return false;
        }
        std::vector<uint8_t> candidate;
        candidate.reserve(result_size);
        size_t cursor = 0;
        const auto read = [&](uint32_t& value)
        {
            if (delta.size() - cursor < sizeof(value))
            {
                return false;
            }
            value = 0;
            for (unsigned i = 0; i < sizeof(value); ++i)
            {
                value |= uint32_t(delta[cursor++]) << (std::numeric_limits<uint8_t>::digits * i);
            }
            return true;
        };
        while (cursor < delta.size())
        {
            const auto opcode = delta[cursor++];
            uint32_t offset{}, length{};
            if (opcode > 1 || (opcode == 0 && !read(offset)) || !read(length) || length == 0 ||
                length > result_size - candidate.size())
            {
                return false;
            }
            if (opcode == 0)
            {
                if (offset > original.size() || length > original.size() - offset)
                {
                    return false;
                }
                candidate.insert(candidate.end(), original.begin() + offset,
                                 original.begin() + offset + length);
            }
            else
            {
                if (length > delta.size() - cursor)
                {
                    return false;
                }
                candidate.insert(candidate.end(), delta.begin() + cursor,
                                 delta.begin() + cursor + length);
                cursor += length;
            }
        }
        if (candidate.size() != result_size || shop_movie_fingerprint(candidate) != result_hash)
        {
            return false;
        }
        result.swap(candidate);
        return true;
    }
} // namespace kh3ap
