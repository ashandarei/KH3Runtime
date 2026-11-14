#pragma once

#include "kh3ap/hooks/hook_phase.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UFunction;
    class UObject;
}

namespace kh3ap
{
    struct RuntimeCutsceneSkipConfig
    {
        std::function<std::optional<std::filesystem::path>()> bridge_dir_resolver;
        std::function<bool()> skip_enabled_resolver;
        std::wstring_view build_stamp{};
    };

    class RuntimeCutsceneSkipService
    {
    public:
        explicit RuntimeCutsceneSkipService(RuntimeCutsceneSkipConfig config);
        ~RuntimeCutsceneSkipService();

        RuntimeCutsceneSkipService(const RuntimeCutsceneSkipService&) = delete;
        auto operator=(const RuntimeCutsceneSkipService&) -> RuntimeCutsceneSkipService& = delete;
        RuntimeCutsceneSkipService(RuntimeCutsceneSkipService&&) noexcept;
        auto operator=(RuntimeCutsceneSkipService&&) noexcept -> RuntimeCutsceneSkipService&;

        auto scene_identity() const -> std::wstring;
        void tick();
        auto on_pause_menu_type(
                RC::Unreal::UFunction* function_object,
                void* params,
                RC::Unreal::UObject* context,
                std::wstring_view context_name,
                HookPhase phase) -> std::optional<int64_t>;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
