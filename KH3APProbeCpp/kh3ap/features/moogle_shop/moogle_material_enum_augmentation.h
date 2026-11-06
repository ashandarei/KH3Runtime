#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace kh3ap
{
    struct MoogleMaterialEnumConfig
    {
        bool enabled{};
        int material_item_index_min{};
        int material_item_index_max{};
        std::wstring_view build_stamp{};
        std::function<std::optional<std::filesystem::path>()> bridge_dir_resolver{};
        std::function<std::chrono::steady_clock::time_point()> unreal_init_time_resolver{};
    };

    // Installed enum storage belongs to Unreal. Attempts and completion survive
    // shop/seed resets, matching the lifetime of that process-wide registration.
    class MoogleMaterialEnumAugmentation
    {
    public:
        explicit MoogleMaterialEnumAugmentation(MoogleMaterialEnumConfig config);
        ~MoogleMaterialEnumAugmentation();
        auto has_pending_work() const -> bool;
        auto process(std::chrono::steady_clock::time_point now, bool eligible,
                     const std::optional<std::filesystem::path>& bridge_dir) -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace kh3ap
