#include "kh3ap/app/config.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/hooks/production_hook_registry.h"

#include "kh3ap/hooks/hook_phase.h"
#include "kh3ap/core/types.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/UObjectArray.hpp>
#include <UnrealDef.hpp>

#include <string>
#include <chrono>
#include <mutex>
#include <vector>
#include <stdexcept>
#include <utility>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        using ::kh3ap::settings::s_apply_map_name_name;
        using ::kh3ap::settings::s_apply_map_name_path;
        using ::kh3ap::settings::s_enable_cutscene_auto_skip_hooks;
        using ::kh3ap::settings::s_enable_playable_context_ready_dispatch_hook;
        using ::kh3ap::settings::s_enable_post_load_probe_hooks;
        using ::kh3ap::settings::s_enable_runtime_event_flow_probe_hooks;
        using ::kh3ap::settings::s_enable_runtime_fz02_save_point_auto_spawn;
        using ::kh3ap::settings::s_enable_starting_player_pawn_cache_hook;
        using ::kh3ap::settings::s_find_player_start_name;
        using ::kh3ap::settings::s_find_player_start_path;
        using ::kh3ap::settings::s_handle_starting_new_player_name;
        using ::kh3ap::settings::s_handle_starting_new_player_path;
        using ::kh3ap::settings::s_tres_map_jump_name;
        using ::kh3ap::settings::s_tres_map_jump_path;
        using ::kh3ap::settings::s_tres_set_pause_menu_type_name;
        using ::kh3ap::settings::s_tres_set_pause_menu_type_path;
        using ::kh3ap::settings::s_tres_set_special_battle_mode_name;
        using ::kh3ap::settings::s_tres_set_special_battle_mode_path;
        using ::kh3ap::settings::s_uip_map_name_on_callback_name;
        using ::kh3ap::settings::s_uip_map_name_on_callback_path;

        template <typename... Args>
        auto warn(const wchar_t* message, Args&&... args) noexcept -> void
        {
            try { Output::send<LogLevel::Warning>(message, std::forward<Args>(args)...); }
            catch (...) {} // Logging must not abort a callback or teardown.
        }
    }

    struct RuntimeProductionHookRegistry::Impl
    {
        explicit Impl(RuntimeProductionHookRegistryConfig config)
            : config_(std::move(config))
        {
        }

        struct CallbackState
        {
            HookTargetInfo target;
            RuntimeProductionHookHandler handler;
            std::wstring game_thread_source;
            std::pair<int, int> ids{};
            FWeakObjectPtr function;
            int32_t function_index{-1};
            bool function_has_serial{};

            auto resolve_function() const -> UFunction*
            {
                if (auto* object = function.Get(); is_uobject_valid_fast(object))
                    return static_cast<UFunction*>(object);
                if (function_has_serial) return nullptr;
                // UE4SS copies a serial; it does not allocate one. KH3's
                // native UFunctions commonly have serial zero. Resolve their
                // exact path afresh, then require the original slot, address
                // and serial before dispatch or unregister. Never dereference
                // the retained address when a weak identity is unavailable.
                auto* object = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, target.function_path);
                if (!is_uobject_valid_fast(object) || object != target.function_object ||
                    object->GetInternalIndex() != function_index ||
                    !function.SerialNumbersMatch(const_cast<FUObjectItem*>(object->GetObjectItem())))
                    return nullptr;
                return object;
            }
        };

        struct Session
        {
            // KH3 callbacks can recursively dispatch another hooked function.
            // Shutdown takes the same gate, draining callbacks before owners
            // are released. Retained UE4SS callbacks own an inert session.
            std::recursive_mutex gate;
            bool active{};
            std::function<void(std::wstring_view)> note_game_thread;
            std::vector<std::shared_ptr<CallbackState>> hooks;
        };

        ~Impl() { uninstall(); }

        auto register_hook(HookTargetInfo& target,
                           RuntimeProductionHookHandler handler,
                           std::wstring_view source = {}) -> std::pair<int, int>
        {
            auto state = std::make_shared<CallbackState>();
            state->target = target;
            state->handler = std::move(handler);
            state->game_thread_source = source;
            auto* function = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, target.function_path);
            if (!is_uobject_valid_fast(function)) throw std::runtime_error("Production hook function is unavailable");
            state->function = function;
            state->target.function_object = function;
            state->function_index = function->GetInternalIndex();
            state->function_has_serial = state->function.Get() == function;
            const auto dispatch = [session = session_, state](
                    UnrealScriptFunctionCallableContext& context, HookPhase phase) {
                std::lock_guard lock{session->gate};
                if (!session->active) return;
                try
                {
                    auto* function = state->resolve_function();
                    if (!is_uobject_valid_fast(function)) return;
                    state->target.function_object = static_cast<UFunction*>(function);
                    const auto note_game_thread = session->note_game_thread;
                    if (phase == HookPhase::Pre && !state->game_thread_source.empty() && note_game_thread)
                        note_game_thread(state->game_thread_source);
                    // Keep the callable alive if an owner shuts down from
                    // inside its callback on this same recursive dispatch.
                    const auto handler = state->handler;
                    if (session->active && handler) handler(context, state->target, phase);
                }
                catch (...)
                {
                    warn(
                        STR("[KH3APProbeCpp] Production hook callback failed path={}\n"), state->target.function_path);
                }
            };
            state->ids = UObjectGlobals::RegisterHook(function,
                [dispatch](auto& context, void*) { dispatch(context, HookPhase::Pre); },
                [dispatch](auto& context, void*) { dispatch(context, HookPhase::Post); },
                nullptr);
            // Capacity is reserved before registration, so tracking a live
            // pair cannot fail from a vector allocation after RegisterHook.
            session_->hooks.push_back(state);
            return state->ids;
        }

        auto uninstall() noexcept -> void
        {
            installed_ = false;
            if (!session_) return;
            const auto session = std::move(session_);
            {
                std::lock_guard lock{session->gate};
                session->active = false;
                session->note_game_thread = {};
                for (const auto& hook : session->hooks) hook->handler = {};
            }
            for (auto it = session->hooks.rbegin(); it != session->hooks.rend(); ++it)
            {
                try
                {
                    auto* function = (*it)->resolve_function();
                    if (!is_uobject_valid_fast(function)) throw std::runtime_error("Production hook function expired");
                    UObjectGlobals::UnregisterHook(static_cast<UFunction*>(function), (*it)->ids);
                }
                catch (...)
                {
                    // A function can expire during engine teardown. The
                    // disabled shared gate still prevents owner access.
                    warn(
                        STR("[KH3APProbeCpp] Production hook unregister failed path={} ids=({}, {})\n"),
                        (*it)->target.function_path, (*it)->ids.first, (*it)->ids.second);
                }
            }
        }

        auto install() -> void
        {
            if (installed_)
            {
                return;
            }

            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Hook install stage=production_registry begin\n"));

            session_ = std::make_shared<Session>();
            session_->hooks.reserve(7);
            session_->note_game_thread = config_.note_game_thread;
            try
            {
            if constexpr (s_enable_playable_context_ready_dispatch_hook &&
                          !s_enable_post_load_probe_hooks)
            {
                apply_map_name_hook_ids_ = register_hook(
                        apply_map_name_hook_info_,
                        config_.apply_map_name);
                uip_map_name_on_callback_hook_ids_ =
                        register_hook(
                                uip_map_name_on_callback_hook_info_,
                                config_.uip_map_name_callback);
            }

            if constexpr (!s_enable_post_load_probe_hooks)
            {
                tres_map_jump_hook_ids_ = register_hook(
                        tres_map_jump_hook_info_,
                        config_.tres_map_jump);
            }

            if constexpr (s_enable_starting_player_pawn_cache_hook)
            {
                handle_starting_new_player_hook_ids_ = register_hook(
                        handle_starting_new_player_hook_info_,
                        config_.handle_starting_new_player,
                        STR("HandleStartingNewPlayer"));
            }

            if constexpr (s_enable_runtime_fz02_save_point_auto_spawn)
            {
                find_player_start_hook_ids_ = register_hook(
                        find_player_start_hook_info_,
                        config_.find_player_start,
                        STR("FindPlayerStart"));
            }

            if constexpr (s_enable_cutscene_auto_skip_hooks)
            {
                tres_set_pause_menu_type_hook_ids_ =
                        register_hook(
                                tres_set_pause_menu_type_hook_info_,
                                config_.set_pause_menu_type);
            }

            if constexpr (s_enable_runtime_event_flow_probe_hooks)
            {
                tres_set_special_battle_mode_hook_ids_ =
                        register_hook(
                                tres_set_special_battle_mode_hook_info_,
                                config_.set_special_battle_mode);
            }

            {
                std::lock_guard lock{session_->gate};
                session_->active = true;
            }
            installed_ = true;
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Hook install stage=production_registry end "
                        "apply=({}, {}) map_callback=({}, {}) map_jump=({}, {}) "
                        "starting_player=({}, {}) find_player_start=({}, {}) "
                        "pause_menu=({}, {}) special_battle=({}, {})\n"),
                    apply_map_name_hook_ids_.first,
                    apply_map_name_hook_ids_.second,
                    uip_map_name_on_callback_hook_ids_.first,
                    uip_map_name_on_callback_hook_ids_.second,
                    tres_map_jump_hook_ids_.first,
                    tres_map_jump_hook_ids_.second,
                    handle_starting_new_player_hook_ids_.first,
                    handle_starting_new_player_hook_ids_.second,
                    find_player_start_hook_ids_.first,
                    find_player_start_hook_ids_.second,
                    tres_set_pause_menu_type_hook_ids_.first,
                    tres_set_pause_menu_type_hook_ids_.second,
                    tres_set_special_battle_mode_hook_ids_.first,
                    tres_set_special_battle_mode_hook_ids_.second);
            }
            catch (...)
            {
                uninstall();
                throw;
            }
        }

        RuntimeProductionHookRegistryConfig config_;
        bool installed_{false};
        std::shared_ptr<Session> session_;

        HookTargetInfo apply_map_name_hook_info_{
                std::wstring{s_apply_map_name_path},
                std::wstring{s_apply_map_name_name},
                STR(""),
                STR(""),
                nullptr};
        HookTargetInfo uip_map_name_on_callback_hook_info_{
                std::wstring{s_uip_map_name_on_callback_path},
                std::wstring{s_uip_map_name_on_callback_name},
                STR(""),
                STR(""),
                nullptr};
        HookTargetInfo tres_map_jump_hook_info_{
                std::wstring{s_tres_map_jump_path},
                std::wstring{s_tres_map_jump_name},
                STR(""),
                STR(""),
                nullptr};
        HookTargetInfo handle_starting_new_player_hook_info_{
                std::wstring{s_handle_starting_new_player_path},
                std::wstring{s_handle_starting_new_player_name},
                STR(""),
                STR(""),
                nullptr};
        HookTargetInfo find_player_start_hook_info_{
                std::wstring{s_find_player_start_path},
                std::wstring{s_find_player_start_name},
                STR("IncomingName"),
                STR("ReturnValue"),
                nullptr};
        HookTargetInfo tres_set_pause_menu_type_hook_info_{
                std::wstring{s_tres_set_pause_menu_type_path},
                std::wstring{s_tres_set_pause_menu_type_name},
                STR("Type"),
                STR(""),
                nullptr};
        HookTargetInfo tres_set_special_battle_mode_hook_info_{
                std::wstring{s_tres_set_special_battle_mode_path},
                std::wstring{s_tres_set_special_battle_mode_name},
                STR("__runtime_event_flow_probe__"),
                STR(""),
                nullptr};

        std::pair<int, int> apply_map_name_hook_ids_{};
        std::pair<int, int> uip_map_name_on_callback_hook_ids_{};
        std::pair<int, int> tres_map_jump_hook_ids_{};
        std::pair<int, int> handle_starting_new_player_hook_ids_{};
        std::pair<int, int> find_player_start_hook_ids_{};
        std::pair<int, int> tres_set_pause_menu_type_hook_ids_{};
        std::pair<int, int> tres_set_special_battle_mode_hook_ids_{};
    };

    RuntimeProductionHookRegistry::RuntimeProductionHookRegistry(
            RuntimeProductionHookRegistryConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeProductionHookRegistry::~RuntimeProductionHookRegistry() = default;
    RuntimeProductionHookRegistry::RuntimeProductionHookRegistry(
            RuntimeProductionHookRegistry&&) noexcept = default;
    auto RuntimeProductionHookRegistry::operator=(
            RuntimeProductionHookRegistry&&) noexcept
            -> RuntimeProductionHookRegistry& = default;

    auto RuntimeProductionHookRegistry::install() -> void
    {
        impl_->install();
    }

    auto RuntimeProductionHookRegistry::uninstall() noexcept -> void
    {
        if (impl_) impl_->uninstall();
    }

    auto RuntimeProductionHookRegistry::installed() const -> bool
    {
        return impl_ && impl_->installed_;
    }
}
