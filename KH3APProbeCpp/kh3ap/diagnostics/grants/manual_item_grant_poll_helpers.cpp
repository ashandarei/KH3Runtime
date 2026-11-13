#include "kh3ap/diagnostics/grants/manual_item_grant_poll_helpers.h"
#include "kh3ap/diagnostics/grants/manual_grant_file_reader.h"

#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/features/grants/grant_safety.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/features/grants/grant_tokens.h"
#include "kh3ap/features/grants/trap_catalog.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/objects/object_cache.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include "kh3ap/features/treasure/treasure_class_cache.h"
#include "kh3ap/features/treasure/treasure_runtime_state.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/UObject.hpp>

#include <filesystem>
#include <format>
#include <string>
#include <utility>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        auto manual_grant_file_reader() -> ManualGrantFileReader&
        {
            static ManualGrantFileReader reader;
            return reader;
        }
    }

    auto make_manual_runtime_item_grant_deps(
            RuntimeState& runtime_state,
            std::optional<std::filesystem::path> bridge_dir,
            std::wstring_view manual_request_filename,
            std::wstring_view manual_result_filename,
            std::wstring_view protocol_name,
            int64_t protocol_version,
            RuntimeItemGrantApplyDeps apply_deps,
            ManualRuntimeLiveAbilityOwnerProbeEmitter live_ability_owner_probe_emitter,
            bool emit_pending_results) -> ManualRuntimeItemGrantDeps
    {
        return {
                runtime_state,
                std::move(bridge_dir),
                manual_request_filename,
                manual_result_filename,
                protocol_name,
                protocol_version,
                apply_deps,
                live_ability_owner_probe_emitter,
                emit_pending_results};
    }

    auto process_armed_manual_runtime_item_grant(
            RuntimeState& runtime_state,
            std::chrono::steady_clock::duration ready_delay,
            const ManualRuntimeItemGrantProcessor& processor) -> void
    {
        if (!processor)
        {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (!runtime_state.grant_state().manual_item_grant_state().consume_if_ready(ready_delay, now))
        {
            return;
        }

        processor(runtime_state.object_cache().last_world_context_object());
    }

    auto process_pending_manual_runtime_item_grant_fallback(
            RuntimeState& runtime_state,
            const std::optional<std::filesystem::path>& bridge_dir,
            std::wstring_view request_filename,
            std::chrono::steady_clock::duration arm_grace,
            std::chrono::steady_clock::duration fallback_cooldown,
            std::wstring_view runtime_context_summary,
            const ManualRuntimeItemGrantProcessor& processor) -> void
    {
        if (!processor || !bridge_dir.has_value())
        {
            return;
        }

        const auto request_path = *bridge_dir / std::filesystem::path{request_filename};
        if (!path_points_to_regular_file(request_path))
        {
            return;
        }

        UObject* const preferred_context = RuntimeObjectResolver::try_get_cached_world_context_object(
                runtime_state.object_cache(),
                runtime_state.treasure_state().class_cache().actor_base_class());

        const auto now = std::chrono::steady_clock::now();
        if (runtime_state.grant_state().manual_item_grant_state().within_arm_grace(arm_grace, now))
        {
            return;
        }

        if (!runtime_state.grant_state().manual_item_grant_state().can_attempt_fallback(fallback_cooldown, now) &&
            !manual_grant_file_reader().ready())
        {
            return;
        }

        runtime_state.grant_state().manual_item_grant_state().mark_fallback_attempt(now);
        Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Manual runtime item fallback poll fired preferred_context={} | context={}\n"),
                preferred_context && kh3ap::is_uobject_valid_fast(preferred_context)
                        ? format_object_debug_name(preferred_context)
                        : std::wstring{STR("<null>")},
                std::wstring{runtime_context_summary});
        processor(preferred_context);
    }

    auto process_pending_manual_runtime_item_grant(
            const ManualRuntimeItemGrantDeps& deps,
            UObject* preferred_world_context_object) -> void
    {
        if (!deps.bridge_dir.has_value())
        {
            return;
        }

        const auto request_path = *deps.bridge_dir / std::filesystem::path{deps.manual_request_filename};
        if (!path_points_to_regular_file(request_path))
        {
            return;
        }

        const auto* request_text = [&] {
            const RuntimeHotPathProfileScope profile{L"manual_item_grant:request_ready"};
            return manual_grant_file_reader().poll(request_path, read_text_file);
        }();
        if (!request_text)
        {
            return;
        }

        Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Manual runtime item handler entered\n"));
        const auto request = [&] {
            const RuntimeHotPathProfileScope parse_profile{L"manual_item_grant:parse_request"};
            return parse_runtime_item_grant_request_from_text(*request_text);
        }();
        if (!request.has_value())
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Manual runtime item handler aborted reason=request_parse_failed\n"));
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const RuntimeGrantSafetyStatus special_battle_mode_status =
                current_runtime_special_battle_mode_status(deps.runtime_state.grant_state(), now);
        const bool allow_special_battle_bad_status_trap =
                supports_alternate_bad_status(request->runtime_token, request->grant_method);
        auto* alternate = allow_special_battle_bad_status_trap
                ? character_swap::bad_status_context() : nullptr;
        if (!special_battle_mode_status.ready && !allow_special_battle_bad_status_trap)
        {
            if (deps.emit_pending_results)
            {
                write_manual_runtime_item_grant_result(
                        deps.bridge_dir,
                        deps.manual_result_filename,
                        *request,
                        STR("pending_prereq"),
                        special_battle_mode_status.details.empty()
                                ? special_battle_mode_status.reason
                                : std::format(STR("{}:{}"), special_battle_mode_status.reason, special_battle_mode_status.details),
                        deps.protocol_name,
                        deps.protocol_version);
            }
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Manual runtime item pending reason={} details={}\n"),
                    special_battle_mode_status.reason,
                    special_battle_mode_status.details);
            return;
        }

        if (allow_special_battle_bad_status_trap)
        {
            const RuntimeGrantSafetyStatus trap_state_status =
                    alternate ? runtime_alternate_bad_status_safety_status(deps.runtime_state, alternate)
                    : current_runtime_bad_status_trap_sora_state_cache_status(
                            deps.apply_deps.sora_current_state_cache);
            if (!trap_state_status.ready)
            {
                if (deps.emit_pending_results)
                {
                    write_manual_runtime_item_grant_result(
                            deps.bridge_dir,
                            deps.manual_result_filename,
                            *request,
                            STR("pending_prereq"),
                            trap_state_status.details.empty()
                                    ? trap_state_status.reason
                                    : std::format(STR("{}:{}"), trap_state_status.reason, trap_state_status.details),
                            deps.protocol_name,
                            deps.protocol_version);
                }
                return;
            }
        }

        const auto world_context_candidates = [&] {
            const RuntimeHotPathProfileScope profile{L"manual_item_grant:context"};
            if (alternate)
            {
                return std::vector<UObject*>{alternate};
            }
            return collect_fast_runtime_item_grant_world_context_candidates(deps.runtime_state, preferred_world_context_object);
        }();
        if (world_context_candidates.empty())
        {
            write_manual_runtime_item_grant_result(
                    deps.bridge_dir,
                    deps.manual_result_filename,
                    *request,
                    STR("no_world_context"),
                    STR("No cached WorldContextObject at TresShowMapName post"),
                    deps.protocol_name,
                    deps.protocol_version);
            try_remove_file(request_path);
            return;
        }

        const std::wstring bundle_summary = RuntimeGrantTokens::build_bundle_summary(
                request->bundle_display,
                request->randomized_summary,
                request->randomized_tokens,
                request->runtime_token);

        Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Manual runtime item step=try index={} method={}\n"),
                request->index,
                request->grant_method);

        std::wstring failure_details{};
        RuntimeItemApplyPayload payload{};
        UObject* applied_world_context_object = nullptr;
        const bool applied = [&] {
            const RuntimeHotPathProfileScope profile{L"manual_item_grant:apply"};
            return try_apply_runtime_item_grant_request(
                    *request,
                    world_context_candidates,
                    deps.apply_deps,
                    &payload,
                    &failure_details,
                    &applied_world_context_object);
        }();
        if (applied)
        {
            const RuntimeHotPathProfileScope profile{L"manual_item_grant:complete"};
            Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] Manual runtime item step=write_applied_result\n"));
            write_manual_runtime_item_grant_result(
                    deps.bridge_dir,
                    deps.manual_result_filename,
                    *request,
                    STR("applied"),
                    STR(""),
                    deps.protocol_name,
                    deps.protocol_version);
            try_remove_file(request_path);
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Applied manual runtime item request_id={} item={} token={} method={} bundle={}\n"),
                    request->request_id.empty() ? std::wstring{STR("<none>")} : request->request_id,
                    request->item_name,
                    request->runtime_token,
                    request->grant_method,
                    bundle_summary.empty() ? std::wstring{STR("<none>")} : bundle_summary);
            if (request->runtime_token.starts_with(STR("ETresAbilityKind::")) && deps.live_ability_owner_probe_emitter)
            {
                deps.live_ability_owner_probe_emitter(
                        std::format(STR("post_manual_runtime_grant:{}"), request->runtime_token),
                        applied_world_context_object);
            }
            return;
        }

        failure_details = failure_details.empty() ? std::wstring{STR("grant_failed")} : truncate_for_log(failure_details, 96);
        Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Manual runtime item candidate failed method={} details={} world={}\n"),
                request->grant_method,
                failure_details,
                applied_world_context_object && kh3ap::is_uobject_valid_fast(applied_world_context_object)
                        ? format_object_debug_name(applied_world_context_object)
                        : std::wstring{STR("<null>")});

        write_manual_runtime_item_grant_result(
                deps.bridge_dir,
                deps.manual_result_filename,
                *request,
                STR("call_failed"),
                failure_details,
                deps.protocol_name,
                deps.protocol_version);
        try_remove_file(request_path);
    }
}
