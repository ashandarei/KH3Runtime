#include "kh3ap/features/player/state/sora_position_probe.h"

#include "kh3ap/features/player/context/context_helpers.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <iomanip>
#include <optional>
#include <sstream>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        struct RuntimeVectorSample
        {
            double x{};
            double y{};
            double z{};
            int element_size{};
            std::wstring property_class{};
            std::wstring struct_name{};
            std::wstring numeric_layout{};
        };

        auto append_json_string_field(std::ostringstream& json, std::string_view key, std::wstring_view value) -> void
        {
            json << ",\"" << key << "\":\"" << json_escape(value) << "\"";
        }

        auto append_json_number_field(std::ostringstream& json, std::string_view key, double value) -> void
        {
            json << ",\"" << key << "\":" << std::fixed << std::setprecision(3) << value;
        }

        auto try_get_actor_location(
                UObject* pawn,
                std::wstring_view actor_location_function_path,
                std::wstring* failure_reason) -> std::optional<RuntimeVectorSample>
        {
            if (!pawn || !kh3ap::is_uobject_valid_fast(pawn))
            {
                if (failure_reason)
                {
                    *failure_reason = STR("invalid_pawn");
                }
                return std::nullopt;
            }
            if (!RuntimeObjectResolver::is_sora_ability_pawn_candidate(pawn))
            {
                if (failure_reason)
                {
                    *failure_reason = STR("pawn_not_sora_candidate");
                }
                return std::nullopt;
            }

            UFunction* const function_object =
                    UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, actor_location_function_path);
            if (!function_object)
            {
                if (failure_reason)
                {
                    *failure_reason = STR("missing_actor_location_function");
                }
                return std::nullopt;
            }

            FProperty* const return_property = find_function_param_by_name(function_object, STR("ReturnValue"));
            if (!return_property)
            {
                if (failure_reason)
                {
                    *failure_reason = STR("missing_return_value_property");
                }
                return std::nullopt;
            }

            std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
            std::fill(params.begin(), params.end(), std::byte{0});
            {
                const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                pawn->ProcessEvent(function_object, params.data());
            }

            void* const value_ptr = return_property->ContainerPtrToValuePtr<void>(params.data());
            const int element_size = return_property->GetElementSize();
            if (!value_ptr || element_size < static_cast<int>(sizeof(float) * 3))
            {
                if (failure_reason)
                {
                    *failure_reason = std::format(STR("invalid_return_value_size:{}"), element_size);
                }
                return std::nullopt;
            }

            RuntimeVectorSample sample{};
            sample.element_size = element_size;
            sample.property_class = return_property->GetClass().GetName();
            if (sample.property_class == STR("StructProperty"))
            {
                if (auto* const struct_property = static_cast<FStructProperty*>(return_property))
                {
                    if (UScriptStruct* const script_struct = struct_property->GetStruct())
                    {
                        sample.struct_name = script_struct->GetFullName();
                    }
                }
            }

            if (element_size >= static_cast<int>(sizeof(double) * 3))
            {
                double values[3]{};
                std::memcpy(values, value_ptr, sizeof(values));
                sample.x = values[0];
                sample.y = values[1];
                sample.z = values[2];
                sample.numeric_layout = STR("double3");
            }
            else
            {
                float values[3]{};
                std::memcpy(values, value_ptr, sizeof(values));
                sample.x = static_cast<double>(values[0]);
                sample.y = static_cast<double>(values[1]);
                sample.z = static_cast<double>(values[2]);
                sample.numeric_layout = STR("float3");
            }
            return sample;
        }

        auto append_position_sample(
                const RuntimeSoraPositionProbeDeps& deps,
                UObject* pawn,
                uint64_t stable_ms,
                std::wstring_view runtime_context_summary,
                std::wstring_view status,
                const std::optional<RuntimeVectorSample>& sample,
                std::wstring_view failure_reason) -> void
        {
            if (!deps.bridge_dir.has_value())
            {
                return;
            }

            std::ostringstream json;
            json << "{";
            json << "\"schema\":\"kh3ap.runtime_sora_position_sample.v1\"";
            json << ",\"protocol_name\":\"" << json_escape(deps.protocol_name) << "\"";
            json << ",\"protocol_version\":" << deps.protocol_version;
            json << ",\"event\":\"runtime_sora_position_sample\"";
            append_json_string_field(json, "status", status);
            append_json_string_field(json, "source", STR("runtime_sora_position_probe"));
            append_json_string_field(json, "build_stamp", deps.build_stamp);
            const std::string observed_at = now_iso8601_utc();
            append_json_string_field(json, "observed_at_utc", std::wstring{observed_at.begin(), observed_at.end()});
            append_json_string_field(json, "pawn", format_object_debug_name(pawn));
            json << ",\"pawn_address\":\"0x" << std::hex
                 << reinterpret_cast<uintptr_t>(pawn) << std::dec << "\"";
            json << ",\"pawn_stable_ms\":" << stable_ms;
            append_json_string_field(json, "runtime_context", runtime_context_summary);

            if (sample.has_value())
            {
                append_json_number_field(json, "x", sample->x);
                append_json_number_field(json, "y", sample->y);
                append_json_number_field(json, "z", sample->z);
                json << ",\"location\":{";
                json << "\"x\":" << std::fixed << std::setprecision(3) << sample->x;
                json << ",\"y\":" << std::fixed << std::setprecision(3) << sample->y;
                json << ",\"z\":" << std::fixed << std::setprecision(3) << sample->z;
                json << "}";
                append_json_string_field(json, "location_property_class", sample->property_class);
                append_json_string_field(json, "location_struct", sample->struct_name);
                append_json_string_field(json, "location_numeric_layout", sample->numeric_layout);
                json << ",\"location_element_size\":" << sample->element_size;
            }
            if (!failure_reason.empty())
            {
                append_json_string_field(json, "reason", failure_reason);
            }
            json << "}\n";

            append_text_file(*deps.bridge_dir / std::filesystem::path{std::wstring{deps.output_filename}}, json.str());
        }
    }

    auto maybe_append_runtime_sora_position_probe(
            RuntimeSoraPositionProbeState& state,
            const RuntimeSoraPositionProbeDeps& deps,
            std::chrono::steady_clock::time_point now) -> void
    {
        if (!deps.bridge_dir.has_value())
        {
            return;
        }
        if (state.last_sample_at.time_since_epoch().count() != 0 &&
            deps.sample_interval.count() > 0 &&
            (now - state.last_sample_at) < deps.sample_interval)
        {
            return;
        }
        if (deps.runtime_state.grant_state().special_battle_mode_active())
        {
            return;
        }

        auto& pawn_state = deps.runtime_state.grant_state().pawn_state();
        UObject* const pawn = pawn_state.object();
        if (!pawn || !kh3ap::is_uobject_valid_fast(pawn) || !RuntimeObjectResolver::is_sora_ability_pawn_candidate(pawn))
        {
            return;
        }
        const auto stable_for = pawn_state.stable_for(now);
        if (deps.stable_delay.count() > 0 && stable_for < deps.stable_delay)
        {
            return;
        }

        state.last_sample_at = now;
        const uint64_t stable_ms = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(stable_for).count());
        std::wstring failure_reason{};
        const auto sample = try_get_actor_location(pawn, deps.actor_location_function_path, &failure_reason);
        const std::wstring context_summary = kh3ap::runtime_context_summary(
                deps.runtime_state,
                deps.bridge_dir);
        append_position_sample(
                deps,
                pawn,
                stable_ms,
                context_summary,
                sample.has_value() ? std::wstring_view{STR("ok")} : std::wstring_view{STR("failed")},
                sample,
                failure_reason);
        if (deps.debug_logging)
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] RuntimeSoraPositionProbe status={} pawn={} stable_ms={} reason={}\n"),
                    sample.has_value() ? STR("ok") : STR("failed"),
                    format_object_debug_name(pawn),
                    stable_ms,
                    failure_reason.empty() ? std::wstring{STR("<none>")} : failure_reason);
        }
    }
}
