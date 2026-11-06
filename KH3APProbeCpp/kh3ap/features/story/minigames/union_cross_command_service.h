#pragma once

#include <functional>
#include <memory>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    struct RuntimeUnionCrossCommandConfig
    {
        bool enabled{};
        std::function<bool()> enabled_for_current_seed{};
    };

    class RuntimeUnionCrossCommandService
    {
    public:
        explicit RuntimeUnionCrossCommandService(
                RuntimeUnionCrossCommandConfig config);
        ~RuntimeUnionCrossCommandService();

        RuntimeUnionCrossCommandService(
                const RuntimeUnionCrossCommandService&) = delete;
        auto operator=(const RuntimeUnionCrossCommandService&)
                -> RuntimeUnionCrossCommandService& = delete;
        RuntimeUnionCrossCommandService(
                RuntimeUnionCrossCommandService&&) noexcept;
        auto operator=(RuntimeUnionCrossCommandService&&) noexcept
                -> RuntimeUnionCrossCommandService&;

        auto process_constructed_object(RC::Unreal::UObject* object) -> void;
        auto tick() -> void;
        auto reset() -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
