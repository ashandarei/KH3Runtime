#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace kh3ap
{
    // Expands KH3's manual save list while leaving the separate autosave entry
    // and the platform save-file implementation unchanged.
    class Kh3ExpandedSaveSlotsPatch
    {
    public:
        Kh3ExpandedSaveSlotsPatch() = default;
        ~Kh3ExpandedSaveSlotsPatch();

        Kh3ExpandedSaveSlotsPatch(const Kh3ExpandedSaveSlotsPatch&) = delete;
        auto operator=(const Kh3ExpandedSaveSlotsPatch&)
                -> Kh3ExpandedSaveSlotsPatch& = delete;
        Kh3ExpandedSaveSlotsPatch(Kh3ExpandedSaveSlotsPatch&&) = delete;
        auto operator=(Kh3ExpandedSaveSlotsPatch&&)
                -> Kh3ExpandedSaveSlotsPatch& = delete;

        auto install(std::wstring_view build_stamp) -> bool;
        auto uninstall(std::wstring_view reason) -> void;
        auto installed() const -> bool;

    private:
        static constexpr size_t max_patch_site_count = 5;
        static constexpr size_t immediate_size = 4;

        std::wstring build_stamp_{};
        std::wstring storefront_{};
        std::array<std::wstring, max_patch_site_count> patch_site_names_{};
        std::array<uintptr_t, max_patch_site_count> patch_addresses_{};
        std::array<std::array<uint8_t, immediate_size>, max_patch_site_count>
                original_bytes_{};
        std::array<std::array<uint8_t, immediate_size>, max_patch_site_count>
                patched_bytes_{};
        size_t active_patch_site_count_{0};
        bool installed_{false};
    };
}
