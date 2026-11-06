#include "kh3ap/app/config.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/story/world_fixes/fz02_player_start_service.h"

#include "kh3ap/hooks/hook_phase.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/story/save_points/save_point_spawn_helpers.h"
#include "kh3ap/core/types.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>

#include <string>
#include <utility>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        using ::kh3ap::settings::s_runtime_fz02_player_start_yaw;
        using ::kh3ap::settings::s_runtime_fz02_player_start_z;
        using ::kh3ap::settings::s_runtime_fz02_save_point_auto_spawn_disp_map_name;
        using ::kh3ap::settings::s_runtime_fz02_save_point_auto_spawn_map_path;
        using ::kh3ap::settings::s_runtime_fz02_save_point_auto_spawn_player_start_tag;
        using ::kh3ap::settings::s_runtime_fz02_save_point_auto_spawn_x;
        using ::kh3ap::settings::s_runtime_fz02_save_point_auto_spawn_y;
    }

    struct RuntimeFz02PlayerStartService::Impl
    {
        explicit Impl(RuntimeFz02PlayerStartConfig value)
            : config(std::move(value))
        {
        }

        auto is_selector_context(UObject* context) const -> bool
        {
            if (!context || !kh3ap::is_uobject_valid_fast(context))
            {
                return false;
            }

            const auto is_fz02_context = [](std::wstring_view value) {
                return value.find(s_runtime_fz02_save_point_auto_spawn_map_path) !=
                       std::wstring_view::npos;
            };
            return is_fz02_context(format_object_debug_name(context)) ||
                   is_fz02_context(format_object_outer_chain(context, 6));
        }

        auto ensure_selector_player_start(
                UObject* context,
                std::wstring_view source) -> UObject*
        {
            if (!is_selector_context(context))
            {
                return nullptr;
            }

            if (selector_player_start &&
                kh3ap::is_uobject_valid_fast(selector_player_start) &&
                selector_player_start_context == context)
            {
                return selector_player_start;
            }

            RuntimePlayerStartRouteSpawnResult result{};
            UObject* const player_start = spawn_runtime_player_start_for_route(
                    context,
                    s_runtime_fz02_save_point_auto_spawn_player_start_tag,
                    s_runtime_fz02_save_point_auto_spawn_disp_map_name,
                    s_runtime_fz02_save_point_auto_spawn_x,
                    s_runtime_fz02_save_point_auto_spawn_y,
                    s_runtime_fz02_player_start_z,
                    s_runtime_fz02_player_start_yaw,
                    &result);
            selector_player_start =
                    player_start && kh3ap::is_uobject_valid_fast(player_start) ? player_start : nullptr;
            selector_player_start_context = selector_player_start ? context : nullptr;

            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] FZ02 selector PlayerStart ensure source={} status={} reason={} tag={} actor={} property={} class_source={} target=({:.3f},{:.3f},{:.3f}) yaw={:.1f} context={}\n"),
                    source,
                    result.status.empty() ? STR("failed") : result.status,
                    result.reason.empty() ? STR("<none>") : result.reason,
                    result.tag.empty()
                            ? std::wstring{s_runtime_fz02_save_point_auto_spawn_player_start_tag}
                            : result.tag,
                    result.actor.empty()
                            ? format_object_debug_name(player_start)
                            : result.actor,
                    result.player_start_property.empty()
                            ? STR("<none>")
                            : result.player_start_property,
                    result.class_resolution_source.empty()
                            ? STR("<none>")
                            : result.class_resolution_source,
                    s_runtime_fz02_save_point_auto_spawn_x,
                    s_runtime_fz02_save_point_auto_spawn_y,
                    s_runtime_fz02_player_start_z,
                    s_runtime_fz02_player_start_yaw,
                    truncate_for_log(format_object_debug_name(context), 260));
            return selector_player_start;
        }

        auto handle_find_player_start(
                UnrealScriptFunctionCallableContext& callback_context,
                HookTargetInfo& hook_info,
                HookPhase phase) -> void
        {
            if (phase != HookPhase::Pre && phase != HookPhase::Post)
            {
                return;
            }

            void* const params = get_frame_locals(callback_context.TheStack);
            UFunction* const target_function = config.function_resolver
                    ? config.function_resolver(hook_info)
                    : nullptr;
            const std::wstring incoming_name = read_function_param_value_as_string(
                    target_function,
                    params,
                    STR("IncomingName"));
            if (incoming_name != s_runtime_fz02_save_point_auto_spawn_player_start_tag)
            {
                return;
            }

            UObject* const context = callback_context.Context;
            const bool map_context_matches = is_selector_context(context);
            UObject* const requested_start = map_context_matches
                    ? ensure_selector_player_start(
                              context,
                              phase == HookPhase::Pre
                                      ? std::wstring_view{STR("FindPlayerStart:Pre")}
                                      : std::wstring_view{STR("FindPlayerStart:Post")})
                    : nullptr;
            if (phase == HookPhase::Pre)
            {
                return;
            }

            UObject* const original_start = callback_context.RESULT_DECL
                    ? *static_cast<UObject**>(callback_context.RESULT_DECL)
                    : nullptr;
            const bool requested_start_available =
                    requested_start && kh3ap::is_uobject_valid_fast(requested_start);
            const auto decision = decide_runtime_player_start_selection(
                    incoming_name,
                    s_runtime_fz02_save_point_auto_spawn_player_start_tag,
                    map_context_matches,
                    requested_start_available,
                    original_start == requested_start);
            if (decision == RuntimePlayerStartSelectionDecision::Ignore)
            {
                return;
            }

            UObject* final_start = original_start;
            std::wstring_view status{STR("verified")};
            if (decision == RuntimePlayerStartSelectionDecision::RequestedActorUnavailable)
            {
                status = STR("requested_actor_unavailable");
            }
            else if (decision == RuntimePlayerStartSelectionDecision::OverrideSelected)
            {
                if (callback_context.RESULT_DECL)
                {
                    *static_cast<UObject**>(callback_context.RESULT_DECL) = requested_start;
                    final_start = requested_start;
                    status = STR("overridden");
                }
                else
                {
                    status = STR("missing_result_storage");
                }
            }

            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] FZ02 FindPlayerStart selection status={} tag={} original={} requested={} final={} context={}\n"),
                    status,
                    incoming_name,
                    truncate_for_log(format_object_debug_name(original_start), 300),
                    truncate_for_log(format_object_debug_name(requested_start), 300),
                    truncate_for_log(format_object_debug_name(final_start), 300),
                    truncate_for_log(format_object_debug_name(context), 300));
        }

        RuntimeFz02PlayerStartConfig config;
        UObject* selector_player_start{};
        UObject* selector_player_start_context{};
    };

    RuntimeFz02PlayerStartService::RuntimeFz02PlayerStartService(
            RuntimeFz02PlayerStartConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeFz02PlayerStartService::~RuntimeFz02PlayerStartService() = default;
    RuntimeFz02PlayerStartService::RuntimeFz02PlayerStartService(
            RuntimeFz02PlayerStartService&&) noexcept = default;
    auto RuntimeFz02PlayerStartService::operator=(
            RuntimeFz02PlayerStartService&&) noexcept
            -> RuntimeFz02PlayerStartService& = default;

    auto RuntimeFz02PlayerStartService::handle_find_player_start(
            UnrealScriptFunctionCallableContext& callback_context,
            HookTargetInfo& hook_info,
            HookPhase phase) -> void
    {
        impl_->handle_find_player_start(callback_context, hook_info, phase);
    }

    auto RuntimeFz02PlayerStartService::reset_map_lifecycle() -> void
    {
        impl_->selector_player_start = nullptr;
        impl_->selector_player_start_context = nullptr;
    }
}
