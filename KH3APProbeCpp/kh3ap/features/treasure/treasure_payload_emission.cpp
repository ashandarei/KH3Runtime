#include "treasure_runtime_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/text_helpers.h"
#include "kh3ap/bridge/utilities.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <format>
#include "kh3ap/bridge/rewards/loot_payload_builder.h"
#include "treasure_mapping_resolver.h"
#include <chrono>
#include <cstdint>

namespace kh3ap
{
    namespace
    {
        constexpr bool s_enable_chest_timing_logging_local = false;
        auto elapsed_us(std::chrono::steady_clock::time_point start,
                        std::chrono::steady_clock::time_point end) -> uint64_t
        {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
        }

        auto annotate_treasure_discovery_route(RuntimeLootPayload& payload,
                                               std::wstring_view source_label) -> void
        {
            payload.discovery_source = source_label;
            if (source_label.find(STR("TresPlayerState_OpenTreasureBox")) !=
                std::wstring_view::npos)
            {
                payload.discovery_route = STR("open_treasure_player_state");
            }
            else if (source_label.find(STR("TresCharState_OpenTreasureBox")) !=
                     std::wstring_view::npos)
            {
                payload.discovery_route = STR("open_treasure_char_state");
            }
            else if (source_label.find(STR("BP_TresGetItem")) != std::wstring_view::npos)
            {
                payload.discovery_route = STR("native_item_grant");
            }
            else if (source_label.find(STR("K2_DestroyActor beacon")) != std::wstring_view::npos)
            {
                payload.discovery_route = STR("treasure_box_ready");
            }
            else if (source_label.find(STR("TreasureLogicActor_C")) != std::wstring_view::npos)
            {
                payload.discovery_route = STR("treasure_logic_actor");
            }
            else if (source_label.find(STR("AP_TreasureLooted")) != std::wstring_view::npos)
            {
                payload.discovery_route = STR("ap_treasure_looted");
            }
            else if (source_label.find(STR("AnimNotify")) != std::wstring_view::npos)
            {
                payload.discovery_route = STR("treasure_anim_notify");
            }
            else
            {
                payload.discovery_route = payload.resolution_kind.empty()
                                              ? std::wstring{STR("unknown")}
                                              : payload.resolution_kind;
            }
        }

    } // namespace

    auto infer_treasure_mapping_from_actor_name(std::wstring_view full_name,
                                                const TreasureBridgeKeyLookup& treasure_lookup)
        -> std::optional<TreasureMappingResult>
    {
        return TreasureMappingResolver::infer_from_actor_name(full_name, treasure_lookup);
    }

    auto try_emit_treasure_payload_from_anim_notify(RC::Unreal::UFunction* target_function,
                                                    void* params, std::wstring_view source_label,
                                                    RC::Unreal::UClass* actor_base_class,
                                                    const TreasureBoxEmitter& emit_box) -> bool
    {
        const auto timing_start = std::chrono::steady_clock::now();
        RC::Unreal::UObject* const mesh_component =
            try_read_object_param(target_function, params, STR("MeshComp"));
        const auto param_end = std::chrono::steady_clock::now();
        if (!mesh_component || !kh3ap::is_uobject_valid_fast(mesh_component))
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Treasure anim notify pending source={} "
                    "reason=no_valid_mesh_component\n"),
                source_label);
            if constexpr (s_enable_chest_timing_logging_local)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] ChestTiming anim_notify status=no_mesh source={} "
                        "param_us={} total_us={}\n"),
                    source_label, elapsed_us(timing_start, param_end),
                    elapsed_us(timing_start, std::chrono::steady_clock::now()));
            }
            return false;
        }

        RC::Unreal::UObject* const treasure_box =
            try_resolve_treasure_box_from_mesh_component(mesh_component, actor_base_class);
        const auto resolve_end = std::chrono::steady_clock::now();
        if (!treasure_box || !kh3ap::is_uobject_valid_fast(treasure_box))
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Treasure anim notify pending source={} mesh_component={} "
                    "reason=no_actor_owner\n"),
                source_label, mesh_component->GetFullName());
            if constexpr (s_enable_chest_timing_logging_local)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] ChestTiming anim_notify status=no_box source={} "
                        "param_us={} resolve_us={} total_us={}\n"),
                    source_label, elapsed_us(timing_start, param_end),
                    elapsed_us(param_end, resolve_end),
                    elapsed_us(timing_start, std::chrono::steady_clock::now()));
            }
            return false;
        }

        const bool emitted = emit_box ? emit_box(treasure_box, source_label) : false;
        const auto emit_end = std::chrono::steady_clock::now();
        if constexpr (s_enable_chest_timing_logging_local)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] ChestTiming anim_notify status={} source={} param_us={} "
                    "resolve_us={} emit_us={} total_us={}\n"),
                emitted ? STR("emitted") : STR("not_emitted"), source_label,
                elapsed_us(timing_start, param_end), elapsed_us(param_end, resolve_end),
                elapsed_us(resolve_end, emit_end), elapsed_us(timing_start, emit_end));
        }
        return emitted;
    }

    auto try_emit_treasure_payload_from_anim_notify_direct(RC::Unreal::UFunction* target_function,
                                                           void* params,
                                                           std::wstring_view source_label,
                                                           const TreasureRuntimeDeps& deps) -> bool
    {
        const auto timing_start = std::chrono::steady_clock::now();
        RC::Unreal::UObject* const mesh_component =
            try_read_object_param(target_function, params, STR("MeshComp"));
        const auto param_end = std::chrono::steady_clock::now();
        if (!mesh_component || !kh3ap::is_uobject_valid_fast(mesh_component))
        {
            if constexpr (s_enable_chest_timing_logging_local)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] ChestTiming anim_notify_direct status=no_mesh source={} "
                        "param_us={} total_us={}\n"),
                    source_label, elapsed_us(timing_start, param_end),
                    elapsed_us(timing_start, std::chrono::steady_clock::now()));
            }
            return false;
        }

        const std::wstring mesh_full_name = mesh_component->GetFullName();
        const auto full_name_end = std::chrono::steady_clock::now();
        const TreasureBridgeKeyLookup empty_lookup{};
        const auto& treasure_lookup = deps.treasure_lookup ? *deps.treasure_lookup : empty_lookup;
        const auto mapping =
            infer_treasure_mapping_from_actor_name(mesh_full_name, treasure_lookup);
        const auto mapping_end = std::chrono::steady_clock::now();
        if (!mapping.has_value())
        {
            if constexpr (s_enable_chest_timing_logging_local)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] ChestTiming anim_notify_direct status=no_mapping "
                        "source={} param_us={} fullname_us={} mapping_us={} total_us={}\n"),
                    source_label, elapsed_us(timing_start, param_end),
                    elapsed_us(param_end, full_name_end), elapsed_us(full_name_end, mapping_end),
                    elapsed_us(timing_start, std::chrono::steady_clock::now()));
            }
            return false;
        }

        RuntimeLootPayload payload = RuntimeLootPayloadBuilder::build_treasure_payload(
            mesh_full_name, *mapping,
            RuntimeTextHelpers::extract_level_path(mesh_full_name).value_or(STR("")),
            STR("treasure_anim_notify_direct"), STR("MeshComp"), STR(""), std::nullopt);
        annotate_treasure_discovery_route(payload, source_label);
        const auto build_end = std::chrono::steady_clock::now();

        if (deps.should_emit && !deps.should_emit(payload))
        {
            if constexpr (s_enable_chest_timing_logging_local)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] ChestTiming anim_notify_direct status=suppressed "
                        "bridge_key={} source={} param_us={} fullname_us={} mapping_us={} "
                        "build_us={} total_us={}\n"),
                    payload.bridge_key, source_label, elapsed_us(timing_start, param_end),
                    elapsed_us(param_end, full_name_end), elapsed_us(full_name_end, mapping_end),
                    elapsed_us(mapping_end, build_end),
                    elapsed_us(timing_start, std::chrono::steady_clock::now()));
            }
            return true;
        }

        const bool emitted = deps.emit_payload ? deps.emit_payload(payload) : false;
        const auto emit_end = std::chrono::steady_clock::now();
        if constexpr (s_enable_chest_timing_logging_local)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] ChestTiming anim_notify_direct status={} bridge_key={} "
                    "source={} param_us={} fullname_us={} mapping_us={} build_us={} emit_us={} "
                    "total_us={}\n"),
                emitted ? STR("emitted") : STR("not_emitted"), payload.bridge_key, source_label,
                elapsed_us(timing_start, param_end), elapsed_us(param_end, full_name_end),
                elapsed_us(full_name_end, mapping_end), elapsed_us(mapping_end, build_end),
                elapsed_us(build_end, emit_end), elapsed_us(timing_start, emit_end));
        }
        return emitted;
    }

    auto try_emit_treasure_payload_from_context_object(RC::Unreal::UObject* context,
                                                       std::wstring_view source_label,
                                                       const TreasureBoxEmitter& emit_box) -> bool
    {
        RC::Unreal::UObject* const treasure_box =
            try_resolve_treasure_box_from_context_object(context);
        if (!treasure_box || !kh3ap::is_uobject_valid_fast(treasure_box))
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Treasure context pending source={} context={} "
                    "reason=no_valid_context_treasure_box\n"),
                source_label, context ? context->GetFullName() : std::wstring{STR("<null>")});
            return false;
        }

        RC::Output::send<RC::LogLevel::Verbose>(
            STR("[KH3APProbeCpp] Treasure context resolved source={} context={} treasure_box={}\n"),
            source_label, context ? context->GetFullName() : std::wstring{STR("<null>")},
            treasure_box->GetFullName());
        return emit_box ? emit_box(treasure_box, source_label) : false;
    }

    auto try_emit_treasure_payload_from_object_property(RC::Unreal::UObject* owner,
                                                        const wchar_t* property_name,
                                                        std::wstring_view source_label,
                                                        const TreasureBoxEmitter& emit_box) -> bool
    {
        if (!owner || !kh3ap::is_uobject_valid_fast(owner))
        {
            return false;
        }

        RC::Unreal::UObject* const treasure_box =
            try_read_object_property_object(owner, property_name);
        if (!treasure_box || !kh3ap::is_uobject_valid_fast(treasure_box))
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Treasure owner-property pending source={} owner={} "
                    "property={} reason=no_valid_treasure_box\n"),
                source_label, owner->GetFullName(), std::wstring{property_name});
            return false;
        }

        RC::Output::send<RC::LogLevel::Verbose>(
            STR("[KH3APProbeCpp] Treasure owner-property resolved source={} owner={} property={} "
                "treasure_box={}\n"),
            source_label, owner->GetFullName(), std::wstring{property_name},
            treasure_box->GetFullName());
        return emit_box ? emit_box(treasure_box, source_label) : false;
    }

    auto try_emit_treasure_payload_from_anim_notify(RC::Unreal::UFunction* target_function,
                                                    void* params, std::wstring_view source_label,
                                                    const TreasureRuntimeDeps& deps) -> bool
    {
        if (try_emit_treasure_payload_from_anim_notify_direct(target_function, params, source_label,
                                                              deps))
        {
            return true;
        }

        return try_emit_treasure_payload_from_anim_notify(
            target_function, params, source_label, deps.actor_base_class,
            [&deps](RC::Unreal::UObject* treasure_box, std::wstring_view emit_source)
            {
                return try_emit_treasure_payload_from_box(treasure_box, emit_source, deps);
            });
    }

    auto try_emit_treasure_payload_from_context_object(RC::Unreal::UObject* context,
                                                       std::wstring_view source_label,
                                                       const TreasureRuntimeDeps& deps) -> bool
    {
        return try_emit_treasure_payload_from_context_object(
            context, source_label,
            [&deps](RC::Unreal::UObject* treasure_box, std::wstring_view emit_source)
            {
                return try_emit_treasure_payload_from_box(treasure_box, emit_source, deps);
            });
    }

    auto try_emit_treasure_payload_from_object_property(RC::Unreal::UObject* owner,
                                                        const wchar_t* property_name,
                                                        std::wstring_view source_label,
                                                        const TreasureRuntimeDeps& deps) -> bool
    {
        return try_emit_treasure_payload_from_object_property(
            owner, property_name, source_label,
            [&deps](RC::Unreal::UObject* treasure_box, std::wstring_view emit_source)
            {
                return try_emit_treasure_payload_from_box(treasure_box, emit_source, deps);
            });
    }

    auto try_emit_treasure_payload_from_box(
        RC::Unreal::UObject* treasure_box, std::wstring_view source_label,
        const TreasureBridgeKeyLookup& treasure_lookup,
        std::wstring_view treasure_box_anim_notify_name, std::wstring_view ap_treasure_looted_name,
        std::wstring_view treasure_logic_actor_get_treasure_name_name,
        std::wstring_view tres_treasure_box_open_optional_name,
        const TreasurePayloadPredicate& should_emit, const TreasurePayloadEmitter& emit_payload,
        bool treasure_box_is_known_live) -> bool
    {
        if (!treasure_box ||
            (!treasure_box_is_known_live && !kh3ap::is_uobject_valid_fast(treasure_box)))
        {
            return false;
        }

        const auto payload_timing_start = std::chrono::steady_clock::now();
        const std::wstring full_name = treasure_box->GetFullName();
        const auto full_name_end = std::chrono::steady_clock::now();
        const auto mapping = infer_treasure_mapping_from_actor_name(full_name, treasure_lookup);
        const auto mapping_end = std::chrono::steady_clock::now();
        if (!mapping.has_value())
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Treasure payload skip source={} actor={} treasure_id={} "
                    "is_large={} reason=no_bridge_mapping\n"),
                std::wstring{source_label}, full_name,
                try_read_object_scalar_property_as_string(treasure_box, STR("m_TreasureID"))
                    .value_or(STR("<none>")),
                try_read_object_bool_property(treasure_box, STR("m_bIsLargeBox")).value_or(false)
                    ? STR("true")
                    : STR("false"));
            if constexpr (s_enable_chest_timing_logging_local)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] ChestTiming payload status=no_mapping source={} "
                        "fullname_us={} mapping_us={} total_us={}\n"),
                    source_label, elapsed_us(payload_timing_start, full_name_end),
                    elapsed_us(full_name_end, mapping_end),
                    elapsed_us(payload_timing_start, std::chrono::steady_clock::now()));
            }
            return true;
        }

        std::wstring resolution_kind{STR("open_treasure_state")};
        std::wstring token_field_name{STR("m_pTreasureBox")};
        if (source_label.find(treasure_box_anim_notify_name) != std::wstring_view::npos)
        {
            resolution_kind = STR("treasure_anim_notify");
            token_field_name = STR("MeshComp");
        }
        else if (source_label.find(ap_treasure_looted_name) != std::wstring_view::npos)
        {
            resolution_kind = STR("ap_treasure_looted");
            token_field_name = STR("TreasureBox");
        }
        else if (source_label.find(treasure_logic_actor_get_treasure_name_name) !=
                     std::wstring_view::npos ||
                 source_label.find(STR("TreasureLogicActor_C")) != std::wstring_view::npos ||
                 source_label.find(STR("BP_TresGetItem")) != std::wstring_view::npos)
        {
            resolution_kind = STR("treasure_logic_actor");
            token_field_name = STR("currentTreasureBox");
        }
        else if (source_label.find(tres_treasure_box_open_optional_name) != std::wstring_view::npos)
        {
            resolution_kind = STR("treasure_box_native");
            token_field_name = STR("Context");
        }

        RuntimeLootPayload payload = RuntimeLootPayloadBuilder::build_treasure_payload(
            full_name, *mapping,
            RuntimeTextHelpers::extract_level_path(full_name).value_or(STR("")), resolution_kind,
            token_field_name,
            try_read_object_scalar_property_as_string(treasure_box, STR("m_TreasureID"))
                .value_or(STR("")),
            try_read_object_bool_property(treasure_box, STR("m_bIsLargeBox")));
        annotate_treasure_discovery_route(payload, source_label);
        const auto build_end = std::chrono::steady_clock::now();

        if (should_emit && !should_emit(payload))
        {
            if constexpr (s_enable_chest_timing_logging_local)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] ChestTiming payload status=suppressed bridge_key={} "
                        "source={} fullname_us={} mapping_us={} build_us={} total_us={}\n"),
                    payload.bridge_key, source_label,
                    elapsed_us(payload_timing_start, full_name_end),
                    elapsed_us(full_name_end, mapping_end), elapsed_us(mapping_end, build_end),
                    elapsed_us(payload_timing_start, std::chrono::steady_clock::now()));
            }
            return true;
        }

        const bool emitted = emit_payload ? emit_payload(payload) : false;
        const auto emit_end = std::chrono::steady_clock::now();
        if constexpr (s_enable_chest_timing_logging_local)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] ChestTiming payload status={} bridge_key={} source={} "
                    "fullname_us={} mapping_us={} build_us={} emit_us={} total_us={}\n"),
                emitted ? STR("emitted") : STR("not_emitted"), payload.bridge_key, source_label,
                elapsed_us(payload_timing_start, full_name_end),
                elapsed_us(full_name_end, mapping_end), elapsed_us(mapping_end, build_end),
                elapsed_us(build_end, emit_end), elapsed_us(payload_timing_start, emit_end));
        }
        return emitted;
    }

    auto try_emit_treasure_payload_from_box(RC::Unreal::UObject* treasure_box,
                                            std::wstring_view source_label,
                                            const TreasureRuntimeDeps& deps,
                                            bool treasure_box_is_known_live) -> bool
    {
        // Both operands must be lvalues: a temporary empty map would make the
        // conditional copy the entire seed lookup even when it is present.
        static const TreasureBridgeKeyLookup empty_lookup{};
        const auto& treasure_lookup = deps.treasure_lookup ? *deps.treasure_lookup : empty_lookup;
        return try_emit_treasure_payload_from_box(
            treasure_box, source_label, treasure_lookup, deps.treasure_box_anim_notify_name,
            deps.ap_treasure_looted_name, deps.treasure_logic_actor_get_treasure_name_name,
            deps.tres_treasure_box_open_optional_name, deps.should_emit, deps.emit_payload,
            treasure_box_is_known_live);
    }

} // namespace kh3ap
