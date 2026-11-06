#include "kh3ap/features/story/world_fixes/kingdom_of_corona_qol_service.h"

#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/features/story/world_fixes/kingdom_of_corona_qol.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        enum class RuntimeCoronaHookKind
        {
            set_game_flag,
            set_game_flag_boolean,
            set_flag_label,
            greater_equal_flag_label,
            map_ready,
        };

        struct RuntimeCoronaHookEntry
        {
            std::wstring function_path{};
            RuntimeCoronaHookKind kind{};
            std::pair<int, int> ids{};
            bool registered{};
        };

        struct RuntimeCoronaFrameIdentity
        {
            std::wstring function_name{};
            std::wstring object_name{};
        };

        [[nodiscard]] auto collect_frame_identities(FFrame& stack)
                -> std::array<RuntimeCoronaFrameIdentity, 4>
        {
            std::array<RuntimeCoronaFrameIdentity, 4> identities{};
            auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
            for (size_t depth = 0;
                 depth < identities.size() && frame;
                 ++depth)
            {
                (void) try_get_uobject_full_name_fast(
                        frame->Node,
                        identities[depth].function_name);
                (void) try_get_uobject_full_name_fast(
                        frame->Object,
                        identities[depth].object_name);
                auto* const previous = frame->PreviousFrame;
                if (previous == frame)
                {
                    break;
                }
                frame = previous;
            }
            return identities;
        }
    }

    struct RuntimeKingdomOfCoronaQolService::Impl
    {
        explicit Impl(RuntimeKingdomOfCoronaQolConfig value)
            : config(std::move(value)),
              hooks{{
                      {
                              std::wstring{config.set_game_flag_path},
                              RuntimeCoronaHookKind::set_game_flag,
                      },
                      {
                              std::wstring{config.set_game_flag_boolean_path},
                              RuntimeCoronaHookKind::set_game_flag_boolean,
                      },
                      {
                              std::wstring{config.set_flag_label_path},
                              RuntimeCoronaHookKind::set_flag_label,
                      },
                      {
                              std::wstring{
                                      config.greater_equal_flag_label_path},
                              RuntimeCoronaHookKind::greater_equal_flag_label,
                      },
                      {
                              std::wstring{config.map_ready_event_path},
                              RuntimeCoronaHookKind::map_ready,
                      },
              }}
        {
        }

        ~Impl()
        {
            uninstall();
        }

        [[nodiscard]] auto set_companion_flag(
                const RuntimeKingdomOfCoronaStoryTransition& transition,
                std::wstring_view source) -> bool
        {
            std::wstring failure_reason{};
            const bool applied = try_call_set_game_flag(
                    transition.companion_flag,
                    transition.companion_value,
                    config.runtime_state,
                    config.set_game_flag_path,
                    config.blueprint_library_default_object_path,
                    config.debug_logging,
                    &failure_reason);
            if (!applied)
            {
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] CoronaQol companion write failed "
                            "source={} flag={} value={} reason={}\n"),
                        source,
                        transition.companion_flag,
                        transition.companion_value,
                        failure_reason);
            }
            return applied;
        }

        auto log_transition(
                const RuntimeKingdomOfCoronaStoryTransition& transition,
                std::wstring_view source) -> void
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] CoronaQol story transition "
                        "source={} gameflow_RA={}->{} companion={}={} "
                        "mode=event_driven no_update=true\n"),
                    source,
                    transition.observed_value,
                    transition.target_value,
                    transition.companion_flag,
                    transition.companion_value);
        }

        auto reconcile_story(std::wstring_view source) -> void
        {
            int32_t story_value{};
            std::wstring failure_reason{};
            if (!try_call_get_game_flag(
                        runtime_corona_story_flag,
                        story_value,
                        config.runtime_state,
                        config.get_game_flag_path,
                        config.blueprint_library_default_object_path,
                        config.debug_logging,
                        &failure_reason))
            {
                if (config.debug_logging)
                {
                    Output::send<LogLevel::Verbose>(
                            STR("[KH3APProbeCpp] CoronaQol story query failed "
                                "source={} reason={}\n"),
                            source,
                            failure_reason);
                }
                return;
            }

            const auto* const transition =
                    find_runtime_corona_story_transition(
                            runtime_corona_story_flag,
                            story_value);
            if (!transition ||
                !set_companion_flag(*transition, source))
            {
                return;
            }

            failure_reason.clear();
            if (!try_call_set_game_flag(
                        runtime_corona_story_flag,
                        transition->target_value,
                        config.runtime_state,
                        config.set_game_flag_path,
                        config.blueprint_library_default_object_path,
                        config.debug_logging,
                        &failure_reason))
            {
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] CoronaQol story write failed "
                            "source={} value={} reason={}\n"),
                        source,
                        transition->target_value,
                        failure_reason);
                return;
            }
            log_transition(*transition, source);
        }

        auto reconcile_camera(std::wstring_view source) -> void
        {
            bool camera_complete{};
            std::wstring failure_reason{};
            if (!try_call_get_game_flag_boolean(
                        runtime_corona_powerwild_camera_flag,
                        camera_complete,
                        config.runtime_state,
                        config.get_game_flag_boolean_path,
                        config.blueprint_library_default_object_path,
                        config.debug_logging,
                        &failure_reason) ||
                camera_complete)
            {
                if (config.debug_logging && !failure_reason.empty())
                {
                    Output::send<LogLevel::Verbose>(
                            STR("[KH3APProbeCpp] CoronaQol camera query failed "
                                "source={} reason={}\n"),
                            source,
                            failure_reason);
                }
                return;
            }

            failure_reason.clear();
            if (try_call_set_game_flag_boolean(
                        runtime_corona_powerwild_camera_flag,
                        true,
                        config.runtime_state,
                        config.set_game_flag_boolean_path,
                        config.blueprint_library_default_object_path,
                        config.debug_logging,
                        &failure_reason))
            {
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] CoronaQol Powerwild camera flag "
                            "forced=true source={} "
                            "mode=event_driven no_update=true\n"),
                        source);
                return;
            }

            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] CoronaQol camera write failed "
                        "source={} reason={}\n"),
                    source,
                    failure_reason);
        }

        auto override_rabbit_passage_gameflow_result(
                UnrealScriptFunctionCallableContext& context,
                const RuntimeCoronaHookEntry& entry) -> void
        {
            const auto frame_identities =
                    collect_frame_identities(context.TheStack);
            const auto matching_identity = std::find_if(
                    frame_identities.begin(),
                    frame_identities.end(),
                    [](const RuntimeCoronaFrameIdentity& identity) {
                        return is_runtime_corona_rabbit_passage_gate_callsite(
                                identity.function_name,
                                identity.object_name);
                    });
            if (matching_identity == frame_identities.end())
            {
                return;
            }
            const std::wstring& frame_name =
                    matching_identity->function_name;
            const std::wstring& caller_object_name =
                    matching_identity->object_name;

            const std::wstring lookup_path{entry.function_path};
            UFunction* const function_object =
                    UObjectGlobals::StaticFindObject<UFunction*>(
                            nullptr,
                            nullptr,
                            lookup_path.c_str());
            const bool function_valid =
                    function_object &&
                    is_uobject_valid_fast(function_object);
            if (!function_valid)
            {
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] CoronaQol rabbit passage "
                            "gameflow gate unavailable frame={} "
                            "caller={} function_valid={}\n"),
                        frame_name,
                        caller_object_name,
                        function_valid);
                return;
            }

            FProperty* const return_property =
                    function_object->GetReturnProperty();
            const bool return_is_bool =
                    return_property &&
                    return_property->IsA<FBoolProperty>();
            if (!context.RESULT_DECL || !return_is_bool)
            {
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] CoronaQol rabbit passage "
                            "gameflow gate override failed frame={} "
                            "caller={} result_storage={} "
                            "return_bool={}\n"),
                        frame_name,
                        caller_object_name,
                        context.RESULT_DECL != nullptr,
                        return_is_bool);
                return;
            }

            auto* const bool_property =
                    static_cast<FBoolProperty*>(return_property);
            const bool original_result =
                    bool_property->GetPropertyValue(context.RESULT_DECL);
            bool_property->SetPropertyValue(context.RESULT_DECL, true);
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] CoronaQol rabbit passage gameflow "
                        "gate source=GreaterEqual_FlagLabel frame={} "
                        "caller={} original={} overridden=true "
                        "mode=callsite_scoped no_flag_write=true "
                        "no_actor_patch=true\n"),
                    frame_name,
                    caller_object_name,
                    original_result);
        }

        [[nodiscard]] auto is_corona_event(
                UnrealScriptFunctionCallableContext& context) const -> bool
        {
            std::wstring frame_name{};
            return try_get_uobject_full_name_fast(
                           get_frame_node(context.TheStack),
                           frame_name) &&
                   is_runtime_corona_event_frame(frame_name);
        }

        auto handle_post(
                UnrealScriptFunctionCallableContext& context,
                RuntimeCoronaHookEntry& entry) -> void
        {
            if (entry.kind ==
                RuntimeCoronaHookKind::greater_equal_flag_label)
            {
                override_rabbit_passage_gameflow_result(context, entry);
                return;
            }
            if (RuntimeGrantInternalCallDepth::is_active())
            {
                return;
            }
            if (entry.kind == RuntimeCoronaHookKind::map_ready)
            {
                reconcile_story(STR("MapNameOnCallback"));
                reconcile_camera(STR("MapNameOnCallback"));
                return;
            }
            if (!is_corona_event(context))
            {
                return;
            }
            switch (entry.kind)
            {
            case RuntimeCoronaHookKind::set_game_flag:
                reconcile_story(STR("SetGameFlagPost"));
                break;
            case RuntimeCoronaHookKind::set_flag_label:
                reconcile_story(STR("SetFlagLabelPost"));
                break;
            case RuntimeCoronaHookKind::set_game_flag_boolean:
                reconcile_camera(STR("SetGameFlagBooleanPost"));
                break;
            case RuntimeCoronaHookKind::greater_equal_flag_label:
            case RuntimeCoronaHookKind::map_ready:
                break;
            }
        }

        auto register_hook(RuntimeCoronaHookEntry& entry) -> void
        {
            if (entry.function_path.empty())
            {
                return;
            }
            try
            {
                entry.ids = UObjectGlobals::RegisterHook(
                        entry.function_path,
                        [](
                                UnrealScriptFunctionCallableContext&,
                                void*) {},
                        [this](
                                UnrealScriptFunctionCallableContext& context,
                                void* custom_data) {
                            try
                            {
                                if (custom_data)
                                {
                                    handle_post(
                                            context,
                                            *static_cast<
                                                    RuntimeCoronaHookEntry*>(
                                                    custom_data));
                                }
                            }
                            catch (...)
                            {
                            }
                        },
                        &entry);
                entry.registered = true;
            }
            catch (...)
            {
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] CoronaQol hook registration "
                            "failed function={}\n"),
                        entry.function_path);
            }
        }

        auto install_once() -> void
        {
            if (!config.enabled || installed)
            {
                return;
            }

            installed = true;
            size_t registered_count{};
            for (auto& hook : hooks)
            {
                register_hook(hook);
                registered_count += hook.registered ? 1 : 0;
            }
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] CoronaQol hooks installed={}/{} "
                        "triggers=flag_setter_post_events+map_ready_event+"
                        "callsite_gameflow_gate no_actor_patch=true "
                        "no_update=true no_periodic_poll=true\n"),
                    static_cast<uint64_t>(registered_count),
                    static_cast<uint64_t>(hooks.size()));
        }

        auto uninstall() -> void
        {
            if (!installed)
            {
                return;
            }
            for (auto& hook : hooks)
            {
                if (!hook.registered)
                {
                    continue;
                }
                try
                {
                    UObjectGlobals::UnregisterHook(
                            hook.function_path,
                            hook.ids);
                }
                catch (...)
                {
                }
                hook.registered = false;
            }
            installed = false;
        }

        RuntimeKingdomOfCoronaQolConfig config;
        std::array<RuntimeCoronaHookEntry, 5> hooks{};
        bool installed{};
    };

    RuntimeKingdomOfCoronaQolService::RuntimeKingdomOfCoronaQolService(
            RuntimeKingdomOfCoronaQolConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeKingdomOfCoronaQolService::~RuntimeKingdomOfCoronaQolService() =
            default;
    RuntimeKingdomOfCoronaQolService::RuntimeKingdomOfCoronaQolService(
            RuntimeKingdomOfCoronaQolService&&) noexcept = default;
    auto RuntimeKingdomOfCoronaQolService::operator=(
            RuntimeKingdomOfCoronaQolService&&) noexcept
            -> RuntimeKingdomOfCoronaQolService& = default;

    auto RuntimeKingdomOfCoronaQolService::install_once() -> void
    {
        impl_->install_once();
    }

    auto RuntimeKingdomOfCoronaQolService::uninstall() -> void
    {
        impl_->uninstall();
    }
}
