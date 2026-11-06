#pragma once

#include <functional>
#include <memory>

namespace RC::Unreal
{
    class UFunction;
    class UnrealScriptFunctionCallableContext;
}

namespace kh3ap
{
    enum class HookPhase;
    struct HookTargetInfo;

    struct RuntimeFz02PlayerStartConfig
    {
        std::function<RC::Unreal::UFunction*(HookTargetInfo&)> function_resolver{};
    };

    class RuntimeFz02PlayerStartService
    {
    public:
        explicit RuntimeFz02PlayerStartService(RuntimeFz02PlayerStartConfig config);
        ~RuntimeFz02PlayerStartService();

        RuntimeFz02PlayerStartService(const RuntimeFz02PlayerStartService&) = delete;
        auto operator=(const RuntimeFz02PlayerStartService&)
                -> RuntimeFz02PlayerStartService& = delete;
        RuntimeFz02PlayerStartService(RuntimeFz02PlayerStartService&&) noexcept;
        auto operator=(RuntimeFz02PlayerStartService&&) noexcept
                -> RuntimeFz02PlayerStartService&;

        auto handle_find_player_start(
                RC::Unreal::UnrealScriptFunctionCallableContext& callback_context,
                HookTargetInfo& hook_info,
                HookPhase phase) -> void;
        auto reset_map_lifecycle() -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
