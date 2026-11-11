#include <memory>
#include <optional>
#include <utility>
#include <cstddef>
#include "kh3ap/features/enemies/bosses/binding.h"
#include "kh3ap/features/enemies/bosses/encounter_repairs.h"
#include "kh3ap/features/enemies/boss_remap_policy.h"
#include "kh3ap/features/enemies/boss_profile.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/Hooks.hpp>

#include <DynamicOutput/DynamicOutput.hpp>
#include <algorithm>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <intrin.h>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap::enemy_remap_detail
{
    namespace
    {
        constexpr auto grim_rising_task_memory_bytes = 0x28;

        constexpr auto larxene_native_solo_phase = 3;
        constexpr auto opening_boss_sample_interval_seconds = 0.25f;
        constexpr auto remind_opening_story_flag = 1000;
        constexpr auto full_boss_hp_percent = 100;
        constexpr auto boss_hp_percent_scale = 100.0f;
        constexpr auto max_encounter_repair_groups = 512;
    } // namespace

    static auto is_tracked_randomized_grim_guardianess(UObject* pawn) -> bool
    {
        auto& state = s_runtime_enemy_remap;
        if (!state.active || !state.boss.boss_enabled.load() || !is_uobject_valid_fast(pawn) ||
            !is_uobject_valid_fast(pawn->GetClassPrivate()) ||
            pawn->GetClassPrivate()->GetPathName() != grim_guardianess_class)
        {
            return false;
        }
        const auto binding = resolve_and_bind_runtime_boss_pawn(pawn);
        if (!is_uobject_valid_fast(binding.level_entity) ||
            read_boss_owner_property(binding.level_entity,
                                     level_entity_actor_property_name.data()) != pawn)
        {
            return false;
        }
        std::lock_guard lock{state.boss.tracking.boss_state_mutex};
        const auto source =
            state.boss.tracking.remapped_boss_source_classes.find(binding.level_entity_path);
        const auto replacement =
            state.boss.tracking.remapped_boss_entities.find(binding.level_entity_path);
        return source != state.boss.tracking.remapped_boss_source_classes.end() &&
               replacement != state.boss.tracking.remapped_boss_entities.end() &&
               needs_grim_guardianess_normal_mode(source->second, replacement->second);
    }

    static auto change_grim_guardianess_mode(UObject* pawn, uint8_t mode) -> void
    {
        if (mode == 1 && is_tracked_randomized_grim_guardianess(pawn))
        {
            mode = 0;
            if (!s_runtime_enemy_remap.boss.repairs.grim_mode_guard_logged.exchange(true))
            {
                Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Randomized Grim Guardianess tower transition suppressed "
                        "scope=tracked_replacement native_normal_mode=true pawn={}\n"),
                    pawn->GetPathName());
            }
        }
        using ChangeMode = void (*)(UObject*, uint8_t);
        reinterpret_cast<ChangeMode>(s_runtime_enemy_remap.boss.repairs.grim_mode_trampoline)(pawn,
                                                                                              mode);
    }

    static auto execute_grim_guardianess_move_task(UObject* task, UObject* owner,
                                                   uint8_t* node_memory) -> int32_t
    {
        auto& repairs = s_runtime_enemy_remap.boss.repairs;
        if (node_memory && repairs.grim_rising_tree_name && is_uobject_valid_fast(task) &&
            is_uobject_valid_fast(owner))
        {
            UObject* const tree = read_boss_owner_property(task, STR("TreeAsset"));
            if (!tree || tree->GetNamePrivate() != *repairs.grim_rising_tree_name)
            {
                using ExecuteTask = int32_t (*)(UObject*, UObject*, uint8_t*);
                return reinterpret_cast<ExecuteTask>(repairs.grim_rising_task_trampoline)(
                    task, owner, node_memory);
            }
            // Check the tree AND its configured locomotion: no shared tree/CDO
            // edits, no suppression of ordinary MoveTo tasks or native Corona.
            FProperty* const property =
                task->GetPropertyByNameInChain(STR("m_LocomotionDefinition"));
            UObject* const* definition =
                property && property->GetClass().GetName() == STR("ClassProperty") &&
                        property->GetElementSize() == sizeof(UObject*)
                    ? property->ContainerPtrToValuePtr<UObject*>(task)
                    : nullptr;
            if (definition && is_uobject_valid_fast(*definition) &&
                is_grim_guardianess_tower_rising_task(tree->GetPathName(),
                                                      (*definition)->GetPathName()))
            {
                UObject* const controller = read_boss_owner_property(owner, STR("AIOwner"));
                UObject* const pawn = read_boss_owner_property(controller, STR("Pawn"));
                if (pawn && read_boss_owner_property(pawn, STR("Controller")) == controller &&
                    is_tracked_randomized_grim_guardianess(pawn))
                {
                    // The engine still runs the task's normal OnTaskFinished.
                    // Start with no observer/timer and an invalid move request,
                    // matching the verified native 40-byte memory contract.
                    std::memset(node_memory, 0, grim_rising_task_memory_bytes);
                    constexpr uint32_t invalid_move_request = 0xffffffff;
                    std::memcpy(node_memory, &invalid_move_request, sizeof(invalid_move_request));
                    if (!repairs.grim_rising_task_guard_logged.exchange(true))
                    {
                        Output::send<LogLevel::Normal>(
                            STR("[KH3APProbeCpp] Randomized Grim Guardianess tower Rising task "
                                "skipped scope=tracked_replacement native_normal_ai_continues=true "
                                "pawn={}\n"),
                            pawn->GetPathName());
                    }
                    return 0; // EBTNodeResult::Succeeded: continue the native tree.
                }
            }
        }
        using ExecuteTask = int32_t (*)(UObject*, UObject*, uint8_t*);
        return reinterpret_cast<ExecuteTask>(repairs.grim_rising_task_trampoline)(task, owner,
                                                                                  node_memory);
    }

    static auto teleport_grim_guardianess(UObject* pawn, const void* location, const void* rotation,
                                          bool is_test, bool no_check) -> bool
    {
        auto& repairs = s_runtime_enemy_remap.boss.repairs;
        if (reinterpret_cast<uintptr_t>(_ReturnAddress()) == repairs.grim_tower_teleport_caller &&
            is_tracked_randomized_grim_guardianess(pawn))
        {
            if (!repairs.grim_tower_teleport_guard_logged.exchange(true))
            {
                Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Randomized Grim Guardianess tower notify teleport "
                        "suppressed scope=tracked_replacement pawn={}\n"),
                    pawn->GetPathName());
            }
            // Complete the notify without relocating: native animation/state
            // cleanup still runs, while the mode guard keeps normal combat.
            return true;
        }
        using Teleport = bool (*)(UObject*, const void*, const void*, bool, bool);
        return reinterpret_cast<Teleport>(repairs.grim_tower_teleport_trampoline)(
            pawn, location, rotation, is_test, no_check);
    }

    static auto validate_grim_guardianess_action(UObject* decorator, UObject* owner,
                                                 uint8_t* node_memory) -> bool
    {
        auto& repairs = s_runtime_enemy_remap.boss.repairs;
        if (repairs.grim_rising_tree_name && is_uobject_valid_fast(decorator) &&
            is_uobject_valid_fast(owner))
        {
            UObject* const tree = read_boss_owner_property(decorator, STR("TreeAsset"));
            if (tree && tree->GetNamePrivate() == *repairs.grim_rising_tree_name)
            {
                FProperty* const property =
                    decorator->GetPropertyByNameInChain(STR("ActionDefinitionOverride"));
                UObject* const* definition =
                    property && property->GetClass().GetName() == STR("ClassProperty") &&
                            property->GetElementSize() == sizeof(UObject*)
                        ? property->ContainerPtrToValuePtr<UObject*>(decorator)
                        : nullptr;
                if (definition && is_uobject_valid_fast(*definition) &&
                    is_grim_guardianess_flash_action(tree->GetPathName(),
                                                     (*definition)->GetPathName()))
                {
                    UObject* const controller = read_boss_owner_property(owner, STR("AIOwner"));
                    UObject* const pawn = read_boss_owner_property(controller, STR("Pawn"));
                    if (pawn && read_boss_owner_property(pawn, STR("Controller")) == controller &&
                        is_tracked_randomized_grim_guardianess(pawn))
                    {
                        if (!repairs.grim_flash_guard_logged.exchange(true))
                        {
                            Output::send<LogLevel::Normal>(
                                STR("[KH3APProbeCpp] Randomized Grim Guardianess Flash branch "
                                    "skipped scope=tracked_replacement native_attack_fallback=true "
                                    "pawn={}\n"),
                                pawn->GetPathName());
                        }
                        // Reject the whole Flash branch before execution, so the
                        // normal selector chooses another attack instead of
                        // repeatedly preparing a tower transition that is blocked.
                        return false;
                    }
                }
            }
        }
        using Validate = bool (*)(UObject*, UObject*, uint8_t*);
        return reinterpret_cast<Validate>(repairs.grim_flash_trampoline)(decorator, owner,
                                                                         node_memory);
    }

    auto install_grim_guardianess_mode_guard() -> void
    {
        auto& boss = s_runtime_enemy_remap.boss;
        if (!std::any_of(boss.boss_class_replacements.begin(), boss.boss_class_replacements.end(),
                         [](const auto& entry)
                         {
                             return needs_grim_guardianess_normal_mode(entry.first, entry.second);
                         }))
        {
            return;
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        {
            return;
        }
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto profile = boss_native::select_profile(nt->FileHeader.TimeDateStamp,
                                                         nt->OptionalHeader.SizeOfImage);
        if (nt->Signature != IMAGE_NT_SIGNATURE || !profile.supported || !profile.grim_mode_rva ||
            std::memcmp(reinterpret_cast<void*>(base + profile.grim_mode_rva),
                        profile.grim_mode_bytes.data(), profile.grim_mode_bytes.size()))
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Grim Guardianess mode guard unavailable "
                    "reason=unsupported_native_signature\n"));
            return;
        }
        auto hook = std::make_unique<PLH::x64Detour>(
            base + profile.grim_mode_rva, reinterpret_cast<uint64_t>(&change_grim_guardianess_mode),
            &boss.repairs.grim_mode_trampoline);
        if (hook->hook())
        {
            boss.repairs.grim_mode_guard = std::move(hook);
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Grim Guardianess native mode guard installed storefront={} "
                    "scope=tracked_replacement\n"),
                profile.steam ? STR("steam") : STR("egs"));
        }
        if (std::memcmp(reinterpret_cast<void*>(base + profile.grim_move_execute_rva),
                        profile.grim_move_execute_bytes.data(),
                        profile.grim_move_execute_bytes.size()) ||
            std::memcmp(reinterpret_cast<void*>(base + profile.grim_move_memory_size_rva),
                        profile.grim_move_memory_size_bytes.data(),
                        profile.grim_move_memory_size_bytes.size()))
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Grim Guardianess Rising guard unavailable "
                    "reason=unsupported_native_task_signature\n"));
            return;
        }
        auto task_hook = std::make_unique<PLH::x64Detour>(
            base + profile.grim_move_execute_rva,
            reinterpret_cast<uint64_t>(&execute_grim_guardianess_move_task),
            &boss.repairs.grim_rising_task_trampoline);
        boss.repairs.grim_rising_tree_name.emplace(STR("e_ex721_BT"), FNAME_Add);
        if (task_hook->hook())
        {
            boss.repairs.grim_rising_task_guard = std::move(task_hook);
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Grim Guardianess Rising task guard installed storefront={} "
                    "scope=tracked_replacement\n"),
                profile.steam ? STR("steam") : STR("egs"));
        }
        if (std::memcmp(reinterpret_cast<void*>(base + profile.grim_teleport_rva),
                        profile.grim_teleport_bytes.data(), profile.grim_teleport_bytes.size()) ||
            std::memcmp(reinterpret_cast<void*>(base + profile.grim_tower_teleport_caller_rva),
                        profile.grim_tower_teleport_caller_bytes.data(),
                        profile.grim_tower_teleport_caller_bytes.size()))
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Grim Guardianess tower teleport guard unavailable "
                    "reason=unsupported_native_signature\n"));
            return;
        }
        boss.repairs.grim_tower_teleport_caller = base + profile.grim_tower_teleport_return_rva;
        auto teleport_hook =
            std::make_unique<PLH::x64Detour>(base + profile.grim_teleport_rva,
                                             reinterpret_cast<uint64_t>(&teleport_grim_guardianess),
                                             &boss.repairs.grim_tower_teleport_trampoline);
        if (teleport_hook->hook())
        {
            boss.repairs.grim_tower_teleport_guard = std::move(teleport_hook);
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Grim Guardianess tower notify teleport guard installed "
                    "storefront={} scope=tracked_replacement\n"),
                profile.steam ? STR("steam") : STR("egs"));
        }
        if (std::memcmp(reinterpret_cast<void*>(base + profile.grim_validate_action_rva),
                        profile.grim_validate_action_bytes.data(),
                        profile.grim_validate_action_bytes.size()))
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Grim Guardianess Flash guard unavailable "
                    "reason=unsupported_native_signature\n"));
            return;
        }
        auto flash_hook = std::make_unique<PLH::x64Detour>(
            base + profile.grim_validate_action_rva,
            reinterpret_cast<uint64_t>(&validate_grim_guardianess_action),
            &boss.repairs.grim_flash_trampoline);
        if (flash_hook->hook())
        {
            boss.repairs.grim_flash_guard = std::move(flash_hook);
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Grim Guardianess Flash branch guard installed storefront={} "
                    "scope=tracked_replacement\n"),
                profile.steam ? STR("steam") : STR("egs"));
        }
    }

    static auto is_tracked_solo_larxene(UObject* pawn) -> bool
    {
        if (!s_runtime_enemy_remap.active || !s_runtime_enemy_remap.boss.boss_enabled.load() ||
            !is_uobject_valid_fast(pawn) || !is_uobject_valid_fast(pawn->GetClassPrivate()))
        {
            return false;
        }
        const auto target = pawn->GetClassPrivate()->GetPathName();
        if (!needs_larxene_solo_phase(L"source", target))
        {
            return false;
        }
        const auto binding = resolve_and_bind_runtime_boss_pawn(pawn);
        if (!is_uobject_valid_fast(binding.level_entity) ||
            read_boss_owner_property(binding.level_entity,
                                     level_entity_actor_property_name.data()) != pawn)
        {
            return false;
        }
        std::lock_guard lock{s_runtime_enemy_remap.boss.tracking.boss_state_mutex};
        const auto source = s_runtime_enemy_remap.boss.tracking.remapped_boss_source_classes.find(
            binding.level_entity_path);
        const auto replacement = s_runtime_enemy_remap.boss.tracking.remapped_boss_entities.find(
            binding.level_entity_path);
        return source != s_runtime_enemy_remap.boss.tracking.remapped_boss_source_classes.end() &&
               replacement != s_runtime_enemy_remap.boss.tracking.remapped_boss_entities.end() &&
               replacement->second == target && needs_larxene_solo_phase(source->second, target);
    }

    static auto notify_runtime_boss_phase(UObject* pawn, uint8_t phase) -> void
    {
        if (phase < larxene_native_solo_phase && is_tracked_solo_larxene(pawn))
        {
            phase = larxene_native_solo_phase;
            if (!s_runtime_enemy_remap.boss.repairs.boss_phase_guard_logged.exchange(true))
            {
                Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Randomized Larxene arena phase "
                                                   "redirected to native solo phase pawn={}\n"),
                                               pawn->GetPathName());
            }
        }
        using Notify = void (*)(UObject*, uint8_t);
        reinterpret_cast<Notify>(s_runtime_enemy_remap.boss.repairs.boss_phase_trampoline)(pawn,
                                                                                           phase);
    }

    auto install_runtime_boss_phase_guard() -> void
    {
        if (!std::any_of(s_runtime_enemy_remap.boss.boss_class_replacements.begin(),
                         s_runtime_enemy_remap.boss.boss_class_replacements.end(),
                         [](const auto& entry)
                         {
                             return needs_larxene_solo_phase(entry.first, entry.second);
                         }))
        {
            return;
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto profile = boss_native::select_profile(nt->FileHeader.TimeDateStamp,
                                                         nt->OptionalHeader.SizeOfImage);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE ||
            !profile.supported ||
            std::memcmp(reinterpret_cast<void*>(base + profile.phase_rva),
                        profile.phase_bytes.data(), profile.phase_bytes.size()))
        {
            Output::send<LogLevel::Error>(STR("[KH3APProbeCpp] Larxene phase guard unavailable "
                                              "reason=unrecognized_native_notify\n"));
            return;
        }
        auto hook = std::make_unique<PLH::x64Detour>(
            base + profile.phase_rva, reinterpret_cast<uint64_t>(&notify_runtime_boss_phase),
            &s_runtime_enemy_remap.boss.repairs.boss_phase_trampoline);
        if (hook->hook())
        {
            s_runtime_enemy_remap.boss.repairs.boss_phase_guard = std::move(hook);
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Larxene native phase guard installed storefront={} "
                    "scope=tracked_replacement\n"),
                profile.steam ? STR("steam") : STR("egs"));
        }
    }

    auto initialize_runtime_larxene_phase(UObject* pawn) -> void
    {
        if (!is_tracked_solo_larxene(pawn))
        {
            return;
        }
        auto* const function = pawn->GetFunctionByNameInChain(STR("NotifyChangePhase"));
        auto* const property =
            function ? find_function_param_by_name(function, L"InNewPhaseType") : nullptr;
        if (!property || property->GetElementSize() != 1 || function->GetParmsSize() != 1)
        {
            return;
        }
        uint8_t phase =
            larxene_native_solo_phase; // Native phase IV has a complete independent attack set.
        pawn->ProcessEvent(function, &phase);
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] Randomized Larxene initialized via native solo phase pawn={}\n"),
            pawn->GetPathName());
    }

    static auto reveal_kg_replacement_bosses(UObject* context, UFunction* function, void*) -> void
    {
        auto& state = s_runtime_enemy_remap;
        // Engine dispatch owns these live arguments. Reject unrelated calls
        // using only a cached FName before reflection or path construction.
        if (!state.active || !state.boss.boss_enabled.load() || !function)
        {
            return;
        }
        const auto event =
            state.boss.repairs.kg_reveal_event_names.find(function->GetNamePrivate());
        if (event == state.boss.repairs.kg_reveal_event_names.end())
        {
            return;
        }
        try
        {
            if (!is_uobject_valid_fast(context))
            {
                return;
            }
            auto* level = context->GetOuterPrivate();
            if (!is_uobject_valid_fast(level) ||
                read_boss_owner_property(level, L"LevelScriptActor") != context)
            {
                return;
            }
            const auto level_path = level->GetPathName();
            if (!is_kg_boss_reveal_level(level_path, event->second))
            {
                return;
            }
            std::vector<std::pair<std::wstring, std::wstring>> replacements;
            {
                std::lock_guard lock{state.boss.tracking.boss_state_mutex};
                for (const auto& [entity_path, target] : state.boss.tracking.remapped_boss_entities)
                {
                    const auto source =
                        state.boss.tracking.remapped_boss_source_classes.find(entity_path);
                    if (entity_path.starts_with(level_path + L".") &&
                        source != state.boss.tracking.remapped_boss_source_classes.end() &&
                        source->second != target)
                    {
                        replacements.emplace_back(entity_path, target);
                    }
                }
            }
            for (const auto& [entity_path, target] : replacements)
            {
                auto* entity = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr,
                                                                          entity_path.c_str());
                auto* pawn =
                    read_boss_owner_property(entity, level_entity_actor_property_name.data());
                if (!pawn || !is_uobject_valid_fast(pawn->GetClassPrivate()) ||
                    pawn->GetClassPrivate()->GetPathName() != target ||
                    read_boss_owner_property(pawn, pawn_level_entity_property_name.data()) !=
                        entity ||
                    pawn->GetFunctionByNameInChain(L"NotifyChangePhase"))
                {
                    continue;
                }
                auto* hidden = pawn->GetPropertyByNameInChain(L"bHidden");
                if (!hidden || hidden->GetClass().GetName() != L"BoolProperty" ||
                    !static_cast<FBoolProperty*>(hidden)->GetPropertyValueInContainer(pawn))
                {
                    continue;
                }
                const auto read_integer = [&](const wchar_t* name) -> std::optional<int32_t>
                {
                    auto* getter = pawn->GetFunctionByNameInChain(name);
                    auto* result =
                        getter ? find_function_param_by_name(getter, L"ReturnValue") : nullptr;
                    if (!result || result->GetOffset_Internal() != 0 ||
                        !((getter->GetParmsSize() == sizeof(int32_t) &&
                           result->GetClass().GetName() == L"IntProperty") ||
                          (getter->GetParmsSize() == 1 &&
                           result->GetClass().GetName() == L"ByteProperty")))
                    {
                        return std::nullopt;
                    }
                    int32_t value{};
                    pawn->ProcessEvent(getter, &value);
                    return value;
                };
                const auto hp = read_integer(L"GetHitPoint"),
                           current_state = read_integer(L"GetStateID");
                if (!hp || *hp <= 0 || !current_state || *current_state == tres_state_die ||
                    *current_state == tres_state_vanish_die)
                {
                    continue;
                }
                auto* show = pawn->GetFunctionByNameInChain(L"SetActorHiddenInGame");
                auto* visible = show ? find_function_param_by_name(show, L"bNewHidden") : nullptr;
                if (!visible || show->GetParmsSize() != 1 || visible->GetOffset_Internal() != 0 ||
                    visible->GetClass().GetName() != L"BoolProperty")
                {
                    continue;
                }
                uint8_t hide = 0;
                pawn->ProcessEvent(show, &hide);
                auto* weapon = pawn->GetFunctionByNameInChain(L"BP_SetWeaponVisible");
                if (weapon && weapon->GetParmsSize() == sizeof(UObject*))
                {
                    std::vector<std::byte> params(sizeof(UObject*));
                    auto* show_weapon = find_function_param_by_name(weapon, L"bVisible");
                    auto* effect = find_function_param_by_name(weapon, L"bAppearEffect");
                    auto* rate = find_function_param_by_name(weapon, L"InPlayRate");
                    if (write_bool_function_param(show_weapon, params.data(), true) &&
                        write_bool_function_param(effect, params.data(), false) &&
                        write_float_function_param(rate, params.data(), 1.0))
                    {
                        pawn->ProcessEvent(weapon, params.data());
                    }
                }
                Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] KG replacement visibility restored event={} pawn={} "
                        "hidden_after={} route=native_arena_reveal\n"),
                    function->GetName(), pawn->GetPathName(),
                    static_cast<FBoolProperty*>(hidden)->GetPropertyValueInContainer(pawn));
            }
        }
        catch (...)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] KG replacement reveal skipped reason=reflection_exception\n"));
        }
    }

    auto install_kg_replacement_reveal_guard() -> void
    {
        for (const auto& event : kg_boss_reveal_events)
        {
            s_runtime_enemy_remap.boss.repairs.kg_reveal_event_names.emplace(
                FName{event.name.data(), FNAME_Add}, event.area);
        }
        Hook::RegisterProcessEventPostCallback(&reveal_kg_replacement_bosses);
    }

    auto read_boss_gameflow_flag(std::wstring_view flag_name) -> std::optional<int64_t>
    {
        auto* library = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/TresGame.Default__TresGameBlueprintLibrary"));
        auto* function = is_uobject_valid_fast(library)
                             ? library->GetFunctionByNameInChain(STR("GetGameFlag"))
                             : nullptr;
        if (!function || function->GetParmsSize() <= 0)
        {
            return std::nullopt;
        }
        auto* flag = find_function_param_by_name(function, L"FlagName");
        if (!flag)
        {
            return std::nullopt;
        }
        std::vector<std::byte> params(function->GetParmsSize());
        if (!write_name_or_string_function_param(flag, params.data(), flag_name))
        {
            return std::nullopt;
        }
        library->ProcessEvent(function, params.data());
        return try_read_integral_param(function, params.data(), L"ReturnValue");
    }

    static auto opening_hp_percent(UObject* pawn) -> int
    {
        if (!is_uobject_valid_fast(pawn))
        {
            return full_boss_hp_percent;
        }
        auto* function = pawn->GetFunctionByNameInChain(STR("GetHitPointRate"));
        auto* result = function ? find_function_param_by_name(function, L"ReturnValue") : nullptr;
        if (!result || function->GetParmsSize() != sizeof(float) ||
            result->GetOffset_Internal() != 0 ||
            result->GetClass().GetName() != STR("FloatProperty"))
        {
            return full_boss_hp_percent;
        }
        float rate = 1.0f;
        pawn->ProcessEvent(function, &rate);
        return rate >= 0.0f && rate <= 1.0f ? static_cast<int>(rate * boss_hp_percent_scale)
                                            : full_boss_hp_percent;
    }

    static auto opening_player_has_control(UObject* world_context) -> bool
    {
        auto* library = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Engine.Default__GameplayStatics"));
        auto* function = is_uobject_valid_fast(library)
                             ? library->GetFunctionByNameInChain(STR("GetPlayerController"))
                             : nullptr;
        struct Params
        {
            UObject* context;
            int32_t index{};
            int32_t padding{};
            UObject* result{};
        } params{world_context};
        if (!function || function->GetParmsSize() != sizeof(Params))
        {
            return false;
        }
        const auto valid_param = [&](const wchar_t* name, const wchar_t* type, int offset, int size)
        {
            auto* property = find_function_param_by_name(function, name);
            return property && property->GetClass().GetName() == type &&
                   property->GetOffset_Internal() == offset && property->GetElementSize() == size;
        };
        if (!valid_param(L"WorldContextObject", L"ObjectProperty", offsetof(Params, context),
                         sizeof(params.context)) ||
            !valid_param(L"PlayerIndex", L"IntProperty", offsetof(Params, index),
                         sizeof(params.index)) ||
            !valid_param(L"ReturnValue", L"ObjectProperty", offsetof(Params, result),
                         sizeof(params.result)))
        {
            return false;
        }
        library->ProcessEvent(function, &params);
        if (!is_uobject_valid_fast(params.result) ||
            !read_boss_owner_property(params.result, L"Pawn"))
        {
            return false;
        }
        auto* ignored_function = params.result->GetFunctionByNameInChain(STR("IsMoveInputIgnored"));
        if (!ignored_function || ignored_function->GetParmsSize() != 1)
        {
            return false;
        }
        uint8_t ignored = 1;
        params.result->ProcessEvent(ignored_function, &ignored);
        return ignored == 0;
    }

    static auto tick_opening_ansem(UObject* pawn, float delta_seconds) -> void
    {
        auto& state = s_runtime_enemy_remap;
        using Tick = void (*)(UObject*, float);
        reinterpret_cast<Tick>(state.boss.repairs.ansem_tick_trampoline)(pawn, delta_seconds);
        if (!state.active || !state.boss.boss_enabled.load() ||
            !state.boss.repairs.opening_ansem_class || !is_uobject_valid_fast(pawn) ||
            !is_uobject_valid_fast(pawn->GetClassPrivate()) ||
            pawn->GetClassPrivate()->GetNamePrivate() != *state.boss.repairs.opening_ansem_class)
        {
            return;
        }
        if (state.boss.repairs.opening_ansem.Get() != pawn)
        {
            state.boss.repairs.opening_ansem = pawn;
            state.boss.repairs.opening_battle_seconds = state.boss.repairs.opening_sample_seconds =
                0;
            state.boss.repairs.opening_retreat_dispatched = false;
        }
        if (state.boss.repairs.opening_ansem.Get() != pawn ||
            state.boss.repairs.opening_retreat_dispatched ||
            !(delta_seconds > 0.0f && delta_seconds < 1.0f))
        {
            return;
        }
        state.boss.repairs.opening_sample_seconds += delta_seconds;
        if (state.boss.repairs.opening_sample_seconds < opening_boss_sample_interval_seconds)
        {
            return;
        }
        const float elapsed = std::exchange(state.boss.repairs.opening_sample_seconds, 0.0f);
        constexpr auto level = L"/Game/Levels/kg_DLC/kg_05/umap/"
                               L"kg_05_enemy_Area_A.kg_05_enemy_Area_A:PersistentLevel.";
        const std::wstring xigbar_entity_path = std::wstring{level} + L"e_ex305_Pawn_s10_3";
        auto* entity = read_boss_owner_property(pawn, pawn_level_entity_property_name.data());
        if (!is_uobject_valid_fast(entity) ||
            entity->GetPathName() != std::wstring{level} + L"e_ex316_Pawn_s11_5" ||
            read_boss_owner_property(entity, level_entity_actor_property_name.data()) != pawn)
        {
            return;
        }
        {
            std::lock_guard lock{state.boss.tracking.boss_state_mutex};
            const auto source =
                state.boss.tracking.remapped_boss_source_classes.find(xigbar_entity_path);
            const auto target = state.boss.tracking.remapped_boss_entities.find(xigbar_entity_path);
            if (source == state.boss.tracking.remapped_boss_source_classes.end() ||
                target == state.boss.tracking.remapped_boss_entities.end() ||
                !needs_remind_ansem_phase_driver(source->second, target->second))
            {
                return;
            }
        }
        // Menu/cinematic time is not battle time. The stock story flag also
        // keeps late deaths/retries from sending phase I's event again.
        auto* queue = read_boss_owner_property(pawn, STR("MyStateComp"));
        auto* current = is_uobject_valid_fast(queue)
                            ? read_boss_owner_property(queue, STR("m_pCurrentState"))
                            : nullptr;
        if (!is_uobject_valid_fast(current) ||
            current->GetName().find(L"Cinematic") != std::wstring::npos ||
            read_boss_gameflow_flag(L"gameflow_KG_DLC_13_A") != remind_opening_story_flag ||
            !opening_player_has_control(pawn))
        {
            return;
        }
        state.boss.repairs.opening_battle_seconds += elapsed;
        int lowest_hp = opening_hp_percent(pawn);
        for (const auto* name : {L"e_ex305_Pawn_s10_3", L"e_ex313_Pawn_s10_1"})
        {
            const auto path = std::wstring{level} + name;
            auto* partner_entity =
                UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, path.c_str());
            if (!is_uobject_valid_fast(partner_entity))
            {
                continue;
            }
            auto* partner =
                read_boss_owner_property(partner_entity, level_entity_actor_property_name.data());
            lowest_hp = std::min(lowest_hp, opening_hp_percent(partner));
        }
        if (!remind_ansem_retreat_due(state.boss.repairs.opening_battle_seconds, lowest_hp))
        {
            return;
        }
        auto* persistent_level = entity->GetOuterPrivate();
        auto* script = is_uobject_valid_fast(persistent_level)
                           ? read_boss_owner_property(persistent_level, STR("LevelScriptActor"))
                           : nullptr;
        auto* event = is_uobject_valid_fast(script)
                          ? script->GetFunctionByNameInChain(STR("RemoteEvent"))
                          : nullptr;
        auto* name = event ? find_function_param_by_name(event, L"EventName") : nullptr;
        if (!name || event->GetParmsSize() <= 0)
        {
            return;
        }
        std::vector<std::byte> params(event->GetParmsSize());
        if (!write_name_or_string_function_param(name, params.data(),
                                                 L"program_e_ex316_s0_REvt_PhaseChange2"))
        {
            return;
        }
        state.boss.repairs.opening_retreat_dispatched = true;
        script->ProcessEvent(event, params.data());
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] Re Mind opening Ansem retreat restored missing_driver=Xigbar "
                "battle_seconds={} lowest_hp_percent={} route=native_phase_event\n"),
            state.boss.repairs.opening_battle_seconds, lowest_hp);
    }

    auto install_opening_ansem_phase_driver() -> void
    {
        auto& state = s_runtime_enemy_remap;
        if (!std::any_of(state.boss.boss_class_replacements.begin(),
                         state.boss.boss_class_replacements.end(),
                         [](const auto& entry)
                         {
                             return needs_remind_ansem_phase_driver(entry.first, entry.second);
                         }))
        {
            return;
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto profile = boss_native::select_profile(nt->FileHeader.TimeDateStamp,
                                                         nt->OptionalHeader.SizeOfImage);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE ||
            !profile.supported ||
            std::memcmp(reinterpret_cast<void*>(base + profile.ansem_tick_rva),
                        profile.ansem_tick_bytes.data(), profile.ansem_tick_bytes.size()))
        {
            Output::send<LogLevel::Error>(STR("[KH3APProbeCpp] Opening Ansem phase driver "
                                              "unavailable reason=unrecognized_native_tick\n"));
            return;
        }
        auto hook = std::make_unique<PLH::x64Detour>(
            base + profile.ansem_tick_rva, reinterpret_cast<uint64_t>(&tick_opening_ansem),
            &state.boss.repairs.ansem_tick_trampoline);
        if (!hook->hook())
        {
            return;
        }
        state.boss.repairs.opening_ansem_class.emplace(STR("e_ex316_Pawn_s11_C"), FNAME_Add);
        state.boss.repairs.ansem_phase_driver = std::move(hook);
        Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Opening Ansem phase driver installed "
                                           "storefront={} scope=remind_area_a_missing_xigbar\n"),
                                       profile.steam ? STR("steam") : STR("egs"));
    }

    static auto is_marshmallow_replacement_cutscene(UObject* matinee) -> bool
    {
        if (!s_runtime_enemy_remap.active || !s_runtime_enemy_remap.boss.boss_enabled.load())
        {
            return false;
        }
        if (!is_uobject_valid_fast(matinee) || matinee->GetName() != STR("fz465"))
        {
            return false;
        }
        const std::wstring path = matinee->GetPathName();
        if (path.find(STR("/fz/cutscene_fz01_fz465.cutscene_fz01_fz465:")) == std::wstring::npos)
        {
            return false;
        }
        const std::wstring source =
            STR("/Game/Blueprints/Enemy/e_fz903/e_fz903_Pawn.e_fz903_Pawn_C");
        const auto replacement = s_runtime_enemy_remap.boss.boss_class_replacements.find(source);
        if (replacement == s_runtime_enemy_remap.boss.boss_class_replacements.end() ||
            replacement->second == source)
        {
            return false;
        }
        return true;
    }

    struct CutsceneObjectRef
    {
        FWeakObjectPtr weak{};
        UObject* slot_guarded{};
        int32_t index{-1};

        auto bind(UObject* object) -> void
        {
            weak = object;
            slot_guarded = object;
            index = object ? object->GetInternalIndex() : -1;
        }

        auto get() const -> UObject*
        {
            if (UObject* object = weak.Get(); is_uobject_valid_fast(object))
            {
                return object;
            }
            // Cooked map exports can have a zero weak serial on KH3.
            return is_uobject_valid_fast(slot_guarded) && slot_guarded->GetInternalIndex() == index
                       ? slot_guarded
                       : nullptr;
        }
    };
    std::vector<CutsceneObjectRef> marshmallow_optional_groups;
    CutsceneObjectRef pending_marshmallow_matinee;
    bool marshmallow_skip_pending{};

    static auto restore_marshmallow_groups() -> void
    {
        for (const CutsceneObjectRef& reference : marshmallow_optional_groups)
        {
            UObject* const group = reference.get();
            if (!group)
            {
                continue;
            }
            FProperty* const property = group->GetPropertyByNameInChain(STR("m_EnsureGroupActor"));
            if (property && property->GetClass().GetName() == STR("BoolProperty"))
            {
                static_cast<FBoolProperty*>(property)->SetPropertyValueInContainer(group, true);
            }
        }
        marshmallow_optional_groups.clear();
    }

    auto
    prepare_marshmallow_replacement_cutscene(UnrealScriptFunctionCallableContext& callback_context,
                                             void*) -> void
    {
        UObject* const matinee = callback_context.Context;
        if (!is_marshmallow_replacement_cutscene(matinee))
        {
            return;
        }
        if (marshmallow_skip_pending)
        {
            return;
        }
        UObject* const data = try_read_object_property_object(matinee, STR("MatineeData"));
        void* const groups_ptr = is_uobject_valid_fast(data)
                                     ? data->GetValuePtrByPropertyNameInChain(STR("InterpGroups"))
                                     : nullptr;
        if (!groups_ptr)
        {
            return;
        }
        ArrayHeader groups{};
        std::memcpy(&groups, groups_ptr, sizeof(groups));
        if (!groups.data || groups.num < 0 || groups.num > max_encounter_repair_groups ||
            groups.max < groups.num)
        {
            return;
        }
        auto** const objects = reinterpret_cast<UObject**>(groups.data);
        for (int32_t index = 0; index < groups.num; ++index)
        {
            UObject* const group = objects[index];
            if (!is_uobject_valid_fast(group))
            {
                continue;
            }
            auto* const name =
                static_cast<FName*>(group->GetValuePtrByPropertyNameInChain(STR("GroupName")));
            FProperty* const property = group->GetPropertyByNameInChain(STR("m_EnsureGroupActor"));
            if (!name || name->ToString() != STR("fz465a_Marshmallow_e_fz903_0") || !property ||
                property->GetClass().GetName() != STR("BoolProperty"))
            {
                continue;
            }
            auto* const required = static_cast<FBoolProperty*>(property);
            if (required->GetPropertyValueInContainer(group))
            {
                CutsceneObjectRef reference{};
                reference.bind(group);
                marshmallow_optional_groups.push_back(reference);
                required->SetPropertyValueInContainer(group, false);
            }
        }
        if (!marshmallow_optional_groups.empty())
        {
            pending_marshmallow_matinee.bind(matinee);
            marshmallow_skip_pending = true;
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Marshmallow cutscene waiting for actor-track initialization; "
                    "original-actor requirement temporarily disabled\n"));
        }
    }

    auto process_pending_marshmallow_cutscene() -> void
    {
        if (!marshmallow_skip_pending)
        {
            return;
        }
        UObject* const matinee = pending_marshmallow_matinee.get();
        if (!is_marshmallow_replacement_cutscene(matinee))
        {
            restore_marshmallow_groups();
            pending_marshmallow_matinee = {};
            marshmallow_skip_pending = false;
            return;
        }
        const std::wstring path = matinee->GetPathName();
        void* const instances = matinee->GetValuePtrByPropertyNameInChain(STR("GroupInst"));
        ArrayHeader groups{};
        if (instances)
        {
            std::memcpy(&groups, instances, sizeof(groups));
        }
        if (groups.num <= 0)
        {
            return;
        }
        FProperty* const waiting = matinee->GetPropertyByNameInChain(STR("m_Waiting"));
        if (!waiting || waiting->GetClass().GetName() != STR("BoolProperty") ||
            static_cast<FBoolProperty*>(waiting)->GetPropertyValueInContainer(matinee))
        {
            return;
        }
        UObject* const level = matinee->GetOuterPrivate();
        UObject* const script =
            is_uobject_valid_fast(level)
                ? try_read_object_property_object(level, STR("LevelScriptActor"))
                : nullptr;
        UFunction* const skip = is_uobject_valid_fast(script)
                                    ? script->GetFunctionByNameInChain(STR("OnEventSkip"))
                                    : nullptr;
        if (!skip || skip->GetParmsSize() != 0)
        {
            return;
        }
        restore_marshmallow_groups();
        pending_marshmallow_matinee = {};
        marshmallow_skip_pending = false;
        // fz465's required TresInterpGroup searches for the e_fz903 mesh.
        // Temporarily relaxing only that group's requirement lets Play
        // initialize the remaining tracks. The stock skip graph then moves
        // Sora to the end-frame position and restores actors/collision.
        // Skipping while Play is still queued leaves GroupInst empty and
        // cannot perform the fight-position handoff.
        script->ProcessEvent(skip, nullptr);
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] Boss runtime Marshmallow cutscene skipped "
                "matinee={} initialized_groups={} route=OnEventSkip "
                "reason=required_original_actor_missing polling=false\n"),
            path, groups.num);
    }

    auto skip_marshmallow_replacement_cutscene(UnrealScriptFunctionCallableContext&, void*) -> void
    {
        process_pending_marshmallow_cutscene();
    }

    static auto guarded_terranort_target_predicate(UObject* pawn) -> bool
    {
        // Turning the option off does not despawn existing replacements.
        // Keep their controller prerequisite guarded until runtime teardown.
        if (s_runtime_enemy_remap.active)
        {
            if (!is_uobject_valid_fast(pawn))
            {
                return false;
            }
            auto* controller = read_boss_owner_property(pawn, L"Controller");
            auto* expected_class = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Script/TresGame.TresAIController");
            if (!is_uobject_a_fast(controller, expected_class))
            {
                if (!s_runtime_enemy_remap.boss.repairs.terranort_controller_guard_logged.exchange(
                        true))
                {
                    Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Terranort target predicate guarded "
                            "reason=missing_ai_controller pawn={} class={} "
                            "native_damage_unchanged=true\n"),
                        pawn->GetPathName(), pawn->GetClassPrivate()->GetPathName());
                }
                return false;
            }
        }
        using Predicate = bool (*)(UObject*);
        return reinterpret_cast<Predicate>(
            s_runtime_enemy_remap.boss.repairs.terranort_controller_trampoline)(pawn);
    }

    auto install_terranort_controller_guard() -> void
    {
        if (!std::any_of(s_runtime_enemy_remap.boss.boss_class_replacements.begin(),
                         s_runtime_enemy_remap.boss.boss_class_replacements.end(),
                         [](const auto& entry)
                         {
                             return entry.first != entry.second &&
                                    is_story_terranort_class(entry.second);
                         }))
        {
            return;
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto profile = boss_native::select_profile(nt->FileHeader.TimeDateStamp,
                                                         nt->OptionalHeader.SizeOfImage);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE ||
            !profile.supported ||
            std::memcmp(reinterpret_cast<void*>(base + profile.target_rva),
                        profile.target_bytes.data(), profile.target_bytes.size()) ||
            std::memcmp(reinterpret_cast<void*>(base + profile.target_dereference_rva),
                        profile.target_dereference_bytes.data(),
                        profile.target_dereference_bytes.size()))
        {
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Terranort controller guard "
                                                "skipped reason=unsupported_native_signature\n"));
            return;
        }
        auto guard = std::make_unique<PLH::x64Detour>(
            base + profile.target_rva,
            reinterpret_cast<uint64_t>(&guarded_terranort_target_predicate),
            &s_runtime_enemy_remap.boss.repairs.terranort_controller_trampoline);
        if (!guard->hook())
        {
            Output::send<LogLevel::Error>(
                STR("[KH3APProbeCpp] Terranort controller guard failed reason=hook_install\n"));
            return;
        }
        s_runtime_enemy_remap.boss.repairs.terranort_controller_guard = std::move(guard);
        Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Terranort controller guard installed "
                                           "storefront={} scope=randomized_story_terranort\n"),
                                       profile.steam ? STR("steam") : STR("egs"));
    }

    struct NativeBossPartnerArray
    {
        UObject** data{};
        int32_t count{};
        int32_t capacity{};
    };
    static_assert(sizeof(NativeBossPartnerArray) == 16);

    // XIII's lethal-damage branch protects every other XIII pawn returned
    // by this query. Those pawns are native encounter partners, not the
    // source level's randomized group. Leave that group to its own counters.
    static auto enumerate_boss_death_partners(UObject* pawn, NativeBossPartnerArray* partners)
        -> void
    {
        const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
        if (caller == s_runtime_enemy_remap.boss.repairs.boss_death_partner_caller &&
            s_runtime_enemy_remap.active && s_runtime_enemy_remap.boss.boss_enabled.load() &&
            is_uobject_valid_fast(pawn) && is_runtime_boss_replacement_pawn(pawn))
        {
            // Some replacement Blueprints drop the source's actor link
            // after appearance. Repair it before the lethal-damage branch,
            // which runs before the ordinary death-state callback.
            const auto binding = resolve_and_bind_runtime_boss_pawn(pawn);
            if (is_uobject_valid_fast(binding.level_entity) &&
                read_boss_owner_property(binding.level_entity,
                                         level_entity_actor_property_name.data()) == pawn)
            {
                bool replaced{};
                {
                    std::lock_guard lock{s_runtime_enemy_remap.boss.tracking.boss_state_mutex};
                    const auto source =
                        s_runtime_enemy_remap.boss.tracking.remapped_boss_source_classes.find(
                            binding.level_entity_path);
                    const auto target =
                        s_runtime_enemy_remap.boss.tracking.remapped_boss_entities.find(
                            binding.level_entity_path);
                    replaced =
                        source != s_runtime_enemy_remap.boss.tracking.remapped_boss_source_classes
                                      .end() &&
                        target !=
                            s_runtime_enemy_remap.boss.tracking.remapped_boss_entities.end() &&
                        source->second != target->second &&
                        target->second == pawn->GetClassPrivate()->GetPathName();
                }
                if (replaced)
                {
                    partners->count = 0;
                    if (!s_runtime_enemy_remap.boss.repairs.boss_death_partner_guard_logged
                             .exchange(true))
                    {
                        Output::send<LogLevel::Warning>(
                            STR("[KH3APProbeCpp] Boss native death-partner protection skipped "
                                "scope=tracked_replacement source_group_owns_completion=true "
                                "pawn={}\n"),
                            pawn->GetPathName());
                    }
                    return;
                }
            }
        }
        using Enumerate = void (*)(UObject*, NativeBossPartnerArray*);
        reinterpret_cast<Enumerate>(
            s_runtime_enemy_remap.boss.repairs.boss_death_partner_trampoline)(pawn, partners);
    }

    auto install_boss_death_partner_guard() -> void
    {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto profile = boss_native::select_profile(nt->FileHeader.TimeDateStamp,
                                                         nt->OptionalHeader.SizeOfImage);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE ||
            !profile.supported ||
            std::memcmp(reinterpret_cast<void*>(base + profile.partners_rva),
                        profile.partners_bytes.data(), profile.partners_bytes.size()) ||
            std::memcmp(reinterpret_cast<void*>(base + profile.lethal_caller_rva),
                        profile.lethal_caller_bytes.data(), profile.lethal_caller_bytes.size()))
        {
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Boss death-partner guard skipped "
                                                "reason=unsupported_native_signature\n"));
            return;
        }
        s_runtime_enemy_remap.boss.repairs.boss_death_partner_caller =
            base + profile.lethal_return_rva;
        auto hook = std::make_unique<PLH::x64Detour>(
            base + profile.partners_rva, reinterpret_cast<uint64_t>(&enumerate_boss_death_partners),
            &s_runtime_enemy_remap.boss.repairs.boss_death_partner_trampoline);
        if (!hook->hook())
        {
            Output::send<LogLevel::Error>(
                STR("[KH3APProbeCpp] Boss death-partner guard failed reason=hook_install\n"));
            return;
        }
        s_runtime_enemy_remap.boss.repairs.boss_death_partner_guard = std::move(hook);
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] Boss death-partner guard installed storefront={} "
                "scope=tracked_replacement_lethal_damage\n"),
            profile.steam ? STR("steam") : STR("egs"));
    }

} // namespace kh3ap::enemy_remap_detail
