#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

namespace kh3ap
{
    class RuntimeGoaSettingsService;

    struct RuntimeExperienceMultiplierStatus
    {
        int32_t multiplier_half_steps{2};
        int32_t seed_multiplier_half_steps{2};
        bool applied{};
    };

    struct RuntimeExperienceMultiplierConfig
    {
        RuntimeGoaSettingsService& goa_settings;
        std::wstring_view build_stamp{};
    };

    class RuntimeExperienceMultiplierService
    {
    public:
        explicit RuntimeExperienceMultiplierService(
                RuntimeExperienceMultiplierConfig config);
        ~RuntimeExperienceMultiplierService();

        RuntimeExperienceMultiplierService(
                const RuntimeExperienceMultiplierService&) = delete;
        auto operator=(const RuntimeExperienceMultiplierService&)
                -> RuntimeExperienceMultiplierService& = delete;
        RuntimeExperienceMultiplierService(
                RuntimeExperienceMultiplierService&&) noexcept;
        auto operator=(RuntimeExperienceMultiplierService&&) noexcept
                -> RuntimeExperienceMultiplierService&;

        auto install_once() -> void;
        auto tick() -> void;
        [[nodiscard]] auto status() const
                -> RuntimeExperienceMultiplierStatus;
        auto set_multiplier_half_steps(int32_t half_steps) -> bool;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
