#include "kh3ap/features/enemies/bosses/binding.h"
#include "kh3ap/features/enemies/bosses/hp_limits.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>

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
    using LuxordHpQueryFn = bool (*)(UObject*);

    static auto luxord_partner_hp_limit(UObject* pawn) -> bool
    {
        if (!is_uobject_valid_fast(pawn)) return false;
        auto& repairs = s_runtime_enemy_remap.boss.repairs;
        LuxordHpQueryFn original{};
        bool locked{};
        bool log_blocked{}, log_released{};
        int32_t spawns{}, kills{};
        {
            std::lock_guard lock{repairs.luxord_hp_mutex};
            void** const vtable = *reinterpret_cast<void***>(pawn);
            const auto hook = repairs.luxord_hp_overrides.find(
                    &vtable[repairs.luxord_hp_slot / sizeof(void*)]);
            if (hook != repairs.luxord_hp_overrides.end())
                original = reinterpret_cast<LuxordHpQueryFn>(hook->second->original);
            const auto found = repairs.luxord_hp_gates.find(pawn);
            if (s_runtime_enemy_remap.active && s_runtime_enemy_remap.boss.boss_enabled.load() &&
                found != repairs.luxord_hp_gates.end() && found->second.pawn.Get() == pawn)
            {
                auto& gate = found->second;
                auto* const entity = gate.entity.Get();
                if (is_uobject_valid_fast(entity))
                {
                    auto* const luxord = gate.luxord_entity.Get();
                    locked = true;
                    if (is_uobject_valid_fast(luxord) && luxord->GetOuterPrivate() == entity->GetOuterPrivate() &&
                        gate.luxord_spawn_offset >= 0 && gate.luxord_kill_offset >= 0)
                    {
                        // A virtual HP query can run under engine object locks.
                        // Resolve objects/properties at the binding event only;
                        // this path performs weak validation and scalar reads.
                        std::memcpy(&spawns, reinterpret_cast<const std::byte*>(luxord) + gate.luxord_spawn_offset, sizeof(spawns));
                        std::memcpy(&kills, reinterpret_cast<const std::byte*>(luxord) + gate.luxord_kill_offset, sizeof(kills));
                        locked = !luxord_native_defeat_observed(spawns, kills);
                    }
                    if (locked && !gate.blocked_logged)
                    {
                        gate.blocked_logged = true;
                        log_blocked = true;
                    }
                    if (!locked && gate.blocked_logged && !gate.released_logged)
                    {
                        gate.released_logged = true;
                        log_released = true;
                    }
                }
            }
        }
        if (log_blocked)
            Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Luxord partner HP floor active pawn_address={} native_card_game_must_finish_first=true polling=false\n"), reinterpret_cast<uintptr_t>(pawn));
        if (log_released)
            Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Luxord partner HP floor released pawn_address={} luxord_spawns={} luxord_kills={} native_defeat=true polling=false\n"), reinterpret_cast<uintptr_t>(pawn), spawns, kills);
        // The encounter's required order takes precedence over a replacement's
        // own scripted-floor bypass. No flags or native damage events are edited.
        return locked || (original && original(pawn));
    }

    void register_luxord_source_entity(UObject* entity, std::wstring_view path)
    {
        if (!is_luxord_source_entity(path) || !is_uobject_valid_fast(entity)) return;
        auto counter_offset = [entity](std::wstring_view name) -> int32_t {
            auto* const field = entity->GetPropertyByNameInChain(name.data());
            return field && field->GetClass().GetName() == STR("IntProperty") && field->GetElementSize() == sizeof(int32_t)
                    ? field->GetOffset_Internal() : -1;
        };
        const auto spawn_offset = counter_offset(level_entity_spawn_count_property_name);
        const auto kill_offset = counter_offset(level_entity_kill_count_property_name);
        auto* const level = entity->GetOuterPrivate();
        auto& repairs = s_runtime_enemy_remap.boss.repairs;
        std::lock_guard lock{repairs.luxord_hp_mutex};
        for (auto it = repairs.luxord_source_entities.begin(); it != repairs.luxord_source_entities.end();)
        {
            const auto& source = it->second.source;
            if (!is_uobject_valid_fast(source.slot_guarded_level_entity) ||
                source.slot_guarded_level_entity->GetInternalIndex() != source.object_index)
                it = repairs.luxord_source_entities.erase(it);
            else ++it;
        }
        auto& cached = repairs.luxord_source_entities[level];
        cached.source.bind(entity);
        cached.source.level_entity_path = path;
        cached.spawn_offset = spawn_offset; cached.kill_offset = kill_offset;
        for (auto& [_, gate] : repairs.luxord_hp_gates)
            if (auto* owner = gate.entity.Get(); owner && owner->GetOuterPrivate() == level)
            {
                gate.luxord_entity = entity;
                gate.luxord_spawn_offset = spawn_offset; gate.luxord_kill_offset = kill_offset;
            }
    }

    void install_luxord_partner_hp_gate(UObject* pawn, const RuntimeBossEntityBinding& binding)
    {
        if (!is_uobject_valid_fast(pawn) || !is_uobject_valid_fast(binding.level_entity)) return;
        const auto source = tracked_boss_source_class(binding.level_entity_path);
        if (!source || *source == pawn->GetClassPrivate()->GetPathName() ||
            !is_luxord_partner_slot(binding.level_entity_path, *source)) return;
        auto* const function = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/TresGame.TresCharPawnBase:IsConditionHpLimitOne"));
        auto* const property = pawn->GetPropertyByNameInChain(STR("m_bIsHpLimitOne"));
        if (!is_uobject_valid_fast(function) || !function->GetFuncPtr() || !property || !property->IsA<FBoolProperty>()) return;
        const auto slot = hp_limit_virtual_slot({reinterpret_cast<const uint8_t*>(function->GetFuncPtr()), hp_limit_exec_thunk.size()});
        if (!slot) return;
        void** const vtable = *reinterpret_cast<void***>(pawn);
        void** const cell = &vtable[*slot / sizeof(void*)];
        auto& repairs = s_runtime_enemy_remap.boss.repairs;
        std::lock_guard lock{repairs.luxord_hp_mutex};
        if (!repairs.luxord_hp_overrides.contains(cell))
        {
            const auto* const target = static_cast<const uint8_t*>(*cell);
            const bool policy_hook = std::any_of(s_runtime_enemy_remap.boss.boss_hp_limits.begin(),
                    s_runtime_enemy_remap.boss.boss_hp_limits.end(), [cell](const auto& state) {
                        return state.override && state.override->slot == cell && state.override->replacement == *cell;
                    });
            const bool native_getter = std::any_of(boss_hp_limit_policies.begin(), boss_hp_limit_policies.end(),
                    [target, property](const auto& policy) {
                        return is_boss_hp_limit_getter(policy.kind,
                                {target, boss_hp_limit_getter_size(policy.kind)}, property->GetOffset_Internal());
                    });
            if (!policy_hook && !native_getter)
            {
                Output::send<LogLevel::Error>(STR("[KH3APProbeCpp] Luxord partner HP guard unavailable pawn={} reason=unrecognized_native_getter\n"), pawn->GetPathName());
                return;
            }
            auto hook = std::make_unique<RuntimeBossHpLimitOverride>();
            hook->slot = cell; hook->original = *cell;
            hook->replacement = reinterpret_cast<void*>(&luxord_partner_hp_limit);
            PLH::MemAccessor memory{};
            PLH::MemoryProtector protection{reinterpret_cast<uint64_t>(cell), sizeof(void*),
                    PLH::ProtFlag::R | PLH::ProtFlag::W, memory};
            if (!protection.isGood()) return;
            repairs.luxord_hp_slot = *slot;
            repairs.luxord_hp_overrides.emplace(cell, std::move(hook));
            *cell = reinterpret_cast<void*>(&luxord_partner_hp_limit);
        }
        for (auto it = repairs.luxord_hp_gates.begin(); it != repairs.luxord_hp_gates.end();)
            if (!it->second.pawn.Get()) it = repairs.luxord_hp_gates.erase(it); else ++it;
        auto& gate = repairs.luxord_hp_gates[pawn];
        gate.pawn = make_uobject_weak(pawn); gate.entity = make_uobject_weak(binding.level_entity);
        const auto cached = repairs.luxord_source_entities.find(binding.level_entity->GetOuterPrivate());
        if (cached != repairs.luxord_source_entities.end())
        {
            gate.luxord_entity = make_uobject_weak(cached->second.source.get());
            gate.luxord_spawn_offset = cached->second.spawn_offset; gate.luxord_kill_offset = cached->second.kill_offset;
        }
        Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Luxord partner HP guard bound entity={} pawn={} luxord_reference_ready={} scope=tracked_area_b_replacement polling=false\n"), binding.level_entity_path, pawn->GetPathName(), gate.luxord_entity.Get() != nullptr);
    }

    void prepare_luxord_partner_hp_gate(UObject* pawn)
    {
        auto* const level = pawn->GetOuterPrivate();
        auto* const world = is_uobject_valid_fast(level) ? level->GetOuterPrivate() : nullptr;
        if (!is_uobject_valid_fast(world)) return;
        static const FName base_area{STR("kg_02_enemy_Area_B"), FNAME_Add};
        static const FName remind_area{STR("kg_05_enemy_Area_B"), FNAME_Add};
        const auto name = world->GetNamePrivate();
        if (name != base_area && name != remind_area) return;
        auto& repairs = s_runtime_enemy_remap.boss.repairs;
        {
            std::lock_guard lock{repairs.luxord_hp_mutex};
            const auto found = repairs.luxord_hp_gates.find(pawn);
            if (found != repairs.luxord_hp_gates.end() && found->second.pawn.Get() == pawn) return;
        }
        // Not every boss constructs an Appear state. Bind at its first native
        // state construction, before a lethal hit can advance the source script.
        install_luxord_partner_hp_gate(pawn, resolve_and_bind_runtime_boss_pawn(pawn));
    }

    template<size_t Index>
    static auto is_tracked_hp_limit_replacement(UObject* pawn) -> bool
    {
        auto& state = s_runtime_enemy_remap.boss.boss_hp_limits[Index];
        constexpr auto& policy = boss_hp_limit_policies[Index];
        if (!s_runtime_enemy_remap.active || !s_runtime_enemy_remap.boss.boss_enabled.load() ||
            !is_uobject_valid_fast(pawn) || !state.class_name)
            return false;
        auto* const pawn_class = pawn->GetClassPrivate();
        if (!is_uobject_valid_fast(pawn_class) || (pawn_class->GetNamePrivate() != *state.class_name &&
             (!state.variant_class_name || pawn_class->GetNamePrivate() != *state.variant_class_name)))
            return false;
        // A matching class alone is insufficient. Require the live source
        // entity to own this particular randomized replacement instance.
        UObject* const entity = read_boss_owner_property(
                pawn, pawn_level_entity_property_name.data());
        if (!is_uobject_valid_fast(entity) ||
            read_boss_owner_property(
                    entity, level_entity_actor_property_name.data()) != pawn)
        {
            return false;
        }
        const std::wstring entity_path = entity->GetPathName();
        {
            std::lock_guard lock{s_runtime_enemy_remap.boss.tracking.boss_state_mutex};
            const auto replacement =
                    s_runtime_enemy_remap.boss.tracking.remapped_boss_entities.find(entity_path);
            const auto source =
                    s_runtime_enemy_remap.boss.tracking.remapped_boss_source_classes.find(entity_path);
            if (replacement == s_runtime_enemy_remap.boss.tracking.remapped_boss_entities.end() ||
                source == s_runtime_enemy_remap.boss.tracking.remapped_boss_source_classes.end() ||
                !matches_boss_hp_limit_policy(policy, replacement->second) ||
                !uses_boss_hp_limit_bypass(source->second, replacement->second))
            {
                return false;
            }
        }
        return true;
    }

    using BossSetHpFn = void (*)(UObject*, int32_t);
    template<size_t Index>
    static auto xion_set_hp_detour(UObject* pawn, int32_t hp) -> void
    {
        auto& state = s_runtime_enemy_remap.boss.boss_hp_limits[Index];
        if (hp <= 0 && is_tracked_hp_limit_replacement<Index>(pawn))
        {
            if (!state.setter_bypass_logged.exchange(true))
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Xion HP setter clamp bypassed scope=tracked_replacement native_notifications=true\n"));
            reinterpret_cast<BossSetHpFn>(state.base_setter)(pawn, hp);
            return;
        }
        reinterpret_cast<BossSetHpFn>(state.setter_override->original)(pawn, hp);
    }

    using BossHpLimitFn = bool (*)(UObject* pawn);

    template<size_t Index>
    static auto tide_set_hp_detour(UObject* pawn, int32_t hp) -> void
    {
        auto& state = s_runtime_enemy_remap.boss.boss_hp_limits[Index];
        if (is_tracked_hp_limit_replacement<Index>(pawn) && state.variant_class_name &&
            pawn->GetClassPrivate()->GetNamePrivate() == *state.variant_class_name)
        {
            const auto current = try_read_int32_object_property(pawn, L"m_HitPoint");
            const auto cutoff = try_read_int32_object_property(pawn, L"m_ttBattleEndHp");
            using Getter = int32_t (*)(UObject*);
            if (current && cutoff)
            {
                const auto max_hp = reinterpret_cast<Getter>(state.max_hp_getter)(pawn);
                auto adjusted = tide_scripted_finish_hp(*current, hp, max_hp, *cutoff);
                // The TT cutoff repair must also honor an enclosing native
                // encounter's death order, just like ordinary damage does.
                if (adjusted == 0 && reinterpret_cast<BossHpLimitFn>(*state.override->slot)(pawn))
                    adjusted = 1;
                if (adjusted == 0 && adjusted != hp && !state.setter_bypass_logged.exchange(true))
                    Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] TT Demon Tide scripted cutoff reached percent={} native_damage_and_death=true\n"), *cutoff);
                hp = adjusted;
            }
        }
        reinterpret_cast<BossSetHpFn>(state.setter_override->original)(pawn, hp);
    }

    template<size_t Index>
    static auto boss_hp_limit_detour(UObject* pawn) -> bool
    {
        auto& state = s_runtime_enemy_remap.boss.boss_hp_limits[Index];
        constexpr auto& policy = boss_hp_limit_policies[Index];
        const auto original = reinterpret_cast<BossHpLimitFn>(state.original);
        const bool limited = original(pawn);
        if (!limited || !s_runtime_enemy_remap.active ||
            !s_runtime_enemy_remap.boss.boss_enabled.load() ||
            !is_uobject_valid_fast(pawn))
        {
            return limited;
        }
        UClass* const pawn_class = pawn->GetClassPrivate();
        if (!is_uobject_valid_fast(pawn_class) ||
            (pawn_class->GetNamePrivate() != *state.class_name &&
             (!state.variant_class_name || pawn_class->GetNamePrivate() != *state.variant_class_name)) ||
            (preserves_explicit_boss_hp_limit(policy.kind) &&
             state.explicit_limit_property->GetPropertyValueInContainer(pawn)))
        {
            return limited;
        }

        if (!is_tracked_hp_limit_replacement<Index>(pawn))
            return limited;
        if (state.release_phase_property && pawn_class->GetNamePrivate() == *state.class_name &&
            !state.release_phase_property->GetPropertyValueInContainer(pawn))
            return limited;
        const auto entity_path = read_boss_owner_property(
                pawn, pawn_level_entity_property_name.data())->GetPathName();
        if (!state.bypass_logged.exchange(true))
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Boss runtime scripted HP limit bypassed boss={} "
                        "entity={} explicit_hp_limit_policy={} "
                        "native_damage_and_death=true polling=false\n"),
                    policy.name, entity_path,
                    preserves_explicit_boss_hp_limit(policy.kind)
                            ? STR("preserved") : STR("bypassed_for_tracked_replacement"));
        }
        // Preserve explicit invulnerability, player finishing-hit rules,
        // AI phases, and native death/group completion. Audited explicit-lock
        // replacements additionally bypass their explicit one-HP lock.
        return false;
    }

    template<size_t Index>
    static auto install_boss_hp_limit_detour() -> void
    {
        constexpr auto& policy = boss_hp_limit_policies[Index];
        auto& state = s_runtime_enemy_remap.boss.boss_hp_limits[Index];
        if (!std::any_of(
                    s_runtime_enemy_remap.boss.boss_class_replacements.begin(),
                    s_runtime_enemy_remap.boss.boss_class_replacements.end(),
                    [](const auto& entry) {
                        return matches_boss_hp_limit_policy(boss_hp_limit_policies[Index], entry.second) &&
                               uses_boss_hp_limit_bypass(entry.first, entry.second);
                    }))
        {
            return;
        }
        UFunction* const function = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr,
                STR("/Script/TresGame.TresCharPawnBase:IsConditionHpLimitOne"));
        UObject* const cdo = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr,
                policy.native_cdo.data());
        FProperty* const property = is_uobject_valid_fast(cdo)
                ? cdo->GetPropertyByNameInChain(STR("m_bIsHpLimitOne"))
                : nullptr;
        if (!is_uobject_valid_fast(function) || !function->GetFuncPtr() ||
            !property || !property->IsA<FBoolProperty>())
        {
            Output::send<LogLevel::Error>(
                    STR("[KH3APProbeCpp] Boss runtime HP limit hook unavailable boss={} "
                        "reason=native_reflection_missing\n"), policy.name);
            return;
        }
        if constexpr (!policy.release_phase_property.empty())
        {
            auto* const phase = cdo->GetPropertyByNameInChain(policy.release_phase_property.data());
            if (!phase || !phase->IsA<FBoolProperty>())
            {
                Output::send<LogLevel::Error>(STR("[KH3APProbeCpp] Boss runtime HP limit hook unavailable boss={} reason=phase_property_missing\n"), policy.name);
                return;
            }
            state.release_phase_property = static_cast<FBoolProperty*>(phase);
        }
        const auto slot = hp_limit_virtual_slot({
                reinterpret_cast<const uint8_t*>(function->GetFuncPtr()),
                hp_limit_exec_thunk.size()});
        void** const vtable = *reinterpret_cast<void***>(cdo);
        const auto* const target = slot && vtable
                ? static_cast<const uint8_t*>(vtable[*slot / sizeof(void*)])
                : nullptr;
        const auto getter_build = target ? identify_boss_hp_limit_build(
                    policy.kind, {target, boss_hp_limit_getter_size(policy.kind)},
                    property->GetOffset_Internal()) : std::nullopt;
        if (!getter_build)
        {
            Output::send<LogLevel::Error>(
                    STR("[KH3APProbeCpp] Boss runtime HP limit hook unavailable boss={} "
                        "reason=unrecognized_native_getter\n"), policy.name);
            return;
        }
        if (*getter_build == BossHpLimitBuild::steam)
        {
            UObject* const base_cdo = UObjectGlobals::StaticFindObject<UObject*>(
                    nullptr, nullptr, STR("/Script/TresGame.Default__TresCharPawnBase"));
            void** const base_vtable = is_uobject_valid_fast(base_cdo)
                    ? *reinterpret_cast<void***>(base_cdo) : nullptr;
            const auto* const fallback = base_vtable
                    ? static_cast<const uint8_t*>(base_vtable[*slot / sizeof(void*)]) : nullptr;
            if (!fallback || !matches_steam_hp_limit_fallback(
                        policy.kind, {target, boss_hp_limit_getter_size(policy.kind)},
                        reinterpret_cast<uintptr_t>(target), reinterpret_cast<uintptr_t>(fallback),
                        {fallback, explicit_hp_limit_getter.size()}, property->GetOffset_Internal()))
            {
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] Boss runtime HP limit hook unavailable boss={} "
                            "reason=unrecognized_native_fallback\n"), policy.name);
                return;
            }
        }
        if constexpr (policy.kind == BossHpLimitKind::data_young_xehanort)
        {
            // The verified getter calls its state predicate at byte 9.
            // Validate that body too before replacing the virtual entry.
            const auto predicate = hp_limit_relative_target(
                    {target, boss_hp_limit_getter_size(policy.kind)},
                    reinterpret_cast<uintptr_t>(target), 9, 0xe8);
            const auto signature = boss_hp_limit_predicate_signature(*getter_build);
            if (!predicate || !matches_hp_limit_signature(
                        {reinterpret_cast<const uint8_t*>(*predicate), signature.size()}, signature))
            {
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] Boss runtime HP limit hook unavailable boss={} "
                            "reason=unrecognized_native_predicate\n"), policy.name);
                return;
            }
        }
        state.explicit_limit_property = static_cast<FBoolProperty*>(property);
        state.class_name.emplace(policy.class_name.data(), FNAME_Add);
        if (!policy.variant_class_name.empty())
            state.variant_class_name.emplace(policy.variant_class_name.data(), FNAME_Add);
        // Replacement Blueprint instances share this native-class table.
        // Other native classes keep the shared getter's original entry.
        auto override = std::make_unique<RuntimeBossHpLimitOverride>();
        override->slot = &vtable[*slot / sizeof(void*)];
        override->original = *override->slot;
        override->replacement = reinterpret_cast<void*>(&boss_hp_limit_detour<Index>);
        PLH::MemAccessor memory{};
        PLH::MemoryProtector protection{
                reinterpret_cast<uint64_t>(override->slot), sizeof(*override->slot),
                PLH::ProtFlag::R | PLH::ProtFlag::W, memory};
        if (!protection.isGood())
        {
            Output::send<LogLevel::Error>(
                    STR("[KH3APProbeCpp] Boss runtime HP limit hook unavailable boss={} "
                        "reason=vtable_write_protection\n"), policy.name);
            return;
        }
        state.original =
                reinterpret_cast<uint64_t>(override->original);
        *override->slot = override->replacement;
        state.override = std::move(override);
        if constexpr (policy.class_name == L"e_dw401_Pawn_s1_C")
        {
            // The EGS native setter and max-HP getter are shared by the two
            // Tide Blueprints. Scope the cutoff to tracked TT replacements.
            constexpr size_t setter_slot = 0x1840 / sizeof(void*);
            constexpr size_t max_hp_slot = 0x1820 / sizeof(void*);
            constexpr std::array<uint8_t, 38> max_hp_signature{
                0x80,0xb9,0x5d,0x08,0x00,0x00,0x12,0x75,0x0a,0x8b,0x81,0x1c,0x06,0x00,0x00,
                0x85,0xc0,0x7f,0x12,0x48,0x8b,0x89,0xd0,0x04,0x00,0x00,0x48,0x85,0xc9,0x0f,
                0x85,0x7d,0xfb,0xfb,0xff,0x33,0xc0,0xc3};
            const auto* setter = static_cast<const uint8_t*>(vtable[setter_slot]);
            const auto* getter = static_cast<const uint8_t*>(vtable[max_hp_slot]);
            if (*getter_build == BossHpLimitBuild::egs &&
                matches_hp_limit_signature({setter, base_hp_setter.size()}, base_hp_setter) &&
                matches_hp_limit_signature({getter, max_hp_signature.size()}, max_hp_signature))
            {
                auto setter_override = std::make_unique<RuntimeBossHpLimitOverride>();
                setter_override->slot = &vtable[setter_slot];
                setter_override->original = *setter_override->slot;
                setter_override->replacement = reinterpret_cast<void*>(&tide_set_hp_detour<Index>);
                PLH::MemoryProtector setter_protection{
                    reinterpret_cast<uint64_t>(setter_override->slot), sizeof(void*),
                    PLH::ProtFlag::R | PLH::ProtFlag::W, memory};
                if (setter_protection.isGood())
                {
                    state.max_hp_getter = reinterpret_cast<uint64_t>(getter);
                    auto* const slot_address = setter_override->slot;
                    auto* const replacement = setter_override->replacement;
                    state.setter_override = std::move(setter_override);
                    *slot_address = replacement;
                    Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] TT Demon Tide scripted cutoff hook installed scope=tracked_replacement\n"));
                }
            }
            else
                Output::send<LogLevel::Error>(STR("[KH3APProbeCpp] TT Demon Tide cutoff hook unavailable reason=unrecognized_native_functions\n"));
        }
        if constexpr (policy.class_name == L"e_ex310_Pawn_C")
        {
            UObject* const base_cdo = UObjectGlobals::StaticFindObject<UObject*>(
                    nullptr, nullptr, STR("/Script/TresGame.Default__TresEnemyXIIIDPawnBase"));
            void** const base_vtable = is_uobject_valid_fast(base_cdo)
                    ? *reinterpret_cast<void***>(base_cdo) : nullptr;
            constexpr size_t setter_slot = 0x1840 / sizeof(void*);
            auto* const setter = static_cast<const uint8_t*>(vtable[setter_slot]);
            auto* const base_setter = base_vtable
                    ? static_cast<const uint8_t*>(base_vtable[setter_slot]) : nullptr;
            const bool steam_setter = matches_hp_limit_signature(
                    {setter, steam_xion_hp_setter.size()}, steam_xion_hp_setter);
            if (!base_setter ||
                (!steam_setter && !matches_hp_limit_signature({setter, xion_hp_setter.size()}, xion_hp_setter)) ||
                !matches_hp_limit_signature({base_setter, base_hp_setter.size()}, base_hp_setter))
            {
                Output::send<LogLevel::Error>(STR("[KH3APProbeCpp] Xion HP setter hook unavailable reason=unrecognized_native_setter\n"));
            }
            else
            {
                auto setter_override = std::make_unique<RuntimeBossHpLimitOverride>();
                setter_override->slot = &vtable[setter_slot];
                setter_override->original = *setter_override->slot;
                setter_override->replacement = reinterpret_cast<void*>(&xion_set_hp_detour<Index>);
                PLH::MemoryProtector setter_protection{
                        reinterpret_cast<uint64_t>(setter_override->slot), sizeof(void*),
                        PLH::ProtFlag::R | PLH::ProtFlag::W, memory};
                if (setter_protection.isGood())
                {
                    state.base_setter = reinterpret_cast<uint64_t>(base_setter);
                    auto* const slot_address = setter_override->slot;
                    auto* const replacement = setter_override->replacement;
                    state.setter_override = std::move(setter_override);
                    *slot_address = replacement;
                    Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Xion HP setter hook installed build={} scope=tracked_replacement\n"),
                            steam_setter ? STR("Steam") : STR("EGS"));
                }
            }
        }

        Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Boss runtime HP limit hook installed boss={} "
                    "build={} "
                    "scope=tracked_replacement explicit_hp_limit={} "
                    "native_damage_and_death=true route=class_vtable polling=false\n"), policy.name,
                *getter_build == BossHpLimitBuild::steam ? STR("Steam") : STR("EGS"),
                preserves_explicit_boss_hp_limit(policy.kind)
                        ? STR("preserved") : STR("bypassed_for_tracked_replacement"));
    }

    template<size_t... Indices>
    static auto install_boss_hp_limit_detours(std::index_sequence<Indices...>) -> void
    {
        (install_boss_hp_limit_detour<Indices>(), ...);
    }

    void install_boss_hp_limits()
    {
        install_boss_hp_limit_detours(std::make_index_sequence<boss_hp_limit_policies.size()>{});
    }
}
