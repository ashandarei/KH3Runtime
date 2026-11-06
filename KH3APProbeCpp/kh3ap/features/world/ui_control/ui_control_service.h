#pragma once
#include "kh3ap/core/input/runtime_controller_input.h"
#include "kh3ap/features/world/startup/starter_options.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <string>
namespace RC::Unreal { class UObject; }
namespace kh3ap
{
    class Kh02Runtime;
    struct RuntimeUiControlConfig
    {
        Kh02Runtime& kh02_runtime;
        std::function<std::optional<std::filesystem::path>()> bridge_dir;
        std::function<RC::Unreal::UObject*()> sora;
        std::function<bool()> gameplay_ready;
        std::function<bool()> game_over_active;
        std::function<bool(RC::Unreal::UObject*)> request_game_over;
        std::function<bool(RC::Unreal::UObject*)> stock_workshop_test_materials;
        std::function<void()> refresh_controller_abilities;
        std::function<std::wstring()> cutscene_identity;
        std::function<std::string(bool)> bootstrap_probe;
        std::function<std::string(RC::Unreal::UObject*, bool)> map_overlay_probe;
        std::function<std::string(RC::Unreal::UObject*)> event_read_benchmark;
    };
    class RuntimeUiControlService
    {
    public:
        explicit RuntimeUiControlService(RuntimeUiControlConfig config);
        ~RuntimeUiControlService();
        void tick(bool initialized);
    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
