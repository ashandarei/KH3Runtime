#pragma once

#include "kh3ap/core/types.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace RC::Unreal
{
    class UClass;
    class UFunction;
    class UObject;
}

namespace kh3ap
{
    class TreasureRuntimeState;

    using TreasureBridgeKeyLookup = std::unordered_map<std::wstring, std::wstring>;
    using TreasurePayloadPredicate = std::function<bool(const RuntimeLootPayload&)>;
    using TreasurePayloadEmitter = std::function<bool(const RuntimeLootPayload&)>;
    using TreasureBoxEmitter = std::function<bool(RC::Unreal::UObject*, std::wstring_view)>;

    struct TreasureRuntimeDeps
    {
        TreasureRuntimeState& treasure_state;
        RC::Unreal::UClass* actor_base_class;
        const TreasureBridgeKeyLookup* treasure_lookup;
        std::wstring_view treasure_box_anim_notify_name;
        std::wstring_view ap_treasure_looted_name;
        std::wstring_view treasure_logic_actor_get_treasure_name_name;
        std::wstring_view tres_treasure_box_open_optional_name;
        TreasurePayloadPredicate should_emit;
        TreasurePayloadEmitter emit_payload;
    };

    auto should_log_treasureish_property(std::wstring_view property_name, std::wstring_view property_class) -> bool;

    auto log_object_treasureish_properties(
            RC::Unreal::UObject* object,
            std::wstring_view source) -> void;

    auto try_read_object_param(
            RC::Unreal::UFunction* function,
            void* params,
            std::wstring_view param_name) -> RC::Unreal::UObject*;

    auto try_resolve_treasure_box_from_mesh_component(
            RC::Unreal::UObject* mesh_component,
            RC::Unreal::UClass* actor_base_class) -> RC::Unreal::UObject*;

    auto try_resolve_treasure_box_from_context_object(RC::Unreal::UObject* context) -> RC::Unreal::UObject*;

    auto infer_treasure_mapping_from_actor_name(
            std::wstring_view full_name,
            const TreasureBridgeKeyLookup& treasure_lookup) -> std::optional<TreasureMappingResult>;

    auto try_emit_treasure_payload_from_anim_notify(
            RC::Unreal::UFunction* target_function,
            void* params,
            std::wstring_view source_label,
            RC::Unreal::UClass* actor_base_class,
            const TreasureBoxEmitter& emit_box) -> bool;

    auto try_emit_treasure_payload_from_anim_notify(
            RC::Unreal::UFunction* target_function,
            void* params,
            std::wstring_view source_label,
            const TreasureRuntimeDeps& deps) -> bool;

    auto try_emit_treasure_payload_from_context_object(
            RC::Unreal::UObject* context,
            std::wstring_view source_label,
            const TreasureBoxEmitter& emit_box) -> bool;

    auto try_emit_treasure_payload_from_context_object(
            RC::Unreal::UObject* context,
            std::wstring_view source_label,
            const TreasureRuntimeDeps& deps) -> bool;

    auto try_emit_treasure_payload_from_object_property(
            RC::Unreal::UObject* owner,
            const wchar_t* property_name,
            std::wstring_view source_label,
            const TreasureBoxEmitter& emit_box) -> bool;

    auto try_emit_treasure_payload_from_object_property(
            RC::Unreal::UObject* owner,
            const wchar_t* property_name,
            std::wstring_view source_label,
            const TreasureRuntimeDeps& deps) -> bool;

    auto try_emit_treasure_payload_from_box(
            RC::Unreal::UObject* treasure_box,
            std::wstring_view source_label,
            const TreasureBridgeKeyLookup& treasure_lookup,
            std::wstring_view treasure_box_anim_notify_name,
            std::wstring_view ap_treasure_looted_name,
            std::wstring_view treasure_logic_actor_get_treasure_name_name,
            std::wstring_view tres_treasure_box_open_optional_name,
            const TreasurePayloadPredicate& should_emit,
            const TreasurePayloadEmitter& emit_payload,
            bool treasure_box_is_known_live = false) -> bool;

    auto try_emit_treasure_payload_from_box(
            RC::Unreal::UObject* treasure_box,
            std::wstring_view source_label,
            const TreasureRuntimeDeps& deps,
            bool treasure_box_is_known_live = false) -> bool;

    auto try_process_open_treasure_state(
            RC::Unreal::UObject* state,
            std::wstring_view source_label,
            const TreasureBoxEmitter& emit_box) -> bool;

    auto try_process_treasure_logic_actor(
            RC::Unreal::UObject* actor,
            std::wstring_view source_label,
            const TreasureBoxEmitter& emit_box,
            bool actor_is_known_live = false) -> bool;

    auto enqueue_open_treasure_state_retry(
            TreasureRuntimeState& treasure_state,
            RC::Unreal::UObject* state,
            std::wstring_view source_label,
            int attempts) -> void;

    auto handle_open_treasure_state_constructed(
            TreasureRuntimeState& treasure_state,
            RC::Unreal::UObject* state,
            std::wstring_view source_label,
            const TreasureBoxEmitter& emit_box) -> void;

    auto handle_open_treasure_state_constructed(
            RC::Unreal::UObject* state,
            std::wstring_view source_label,
            const TreasureRuntimeDeps& deps) -> void;

    auto drain_due_open_treasure_state_retries(
            TreasureRuntimeState& treasure_state,
            const TreasureBoxEmitter& emit_box) -> void;

    auto drain_due_open_treasure_state_retries(const TreasureRuntimeDeps& deps) -> void;

    auto enqueue_treasure_logic_actor_retry(
            TreasureRuntimeState& treasure_state,
            RC::Unreal::UObject* actor,
            std::wstring_view source_label,
            int attempts) -> void;

    auto enqueue_treasure_logic_actor_retry(
            RC::Unreal::UObject* actor,
            std::wstring_view source_label,
            int attempts,
            const TreasureRuntimeDeps& deps) -> void;

    auto handle_treasure_logic_actor_constructed(
            TreasureRuntimeState& treasure_state,
            RC::Unreal::UObject* actor,
            std::wstring_view source_label,
            const TreasureBoxEmitter& emit_box,
            bool actor_is_known_live = false) -> void;

    auto handle_treasure_logic_actor_constructed(
            RC::Unreal::UObject* actor,
            std::wstring_view source_label,
            const TreasureRuntimeDeps& deps,
            bool actor_is_known_live = false) -> void;

    auto drain_due_treasure_logic_actor_retries(
            TreasureRuntimeState& treasure_state,
            const TreasureBoxEmitter& emit_box) -> void;

    auto drain_due_treasure_logic_actor_retries(const TreasureRuntimeDeps& deps) -> void;
}
