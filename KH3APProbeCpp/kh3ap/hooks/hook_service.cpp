#include <chrono>
#include <memory>
#include "kh3ap/app/config.h"
#include "kh3ap/features/music/events/music_update_events.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include <polyhook2/Detour/x64Detour.hpp>

#include "kh3ap/hooks/hook_service.h"
#include "kh3ap/features/world/space/space_service.h"

#include "kh3ap/features/grants/ap_ignored_map.h"
#include "kh3ap/features/grants/bad_status_helpers.h"
#include "kh3ap/features/grants/battle_level/battle_level_sync_service.h"
#include "kh3ap/features/player/context/context_state.h"
#include "kh3ap/features/player/context/context_subsystem_state.h"
#include "kh3ap/core/conditional_profile_scope.h"
#include "kh3ap/features/story/cutscenes/cutscene_skip_service.h"
#include "kh3ap/features/tutorials/final_world_tutorial_pawn_capture.h"
#include "kh3ap/features/story/world_fixes/fz02_player_start_service.h"
#include "kh3ap/features/world/portals/map_jump_command_helpers.h"
#include "kh3ap/features/grants/grant_pawn_state.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/hooks/hook_phase.h"
#include "kh3ap/features/grants/delivery/item_grant_poll_state.h"
#include "kh3ap/features/moogle_shop/moogle_shop_service.h"
#include "kh3ap/features/player/objects/object_cache.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/features/player/context/player_context_service.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/story/remind/remind_portal_route.h"
#include "kh3ap/features/story/remind/yozora_return_route.h"
#include "kh3ap/features/player/state/sora_level_monitor_state.h"
#include "kh3ap/app/state.h"
#include "kh3ap/features/story/progression/story_progress_service.h"
#include "kh3ap/bridge/text_helpers.h"
#include "kh3ap/features/tutorials/tutorial_skip_service.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#define NOMINMAX
#include <windows.h>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        constexpr auto native_script_true_token = 0x27;
        constexpr auto native_script_false_token = 0x28;
        constexpr auto native_boolean_result_scan_bytes = 8;

        constexpr auto native_bool_token_window_bytes = 32;
        using ::kh3ap::settings::s_enable_native_function_hot_path_profiling;
        using ::kh3ap::settings::s_enable_runtime_hot_path_profiling;
        using ::kh3ap::settings::s_enable_starting_player_bad_status_cache_prewarm;

        auto lower_ascii_copy(std::wstring_view value) -> std::wstring
        {
            std::wstring lowered{value};
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](wchar_t ch)
                           {
                               if (ch >= L'A' && ch <= L'Z')
                               {
                                   return static_cast<wchar_t>(ch + (L'a' - L'A'));
                               }
                               return ch;
                           });
            return lowered;
        }

        auto safe_full_name(const UObject* object, std::wstring_view fallback = STR("<null>"))
            -> std::wstring
        {
            std::wstring full_name{};
            return try_get_uobject_full_name_fast(object, full_name) ? full_name
                                                                     : std::wstring{fallback};
        }

        auto contains_station_of_awakening_path(std::wstring_view value) -> bool
        {
            return value.find(STR("/Game/Maps/StationOfAwakening/")) != std::wstring_view::npos ||
                   value.find(STR("/Game/Maps/StationOfAwakening.")) != std::wstring_view::npos ||
                   value.find(STR("StationOfAwakening")) != std::wstring_view::npos;
        }

        auto resolve_hook_function(HookTargetInfo& hook_info) -> UFunction*
        {
            if (!hook_info.function_object)
            {
                hook_info.function_object = UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr, nullptr, hook_info.function_path);
            }
            return hook_info.function_object;
        }

        auto format_native_hook_bool_literal_candidates(FFrame& stack) -> std::wstring
        {
            auto* const frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
            if (!frame)
            {
                return STR("<none>");
            }

            const uintptr_t code = reinterpret_cast<uintptr_t>(frame->Code);
            if (code == 0 || !looks_like_process_pointer(static_cast<uint64_t>(code)))
            {
                return STR("<none>");
            }

            constexpr uintptr_t bytes_before = 48;
            constexpr size_t window_size = 128;
            const uintptr_t window_begin = code > bytes_before ? code - bytes_before : code;
            std::vector<unsigned char> buffer(window_size);
            SIZE_T bytes_read = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(window_begin),
                                   buffer.data(), buffer.size(), &bytes_read) ||
                bytes_read == 0)
            {
                return STR("<unreadable>");
            }

            std::vector<std::wstring> candidates;
            for (size_t index = 0; index < bytes_read; ++index)
            {
                const unsigned char token = buffer[index];
                if (token != native_script_true_token && token != native_script_false_token)
                {
                    continue;
                }
                const intptr_t relative =
                    static_cast<intptr_t>(window_begin + index) - static_cast<intptr_t>(code);
                if (relative < -native_bool_token_window_bytes ||
                    relative > native_bool_token_window_bytes)
                {
                    continue;
                }

                const std::wstring token_name = token == 0x27 ? STR("EX_True") : STR("EX_False");
                const std::wstring value = token == 0x27 ? STR("true") : STR("false");
                const size_t context_begin = index >= 4 ? index - 4 : 0;
                const size_t context_end = std::min(bytes_read, index + 5);
                candidates.push_back(std::format(
                    STR("rel={} token=0x{:02X} token_name={} value={} context={}"), relative, token,
                    token_name, value,
                    format_bytes_hex(buffer.data() + context_begin, context_end - context_begin)));
            }

            if (candidates.empty())
            {
                return STR("<none>");
            }
            std::wstring joined;
            for (const std::wstring& candidate : candidates)
            {
                if (!joined.empty())
                {
                    joined += STR("; ");
                }
                joined += candidate;
            }
            return joined;
        }

        auto try_read_native_hook_bool_literal_argument(FFrame& stack) -> std::optional<bool>
        {
            auto* const frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
            if (!frame)
            {
                return std::nullopt;
            }

            const uintptr_t code = reinterpret_cast<uintptr_t>(frame->Code);
            if (code == 0 || !looks_like_process_pointer(static_cast<uint64_t>(code)))
            {
                return std::nullopt;
            }

            constexpr uintptr_t bytes_before = 8;
            constexpr size_t window_size = 32;
            const uintptr_t window_begin = code > bytes_before ? code - bytes_before : code;
            std::array<unsigned char, window_size> buffer{};
            SIZE_T bytes_read = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(window_begin),
                                   buffer.data(), buffer.size(), &bytes_read) ||
                bytes_read == 0)
            {
                return std::nullopt;
            }

            for (size_t index = 0; index < bytes_read; ++index)
            {
                const intptr_t relative =
                    static_cast<intptr_t>(window_begin + index) - static_cast<intptr_t>(code);
                if (relative < 0 || relative > native_boolean_result_scan_bytes)
                {
                    continue;
                }
                if (buffer[index] == native_script_true_token)
                {
                    return true;
                }
                if (buffer[index] == native_script_false_token)
                {
                    return false;
                }
            }
            return std::nullopt;
        }
    } // namespace

    struct RuntimeNativeHookService::Impl
    {
        struct StandardHookContext
        {
            UObject* context{};
            void* params{};
            UFunction* target_function{};
            UFunction* frame_node{};
            std::wstring context_name{};
            std::wstring frame_name{};
        };

        explicit Impl(RuntimeNativeHookConfig config) : config(std::move(config))
        {
        }

        auto ignored_map_active() const -> bool
        {
            return config.ignored_map_active && config.ignored_map_active();
        }

        auto runtime_context() const -> std::wstring
        {
            return config.runtime_context_resolver ? config.runtime_context_resolver()
                                                   : std::wstring{};
        }

        auto prepare_standard_hook(UnrealScriptFunctionCallableContext& callback_context,
                                   HookTargetInfo& hook_info, bool allow_ignored_map = false)
            -> std::optional<StandardHookContext>
        {
            if (!allow_ignored_map && ignored_map_active())
            {
                return std::nullopt;
            }

            UFunction* const frame_node = get_frame_node(callback_context.TheStack);
            if (config.update_frame_context)
            {
                const auto updated =
                    config.update_frame_context(frame_node, hook_info.function_name);
                if (updated.has_value())
                {
                    config.tutorial_skips.try_apply_final_world_for_context(
                        std::format(STR("{} {}"), updated->world, updated->outer),
                        hook_info.function_name);
                }
            }

            UObject* const context = callback_context.Context;
            config.player_context.maybe_cache_blueprint_library_default_object(context, false);
            void* const params = get_frame_locals(callback_context.TheStack);
            UFunction* const target_function = resolve_hook_function(hook_info);
            if (!target_function || !params)
            {
                return std::nullopt;
            }

            StandardHookContext result{
                .context = context,
                .params = params,
                .target_function = target_function,
                .frame_node = frame_node,
                .context_name = safe_full_name(context),
                .frame_name = safe_full_name(frame_node),
            };
            return result;
        }

        auto handle_find_player_start(UnrealScriptFunctionCallableContext& context,
                                      HookTargetInfo& hook_info, HookPhase phase) -> void
        {
            config.fz02_player_start.handle_find_player_start(context, hook_info, phase);
            handle_map_jump_player_start(context, hook_info, phase);
        }

        auto handle_set_special_battle_mode(UnrealScriptFunctionCallableContext& callback_context,
                                            HookTargetInfo& hook_info, HookPhase phase) -> void
        {
            if (ignored_map_active())
            {
                return;
            }

            RuntimeConditionalProfileScope<s_enable_native_function_hot_path_profiling> profile{
                STR("hook_branch:runtime_event_flow")};
            const auto* const frame_node = get_frame_node(callback_context.TheStack);
            const std::wstring frame_name = safe_full_name(frame_node);
            const bool olympus_coliseum_frame =
                frame_name.find(STR("/Game/Mods/Coliseum/")) != std::wstring::npos;
            config.tutorial_skips.try_apply_final_world_for_context(frame_name,
                                                                    hook_info.function_name);
            const std::wstring bool_candidates =
                format_native_hook_bool_literal_candidates(callback_context.TheStack);
            const std::optional<bool> special_battle_mode =
                try_read_native_hook_bool_literal_argument(callback_context.TheStack);

            if (phase == HookPhase::Pre && special_battle_mode.has_value())
            {
                special_battle_level_sync_pending = *special_battle_mode;
                const std::wstring map_root =
                    RuntimeTextHelpers::extract_game_map_root(frame_name).value_or(std::wstring{});
                const bool changed = config.runtime_state.grant_state().update_special_battle_mode(
                    *special_battle_mode, hook_info.function_name,
                    std::format(STR("frame={} map_root={} bytecode_bool_literal_candidates={}"),
                                frame_name,
                                map_root.empty() ? std::wstring{STR("<unknown>")} : map_root,
                                bool_candidates),
                    std::chrono::steady_clock::now(), map_root);
                if (!*special_battle_mode)
                {
                    config.player_context.clear_special_battle_recovery_pending();
                }
                if (changed)
                {
                    Output::send<LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] Runtime special battle grant gate {} frame={} "
                            "map_root={} bool_candidates={}\n"),
                        *special_battle_mode ? STR("active") : STR("cleared"), frame_name,
                        map_root.empty() ? std::wstring{STR("<unknown>")} : map_root,
                        bool_candidates);
                    if (!*special_battle_mode && !olympus_coliseum_frame)
                    {
                        config.player_context.process_pending_grant_from_runtime_ready_event(
                            STR("special_battle_mode"));
                    }
                }
            }

            if (phase != HookPhase::Post || !special_battle_level_sync_pending)
            {
                return;
            }
            special_battle_level_sync_pending = false;
            auto& pawn_state = config.runtime_state.grant_state().pawn_state();
            UObject* const controller = pawn_state.controller_object();
            if (controller && !pawn_state.controller_name().empty())
            {
                config.player_context.cache_pawn_from_player_controller(
                    controller, STR("SpecialBattleLevelSync:cached_controller"),
                    pawn_state.controller_name(), true);
            }
            if (controller && config.player_context.final_world_capture_armed() &&
                is_runtime_final_world_tutorial_pawn_capture_event_caller(frame_name))
            {
                config.player_context.try_capture_final_world_tutorial_pawn_once(
                    controller, STR("SpecialBattleMode:GetLocalTresPlayerPawnBase"), frame_name);
            }
            if (!config.battle_level_sync.schedule_he06_special(frame_name,
                                                                std::chrono::steady_clock::now()))
            {
                config.battle_level_sync.sync_observed(STR("special_battle_mode_triggered"), true);
            }
        }

        auto handle_set_pause_menu_type(UnrealScriptFunctionCallableContext& callback_context,
                                        HookTargetInfo& hook_info, HookPhase phase) -> void
        {
            const auto prepared = prepare_standard_hook(callback_context, hook_info);
            if (!prepared.has_value())
            {
                return;
            }
            const auto pause_type = config.cutscene_skips.on_pause_menu_type(
                prepared->target_function, prepared->params, prepared->context,
                prepared->context_name, phase);
            if (phase == HookPhase::Post)
            {
                if (pause_type == 0)
                {
                    character_swap::notify_cinematic_finished(prepared->context);
                }
                else if (pause_type == 1)
                {
                    character_swap::notify_cinematic_started(prepared->context);
                }
            }
            // Station sets its normal camp menu after possession. Restore the
            // verified Sora context that this level-script event just replaced.
            if (phase == HookPhase::Post &&
                prepared->frame_name.find(STR("/Game/Maps/StationOfAwakening/")) !=
                    std::wstring::npos)
            {
                config.player_context.process_pending_grant_from_runtime_ready_event(
                    STR("station_camp_menu_ready"));
            }
        }

        auto handle_tres_map_jump(UnrealScriptFunctionCallableContext& callback_context,
                                  HookTargetInfo& hook_info, HookPhase phase) -> void
        {
            // Release map-owned portrait roots before travel, even when the
            // source has no Sora or is excluded from AP gameplay processing.
            if (phase == HookPhase::Pre && config.portal_map_jump)
            {
                config.portal_map_jump();
            }
            const auto* const frame_node = get_frame_node(callback_context.TheStack);
            const std::wstring frame_name = safe_full_name(frame_node, {});
            const std::wstring context_name = safe_full_name(callback_context.Context, {});
            if (is_runtime_ap_ignored_map_context(frame_name))
            {
                if (config.enter_ignored_map)
                {
                    config.enter_ignored_map(frame_name, STR("TresMapJump:frame"));
                }
                return;
            }
            if (is_runtime_ap_ignored_map_context(context_name))
            {
                if (config.enter_ignored_map)
                {
                    config.enter_ignored_map(context_name, STR("TresMapJump:context"));
                }
                return;
            }

            const auto prepared = prepare_standard_hook(callback_context, hook_info);
            if (!prepared.has_value())
            {
                return;
            }

            if (phase == HookPhase::Pre)
            {
                character_swap::notify_map_jump(prepared->context, prepared->frame_name);
                try_redirect_remind_scala_portal(callback_context.TheStack,
                                                 prepared->target_function, prepared->frame_name);
                try_redirect_yozora_return(callback_context.TheStack,
                                          prepared->target_function, prepared->frame_name);
                config.runtime_state.grant_state().item_grant_poll_state().disarm();
                config.runtime_state.grant_state().pawn_state().reset();
                config.runtime_state.context_subsystem_state()
                    .context_state()
                    .reset_playable_publish();
                if (config.color_map_jump)
                {
                    config.color_map_jump();
                }
                if (config.reset_map_lifecycle)
                {
                    config.reset_map_lifecycle();
                }
            }
            else
            {
                Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] MapJumpGrantProbe skipped runtime item delivery during "
                        "map jump | context={}\n"),
                    runtime_context());
            }
        }

        auto handle_starting_new_player(UnrealScriptFunctionCallableContext& callback_context,
                                        HookTargetInfo& hook_info, HookPhase phase) -> void
        {
            const auto prepared = prepare_standard_hook(callback_context, hook_info, true);
            if (!prepared.has_value() || phase != HookPhase::Post)
            {
                return;
            }

            RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                STR("hook:HandleStartingNewPlayer")};
            RuntimeSpaceService::notify_context_changed();
            character_swap::notify_player_started();
            UObject* const new_player = config.player_context.read_object_function_param(
                prepared->target_function, prepared->params, STR("NewPlayer"));
            std::wstring new_player_name{};
            bool cached = false;
            if (new_player && kh3ap::is_uobject_valid_fast(new_player))
            {
                new_player_name = format_object_debug_name(new_player);
                if (is_runtime_ap_ignored_map_context(new_player_name))
                {
                    if (config.enter_ignored_map)
                    {
                        config.enter_ignored_map(new_player_name,
                                                 STR("HandleStartingNewPlayer:NewPlayer"));
                    }
                    return;
                }

                if (config.reset_map_actor_lifecycle)
                {
                    config.reset_map_actor_lifecycle();
                }
                config.moogle_shop.observe_player_started(STR("HandleStartingNewPlayer"));

                const std::wstring lowered_name = lower_ascii_copy(new_player_name);
                const bool title_controller =
                    lowered_name.find(STR("tresuiplayercontroller")) != std::wstring::npos ||
                    lowered_name.find(STR("/game/maps/tresgametitle")) != std::wstring::npos;
                if (title_or_ui_controller_active != title_controller)
                {
                    request_music_update(MusicUpdate::Context);
                }
                title_or_ui_controller_active = title_controller;

                const bool bistro_starting_player =
                    config.player_context.is_bistro_starting_player(new_player_name);
                const bool station_starting_player =
                    contains_station_of_awakening_path(new_player_name);
                config.tutorial_skips.try_apply_for_context(new_player_name,
                                                            STR("HandleStartingNewPlayer"));
                config.story_progress.arm_caribbean_chart_unlock(new_player_name);

                if (bistro_starting_player)
                {
                    config.player_context.mark_bistro_visited_from_starting_player(new_player_name);
                }
                else if (title_controller)
                {
                    config.runtime_state.grant_state().item_grant_poll_state().disarm();
                    config.runtime_state.grant_state().pawn_state().reset();
                    config.runtime_state.grant_state().clear_bistro_return_gate(
                        STR("HandleStartingNewPlayer:title_or_ui"),
                        std::chrono::steady_clock::now());
                    config.runtime_state.context_subsystem_state()
                        .context_state()
                        .reset_playable_publish();
                    Output::send<LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] StartingPlayerPawnCache reset for title/ui controller "
                            "new_player={}\n"),
                        new_player_name);
                }
                else
                {
                    if (station_starting_player)
                    {
                        // Publish the new-run replay boundary before admitting
                        // Station Sora to ordinary AP grant processing.
                        config.player_context.try_write_new_game_staging_from_controller_pawn(
                            new_player, STR("HandleStartingNewPlayer:NewPlayer"));
                    }
                    config.player_context.maybe_clear_special_battle_from_game_over_recovery(
                        new_player_name,
                        STR("HandleStartingNewPlayer:NewPlayer:game_over_recovery"));
                    cached = config.player_context.cache_pawn_from_player_controller(
                        new_player, STR("HandleStartingNewPlayer:NewPlayer"), new_player_name);

                    if constexpr (s_enable_starting_player_bad_status_cache_prewarm)
                    {
                        if (cached)
                        {
                            UObject* const cached_sora =
                                config.runtime_state.grant_state().pawn_state().object();
                            const bool state_cache_matches =
                                cached_sora &&
                                config.sora_monitor.cached_runtime_sora_pawn_address ==
                                    reinterpret_cast<uintptr_t>(cached_sora);
                            prewarm_direct_player_bad_status_runtime_cache(
                                cached_sora,
                                state_cache_matches &&
                                        config.sora_monitor.cached_runtime_sora_state_comp_valid
                                    ? config.sora_monitor.cached_runtime_sora_state_comp
                                    : nullptr,
                                state_cache_matches &&
                                        config.sora_monitor.cached_runtime_sora_current_state_valid
                                    ? config.sora_monitor.cached_runtime_sora_current_state
                                    : nullptr);
                        }
                    }

                    if (!cached && config.tutorial_skips.final_world_ui_bypasses_applied())
                    {
                        if (config.suppress_controller_reacquire)
                        {
                            config.suppress_controller_reacquire();
                        }
                        config.player_context.arm_final_world_capture();
                        cached = config.player_context.try_capture_final_world_tutorial_pawn_once(
                            new_player, STR("HandleStartingNewPlayer:GetLocalTresPlayerPawnBase"),
                            new_player_name);
                        if (!cached)
                        {
                            Output::send<LogLevel::Verbose>(
                                STR("[KH3APProbeCpp] FinalWorldTutorialPawnCapture "
                                    "awaiting_special_battle event=ew_01 polling=false "
                                    "controller={}\n"),
                                new_player_name);
                        }
                    }
                    else if (!cached && config.arm_controller_reacquire)
                    {
                        config.arm_controller_reacquire(
                            new_player, STR("HandleStartingNewPlayer:NewPlayer:no_sora_pawn"));
                    }

                    if (cached && config.fresh_sora_context_ready)
                    {
                        config.fresh_sora_context_ready(STR("HandleStartingNewPlayer:NewPlayer"));
                    }

                    if (cached && config.runtime_state.grant_state().bistro_visited())
                    {
                        config.player_context.mark_bistro_return_waiting_for_map_name(
                            STR("HandleStartingNewPlayer:NewPlayer:post_bistro_sora"));
                    }
                    const auto& pawn_name = config.runtime_state.grant_state().pawn_state().name();
                    const std::wstring map_source =
                        cached && !pawn_name.empty() ? pawn_name : new_player_name;
                    config.player_context.maybe_clear_special_battle_from_map_root_change(
                        map_source, STR("HandleStartingNewPlayer:NewPlayer:map_root_changed"));
                }
                config.player_context.clear_blocking_ui_from_starting_player(new_player_name);
            }

            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] StartingPlayerPawnCache hook={} cached={} new_player={} "
                    "pawn={}\n"),
                hook_info.function_name, cached,
                new_player_name.empty() ? format_object_debug_name(new_player) : new_player_name,
                config.runtime_state.grant_state().pawn_state().name().empty()
                    ? std::wstring{STR("<none>")}
                    : config.runtime_state.grant_state().pawn_state().name());
        }

        auto handle_map_name(UnrealScriptFunctionCallableContext& callback_context,
                             HookTargetInfo& hook_info, HookPhase phase, bool uip_callback) -> void
        {
            if (phase != HookPhase::Post)
            {
                return;
            }
            RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                STR("hook:map_name_ready")};
            const auto prepared = prepare_standard_hook(callback_context, hook_info);
            if (!prepared.has_value())
            {
                return;
            }

            const auto current_snapshot =
                config.runtime_state.context_subsystem_state().context_state().current_snapshot();
            const std::wstring current_context_text =
                current_snapshot.has_value()
                    ? current_snapshot->world + STR(" ") + current_snapshot->outer
                    : std::wstring{};
            const bool is_goa_context =
                current_context_text.find(STR("/Game/Maps/GardenOfAssemblage/")) !=
                    std::wstring::npos ||
                config.runtime_state.grant_state().pawn_state().name().find(
                    STR("/Game/Maps/GardenOfAssemblage/")) != std::wstring::npos;
            const std::wstring source = uip_callback
                                            ? std::wstring{STR("MapName:UIPMapNameCallback")}
                                            : std::wstring{STR("MapName:ApplyMapName")};
            if (uip_callback)
            {
                RuntimeSpaceService::notify_map_ready();
            }
            UObject* const frame_object = get_frame_object(callback_context.TheStack);
            const std::wstring context_hint =
                std::format(STR("{} {} {}"), prepared->context_name,
                            format_object_debug_name(frame_object), runtime_context());
            config.tutorial_skips.try_apply_for_context(context_hint, source);
            if (uip_callback)
            {
                config.story_progress.mark_caribbean_chart_map_ready(context_hint);
            }

            const bool released_bistro_gate =
                config.player_context.release_bistro_return_gate_from_map_name(
                    std::format(STR("{}:bistro_return"), source));
            if (released_bistro_gate && !is_goa_context)
            {
                if (frame_object && kh3ap::is_uobject_valid_fast(frame_object))
                {
                    RuntimeObjectResolver::cache_world_context_object(
                        config.runtime_state.object_cache(), frame_object);
                }
                config.player_context.write_map_ready_marker(
                    std::format(STR("{}:bistro_return"), source), prepared->context, frame_object,
                    true);
                config.runtime_state.grant_state().item_grant_poll_state().arm();
                Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] BistroMapNameReadyProbe armed "
                                                    "runtime item delivery hook={} context={}\n"),
                                                hook_info.function_name, runtime_context());
            }

            if (is_goa_context)
            {
                if (frame_object && kh3ap::is_uobject_valid_fast(frame_object))
                {
                    RuntimeObjectResolver::cache_world_context_object(
                        config.runtime_state.object_cache(), frame_object);
                }
                const std::wstring pawn_source =
                    uip_callback ? std::wstring{STR("GoAMapName:UIPMapNameCallback:cached_pawn")}
                                 : std::wstring{STR("GoAMapName:ApplyMapName:cached_pawn")};
                if (!config.player_context.trusted_current_playable_cached_sora_pawn())
                {
                    config.player_context.cache_pawn_from_hook_context(prepared->context,
                                                                       frame_object, pawn_source);
                }
                config.player_context.bootstrap_sora_state_from_trusted_pawn(
                    config.runtime_state.grant_state().pawn_state().object(), pawn_source);
                const bool new_entry = config.player_context.write_map_ready_marker(
                    pawn_source, prepared->context, frame_object, true);
                if (new_entry && config.goa_party_map_ready)
                {
                    config.goa_party_map_ready();
                }
                config.runtime_state.grant_state().item_grant_poll_state().arm();
                Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] GoAMapNameReadyProbe armed "
                                                    "runtime item delivery hook={} context={}\n"),
                                                hook_info.function_name, runtime_context());
            }
            else if (!released_bistro_gate)
            {
                // World-script UI hooks can replace current_snapshot during a
                // cinematic. The map-name completion belongs to the current
                // trusted Sora pawn, not that script's streaming-level context.
                config.player_context.write_map_ready_marker(
                    std::format(STR("{}:cached_pawn"), source), prepared->context, frame_object,
                    true);
            }

            config.player_context.clear_blocking_ui_for_playable_context(STR("map_name_ready"),
                                                                         hook_info.function_name);
            if (uip_callback)
            {
                const auto now = std::chrono::steady_clock::now();
                const std::wstring control_ready_source =
                    std::format(STR("RuntimeControlReady:{}"), source);
                if (config.runtime_state.grant_state().clear_classic_kingdom_transition_gate(
                        control_ready_source, now))
                {
                    Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] ClassicKingdom runtime grant gate cleared source={} "
                            "grants_safe=true retry_pending=true ready_event=map_name_callback "
                            "mode=event_driven\n"),
                        control_ready_source);
                }
                if (config.runtime_state.grant_state().clear_olympus_coliseum_transition_gate(
                        control_ready_source, now))
                {
                    Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] OlympusColiseum runtime grant gate cleared source={} "
                            "grants_safe=true retry_pending=true ready_event=map_name_callback "
                            "mode=event_driven\n"),
                        control_ready_source);
                }
            }
            config.battle_level_sync.sync_observed(source);
            if (config.runtime_state.grant_state().item_grant_poll_state().is_armed())
            {
                config.player_context.process_pending_grant_from_runtime_ready_event(source, false);
            }
        }

        RuntimeNativeHookConfig config;
        bool title_or_ui_controller_active{true};
        bool special_battle_level_sync_pending{false};
    };

    RuntimeNativeHookService::RuntimeNativeHookService(RuntimeNativeHookConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeNativeHookService::~RuntimeNativeHookService() = default;
    RuntimeNativeHookService::RuntimeNativeHookService(RuntimeNativeHookService&&) noexcept =
        default;
    auto RuntimeNativeHookService::operator=(RuntimeNativeHookService&&) noexcept
        -> RuntimeNativeHookService& = default;

    auto RuntimeNativeHookService::title_or_ui_controller_active() const -> bool
    {
        return impl_->title_or_ui_controller_active;
    }

    auto RuntimeNativeHookService::clear_special_battle_pending() -> void
    {
        impl_->special_battle_level_sync_pending = false;
    }

    auto
    RuntimeNativeHookService::handle_apply_map_name(UnrealScriptFunctionCallableContext& context,
                                                    HookTargetInfo& hook_info, HookPhase phase)
        -> void
    {
        impl_->handle_map_name(context, hook_info, phase, false);
    }

    auto RuntimeNativeHookService::handle_uip_map_name_callback(
        UnrealScriptFunctionCallableContext& context, HookTargetInfo& hook_info, HookPhase phase)
        -> void
    {
        impl_->handle_map_name(context, hook_info, phase, true);
    }

    auto
    RuntimeNativeHookService::handle_tres_map_jump(UnrealScriptFunctionCallableContext& context,
                                                   HookTargetInfo& hook_info, HookPhase phase)
        -> void
    {
        impl_->handle_tres_map_jump(context, hook_info, phase);
    }

    auto RuntimeNativeHookService::handle_starting_new_player(
        UnrealScriptFunctionCallableContext& context, HookTargetInfo& hook_info, HookPhase phase)
        -> void
    {
        impl_->handle_starting_new_player(context, hook_info, phase);
    }

    auto
    RuntimeNativeHookService::handle_find_player_start(UnrealScriptFunctionCallableContext& context,
                                                       HookTargetInfo& hook_info, HookPhase phase)
        -> void
    {
        impl_->handle_find_player_start(context, hook_info, phase);
    }

    auto RuntimeNativeHookService::handle_set_pause_menu_type(
        UnrealScriptFunctionCallableContext& context, HookTargetInfo& hook_info, HookPhase phase)
        -> void
    {
        impl_->handle_set_pause_menu_type(context, hook_info, phase);
    }

    auto RuntimeNativeHookService::handle_set_special_battle_mode(
        UnrealScriptFunctionCallableContext& context, HookTargetInfo& hook_info, HookPhase phase)
        -> void
    {
        impl_->handle_set_special_battle_mode(context, hook_info, phase);
    }
} // namespace kh3ap
