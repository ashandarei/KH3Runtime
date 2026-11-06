#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <vector>

namespace kh3ap
{
    // Each reflected call owns its scratch storage, including nested ProcessEvent
    // calls. Larger records retain the same dynamically sized behavior.
    class Kh02CallParameters
    {
      public:
        static constexpr size_t inline_capacity = 256;
        // Shipping KH3's Transform parameters use aligned SIMD loads. MSVC's
        // max_align_t is only eight bytes, even on this 64-bit target.
        static constexpr size_t native_alignment = 16;
        static constexpr size_t alignment_padding = native_alignment - 1;

        Kh02CallParameters() = default;
        Kh02CallParameters(const Kh02CallParameters&) = delete;
        auto operator=(const Kh02CallParameters&) -> Kh02CallParameters& = delete;

        void resize(size_t size)
        {
            if (size > inline_capacity)
            {
                if (size > overflow_.max_size() - alignment_padding)
                {
                    throw std::length_error("kh02_call_parameter_size");
                }
                overflow_.assign(size + alignment_padding, std::byte{});
            }
            else
            {
                overflow_.clear();
                std::fill_n(inline_.data(), size, std::byte{});
            }
            size_ = size;
        }

        auto data() -> std::byte*
        {
            if (empty())
            {
                return nullptr;
            }
            if (size_ > inline_capacity)
            {
                void* buffer = overflow_.data();
                auto available = overflow_.size();
                return static_cast<std::byte*>(std::align(native_alignment, size_, buffer, available));
            }
            return inline_.data();
        }

        auto empty() const -> bool
        {
            return size_ == 0;
        }

        auto size() const -> size_t
        {
            return size_;
        }

      private:
        alignas(native_alignment) std::array<std::byte, inline_capacity> inline_;
        std::vector<std::byte> overflow_;
        size_t size_{};
    };
} // namespace kh3ap
