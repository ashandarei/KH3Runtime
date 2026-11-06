#include "kh3ap/diagnostics/ability_probe/manual_ability_state_probe_helpers.h"

#include "kh3ap/diagnostics/output/diagnostic_probe_serializer.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/features/grants/abilities/ability_diagnostic_state.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/diagnostics/diagnostic_state.h"
#include "kh3ap/features/player/objects/object_cache.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/UObject.hpp>

#include <filesystem>
#include <string>
#include <utility>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;

    auto make_manual_ability_state_probe_deps(
            RuntimeState& runtime_state,
            std::optional<std::filesystem::path> bridge_dir,
            std::wstring_view request_filename,
            std::wstring_view result_filename,
            std::wstring_view protocol_name,
            int64_t protocol_version,
            std::wstring_view runtime_context_summary,
            std::wstring_view build_stamp,
            ManualAbilityStateProbeEmitter ability_array_needle_probe_emitter,
            ManualAbilityStateProbeEmitter targeted_sora_state_probe_emitter,
            ManualAbilityStateProbeEmitter live_ability_owner_probe_emitter) -> ManualAbilityStateProbeDeps
    {
        return {
                runtime_state,
                std::move(bridge_dir),
                request_filename,
                result_filename,
                protocol_name,
                protocol_version,
                runtime_context_summary,
                build_stamp,
                ability_array_needle_probe_emitter,
                targeted_sora_state_probe_emitter,
                live_ability_owner_probe_emitter};
    }

    namespace
    {
        auto write_manual_ability_state_probe_result(
                const ManualAbilityStateProbeDeps& deps,
                std::wstring_view request_id,
                std::wstring_view status,
                std::wstring_view reason,
                bool include_live_owner) -> bool
        {
            if (!deps.bridge_dir.has_value())
            {
                return false;
            }

            const std::string json = DiagnosticProbeSerializer::serialize_manual_ability_state_probe_result(
                    deps.protocol_name,
                    deps.protocol_version,
                    request_id,
                    status,
                    reason,
                    include_live_owner,
                    deps.runtime_context_summary,
                    deps.build_stamp);
            return write_text_file(*deps.bridge_dir / std::filesystem::path{deps.result_filename}, json);
        }
    }

    auto process_pending_manual_ability_state_probe(const ManualAbilityStateProbeDeps& deps) -> void
    {
        if (!deps.bridge_dir.has_value())
        {
            return;
        }

        const auto request_path = *deps.bridge_dir / std::filesystem::path{deps.request_filename};
        if (!std::filesystem::exists(request_path))
        {
            return;
        }

        const auto request_text = read_text_file(request_path);
        const std::wstring request_id = request_text.has_value()
                                                ? JsonFieldReader::string_field(*request_text, "request_id").value_or(std::wstring{})
                                                : std::wstring{};
        const std::wstring label = request_text.has_value()
                                           ? JsonFieldReader::string_field(*request_text, "label").value_or(std::wstring{})
                                           : std::wstring{};
        const bool include_live_owner_requested = request_text.has_value()
                                                          ? JsonFieldReader::bool_field(*request_text, "include_live_owner").value_or(false)
                                                          : false;
        const bool targeted_only = request_text.has_value()
                                             ? JsonFieldReader::bool_field(*request_text, "targeted_only").value_or(false)
                                             : false;
        // The broad live-owner scan can walk into volatile objects around
        // save/menu transitions. Keep manual probes on the narrower scans.
        const bool include_live_owner = false;

        const std::wstring effective_request_id = request_id.empty()
                                                          ? std::wstring{STR("manual-ability-state-probe")}
                                                          : request_id;
        std::wstring reason = std::wstring{STR("manual_ability_state_probe:")} + effective_request_id;
        if (!label.empty())
        {
            reason += STR(":");
            reason += label;
        }

        UObject* const last_world_context_object = deps.runtime_state.object_cache().last_world_context_object();
        UObject* const preferred_object =
                last_world_context_object && kh3ap::is_uobject_valid_fast(last_world_context_object)
                        ? last_world_context_object
                        : nullptr;

        try_remove_file(request_path);
        write_manual_ability_state_probe_result(
                deps,
                effective_request_id,
                include_live_owner_requested ? STR("running_live_owner_ignored") : STR("running"),
                reason,
                include_live_owner);

        Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] ManualAbilityStateProbe begin request_id={} include_live_owner={} requested_live_owner={} preferred={} context={}\n"),
                effective_request_id,
                include_live_owner ? STR("true") : STR("false"),
                include_live_owner_requested ? STR("true") : STR("false"),
                preferred_object ? format_object_debug_name(preferred_object) : std::wstring{STR("<null>")},
                deps.runtime_context_summary);

        if (!targeted_only)
        {
            deps.runtime_state.ability_diagnostic_state().probe_diagnostic_state().reset_ability_array_needle_probe_key();
            deps.runtime_state.ability_diagnostic_state().probe_diagnostic_state().set_ability_array_needle_force_enabled(true);
            if (deps.ability_array_needle_probe_emitter)
            {
                deps.ability_array_needle_probe_emitter(reason, preferred_object);
            }
            deps.runtime_state.ability_diagnostic_state().probe_diagnostic_state().set_ability_array_needle_force_enabled(false);
        }
        if (deps.targeted_sora_state_probe_emitter)
        {
            deps.targeted_sora_state_probe_emitter(reason, preferred_object);
        }

        if (include_live_owner && deps.live_ability_owner_probe_emitter)
        {
            deps.runtime_state.ability_diagnostic_state().probe_diagnostic_state().reset_live_owner_probe_key();
            deps.runtime_state.ability_diagnostic_state().probe_diagnostic_state().set_live_owner_force_enabled(true);
            deps.live_ability_owner_probe_emitter(reason, preferred_object);
            deps.runtime_state.ability_diagnostic_state().probe_diagnostic_state().set_live_owner_force_enabled(false);
        }

        write_manual_ability_state_probe_result(deps, effective_request_id, STR("completed"), reason, include_live_owner);
        Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] ManualAbilityStateProbe completed request_id={} include_live_owner={} requested_live_owner={}\n"),
                effective_request_id,
                include_live_owner ? STR("true") : STR("false"),
                include_live_owner_requested ? STR("true") : STR("false"));
    }
}
