#pragma once

#include "kh3ap/core/types.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
    class UFunction;
    class UnrealScriptFunctionCallableContext;
}

namespace kh3ap
{
    class RuntimeBattleLevelSyncService;
    class RuntimeCutsceneSkipService;
    class RuntimeFz02PlayerStartService;
    class RuntimeMoogleShopService;
    class RuntimePlayerContextService;
    class RuntimeStoryProgressService;
    class RuntimeState;
    class RuntimeTutorialSkipService;
    enum class HookPhase;
    struct HookTargetInfo;
    struct RuntimeSoraLevelMonitorState;

    struct RuntimeNativeHookConfig
    {
        RuntimeState& runtime_state;
        RuntimeFz02PlayerStartService& fz02_player_start;
        RuntimeTutorialSkipService& tutorial_skips;
        RuntimePlayerContextService& player_context;
        RuntimeBattleLevelSyncService& battle_level_sync;
        RuntimeCutsceneSkipService& cutscene_skips;
        RuntimeStoryProgressService& story_progress;
        RuntimeMoogleShopService& moogle_shop;
        RuntimeSoraLevelMonitorState& sora_monitor;
        std::function<std::optional<RuntimeContextSnapshot>(
                RC::Unreal::UFunction*,
                std::wstring_view)> update_frame_context{};
        std::function<std::wstring()> runtime_context_resolver{};
        std::function<void(std::wstring_view, std::wstring_view)> enter_ignored_map{};
        std::function<bool()> ignored_map_active{};
        std::function<void()> reset_map_actor_lifecycle{};
        std::function<void()> reset_map_lifecycle{};
        std::function<void(std::wstring_view)> fresh_sora_context_ready{};
        std::function<void(RC::Unreal::UObject*, std::wstring_view)>
                arm_controller_reacquire{};
        std::function<void()> suppress_controller_reacquire{};
        std::function<void()> goa_party_map_ready{};
        std::function<void()> color_map_jump{};
        std::function<void()> portal_map_jump{};
    };

    class RuntimeNativeHookService
    {
    public:
        explicit RuntimeNativeHookService(RuntimeNativeHookConfig config);
        ~RuntimeNativeHookService();

        RuntimeNativeHookService(const RuntimeNativeHookService&) = delete;
        auto operator=(const RuntimeNativeHookService&)
                -> RuntimeNativeHookService& = delete;
        RuntimeNativeHookService(RuntimeNativeHookService&&) noexcept;
        auto operator=(RuntimeNativeHookService&&) noexcept
                -> RuntimeNativeHookService&;

        auto title_or_ui_controller_active() const -> bool;
        auto clear_special_battle_pending() -> void;

        auto handle_apply_map_name(
                RC::Unreal::UnrealScriptFunctionCallableContext& context,
                HookTargetInfo& hook_info,
                HookPhase phase) -> void;
        auto handle_uip_map_name_callback(
                RC::Unreal::UnrealScriptFunctionCallableContext& context,
                HookTargetInfo& hook_info,
                HookPhase phase) -> void;
        auto handle_tres_map_jump(
                RC::Unreal::UnrealScriptFunctionCallableContext& context,
                HookTargetInfo& hook_info,
                HookPhase phase) -> void;
        auto handle_starting_new_player(
                RC::Unreal::UnrealScriptFunctionCallableContext& context,
                HookTargetInfo& hook_info,
                HookPhase phase) -> void;
        auto handle_find_player_start(
                RC::Unreal::UnrealScriptFunctionCallableContext& context,
                HookTargetInfo& hook_info,
                HookPhase phase) -> void;
        auto handle_set_pause_menu_type(
                RC::Unreal::UnrealScriptFunctionCallableContext& context,
                HookTargetInfo& hook_info,
                HookPhase phase) -> void;
        auto handle_set_special_battle_mode(
                RC::Unreal::UnrealScriptFunctionCallableContext& context,
                HookTargetInfo& hook_info,
                HookPhase phase) -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
