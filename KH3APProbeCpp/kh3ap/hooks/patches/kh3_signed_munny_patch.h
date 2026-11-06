#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace kh3ap
{
    class Kh3SignedMunnyPatch
    {
    public:
        Kh3SignedMunnyPatch() = default;
        ~Kh3SignedMunnyPatch();

        Kh3SignedMunnyPatch(const Kh3SignedMunnyPatch&) = delete;
        auto operator=(const Kh3SignedMunnyPatch&) -> Kh3SignedMunnyPatch& = delete;
        Kh3SignedMunnyPatch(Kh3SignedMunnyPatch&&) = delete;
        auto operator=(Kh3SignedMunnyPatch&&) -> Kh3SignedMunnyPatch& = delete;

        auto install(std::wstring_view build_stamp) -> bool;
        auto uninstall(std::wstring_view reason) -> void;
        auto installed() const -> bool;
        auto branch_address() const -> uintptr_t;

    private:
        std::wstring build_stamp_{};
        std::array<uintptr_t, 2> branch_addresses_{};
        std::array<std::array<uint8_t, 2>, 2> original_branch_bytes_{};
        size_t branch_count_{0};
        bool installed_{false};
    };
}
