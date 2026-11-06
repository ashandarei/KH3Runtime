#pragma once

#include <functional>
#include <memory>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    struct RuntimeFestivalDanceStoryConfig
    {
        bool enabled{};
        std::function<bool()> enabled_for_current_seed{};
        std::function<void()> disable_object_observation{};
    };

    class RuntimeFestivalDanceStoryService
    {
    public:
        explicit RuntimeFestivalDanceStoryService(
                RuntimeFestivalDanceStoryConfig config);
        ~RuntimeFestivalDanceStoryService();

        RuntimeFestivalDanceStoryService(
                const RuntimeFestivalDanceStoryService&) = delete;
        auto operator=(const RuntimeFestivalDanceStoryService&)
                -> RuntimeFestivalDanceStoryService& = delete;
        RuntimeFestivalDanceStoryService(
                RuntimeFestivalDanceStoryService&&) noexcept;
        auto operator=(RuntimeFestivalDanceStoryService&&) noexcept
                -> RuntimeFestivalDanceStoryService&;

        auto process_constructed_object(RC::Unreal::UObject* object) -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
