#include "kh3ap/core/settings/pak_seed_settings.h"
#include "randomization.h"
#include "kh3ap/features/enemies/bosses/completion.h"
#include "kh3ap/features/enemies/bosses/encounter_repairs.h"
#include "kh3ap/features/enemies/bosses/hp_limits.h"
#include "kh3ap/features/enemies/level_entity_remap.h"
#include "kh3ap/features/enemies/remap_catalog.h"
#include "kh3ap/features/enemies/remap_runtime.h"
#include "kh3ap/features/enemies/enemy_remap_service.h"
#include "kh3ap/features/enemies/titan_arena.h"
#include "kh3ap/core/settings/goa_settings_service.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObjectGlobals.hpp>

#include <DynamicOutput/DynamicOutput.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        constexpr auto maximum_test_remap_lifetime_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::hours(6)).count();
    }

    using namespace enemy_remap_detail;

    auto ensure_runtime_level_entity_serialize_hook() -> bool
    {
        return install_level_entity_serialize_detour(
            UObjectGlobals::StaticFindObject<UObject*>(nullptr,nullptr,level_entity_default_object_path.data()));
    }

    RuntimeEnemyRemapService::RuntimeEnemyRemapService(
            RuntimeEnemyRemapConfig config)
        : config_(std::move(config))
    {
    }

    auto RuntimeEnemyRemapService::install_once() -> void
    {
        if (installed_ || !config_.enabled)
        {
            return;
        }

        const auto bridge_dir = config_.bridge_dir_resolver
                ? config_.bridge_dir_resolver()
                : std::nullopt;
        const auto& seed_settings = installed_pak_seed_settings();
        if (!seed_settings) return;
        std::optional<std::string> metadata;
        try
        {
            metadata = randomization::generate(nlohmann::json::parse(*seed_settings)).dump();
        }
        catch (const std::exception& error)
        {
            const std::string reason = error.what();
            Output::send<LogLevel::Error>(L"[KH3APProbeCpp] Native encounter generation failed: {}\n", std::wstring(reason.begin(), reason.end()));
            return;
        }
        const bool has_enemy_manifest =
                metadata->find("\"enemy_runtime_remap\"") !=
                std::string::npos;
        const bool has_boss_manifest =
                metadata->find("\"boss_runtime_remap\"") !=
                std::string::npos;
        if (!has_enemy_manifest && !has_boss_manifest)
        {
            return;
        }

        std::unordered_map<std::wstring, std::wstring> enemy_replacements{};
        bool diagnostic_enemy_override = false;
        if (has_enemy_manifest)
        {
            auto replacements = parse_runtime_replacements(*metadata);
            if (!replacements.has_value())
            {
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] Enemy runtime remap manifest rejected "
                            "bridge_dir={}\n"),
                        bridge_dir ? bridge_dir->wstring() : L"<no bridge>");
                return;
            }
            enemy_replacements = std::move(*replacements);
        }

        std::unordered_map<std::wstring, std::wstring>
                boss_class_replacements{};
        bool diagnostic_boss_override = false;
        if (has_boss_manifest)
        {
            auto replacements = parse_runtime_boss_replacements(*metadata);
            if (!replacements.has_value())
            {
                Output::send<LogLevel::Error>(
                        STR("[KH3APProbeCpp] Boss runtime remap manifest rejected "
                            "bridge_dir={}\n"),
                        bridge_dir ? bridge_dir->wstring() : L"<no bridge>");
                return;
            }
            boss_class_replacements = std::move(*replacements);
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        // Explicit, expiring local experiments. Never alter the seed manifest or
        // persist the override in player settings. A restart returns to seed behavior.
        if (const auto test = bridge_dir ? read_text_file(*bridge_dir / "enemy_test_remap.json") : std::nullopt)
        {
            const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
            const auto expires = JsonFieldReader::int64_field(*test, "expires_unix_ms").value_or(0);
            if (JsonFieldReader::string_field(*test, "schema") == L"kh3ap.enemy_test_remap.v1" &&
                expires > now && expires <= now + maximum_test_remap_lifetime_ms)
            {
                if (auto replacements = parse_runtime_replacements(*test); replacements && !replacements->empty())
                {
                    for (auto& [source, target] : *replacements)
                    {
                        enemy_replacements.insert_or_assign(source, std::move(target));
                    }
                    diagnostic_enemy_override = true;
                    Output::send<LogLevel::Warning>(
                            STR("[KH3APProbeCpp] Diagnostic enemy-test remap enabled entries={} expires={} restart_without_file_restores_seed=true\n"),
                            replacements->size(), expires);
                }
            }
        }
        if (const auto test = bridge_dir ? read_text_file(*bridge_dir / "boss_test_remap.json") : std::nullopt)
        {
            const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
            const auto expires = JsonFieldReader::int64_field(*test,"expires_unix_ms").value_or(0);
            if (JsonFieldReader::string_field(*test, "schema") == L"kh3ap.boss_test_remap.v1" &&
                expires > now && expires <= now + maximum_test_remap_lifetime_ms)
            {
                const bool allow_incompatible = JsonFieldReader::bool_field(*test,"allow_incompatible_arena_pairs").value_or(false);
                if (auto replacements = parse_runtime_boss_replacements(*test, !allow_incompatible); replacements && !replacements->empty())
                {
                    boss_class_replacements = std::move(*replacements);
                    diagnostic_boss_override = true;
                    Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Diagnostic boss-test remap enabled entries={} expires={} restart_without_file_restores_seed=true\n"),boss_class_replacements.size(),expires);
                }
            }
        }
#endif

        installed_ = true;
        s_runtime_enemy_remap.enemy_replacements =
                std::move(enemy_replacements);
        s_runtime_enemy_remap.boss.boss_class_replacements =
                std::move(boss_class_replacements);
        for (const auto& [source, target] : s_runtime_enemy_remap.boss.boss_class_replacements)
            configure_titan_arena_replacement(source,target);
        for (const auto& [_, replacement_class_path] :
             s_runtime_enemy_remap.boss.boss_class_replacements)
        {
            const size_t object_separator =
                    replacement_class_path.rfind(L'.');
            if (object_separator != std::wstring::npos &&
                object_separator + 1 < replacement_class_path.size())
            {
                s_runtime_enemy_remap.boss.boss_replacement_class_names.emplace(
                        std::wstring_view{replacement_class_path}.substr(
                                object_separator + 1),
                        FNAME_Add);
            }
        }
        s_runtime_enemy_remap.level_entity_class_f_name.emplace(
                level_entity_class_name.data(),
                FNAME_Add);
        const RuntimeGoaSettingsStatus persisted_settings =
                config_.goa_settings.status();
        s_runtime_enemy_remap.enemy_enabled.store(
                diagnostic_enemy_override || persisted_settings.enemy_randomization_enabled);
        s_runtime_enemy_remap.boss.boss_enabled.store(
                diagnostic_boss_override || persisted_settings.boss_randomization_enabled);
        s_runtime_enemy_remap.active =
                !s_runtime_enemy_remap.enemy_replacements.empty() ||
                !s_runtime_enemy_remap.boss.boss_class_replacements.empty();

        if (!s_runtime_enemy_remap.enemy_replacements.empty())
        {
            install_cactuar_replacement_reveal_guard();
        }

        if (!s_runtime_enemy_remap.boss.boss_class_replacements.empty())
        {
            install_boss_hp_limits();
            install_terranort_controller_guard();
            install_boss_death_partner_guard();
            install_runtime_boss_phase_guard();
            install_grim_guardianess_mode_guard();
            install_opening_ansem_phase_driver();
            install_kg_replacement_reveal_guard();
            s_runtime_enemy_remap.boss.tracking.boss_state_hook_ids =
                    UObjectGlobals::RegisterHook(
                            std::wstring{ai_ctor_state_event_path},
                            &prepare_runtime_boss_state,
                            &observe_runtime_boss_state,
                            nullptr);
            // Limitcut's native override does not forward every state event
            // through TresAIPawnBase. Observe the actual reflected override as
            // well, so Data replacements receive ownership/death handling.
            s_runtime_enemy_remap.boss.tracking.data_boss_state_hook_ids =
                    UObjectGlobals::RegisterHook(
                            std::wstring{data_boss_ctor_state_event_path},
                            &prepare_runtime_boss_state,
                            &observe_runtime_boss_state,
                            nullptr);
            s_runtime_enemy_remap.boss.tracking.wipe_out_hook_ids =
                    UObjectGlobals::RegisterHook(
                            std::wstring{raise_wipe_out_event_path},
                            &observe_or_suppress_runtime_boss_wipe_out,
                            [](UnrealScriptFunctionCallableContext&, void*) {},
                            nullptr);
            s_runtime_enemy_remap.boss.repairs.boss_cutscene_hook_ids =
                    UObjectGlobals::RegisterHook(
                            STR("/Script/Engine.MatineeActor:Play"),
                            &prepare_marshmallow_replacement_cutscene,
                            &skip_marshmallow_replacement_cutscene,
                            nullptr);
        }
        if (!s_runtime_enemy_remap.active)
        {
            return;
        }
        UObject* const level_entity_cdo =
                UObjectGlobals::StaticFindObject<UObject*>(
                        nullptr,
                        nullptr,
                        level_entity_default_object_path.data());
        const bool native_hook_ready =
                install_level_entity_serialize_detour(level_entity_cdo);

        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] Level entity runtime remap installed "
                    "enemy_entries={} boss_entries={} "
                    "enemy_enabled={} boss_enabled={} settings_loaded={} "
                    "hook=TresLevelEntity::SerializePost "
                    "native_hook={} "
                    "boss_state_hook=({}, {}) "
                    "data_boss_state_hook=({}, {}) "
                    "boss_state_filter=appear_and_death_only "
                    "wipe_out_hook=({}, {}) "
                    "install_fallback=exact_cdo_retry_on_update "
                    "mode=event_driven polling=false "
                    "class_prewarm=disabled_on_demand_toggle_load "
                    "prewarm_requests_per_update=0 "
                    "object_scans=false\n"),
            static_cast<uint64_t>(
                    s_runtime_enemy_remap.enemy_replacements.size()),
            static_cast<uint64_t>(
                    s_runtime_enemy_remap.boss.boss_class_replacements.size()),
            s_runtime_enemy_remap.enemy_enabled.load(),
            s_runtime_enemy_remap.boss.boss_enabled.load(),
            persisted_settings.loaded_from_disk,
            native_hook_ready ? STR("installed") : STR("pending"),
            s_runtime_enemy_remap.boss.tracking.boss_state_hook_ids.first,
            s_runtime_enemy_remap.boss.tracking.boss_state_hook_ids.second,
            s_runtime_enemy_remap.boss.tracking.data_boss_state_hook_ids.first,
            s_runtime_enemy_remap.boss.tracking.data_boss_state_hook_ids.second,
            s_runtime_enemy_remap.boss.tracking.wipe_out_hook_ids.first,
            s_runtime_enemy_remap.boss.tracking.wipe_out_hook_ids.second);
    }

    auto RuntimeEnemyRemapService::status() const
            -> RuntimeEnemyRemapStatus
    {
        std::lock_guard lock{s_runtime_enemy_remap.loaded_level_entity_remaps_mutex};
        const bool enemy_available =
                !s_runtime_enemy_remap.enemy_replacements.empty();
        const bool boss_available =
                !s_runtime_enemy_remap.boss.boss_class_replacements.empty();
        return RuntimeEnemyRemapStatus{
                .enemy_available = enemy_available,
                .boss_available = boss_available,
                .enemy_enabled = enemy_available &&
                                 s_runtime_enemy_remap.enemy_enabled.load(),
                .boss_enabled = boss_available &&
                                 s_runtime_enemy_remap.boss.boss_enabled.load(),
                .boss_pending_class_loads = static_cast<uint32_t>(std::count_if(
                        s_runtime_enemy_remap.pending_class_rebinds.begin(),
                        s_runtime_enemy_remap.pending_class_rebinds.end(),
                        [](const auto& entry) { return entry.remap.kind == RuntimeLevelEntityRemapKind::boss; })),
                .boss_class_load_failed = s_runtime_enemy_remap.boss.boss_class_load_failed,
        };
    }

    auto RuntimeEnemyRemapService::request_loaded_toggle_class_prewarm()
            -> uint32_t
    {
        if constexpr (!eager_toggle_class_prewarm_enabled)
        {
            return 0;
        }
        if (!s_runtime_enemy_remap.active)
        {
            return 0;
        }
        const uint32_t queued =
                queue_loaded_toggle_class_prewarm_paths();
        return queued;
    }

    auto RuntimeEnemyRemapService::process_pending_class_prewarm() -> void
    {
        process_titan_arena_replacement(s_runtime_enemy_remap.boss.boss_enabled.load());
        process_pending_marshmallow_cutscene();
        process_pending_class_rebinds();
        if (!s_runtime_enemy_remap.active)
        {
            return;
        }
        if (!s_runtime_enemy_remap.native_hook_installed &&
            !s_runtime_enemy_remap.native_hook_unavailable)
        {
            UObject* const level_entity_cdo =
                    UObjectGlobals::StaticFindObject<UObject*>(
                            nullptr,
                            nullptr,
                            level_entity_default_object_path.data());
            install_level_entity_serialize_detour(level_entity_cdo);
        }
        if constexpr (eager_toggle_class_prewarm_enabled)
        {
            for (uint32_t request = 0;
                 request < class_prewarm_requests_per_update;
                 ++request)
            {
                if (!process_one_pending_class_prewarm())
                {
                    break;
                }
            }
        }
    }

    auto RuntimeEnemyRemapService::set_enemy_enabled(bool enabled) -> bool
    {
        if (s_runtime_enemy_remap.enemy_replacements.empty())
        {
            return false;
        }
        const bool previous =
                s_runtime_enemy_remap.enemy_enabled.exchange(enabled);
        const RuntimeLevelEntityReconcileResult reconcile =
                reconcile_loaded_level_entity_remaps(
                        RuntimeLevelEntityRemapKind::enemy,
                        enabled);
        const bool persisted =
                config_.goa_settings.set_enemy_randomization_enabled(enabled);
        Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] GoA setting changed kind=enemy "
                    "previous={} enabled={} persisted={} "
                    "tracked={} live={} changed={} "
                    "class_cache_rebound={} "
                    "class_cache_rebind_failed={} "
                    "blocking_class_load_attempts={} "
                    "blocking_class_load_us={} expired={} "
                    "apply=next_same_map_encounter_spawn "
                    "current_actors_unchanged=true\n"),
                previous,
                enabled,
                persisted,
                reconcile.tracked,
                reconcile.live,
                reconcile.changed,
                reconcile.class_cache_rebound,
                reconcile.class_cache_rebind_failed,
                reconcile.blocking_class_load_attempts,
                reconcile.blocking_class_load_us,
                reconcile.expired);
        return true;
    }

    auto RuntimeEnemyRemapService::set_boss_enabled(bool enabled) -> bool
    {
        if (s_runtime_enemy_remap.boss.boss_class_replacements.empty())
        {
            return false;
        }
        const bool previous =
                s_runtime_enemy_remap.boss.boss_enabled.exchange(enabled);
        const RuntimeLevelEntityReconcileResult reconcile =
                reconcile_loaded_level_entity_remaps(
                        RuntimeLevelEntityRemapKind::boss,
                        enabled);
        const bool persisted =
                config_.goa_settings.set_boss_randomization_enabled(enabled);
        Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] GoA setting changed kind=boss "
                    "previous={} enabled={} persisted={} "
                    "tracked={} live={} changed={} "
                    "class_cache_rebound={} "
                    "class_cache_rebind_failed={} "
                    "blocking_class_load_attempts={} "
                    "blocking_class_load_us={} expired={} "
                    "apply=next_same_map_encounter_spawn "
                    "current_actors_unchanged=true\n"),
                previous,
                enabled,
                persisted,
                reconcile.tracked,
                reconcile.live,
                reconcile.changed,
                reconcile.class_cache_rebound,
                reconcile.class_cache_rebind_failed,
                reconcile.blocking_class_load_attempts,
                reconcile.blocking_class_load_us,
                reconcile.expired);
        return true;
    }
}
