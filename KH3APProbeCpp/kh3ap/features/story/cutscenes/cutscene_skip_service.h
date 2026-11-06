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
        std::function<std::wstring()> runtime_context_resolver;
        std::function<bool()> skip_enabled_resolver;
        std::wstring_view build_stamp{};
        std::wstring_view event_skip_function_path{};
        bool candidate_catalog_enabled{false};
        bool ui_probe_enabled{false};
        bool end_skip_probe_logging_enabled{false};
        bool state_probe_logging_enabled{false};
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

        auto dump_candidate_catalog_once() -> void;
        auto scene_identity() const -> std::wstring;
        void tick();
        auto on_event_skip_enter(std::wstring_view context_name) -> void;
        auto on_pause_menu_type(
                RC::Unreal::UFunction* function_object,
                void* params,
                RC::Unreal::UObject* context,
                std::wstring_view context_name,
                HookPhase phase) -> std::optional<int64_t>;
        auto on_ui_probe(
                RC::Unreal::UFunction* function_object,
                void* params,
                std::wstring_view context_name,
                std::wstring_view frame_name,
                HookPhase phase) -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
