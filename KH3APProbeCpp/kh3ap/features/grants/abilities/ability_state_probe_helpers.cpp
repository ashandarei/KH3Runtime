#include "kh3ap/features/grants/abilities/ability_state_probe_helpers.h"

#include "kh3ap/diagnostics/ability_probe/ability_probe_diagnostic_state.h"
#include "kh3ap/diagnostics/output/diagnostic_probe_serializer.h"
#include "kh3ap/diagnostics/ability_probe/ability_reflection_probe_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr auto ability_probe_log_limit = 32;
    }

    using namespace RC;
    using namespace RC::Unreal;

    auto make_runtime_ability_state_probe_deps(
            std::optional<std::filesystem::path> bridge_dir,
            std::wstring_view build_stamp,
            std::wstring_view runtime_context_summary,
            RuntimeState& runtime_state) -> RuntimeAbilityStateProbeDeps
    {
        return {
                std::move(bridge_dir),
                build_stamp,
                runtime_context_summary,
                runtime_state.ability_diagnostic_state().probe_diagnostic_state(),
                runtime_state.object_cache().last_world_context_object(),
        };
    }

    auto append_ability_state_probe_jsonl(
            const RuntimeAbilityStateProbeDeps& deps,
            std::wstring_view kind,
            std::wstring_view reason,
            std::wstring_view object_name,
            std::wstring_view class_name,
            std::wstring_view property_name,
            std::wstring_view property_class,
            int offset,
            int element_size,
            std::wstring_view value) -> void
    {
        if (!deps.bridge_dir.has_value())
        {
            return;
        }

        const std::string json = DiagnosticProbeSerializer::serialize_property_probe_event(
                deps.build_stamp,
                kind,
                reason,
                deps.runtime_context_summary,
                object_name,
                class_name,
                property_name,
                property_class,
                offset,
                element_size,
                value);
        auto lock = deps.diagnostic_state.acquire_log_lock();
        append_text_file(*deps.bridge_dir / "ability_state_probe.jsonl", json);
    }

    auto log_ability_state_snapshot(
            const RuntimeAbilityStateProbeDeps& deps,
            std::wstring_view reason,
            UObject* preferred_object) -> void
    {
        const auto candidates = collect_ability_state_probe_candidates(
                preferred_object,
                deps.last_world_context_object);
        Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] AbilityStateProbe reason={} candidates={} context={}\n"),
                reason,
                static_cast<uint64_t>(candidates.size()),
                deps.runtime_context_summary);
        append_ability_state_probe_jsonl(
                deps,
                STR("summary"),
                reason,
                format_object_debug_name(preferred_object),
                std::format(STR("candidates={}"), static_cast<uint64_t>(candidates.size())),
                {},
                {},
                -1,
                -1,
                {});

        for (UObject* object : candidates)
        {
            if (!object || !kh3ap::is_uobject_valid_fast(object))
            {
                continue;
            }
            UClass* object_class = object->GetClassPrivate();
            if (!object_class)
            {
                continue;
            }

            append_ability_state_probe_jsonl(
                    deps,
                    STR("object"),
                    reason,
                    object->GetFullName(),
                    object_class->GetName(),
                    {},
                    {},
                    -1,
                    -1,
                    {});

            int logged_count = 0;
            for (FProperty* property : object_class->ForEachPropertyInChain())
            {
                if (!property)
                {
                    continue;
                }

                const std::wstring property_name = property->GetName();
                const std::wstring property_class = property->GetClass().GetName();
                if (!should_probe_ability_state_property(property_name, property_class))
                {
                    continue;
                }

                const int offset = property->GetOffset_Internal();
                const int element_size = property->GetElementSize();
                if (offset < 0 || element_size <= 0)
                {
                    continue;
                }

                const auto* object_bytes = reinterpret_cast<const std::byte*>(object);
                const void* value_ptr = object_bytes + offset;
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] AbilityStateProbe reason={} object={} class={} property={} prop_class={} offset={} elem_size={} raw={} value={}\n"),
                        reason,
                        object->GetFullName(),
                        object_class->GetName(),
                        property_name,
                        property_class,
                        offset,
                        element_size,
                        format_bytes_hex(value_ptr, static_cast<size_t>(std::clamp(element_size, 1, 16))),
                        format_ability_state_property_value(object, property, value_ptr, element_size));
                append_ability_state_probe_jsonl(
                        deps,
                        STR("property"),
                        reason,
                        object->GetFullName(),
                        object_class->GetName(),
                        property_name,
                        property_class,
                        offset,
                        element_size,
                        truncate_for_log(format_ability_state_property_value(object, property, value_ptr, element_size), 320));

                ++logged_count;
                if (logged_count >= ability_probe_log_limit)
                {
                    break;
                }
            }
        }
    }
}
