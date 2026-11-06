#include "kh3ap/features/enemies/bosses/binding.h"
#include "kh3ap/features/enemies/bosses/completion.h"
#include "kh3ap/features/enemies/bosses/encounter_repairs.h"
#include "kh3ap/features/enemies/bosses/hp_limits.h"
#include "kh3ap/features/enemies/boss_remap_policy.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UFunctionStructs.hpp>

#include <DynamicOutput/DynamicOutput.hpp>
#include <algorithm>
#include <cstring>
#include <cwctype>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap::enemy_remap_detail
{
    namespace
    {
        constexpr auto max_level_entity_group_names = 64;
    }

    [[nodiscard]] static auto read_level_entity_group_names(UObject* level_entity)
            -> std::vector<std::wstring>
    {
        std::vector<std::wstring> group_names{};
        if (!is_uobject_valid_fast(level_entity))
        {
            return group_names;
        }
        FProperty* const property = level_entity->GetPropertyByNameInChain(
                level_entity_group_names_property_name.data());
        void* const value_ptr =
                level_entity->GetValuePtrByPropertyNameInChain(
                        level_entity_group_names_property_name.data());
        if (!property || !value_ptr ||
            property->GetClass().GetName() != STR("ArrayProperty"))
        {
            return group_names;
        }
        auto* const array_property = static_cast<FArrayProperty*>(property);
        FProperty* const inner =
                const_cast<FProperty*>(array_property->GetInner());
        if (!inner || inner->GetClass().GetName() != STR("NameProperty") ||
            inner->GetElementSize() < static_cast<int32_t>(sizeof(FName)))
        {
            return group_names;
        }

        ArrayHeader header{};
        std::memcpy(&header, value_ptr, sizeof(header));
        if (header.num < 0 || header.num > max_level_entity_group_names ||
            header.max < header.num ||
            (header.num > 0 && header.data == 0))
        {
            return group_names;
        }
        const auto* const base = reinterpret_cast<const std::byte*>(
                header.data);
        const size_t element_size =
                static_cast<size_t>(inner->GetElementSize());
        group_names.reserve(static_cast<size_t>(header.num));
        for (int32_t index = 0; index < header.num; ++index)
        {
            const auto* const name = reinterpret_cast<const FName*>(
                    base + static_cast<size_t>(index) * element_size);
            const std::wstring value = name ? name->ToString() : STR("");
            if (!value.empty() && value != STR("None"))
            {
                group_names.emplace_back(value);
            }
        }
        return group_names;
    }

    [[nodiscard]] static auto is_wipe_out_group(std::wstring_view group_name)
            -> bool
    {
        std::wstring normalized{};
        normalized.reserve(group_name.size());
        for (const wchar_t ch : group_name)
        {
            if (std::iswalnum(ch))
            {
                normalized.push_back(
                        static_cast<wchar_t>(std::towlower(ch)));
            }
        }
        return normalized.find(STR("wipeout")) != std::wstring::npos;
    }

    auto register_level_entity_wipe_out_groups(
            UObject* level_entity,
            std::wstring_view level_entity_path) -> void
    {
        UObject* const persistent_level = level_entity
                ? level_entity->GetOuterPrivate()
                : nullptr;
        if (!persistent_level ||
            persistent_level->GetNamePrivate() != STR("PersistentLevel"))
        {
            return;
        }

        const std::vector<std::wstring> group_names =
                read_level_entity_group_names(level_entity);
        const uintptr_t level_identity =
                reinterpret_cast<uintptr_t>(persistent_level);
        std::lock_guard lock{s_runtime_enemy_remap.boss.tracking.boss_state_mutex};
        auto& groups = s_runtime_enemy_remap
                               .boss.tracking.boss_wipe_out_group_members[level_identity];
        for (const std::wstring& group_name : group_names)
        {
            if (!is_wipe_out_group(group_name))
            {
                continue;
            }
            auto& members = groups[group_name];
            const auto existing = std::find_if(
                    members.begin(),
                    members.end(),
                    [&](const RuntimeBossWipeOutGroupMember& member) {
                        return member.level_entity_path == level_entity_path;
                    });
            if (existing != members.end())
            {
                existing->bind(level_entity);
            }
            else
            {
                members.emplace_back(RuntimeBossWipeOutGroupMember{
                        .level_entity_path =
                                std::wstring{level_entity_path},
                });
                members.back().bind(level_entity);
            }
        }
    }

    struct RuntimeBossWipeOutDispatchScope
    {
        RuntimeBossWipeOutDispatchScope()
            : previous(s_dispatching_runtime_boss_wipe_out)
        {
            s_dispatching_runtime_boss_wipe_out = true;
        }

        ~RuntimeBossWipeOutDispatchScope()
        {
            s_dispatching_runtime_boss_wipe_out = previous;
        }

        bool previous{};
    };

    [[nodiscard]] static auto mark_boss_wipe_out_group(
            std::wstring_view level_entity_path,
            std::wstring_view group_name) -> bool
    {
        std::lock_guard lock{s_runtime_enemy_remap.boss.tracking.boss_state_mutex};
        return s_runtime_enemy_remap
                .boss.tracking.boss_dispatched_wipe_out_groups[
                        std::wstring{level_entity_path}]
                .emplace(group_name)
                .second;
    }

    static auto unmark_boss_wipe_out_group(
            std::wstring_view level_entity_path,
            std::wstring_view group_name) -> void
    {
        std::lock_guard lock{s_runtime_enemy_remap.boss.tracking.boss_state_mutex};
        const auto entity = s_runtime_enemy_remap
                                    .boss.tracking.boss_dispatched_wipe_out_groups.find(
                                            std::wstring{level_entity_path});
        if (entity == s_runtime_enemy_remap
                              .boss.tracking.boss_dispatched_wipe_out_groups.end())
        {
            return;
        }
        entity->second.erase(std::wstring{group_name});
        if (entity->second.empty())
        {
            s_runtime_enemy_remap
                    .boss.tracking.boss_dispatched_wipe_out_groups.erase(entity);
        }
    }

    static auto clear_boss_wipe_out_groups(
            std::wstring_view level_entity_path) -> void
    {
        std::lock_guard lock{s_runtime_enemy_remap.boss.tracking.boss_state_mutex};
        s_runtime_enemy_remap.boss.tracking.boss_dispatched_wipe_out_groups.erase(
                std::wstring{level_entity_path});
    }

    struct RuntimeBossCompletionCounts
    {
        int32_t spawn_count{};
        int32_t kill_count_before{};
        int32_t kill_count_after{};
        bool synchronized{};
    };

    struct RuntimeBossWipeOutGroupCounts
    {
        int64_t aggregate_spawn_count{};
        int64_t aggregate_kill_count{};
        uint32_t member_count{};
        bool available{};
        bool complete{};
    };

    struct RuntimeBossSourceCompletion
    {
        std::wstring_view event_name{};
        bool available{};
        bool dispatched{};
        bool succeeded{};
    };

    [[nodiscard]] static auto source_boss_completion_event(
            std::wstring_view source_class_path)
            -> std::wstring_view
    {
        // The Toy Box Demon Tide owns an additional death broadcast in its
        // pawn Blueprint. A substituted class cannot emit that source-only
        // event, but both stock level scripts still listen for it.
        if (source_class_path == scala_first_xehanort_class)
            return scala_transition_event;
        if (source_class_path == kg_demon_tide_class)
            return kg_demon_tide_transition_event;
        return source_class_path == demon_tide_tt_source_class_path
                ? demon_tide_tt_completion_event_name
                : std::wstring_view{};
    }

    [[nodiscard]] static auto dispatch_source_boss_completion(
            UObject* level_script,
            std::wstring_view source_class_path)
            -> RuntimeBossSourceCompletion
    {
        RuntimeBossSourceCompletion result{
                .event_name = source_boss_completion_event(
                        source_class_path),
        };
        if (result.event_name.empty())
        {
            return result;
        }

        UFunction* const remote_event =
                level_script
                ? level_script->GetFunctionByNameInChain(
                          remote_event_name.data())
                : nullptr;
        FProperty* const event_name_property = remote_event
                ? find_function_param_by_name(
                          remote_event,
                          STR("EventName"))
                : nullptr;
        FProperty* const return_property = remote_event
                ? find_function_param_by_name(
                          remote_event,
                          STR("ReturnValue"))
                : nullptr;
        const int32_t params_size = remote_event
                ? remote_event->GetParmsSize()
                : 0;
        result.available = remote_event && event_name_property &&
                           return_property && params_size > 0 &&
                           return_property->GetClass().GetName() ==
                                   STR("BoolProperty");
        if (!result.available)
        {
            return result;
        }

        std::vector<std::byte> params(
                static_cast<size_t>(params_size));
        if (!write_name_or_string_function_param(
                    event_name_property,
                    params.data(),
                    result.event_name))
        {
            return result;
        }

        try
        {
            const RuntimeBossWipeOutDispatchScope dispatch_scope{};
            level_script->ProcessEvent(remote_event, params.data());
            result.dispatched = true;
            result.succeeded =
                    static_cast<FBoolProperty*>(return_property)
                            ->GetPropertyValueInContainer(params.data());
        }
        catch (...)
        {
            result.dispatched = false;
            result.succeeded = false;
        }
        return result;
    }

    [[nodiscard]] static auto read_runtime_boss_completion_counts(
            const RuntimeBossEntityBinding& binding)
            -> RuntimeBossCompletionCounts
    {
        RuntimeBossCompletionCounts result{};
        if (!is_uobject_valid_fast(binding.level_entity))
        {
            return result;
        }
        const auto spawn_count = try_read_int32_object_property(
                binding.level_entity,
                level_entity_spawn_count_property_name);
        const auto kill_count = try_read_int32_object_property(
                binding.level_entity,
                level_entity_kill_count_property_name);
        if (!spawn_count.has_value() || !kill_count.has_value())
        {
            return result;
        }
        result.spawn_count = *spawn_count;
        result.kill_count_before = *kill_count;
        result.kill_count_after = *kill_count;
        // This runs from the OnCtorStateEvent post-hook, after KH3 updates
        // the source level entity's kill count. Never force the entity to
        // its spawn count here: multiple entities can share one wipeout
        // group, and doing so makes the first death finish the encounter.
        result.synchronized = true;
        return result;
    }

    [[nodiscard]] static auto evaluate_runtime_boss_wipe_out_group(
            const RuntimeBossEntityBinding& binding,
            std::wstring_view group_name)
            -> RuntimeBossWipeOutGroupCounts
    {
        RuntimeBossWipeOutGroupCounts result{};
        UObject* const persistent_level = binding.level_entity
                ? binding.level_entity->GetOuterPrivate()
                : nullptr;
        if (!persistent_level)
        {
            return result;
        }

        std::vector<RuntimeBossWipeOutGroupMember> members{};
        {
            std::lock_guard lock{s_runtime_enemy_remap.boss.tracking.boss_state_mutex};
            const auto level = s_runtime_enemy_remap
                                       .boss.tracking.boss_wipe_out_group_members.find(
                                               reinterpret_cast<uintptr_t>(
                                                       persistent_level));
            if (level == s_runtime_enemy_remap
                                 .boss.tracking.boss_wipe_out_group_members.end())
            {
                return result;
            }
            const auto group = level->second.find(
                    std::wstring{group_name});
            if (group == level->second.end() || group->second.empty())
            {
                return result;
            }
            members = group->second;
        }

        result.member_count = static_cast<uint32_t>(members.size());
        for (const RuntimeBossWipeOutGroupMember& member : members)
        {
            auto* entity = member.get();
            if (!is_uobject_valid_fast(entity) || entity->GetOuterPrivate() != persistent_level)
                return result;
            const auto spawn_count = try_read_int32_object_property(
                    entity,
                    level_entity_spawn_count_property_name);
            const auto kill_count = try_read_int32_object_property(
                    entity,
                    level_entity_kill_count_property_name);
            if (!spawn_count.has_value() || !kill_count.has_value() ||
                *spawn_count < 0 || *kill_count < 0)
            {
                return result;
            }
            result.aggregate_spawn_count += *spawn_count;
            result.aggregate_kill_count += *kill_count;
        }

        result.available = true;
        result.complete = result.aggregate_spawn_count > 0 &&
                          result.aggregate_kill_count >=
                                  result.aggregate_spawn_count;
        return result;
    }

    static auto dispatch_runtime_boss_wipe_out(
            UObject* pawn,
            const RuntimeBossEntityBinding& binding) -> void
    {
        if (!is_uobject_valid_fast(pawn) || !is_uobject_valid_fast(binding.level_entity))
        {
            return;
        }
        const std::vector<std::wstring> all_groups =
                read_level_entity_group_names(binding.level_entity);
        std::vector<std::wstring> wipe_out_groups{};
        std::copy_if(
                all_groups.begin(),
                all_groups.end(),
                std::back_inserter(wipe_out_groups),
                [](const std::wstring& group) {
                    return is_wipe_out_group(group);
                });
        UObject* const persistent_level =
                binding.level_entity->GetOuterPrivate();
        UObject* const level_script =
                persistent_level &&
                        persistent_level->GetName() == STR("PersistentLevel")
                ? try_read_object_property_object(
                          persistent_level,
                          STR("LevelScriptActor"))
                : nullptr;
        if (!level_script)
        {
            Output::send<LogLevel::Error>(
                    STR("[KH3APProbeCpp] Boss runtime completion deferred "
                        "reason=level_script_unavailable entity={} "
                        "polling=false\n"),
                    binding.level_entity_path);
            return;
        }

        const RuntimeBossCompletionCounts completion_counts =
                read_runtime_boss_completion_counts(binding);
        if (!completion_counts.synchronized)
        {
            Output::send<LogLevel::Error>(
                    STR("[KH3APProbeCpp] Boss runtime completion deferred "
                        "reason=entity_counts_unavailable entity={} "
                        "polling=false\n"),
                    binding.level_entity_path);
            return;
        }

        // These exact slots have no wipeout-named group. Their native
        // pawns broadcast a transition event which replacements cannot
        // emit. Notify the owning level only after a verified defeat;
        // the stock level handler owns the cinematic and story progress.
        const std::wstring transition_source = tracked_boss_source_class(
                binding.level_entity_path).value_or(STR(""));
        const bool kg_tide_transition = all_groups.size() == 1 &&
                is_kg_demon_tide_transition(binding.level_entity_path,
                        transition_source, all_groups.front());
        if (all_groups.size() == 1 && (kg_tide_transition || is_scala_final_transition(
                    binding.level_entity_path, transition_source, all_groups.front())))
        {
            if (completion_counts.spawn_count <= 0 ||
                completion_counts.kill_count_after < completion_counts.spawn_count)
                return;
            const auto& group = all_groups.front();
            if (!mark_boss_wipe_out_group(binding.level_entity_path, group))
                return;
            const auto completion = dispatch_source_boss_completion(
                    level_script, transition_source);
            if (!completion.succeeded)
                unmark_boss_wipe_out_group(binding.level_entity_path, group);
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Boss runtime {} transition event={} "
                        "available={} dispatched={} succeeded={} "
                        "route=level_script.RemoteEvent verified_defeat=true polling=false\n"),
                    kg_tide_transition ? STR("KG Demon Tide") : STR("Scala"),
                    completion.event_name, completion.available,
                    completion.dispatched, completion.succeeded);
            return;
        }

        if (wipe_out_groups.empty())
        {
            Output::send<LogLevel::Error>(
                    STR("[KH3APProbeCpp] Boss runtime completion deferred "
                        "reason=no_wipe_out_group entity={} groups={} "
                        "replacement_remains_registered=true "
                        "polling=false\n"),
                    binding.level_entity_path,
                    static_cast<uint64_t>(all_groups.size()));
            return;
        }

        std::vector<std::pair<
                std::wstring,
                RuntimeBossWipeOutGroupCounts>>
                ready_wipe_out_groups{};
        ready_wipe_out_groups.reserve(wipe_out_groups.size());
        for (const std::wstring& group_name : wipe_out_groups)
        {
            const RuntimeBossWipeOutGroupCounts group_counts =
                    evaluate_runtime_boss_wipe_out_group(
                            binding,
                            group_name);
            if (!group_counts.available)
            {
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] Boss runtime completion deferred "
                            "reason=wipe_out_group_counts_unavailable "
                            "entity={} group={} polling=false\n"),
                        binding.level_entity_path,
                        group_name);
                continue;
            }
            if (!group_counts.complete)
            {
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Boss runtime completion deferred "
                            "reason=wipe_out_group_incomplete entity={} "
                            "group={} aggregate_spawn_count={} "
                            "aggregate_kill_count={} member_count={} "
                            "polling=false\n"),
                        binding.level_entity_path,
                        group_name,
                        group_counts.aggregate_spawn_count,
                        group_counts.aggregate_kill_count,
                        group_counts.member_count);
                continue;
            }
            ready_wipe_out_groups.emplace_back(
                    group_name,
                    group_counts);
        }
        if (ready_wipe_out_groups.empty())
        {
            return;
        }

        const std::wstring source_class_path =
                tracked_boss_source_class(
                        binding.level_entity_path).value_or(STR(""));
        const std::wstring_view source_event_name =
                source_boss_completion_event(source_class_path);
        if (!source_event_name.empty())
        {
            if (ready_wipe_out_groups.size() != wipe_out_groups.size())
            {
                return;
            }
            // Source-specific boss events already enter the matching stock
            // encounter-completion branch. Calling OnWipeOut first would
            // execute that branch once here and again when RemoteEvent
            // reaches the source event handler. Keep the two routes
            // mutually exclusive so this follows the source boss's
            // vanilla completion path exactly once.
            std::vector<std::wstring> newly_marked_groups{};
            newly_marked_groups.reserve(ready_wipe_out_groups.size());
            for (const auto& [group_name, _] : ready_wipe_out_groups)
            {
                if (mark_boss_wipe_out_group(
                            binding.level_entity_path,
                            group_name))
                {
                    newly_marked_groups.emplace_back(group_name);
                }
            }
            if (newly_marked_groups.empty())
            {
                return;
            }

            const RuntimeBossSourceCompletion source_completion =
                    dispatch_source_boss_completion(
                            level_script,
                            source_class_path);
            const bool source_completed =
                    source_completion.available &&
                    source_completion.dispatched &&
                    source_completion.succeeded;
            if (!source_completed)
            {
                for (const std::wstring& group_name :
                     newly_marked_groups)
                {
                    unmark_boss_wipe_out_group(
                            binding.level_entity_path,
                            group_name);
                }
            }

            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Boss runtime source completion "
                        "event={} source_class={} available={} "
                        "dispatched={} succeeded={} "
                        "route=level_script.RemoteEvent "
                        "custom_source_event_exclusive=true "
                        "generic_wipeout_skipped=true "
                        "spawn_count={} kill_count_before={} "
                        "kill_count_after={} original_proxy=false "
                        "polling=false\n"),
                    source_completion.event_name,
                    source_class_path,
                    source_completion.available,
                    source_completion.dispatched,
                    source_completion.succeeded,
                    completion_counts.spawn_count,
                    completion_counts.kill_count_before,
                    completion_counts.kill_count_after);
            if (source_completed)
            {
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Boss runtime replacement "
                            "handed to story flow entity={} "
                            "completion_route=source_event "
                            "dispatched_groups={} destroyed=false "
                            "original_proxy=false polling=false\n"),
                        binding.level_entity_path,
                        static_cast<uint64_t>(
                                newly_marked_groups.size()));
            }
            return;
        }

        UFunction* const on_wipe_out =
                level_script->GetFunctionByNameInChain(
                        on_wipe_out_event_name.data());
        FProperty* const group_property = on_wipe_out
                ? find_function_param_by_name(
                          on_wipe_out,
                          STR("GroupName"))
                : nullptr;
        FProperty* const enemy_property = on_wipe_out
                ? find_function_param_by_name(
                          on_wipe_out,
                          STR("Enemy"))
                : nullptr;
        FProperty* const killed_by_property = on_wipe_out
                ? find_function_param_by_name(
                          on_wipe_out,
                          STR("KilledBy"))
                : nullptr;
        const int32_t params_size =
                on_wipe_out ? on_wipe_out->GetParmsSize() : 0;
        if (!on_wipe_out || params_size <= 0 ||
            !group_property || !enemy_property || !killed_by_property)
        {
            Output::send<LogLevel::Error>(
                    STR("[KH3APProbeCpp] Boss runtime completion deferred "
                        "reason=on_wipe_out_unavailable entity={} "
                        "level_script={} function={} params_size={} "
                        "polling=false\n"),
                    binding.level_entity_path,
                    level_script != nullptr,
                    on_wipe_out != nullptr,
                    params_size);
            return;
        }

        uint32_t dispatched_count = 0;
        for (const auto& [group_name, group_counts] :
             ready_wipe_out_groups)
        {
            if (!mark_boss_wipe_out_group(
                        binding.level_entity_path,
                        group_name))
            {
                continue;
            }
            std::vector<std::byte> params(
                    static_cast<size_t>(params_size));
            const bool params_written =
                    write_name_or_string_function_param(
                            group_property,
                            params.data(),
                            group_name) &&
                    write_object_function_param(
                            on_wipe_out,
                            params.data(),
                            STR("Enemy"),
                            pawn) &&
                    write_object_function_param(
                            on_wipe_out,
                            params.data(),
                            STR("KilledBy"),
                            nullptr);
            if (!params_written)
            {
                unmark_boss_wipe_out_group(
                        binding.level_entity_path,
                        group_name);
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] Boss runtime completion deferred "
                            "reason=wipe_out_params_unavailable entity={} "
                            "group={} polling=false\n"),
                        binding.level_entity_path,
                        group_name);
                continue;
            }

            try
            {
                const RuntimeBossWipeOutDispatchScope dispatch_scope{};
                level_script->ProcessEvent(
                        on_wipe_out,
                        params.data());
            }
            catch (...)
            {
                unmark_boss_wipe_out_group(
                        binding.level_entity_path,
                        group_name);
                    Output::send<LogLevel::Error>(
                            STR("[KH3APProbeCpp] Boss runtime completion deferred "
                            "reason=on_wipe_out_threw entity={} group={} "
                            "polling=false\n"),
                        binding.level_entity_path,
                        group_name);
                continue;
            }

            ++dispatched_count;
            const uint32_t observation =
                    s_runtime_enemy_remap
                            .boss.tracking.boss_completion_observation_count
                            .fetch_add(1) +
                    1;
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Boss runtime completion dispatched "
                        "sequence={} trigger=death_state entity={} "
                        "group={} enemy={} route=level_script.OnWipeOut "
                        "spawn_count={} kill_count_before={} "
                        "kill_count_after={} "
                        "aggregate_spawn_count={} "
                        "aggregate_kill_count={} member_count={} "
                        "group_gate=complete "
                        "replacement_retained_for_story_flow=true "
                        "original_proxy=false polling=false\n"),
                    observation,
                    binding.level_entity_path,
                    group_name,
                    pawn->GetPathName(),
                    completion_counts.spawn_count,
                    completion_counts.kill_count_before,
                    completion_counts.kill_count_after,
                    group_counts.aggregate_spawn_count,
                    group_counts.aggregate_kill_count,
                    group_counts.member_count);
        }

        if (dispatched_count > 0)
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Boss runtime replacement handed to "
                        "story flow entity={} dispatched_groups={} "
                        "destroyed=false original_proxy=false "
                        "polling=false\n"),
                    binding.level_entity_path,
                    dispatched_count);
        }
    }

    auto prepare_runtime_boss_state(
            UnrealScriptFunctionCallableContext& callback_context,
            void*) -> void
    {
        if (!s_runtime_enemy_remap.active ||
            !s_runtime_enemy_remap.boss.boss_enabled.load() ||
            s_runtime_enemy_remap.boss.boss_class_replacements.empty())
        {
            return;
        }
        UObject* const pawn = callback_context.Context;
        // Context is the actor currently executing OnCtorStateEvent, so it
        // is live by definition. Keep the all-enemy path to a pointer read
        // and an allocation-free FName lookup; reflection starts only for
        // replacement-boss classes.
        if (!has_runtime_boss_replacement_class(pawn))
        {
            return;
        }

        prepare_luxord_partner_hp_gate(pawn);

        UFunction* const function =
                get_frame_node(callback_context.TheStack);
        void* const params = get_frame_locals(callback_context.TheStack);
        const auto state_id = try_read_integral_param(
                function,
                params,
                STR("inStateID"));
        if (!state_id.has_value() ||
            (*state_id != tres_state_appear &&
             *state_id != tres_state_die &&
             *state_id != tres_state_vanish_die))
        {
            return;
        }

        // Replacement classes clear the inherited level-entity link. It
        // only needs restoring at appearance and immediately before a
        // death transition; doing reflection work for every AI state was
        // unnecessary and could amplify encounter-start stalls.
        (void)resolve_and_bind_runtime_boss_pawn(pawn);
    }

    auto observe_runtime_boss_state(
            UnrealScriptFunctionCallableContext& callback_context,
            void*) -> void
    {
        if (!s_runtime_enemy_remap.active ||
            !s_runtime_enemy_remap.boss.boss_enabled.load() ||
            s_runtime_enemy_remap.boss.boss_class_replacements.empty())
        {
            return;
        }
        UObject* const pawn = callback_context.Context;
        if (!has_runtime_boss_replacement_class(pawn))
        {
            return;
        }

        UFunction* const function =
                get_frame_node(callback_context.TheStack);
        void* const params = get_frame_locals(callback_context.TheStack);
        const auto state_id = try_read_integral_param(
                function,
                params,
                STR("inStateID"));
        if (!state_id.has_value() ||
            (*state_id != tres_state_appear &&
             *state_id != tres_state_die &&
             *state_id != tres_state_vanish_die))
        {
            return;
        }

        RuntimeBossEntityBinding binding =
                resolve_tracked_runtime_boss_pawn(pawn);
        if (!binding.level_entity)
        {
            binding = resolve_and_bind_runtime_boss_pawn(pawn);
        }
        if (!binding.level_entity)
        {
            return;
        }
        if (*state_id == tres_state_appear)
        {
            initialize_runtime_larxene_phase(pawn);
        }
        if (*state_id == tres_state_appear &&
            !binding.new_actor)
        {
            clear_boss_wipe_out_groups(binding.level_entity_path);
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Boss runtime completion rearmed "
                        "trigger=TSID_STATE_APPEAR entity={} "
                        "same_actor=true polling=false\n"),
                    binding.level_entity_path);
        }
        if (*state_id != tres_state_die &&
            *state_id != tres_state_vanish_die)
        {
            return;
        }
        dispatch_runtime_boss_wipe_out(pawn, binding);
    }

    auto observe_or_suppress_runtime_boss_wipe_out(
            UnrealScriptFunctionCallableContext& callback_context,
            void*) -> void
    {
        if (s_dispatching_runtime_boss_wipe_out ||
            !s_runtime_enemy_remap.active ||
            !s_runtime_enemy_remap.boss.boss_enabled.load() ||
            s_runtime_enemy_remap.boss.boss_class_replacements.empty())
        {
            return;
        }
        UFunction* const function =
                get_frame_node(callback_context.TheStack);
        void* const params = get_frame_locals(callback_context.TheStack);
        FProperty* const group_property = function && params
                ? find_function_param_by_name(function, STR("GroupName"))
                : nullptr;
        FProperty* const enemy_property = function && params
                ? find_function_param_by_name(function, STR("Enemy"))
                : nullptr;
        FName* const group_value = group_property
                ? group_property->ContainerPtrToValuePtr<FName>(params)
                : nullptr;
        UObject** const enemy_value = enemy_property
                ? enemy_property->ContainerPtrToValuePtr<UObject*>(params)
                : nullptr;
        UObject* const enemy = enemy_value ? *enemy_value : nullptr;
        if (!group_value || !is_runtime_boss_replacement_pawn(enemy))
        {
            return;
        }
        const std::wstring group_name = group_value->ToString();
        if (group_name.empty() || group_name == STR("None"))
        {
            return;
        }
        RuntimeBossEntityBinding binding =
                resolve_tracked_runtime_boss_pawn(enemy);
        if (!binding.level_entity)
        {
            binding = resolve_and_bind_runtime_boss_pawn(enemy);
        }
        if (!binding.level_entity)
        {
            return;
        }

        if (mark_boss_wipe_out_group(
                    binding.level_entity_path,
                    group_name))
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Boss runtime natural wipeout "
                        "observed entity={} group={} enemy={} "
                        "replacement_is_event_actor=true polling=false\n"),
                    binding.level_entity_path,
                    group_name,
                    enemy->GetPathName());
            return;
        }

        *group_value = FName(
                consumed_wipe_out_group_name.data(),
                FNAME_Add);
        Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Boss runtime duplicate wipeout "
                    "suppressed entity={} group={} polling=false\n"),
                binding.level_entity_path,
                    group_name);
    }
}
