#pragma once
#include "kh3ap/features/enemies/boss_hp_limit_policy.h"
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/core/objects/uobject_validity.h"
#ifdef S
#pragma push_macro("S")
#undef S
#define KH3AP_REMAP_RESTORE_S
#endif
#include <polyhook2/Detour/x64Detour.hpp>
#include <polyhook2/MemProtector.hpp>
#ifdef KH3AP_REMAP_RESTORE_S
#pragma pop_macro("S")
#undef KH3AP_REMAP_RESTORE_S
#endif
#include <Unreal/NameTypes.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace RC::Unreal
{
    class FProperty;
    class FObjectPropertyBase;
    class FBoolProperty;
    class UClass;
    class UFunction;
    class UnrealScriptFunctionCallableContext;
} // namespace RC::Unreal
namespace kh3ap::enemy_remap_detail
{
    using namespace RC;
    using namespace RC::Unreal;
    inline constexpr std::wstring_view level_entity_class_name = STR("TresLevelEntity");
    inline constexpr std::wstring_view level_entity_default_object_path =
        STR("/Script/TresGame.Default__TresLevelEntity");
    inline constexpr std::wstring_view actor_class_path_property_name = STR("m_ActorClassPath");
    inline constexpr std::wstring_view actor_class_property_name = STR("m_ActorClass");
    inline constexpr std::wstring_view string_asset_reference_struct_name =
        STR("StringAssetReference");
    // Loading both sides of every discovered remap keeps menu toggles
    // instant, but it also retains hundreds of thousands of UObjects in
    // asset-heavy maps such as the Garden of Assemblage. Keep the remap
    // data available and let reconcile_loaded_level_entity_remaps load a
    // missing class only when the player actually changes a toggle.
    inline constexpr bool eager_toggle_class_prewarm_enabled = false;
    inline constexpr uint32_t class_prewarm_requests_per_update = 4;
    inline constexpr uint32_t class_prewarm_latent_uuid_base = 0x4b485000;
    inline constexpr auto class_prewarm_repeat_request_delay = std::chrono::seconds(2);
    inline constexpr std::wstring_view ai_ctor_state_event_path =
        STR("/Script/TresGame.TresAIPawnBase:OnCtorStateEvent");
    inline constexpr std::wstring_view data_boss_ctor_state_event_path =
        STR("/Script/TresGame.TresEnemyXIIILPawnBase:OnCtorStateEvent");
    inline constexpr std::wstring_view raise_wipe_out_event_path =
        STR("/Script/TresGame.TresLevelScriptActor:RaiseWipeOutEvent");
    inline constexpr std::wstring_view on_wipe_out_event_name = STR("OnWipeOut");
    inline constexpr std::wstring_view remote_event_name = STR("RemoteEvent");
    inline constexpr std::wstring_view demon_tide_tt_source_class_path =
        STR("/Game/Blueprints/Enemy/e_dw401_s0/e_dw401_Pawn_s0."
            "e_dw401_Pawn_s0_C");
    inline constexpr std::wstring_view demon_tide_tt_completion_event_name =
        STR("e_dw401_s0_REvt_tt_mission_escape");
    inline constexpr std::wstring_view pawn_level_entity_property_name = STR("m_LevelEntity");
    inline constexpr std::wstring_view level_entity_actor_property_name = STR("m_EntityActor");
    inline constexpr std::wstring_view level_entity_group_names_property_name = STR("m_GroupNames");
    inline constexpr std::wstring_view level_entity_spawn_count_property_name = STR("m_SpawnCount");
    inline constexpr std::wstring_view level_entity_kill_count_property_name = STR("m_KillCount");
    inline constexpr std::wstring_view consumed_wipe_out_group_name =
        STR("KH3AP_RuntimeBossWipeOut_Consumed");
    // ETresStateID values from the EGS TresGame reflection data.
    inline constexpr uint64_t tres_state_appear = 58;
    inline constexpr uint64_t tres_state_die = player_state_ids::die;
    inline constexpr uint64_t tres_state_vanish_die = player_state_ids::vanish_die;

    struct ArrayHeader
    {
        uintptr_t data{};
        int32_t num{};
        int32_t max{};
    };

    struct RuntimeBossWipeOutGroupMember
    {
        FWeakObjectPtr level_entity{};
        UObject* slot_guarded_level_entity{};
        int32_t object_index{-1};
        std::wstring level_entity_path{};

        auto bind(UObject* entity) -> void
        {
            level_entity = entity;
            slot_guarded_level_entity = entity;
            object_index = entity->GetInternalIndex();
        }

        [[nodiscard]] auto get() const -> UObject*
        {
            auto* entity = level_entity.Get();
            if (!is_uobject_valid_fast(entity))
            {
                // Cooked KH3 exports may have no weak serial yet.
                entity = slot_guarded_level_entity;
                if (!is_uobject_valid_fast(entity) || entity->GetInternalIndex() != object_index)
                {
                    return nullptr;
                }
            }
            return entity->GetPathName() == level_entity_path ? entity : nullptr;
        }
    };

    enum class RuntimeLevelEntityRemapKind
    {
        enemy,
        boss,
    };

    struct RuntimeLoadedLevelEntityRemap
    {
        FWeakObjectPtr level_entity{};
        UObject* slot_guarded_level_entity{};
        int32_t object_index{-1};
        uintptr_t object_identity{};
        std::wstring object_path{};
        std::wstring source_class_path{};
        std::wstring replacement_class_path{};
        RuntimeLevelEntityRemapKind kind{RuntimeLevelEntityRemapKind::enemy};
    };

    struct RuntimeLevelEntityReconcileResult
    {
        uint32_t tracked{};
        uint32_t live{};
        uint32_t changed{};
        uint32_t class_cache_rebound{};
        uint32_t class_cache_rebind_failed{};
        uint32_t blocking_class_load_attempts{};
        uint64_t blocking_class_load_us{};
        uint32_t expired{};
    };

    struct RuntimeBossHpLimitOverride
    {
        void** slot{};
        void* original{};
        void* replacement{};

        ~RuntimeBossHpLimitOverride()
        {
            if (slot && *slot == replacement)
            {
                PLH::MemAccessor memory{};
                PLH::MemoryProtector protection{reinterpret_cast<uint64_t>(slot), sizeof(*slot),
                                                PLH::ProtFlag::R | PLH::ProtFlag::W, memory};
                if (protection.isGood())
                {
                    *slot = original;
                }
            }
        }
    };

    struct RuntimeBossHpLimitState
    {
        std::unique_ptr<RuntimeBossHpLimitOverride> override{};
        uint64_t original{};
        std::unique_ptr<RuntimeBossHpLimitOverride> setter_override{};
        uint64_t base_setter{};
        uint64_t max_hp_getter{};
        std::atomic_bool setter_bypass_logged{};
        FBoolProperty* explicit_limit_property{};
        FBoolProperty* release_phase_property{};
        std::optional<FName> class_name{};
        std::optional<FName> variant_class_name{};
        std::atomic_bool bypass_logged{};
    };

    struct RuntimeBossTrackingState
    {
        std::pair<int, int> boss_state_hook_ids{};
        std::pair<int, int> data_boss_state_hook_ids{};
        std::pair<int, int> wipe_out_hook_ids{};
        std::unordered_map<std::wstring, std::wstring> remapped_boss_entities{};
        std::unordered_map<std::wstring, std::wstring> remapped_boss_source_classes{};
        std::unordered_map<std::wstring, FWeakObjectPtr> boss_bound_actors{};
        std::unordered_map<std::wstring, std::unordered_set<std::wstring>>
            boss_dispatched_wipe_out_groups{};
        std::unordered_map<
            uintptr_t, std::unordered_map<std::wstring, std::vector<RuntimeBossWipeOutGroupMember>>>
            boss_wipe_out_group_members{};
        std::mutex boss_state_mutex{};
        std::atomic_uint32_t boss_binding_observation_count{};
        std::atomic_uint32_t boss_completion_observation_count{};
    };

    struct RuntimeLuxordPartnerHpGate
    {
        FWeakObjectPtr pawn{}, entity{}, luxord_entity{};
        int32_t luxord_spawn_offset{-1}, luxord_kill_offset{-1};
        bool blocked_logged{}, released_logged{};
    };

    struct RuntimeLuxordSourceEntity
    {
        // Serialization may precede native weak-serial allocation. Retain the
        // same slot/index-protected export reference used by wipe-out tracking,
        // then create a native weak reference at game-thread pawn binding.
        RuntimeBossWipeOutGroupMember source{};
        int32_t spawn_offset{-1}, kill_offset{-1};
    };

    struct RuntimeBossRepairState
    {
        std::unique_ptr<PLH::x64Detour> terranort_controller_guard{};
        uint64_t terranort_controller_trampoline{};
        std::atomic_bool terranort_controller_guard_logged{};
        std::unique_ptr<PLH::x64Detour> boss_death_partner_guard{};
        uint64_t boss_death_partner_trampoline{};
        uintptr_t boss_death_partner_caller{};
        std::atomic_bool boss_death_partner_guard_logged{};
        std::unique_ptr<PLH::x64Detour> boss_phase_guard{};
        uint64_t boss_phase_trampoline{};
        std::atomic_bool boss_phase_guard_logged{};
        std::unique_ptr<PLH::x64Detour> grim_mode_guard{};
        uint64_t grim_mode_trampoline{};
        std::atomic_bool grim_mode_guard_logged{};
        std::unique_ptr<PLH::x64Detour> grim_rising_task_guard{};
        uint64_t grim_rising_task_trampoline{};
        std::optional<FName> grim_rising_tree_name{};
        std::atomic_bool grim_rising_task_guard_logged{};
        std::unique_ptr<PLH::x64Detour> grim_tower_teleport_guard{};
        uint64_t grim_tower_teleport_trampoline{}, grim_tower_teleport_caller{};
        std::atomic_bool grim_tower_teleport_guard_logged{};
        std::unique_ptr<PLH::x64Detour> grim_flash_guard{};
        uint64_t grim_flash_trampoline{};
        std::atomic_bool grim_flash_guard_logged{};
        std::unique_ptr<PLH::x64Detour> ansem_phase_driver{};
        uint64_t ansem_tick_trampoline{};
        std::optional<FName> opening_ansem_class{};
        FWeakObjectPtr opening_ansem{};
        float opening_battle_seconds{}, opening_sample_seconds{};
        bool opening_retreat_dispatched{};
        std::unordered_map<FName, wchar_t> kg_reveal_event_names{};
        std::pair<int, int> boss_cutscene_hook_ids{};
        // Wrappers are destroyed before the ordinary HP policy overrides.
        std::unordered_map<void**, std::unique_ptr<RuntimeBossHpLimitOverride>>
            luxord_hp_overrides{};
        std::unordered_map<UObject*, RuntimeLuxordPartnerHpGate> luxord_hp_gates{};
        std::unordered_map<UObject*, RuntimeLuxordSourceEntity> luxord_source_entities{};
        std::mutex luxord_hp_mutex{};
        uint32_t luxord_hp_slot{};
    };

    struct RuntimeBossRemapState
    {
        std::unordered_map<std::wstring, std::wstring> boss_class_replacements{};
        std::unordered_set<FName> boss_replacement_class_names{};
        std::array<RuntimeBossHpLimitState, boss_hp_limit_policies.size()> boss_hp_limits{};
        bool boss_class_load_failed{};
        std::atomic_bool boss_enabled{true};
        RuntimeBossTrackingState tracking{};
        RuntimeBossRepairState repairs{};
    };

    struct RuntimeEnemyRemapState
    {
        std::unordered_map<std::wstring, std::wstring> enemy_replacements{};
        FProperty* actor_class_path_property{};
        FObjectPropertyBase* actor_class_property{};
        std::optional<FName> level_entity_class_f_name{};
        std::atomic_bool first_replacement_logged{};
        std::atomic_bool first_boss_replacement_logged{};
        std::atomic_bool first_level_entity_callback_logged{};
        std::atomic_bool property_failure_logged{};
        std::atomic_bool class_cache_failure_logged{};
        std::unique_ptr<PLH::x64Detour> level_entity_serialize_detour{};
        uint64_t level_entity_serialize_trampoline{};
        uintptr_t level_entity_serialize_target{};
        std::atomic_uint32_t replacement_observation_count{};
        std::mutex native_hook_mutex{};
        bool native_hook_installed{};
        bool native_hook_unavailable{};
        std::vector<RuntimeLoadedLevelEntityRemap> loaded_level_entity_remaps{};
        std::mutex loaded_level_entity_remaps_mutex{};
        struct PendingClassRebind
        {
            RuntimeLoadedLevelEntityRemap remap;
            std::wstring desired_class_path;
            std::chrono::steady_clock::time_point expires;
        };
        std::vector<PendingClassRebind> pending_class_rebinds;
        std::chrono::steady_clock::time_point next_class_rebind_poll{};
        std::deque<std::wstring> pending_class_prewarm_paths{};
        std::unordered_set<std::wstring> pending_class_prewarm_path_set{};
        std::unordered_map<std::wstring, std::chrono::steady_clock::time_point>
            class_prewarm_requested_at{};
        std::mutex class_prewarm_mutex{};
        std::atomic_uint32_t next_class_prewarm_uuid{};
        std::atomic_bool enemy_enabled{true};
        bool active{};
        RuntimeBossRemapState boss{};
    };

    struct RuntimeBossEntityBinding
    {
        UObject* level_entity{};
        std::wstring level_entity_path{};
        bool repaired{};
        bool new_actor{};
    };

    // Native hooks require process-lifetime state. Define it once, preserving the
    // original lifetime and mutex boundaries across the feature components.
    extern RuntimeEnemyRemapState s_runtime_enemy_remap;
    extern thread_local bool s_dispatching_runtime_boss_wipe_out;

} // namespace kh3ap::enemy_remap_detail
