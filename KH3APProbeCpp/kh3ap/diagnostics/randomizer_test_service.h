#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace RC::Unreal { class UObject; }
namespace kh3ap
{
    class RuntimeGoaSettingsService;
    class RuntimeBgmShuffleService;
    class RuntimeColorService;
    struct RuntimeRandomizerTestConfig
    {
        RuntimeGoaSettingsService& settings;
        RuntimeBgmShuffleService& music;
        RuntimeColorService& colors;
        std::function<std::optional<std::filesystem::path>()> bridge_dir;
        std::function<RC::Unreal::UObject*()> sora;
        std::function<bool()> gameplay;
        std::function<std::string()> now_playing;
    };

    // Diagnostics-only, explicit requests. Settings and temporary music are
    // restored on request or after the owner's bounded session lease expires.
    class RuntimeRandomizerTestService
    {
    public:
        explicit RuntimeRandomizerTestService(RuntimeRandomizerTestConfig config);
        ~RuntimeRandomizerTestService();
        void tick(bool initialized);
    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
