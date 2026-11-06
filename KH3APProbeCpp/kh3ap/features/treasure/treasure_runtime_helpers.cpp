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
#include "treasure_runtime_state.h"
#include "treasure_retry_processing.h"
#include <chrono>

namespace kh3ap
{
    namespace
    {
        constexpr auto chest_actor_retry_interval = std::chrono::milliseconds(100);

        auto live_retry_object(const DeferredTreasureProbe& probe) -> RC::Unreal::UObject*
        {
            auto* object = probe.object.Get();
            return object && kh3ap::is_uobject_valid_fast(object) ? object : nullptr;
        }
    } // namespace

    auto try_process_open_treasure_state(RC::Unreal::UObject* state, std::wstring_view source_label,
                                         const TreasureBoxEmitter& emit_box) -> bool
    {
        if (!state || !kh3ap::is_uobject_valid_fast(state))
        {
            return true;
        }

        RC::Unreal::UObject* const treasure_box =
            try_read_object_property_object(state, STR("m_pTreasureBox"));
        if (!treasure_box || !kh3ap::is_uobject_valid_fast(treasure_box))
        {
            if (!RuntimeTextHelpers::is_retry_source_label(source_label))
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] OpenTreasureState pending source={} state={} "
                        "reason=no_valid_m_pTreasureBox\n"),
                    std::wstring{source_label}, state->GetFullName());
            }
            return false;
        }

        RC::Output::send<RC::LogLevel::Verbose>(
            STR("[KH3APProbeCpp] OpenTreasureState resolved source={} state={} treasure_box={}\n"),
            std::wstring{source_label}, state->GetFullName(), treasure_box->GetFullName());
        return emit_box ? emit_box(treasure_box, source_label) : false;
    }

    auto try_process_treasure_logic_actor(RC::Unreal::UObject* actor,
                                          std::wstring_view source_label,
                                          const TreasureBoxEmitter& emit_box,
                                          bool actor_is_known_live) -> bool
    {
        if (!actor || (!actor_is_known_live && !kh3ap::is_uobject_valid_fast(actor)))
        {
            return true;
        }

        RC::Unreal::UObject* const treasure_box =
            try_read_object_property_object(actor, STR("currentTreasureBox"));
        if (!treasure_box || (!actor_is_known_live && !kh3ap::is_uobject_valid_fast(treasure_box)))
        {
            if (!RuntimeTextHelpers::is_retry_source_label(source_label))
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] TreasureLogicActor pending source={} actor={} "
                        "reason=no_valid_currentTreasureBox\n"),
                    std::wstring{source_label}, actor->GetFullName());
            }
            return false;
        }

        RC::Output::send<RC::LogLevel::Verbose>(
            STR("[KH3APProbeCpp] TreasureLogicActor resolved source={} actor={} treasure_box={}\n"),
            std::wstring{source_label}, actor->GetFullName(), treasure_box->GetFullName());
        return emit_box ? emit_box(treasure_box, source_label) : false;
    }

    auto enqueue_open_treasure_state_retry(TreasureRuntimeState& treasure_state,
                                           RC::Unreal::UObject* state,
                                           std::wstring_view source_label, int attempts) -> void
    {
        treasure_state.treasure_state_retry_queue().schedule(
            state, std::wstring{source_label}, attempts, chest_actor_retry_interval,
            TreasureRuntimeState::pending_treasure_probe_max);
    }

    auto handle_open_treasure_state_constructed(TreasureRuntimeState& treasure_state,
                                                RC::Unreal::UObject* state,
                                                std::wstring_view source_label,
                                                const TreasureBoxEmitter& emit_box) -> void
    {
        if (state && kh3ap::is_uobject_valid_fast(state))
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[KH3APProbeCpp] OpenTreasureState observed source={} state={}\n"),
                std::wstring{source_label}, state->GetFullName());

            const uintptr_t state_key = reinterpret_cast<uintptr_t>(state);
            if (treasure_state.should_dump_property_diagnostics(state_key))
            {
                log_object_treasureish_properties(state,
                                                  std::format(STR("{} state_dump"), source_label));
            }
        }

        if (!try_process_open_treasure_state(state, source_label, emit_box))
        {
            enqueue_open_treasure_state_retry(treasure_state, state, source_label, 0);
        }
    }

    auto drain_due_open_treasure_state_retries(TreasureRuntimeState& treasure_state,
                                               const TreasureBoxEmitter& emit_box) -> void
    {
        auto due_entries = treasure_state.treasure_state_retry_queue().pop_due();
        treasure_retry::drain(
            due_entries, TreasureRuntimeState::open_treasure_state_retry_max, live_retry_object,
            [&emit_box](RC::Unreal::UObject* object, std::wstring_view source)
            {
                return try_process_open_treasure_state(object, source, emit_box);
            },
            [](RC::Unreal::UObject* object, std::wstring_view source)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Treasure state retry exhausted source={} state={}\n"),
                    source, object->GetFullName());
            },
            [&treasure_state](RC::Unreal::UObject* object, std::wstring_view source, int attempts)
            {
                enqueue_open_treasure_state_retry(treasure_state, object, source, attempts);
            });
    }

    auto handle_open_treasure_state_constructed(RC::Unreal::UObject* state,
                                                std::wstring_view source_label,
                                                const TreasureRuntimeDeps& deps) -> void
    {
        handle_open_treasure_state_constructed(
            deps.treasure_state, state, source_label,
            [&deps](RC::Unreal::UObject* treasure_box, std::wstring_view emit_source)
            {
                return try_emit_treasure_payload_from_box(treasure_box, emit_source, deps);
            });
    }

    auto drain_due_open_treasure_state_retries(const TreasureRuntimeDeps& deps) -> void
    {
        drain_due_open_treasure_state_retries(
            deps.treasure_state,
            [&deps](RC::Unreal::UObject* treasure_box, std::wstring_view emit_source)
            {
                return try_emit_treasure_payload_from_box(treasure_box, emit_source, deps);
            });
    }

    auto enqueue_treasure_logic_actor_retry(TreasureRuntimeState& treasure_state,
                                            RC::Unreal::UObject* actor,
                                            std::wstring_view source_label, int attempts) -> void
    {
        const auto result = treasure_state.treasure_logic_actor_retry_queue().schedule(
            actor, std::wstring{source_label}, attempts, chest_actor_retry_interval,
            TreasureRuntimeState::pending_treasure_logic_actor_probe_max);
        if (attempts == 0)
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[KH3APProbeCpp] TreasureLogicActor retry queued source={} actor={} "
                    "attempts={} pending={}\n"),
                std::wstring{source_label},
                actor && kh3ap::is_uobject_valid_fast(actor) ? actor->GetFullName()
                                                             : std::wstring{STR("<null>")},
                static_cast<int64_t>(attempts), static_cast<uint64_t>(result.pending_count));
        }
    }

    auto enqueue_treasure_logic_actor_retry(RC::Unreal::UObject* actor,
                                            std::wstring_view source_label, int attempts,
                                            const TreasureRuntimeDeps& deps) -> void
    {
        enqueue_treasure_logic_actor_retry(deps.treasure_state, actor, source_label, attempts);
    }

    auto handle_treasure_logic_actor_constructed(TreasureRuntimeState& treasure_state,
                                                 RC::Unreal::UObject* actor,
                                                 std::wstring_view source_label,
                                                 const TreasureBoxEmitter& emit_box,
                                                 bool actor_is_known_live) -> void
    {
        if (actor && (actor_is_known_live || kh3ap::is_uobject_valid_fast(actor)))
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("[KH3APProbeCpp] TreasureLogicActor observed source={} actor={}\n"),
                std::wstring{source_label}, actor->GetFullName());

            const uintptr_t actor_key = reinterpret_cast<uintptr_t>(actor);
            if (treasure_state.should_dump_property_diagnostics(actor_key))
            {
                log_object_treasureish_properties(actor,
                                                  std::format(STR("{} actor_dump"), source_label));
            }
        }

        if (!try_process_treasure_logic_actor(actor, source_label, emit_box, actor_is_known_live))
        {
            enqueue_treasure_logic_actor_retry(treasure_state, actor, source_label, 0);
        }
    }

    auto handle_treasure_logic_actor_constructed(RC::Unreal::UObject* actor,
                                                 std::wstring_view source_label,
                                                 const TreasureRuntimeDeps& deps,
                                                 bool actor_is_known_live) -> void
    {
        handle_treasure_logic_actor_constructed(
            deps.treasure_state, actor, source_label,
            [&deps, actor_is_known_live](RC::Unreal::UObject* treasure_box,
                                         std::wstring_view emit_source)
            {
                return try_emit_treasure_payload_from_box(treasure_box, emit_source, deps,
                                                          actor_is_known_live);
            },
            actor_is_known_live);
    }

    auto drain_due_treasure_logic_actor_retries(TreasureRuntimeState& treasure_state,
                                                const TreasureBoxEmitter& emit_box) -> void
    {
        auto due_entries = treasure_state.treasure_logic_actor_retry_queue().pop_due();
        treasure_retry::drain(
            due_entries, TreasureRuntimeState::treasure_logic_actor_retry_max, live_retry_object,
            [&emit_box](RC::Unreal::UObject* object, std::wstring_view source)
            {
                return try_process_treasure_logic_actor(object, source, emit_box);
            },
            [](RC::Unreal::UObject* object, std::wstring_view source)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] TreasureLogicActor retry exhausted source={} actor={}\n"),
                    source, object->GetFullName());
            },
            [&treasure_state](RC::Unreal::UObject* object, std::wstring_view source, int attempts)
            {
                enqueue_treasure_logic_actor_retry(treasure_state, object, source, attempts);
            });
    }

    auto drain_due_treasure_logic_actor_retries(const TreasureRuntimeDeps& deps) -> void
    {
        drain_due_treasure_logic_actor_retries(
            deps.treasure_state,
            [&deps](RC::Unreal::UObject* treasure_box, std::wstring_view emit_source)
            {
                return try_emit_treasure_payload_from_box(treasure_box, emit_source, deps);
            });
    }
} // namespace kh3ap
