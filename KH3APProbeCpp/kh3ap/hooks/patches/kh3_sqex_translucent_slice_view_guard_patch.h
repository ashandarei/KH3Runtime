#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace kh3ap
{
    // Guards Square's translucent-slice material path before it reads a field
    // that exists on FViewInfo but not on the FSceneView accepted by the API.
    class Kh3SqexTranslucentSliceViewGuardPatch
    {
    public:
        Kh3SqexTranslucentSliceViewGuardPatch() = default;
        ~Kh3SqexTranslucentSliceViewGuardPatch();

        Kh3SqexTranslucentSliceViewGuardPatch(
                const Kh3SqexTranslucentSliceViewGuardPatch&) = delete;
        auto operator=(const Kh3SqexTranslucentSliceViewGuardPatch&)
                -> Kh3SqexTranslucentSliceViewGuardPatch& = delete;
        Kh3SqexTranslucentSliceViewGuardPatch(
                Kh3SqexTranslucentSliceViewGuardPatch&&) = delete;
        auto operator=(Kh3SqexTranslucentSliceViewGuardPatch&&)
                -> Kh3SqexTranslucentSliceViewGuardPatch& = delete;

        auto install(std::wstring_view build_stamp) -> bool;
        auto uninstall(std::wstring_view reason) -> void;
        auto installed() const -> bool;

    private:
        static constexpr size_t patch_site_count = 10;
        static constexpr size_t max_site_patch_size = 17;

        std::wstring build_stamp_{};
        std::wstring storefront_{};
        void* trampoline_{nullptr};
        std::array<uintptr_t, patch_site_count> patch_addresses_{};
        std::array<std::array<uint8_t, max_site_patch_size>, patch_site_count>
                installed_patch_bytes_{};
        std::array<std::array<uint8_t, max_site_patch_size>, patch_site_count>
                original_site_bytes_{};
        std::array<size_t, patch_site_count> installed_patch_sizes_{};
        std::array<bool, patch_site_count> site_installed_{};
        bool installed_{false};
    };
}
