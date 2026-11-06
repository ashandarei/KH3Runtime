#pragma once

#include <functional>
#include <memory>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    struct RuntimeBeatOfLeadConfig
    {
        bool enabled{};
        std::function<bool()> enabled_for_current_seed{};
    };

    class RuntimeBeatOfLeadService
    {
    public:
        explicit RuntimeBeatOfLeadService(RuntimeBeatOfLeadConfig config);
        ~RuntimeBeatOfLeadService();

        RuntimeBeatOfLeadService(const RuntimeBeatOfLeadService&) = delete;
        auto operator=(const RuntimeBeatOfLeadService&)
                -> RuntimeBeatOfLeadService& = delete;
        RuntimeBeatOfLeadService(RuntimeBeatOfLeadService&&) noexcept;
        auto operator=(RuntimeBeatOfLeadService&&) noexcept
                -> RuntimeBeatOfLeadService&;

        auto process_constructed_object(RC::Unreal::UObject* object) -> void;
        auto tick() -> void;
        auto reset() -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
