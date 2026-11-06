#include "kh3ap/app/config.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/grants/battle_level/battle_level_sync_service.h"
#include "kh3ap/features/grants/battle_level/battle_level_functions.h"

#ifdef S
#pragma push_macro("S")
#undef S
#define KH3AP_RESTORE_S_MACRO_AFTER_POLYHOOK
#endif
#include <polyhook2/Detour/x64Detour.hpp>
#ifdef KH3AP_RESTORE_S_MACRO_AFTER_POLYHOOK
#pragma pop_macro("S")
#undef KH3AP_RESTORE_S_MACRO_AFTER_POLYHOOK
#endif

#include <UnrealDef.hpp>

#include "kh3ap/features/grants/ap_ignored_map.h"
#include "kh3ap/features/grants/battle_level/battle_level_progress.h"
#include "kh3ap/core/settings/goa_settings_service.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/player/state/sora_level_monitor_state.h"
#include "kh3ap/features/player/state/sora_state_catalog.h"
#include "kh3ap/app/state.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <utility>
#include <vector>

namespace kh3ap
{
    using ::kh3ap::settings::s_enable_bp_tres_set_battle_level;
    using ::kh3ap::settings::s_enable_runtime_sora_level_profiling;

    namespace
    {
        constexpr auto battle_level_initial_sync_delay = std::chrono::seconds(3);
        constexpr auto battle_level_resync_delay = std::chrono::seconds(1);
        constexpr auto max_native_battle_level = 255;
    }

    using namespace RC;
    using namespace RC::Unreal;

    class RuntimeBattleLevelProfileScope
    {
    public:
        RuntimeBattleLevelProfileScope(
                const std::function<void(std::wstring_view, uint64_t)>& recorder,
                std::wstring_view scope_name)
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            : recorder_(recorder),
              scope_name_(scope_name),
              started_at_(std::chrono::steady_clock::now())
#endif
        {
        }

        RuntimeBattleLevelProfileScope(const RuntimeBattleLevelProfileScope&) = delete;
        auto operator=(const RuntimeBattleLevelProfileScope&)
                -> RuntimeBattleLevelProfileScope& = delete;

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        ~RuntimeBattleLevelProfileScope()
        {
            if (!recorder_)
            {
                return;
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - started_at_);
            recorder_(scope_name_, static_cast<uint64_t>(elapsed.count()));
        }

    private:
        const std::function<void(std::wstring_view, uint64_t)>& recorder_;
        std::wstring_view scope_name_{};
        std::chrono::steady_clock::time_point started_at_{};
#endif
    };

    static auto battle_level_profile_now() -> std::chrono::steady_clock::time_point
    {
        if constexpr (s_enable_runtime_sora_level_profiling)
            return std::chrono::steady_clock::now();
        else
            return {};
    }

    struct RuntimeBattleLevelSyncService::Impl
    {
        explicit Impl(RuntimeBattleLevelSyncConfig value)
            : config_(std::move(value))
        {
        }

        static auto lower_ascii_copy(std::wstring_view value) -> std::wstring
        {
            std::wstring lowered{value};
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch) {
                return ch >= L'A' && ch <= L'Z'
                        ? static_cast<wchar_t>(ch + (L'a' - L'A'))
                        : ch;
            });
            return lowered;
        }

        static auto plausible_runtime_battle_level(uint64_t value) -> bool
        {
            return value >= 1 && value <= max_native_battle_level;
        }

        auto cache_pawn_from_controller(
                UObject* controller,
                std::wstring_view source,
                std::wstring_view controller_name,
                bool force) -> bool
        {
            return config_.cache_pawn_from_controller &&
                   config_.cache_pawn_from_controller(
                           controller,
                           source,
                           controller_name,
                           force);
        }

        auto trusted_current_playable_sora_pawn() const -> UObject*
        {
            auto& pawn_state = config_.runtime_state.grant_state().pawn_state();
            UObject* const cached_pawn = pawn_state.object();
            if (!cached_pawn || !pawn_state.candidate_trusted())
            {
                return nullptr;
            }
            return cached_pawn;
        }

        auto reset_for_map_lifecycle() -> void
        {
            m_pending_battle_level.reset();
            m_pending_battle_level_sync_at = {};
            m_pending_battle_level_source.clear();
            m_pending_battle_level_sora_pawn = nullptr;
            m_pending_battle_level_sora_pawn_already_classified = false;
            m_pending_battle_level_bypass_cinematic_gate = false;
            m_battle_level_progress_signature.clear();
            m_native_battle_level.reset();
            m_native_battle_level_map_identity.clear();
        }

        auto sync_after_controller_reacquire(std::wstring_view source) -> bool
        {
            const bool fixed_map_level = current_fixed_battle_level().has_value();
            if (!fixed_map_level &&
                (!config_.runtime_state.grant_state().special_battle_mode_active() ||
                 !config_.sora_level_monitor.last_observed_runtime_sora_level.has_value()))
            {
                return false;
            }

            const auto& pawn_state = config_.runtime_state.grant_state().pawn_state();
            UObject* const sora_pawn = pawn_state.object();
            if (!sora_pawn || !pawn_state.candidate_trusted())
            {
                return false;
            }

            const auto target_level = resolve_configured_battle_level(
                    config_.sora_level_monitor.last_observed_runtime_sora_level.value_or(0),
                    source);
            if (!target_level.has_value())
            {
                return false;
            }
            return try_sync_battle_level_to_sora(
                    *target_level,
                    source,
                    sora_pawn,
                    true,
                    true,
                    true);
        }

        [[nodiscard]] auto status() const -> RuntimeBattleLevelSyncStatus
        {
            const RuntimeGoaSettingsStatus settings =
                    config_.goa_settings.status();
            return RuntimeBattleLevelSyncStatus{
                    .enabled = settings.battle_level_sync_enabled,
                    .offset = settings.battle_level_offset,
            };
        }

        auto set_enabled(bool enabled) -> bool
        {
            const RuntimeBattleLevelSyncStatus previous = status();
            bool applied = false;
            if (!enabled && previous.enabled && m_native_battle_level.has_value())
            {
                UObject* const sora_pawn = trusted_current_playable_sora_pawn();
                applied = sora_pawn && try_sync_battle_level_to_sora(
                        *m_native_battle_level,
                        STR("goa_options_battle_level_sync_restore_native"),
                        sora_pawn,
                        true,
                        true,
                        true);
            }
            const bool persisted =
                    config_.goa_settings.set_battle_level_sync_enabled(enabled);
            m_battle_level_progress_signature.clear();
            config_.sora_level_monitor.last_battle_level_sync_signature.clear();

            if (!enabled)
            {
                reset_for_map_lifecycle();
                // Fixed map levels are independent of this user setting.
                applied = try_sync_battle_level_to_observed_sora_level(
                        STR("goa_options_fixed_map_battle_level"), true) || applied;
            }
            else
            {
                applied = try_sync_battle_level_to_observed_sora_level(
                        STR("goa_options_battle_level_sync"),
                        true);
            }
            Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] GoA setting changed "
                        "kind=battle_level_sync previous={} enabled={} "
                        "offset={} persisted={} applied_now={} "
                        "native_restore_requested={}\n"),
                    previous.enabled,
                    enabled,
                    status().offset,
                    persisted,
                    applied,
                    !enabled && previous.enabled);
            return true;
        }

        auto set_offset(int32_t offset) -> bool
        {
            const RuntimeBattleLevelSyncStatus previous = status();
            const int32_t clamped = clamp_battle_level_offset(offset);
            const bool persisted =
                    config_.goa_settings.set_battle_level_offset(clamped);
            m_battle_level_progress_signature.clear();
            config_.sora_level_monitor.last_battle_level_sync_signature.clear();

            const bool applied = try_sync_battle_level_to_observed_sora_level(
                    STR("goa_options_battle_level_offset"),
                    true);
            Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] GoA setting changed "
                        "kind=battle_level_offset previous={} offset={} "
                        "sync_enabled={} persisted={} applied_now={}\n"),
                    previous.offset,
                    clamped,
                    status().enabled,
                    persisted,
                    applied);
            return true;
        }

        RuntimeBattleLevelSyncConfig config_;
        RuntimeBattleLevelFunctions m_battle_level_functions{};
        std::optional<uint64_t> m_pending_battle_level{};
        std::chrono::steady_clock::time_point m_pending_battle_level_sync_at{};
        std::wstring m_pending_battle_level_source{};
        UObject* m_pending_battle_level_sora_pawn{nullptr};
        bool m_pending_battle_level_sora_pawn_already_classified{false};
        bool m_pending_battle_level_bypass_cinematic_gate{false};
        std::wstring m_battle_level_progress_signature{};
        bool m_battle_level_sync_cinematic_gate_logged{false};
        std::optional<uint64_t> m_native_battle_level{};
        std::wstring m_native_battle_level_map_identity{};
    static auto is_battle_level_sync_excluded_context(std::wstring_view value) -> bool
    {
        const std::wstring lowered = lower_ascii_copy(value);
        return kh3ap::is_runtime_ap_ignored_map_context(value) ||
               lowered.find(STR("stationofawakening")) != std::wstring::npos ||
               lowered.find(STR("station_of_awakening")) != std::wstring::npos ||
               lowered.find(STR("station of awakening")) != std::wstring::npos ||
               lowered.find(STR("goa_01")) != std::wstring::npos ||
               lowered.find(STR("goa_02")) != std::wstring::npos;
    }

    auto current_runtime_context_excludes_battle_level_sync() const -> bool
    {
        const auto snapshot = config_.runtime_state.context_subsystem_state().context_state().current_snapshot();
        return snapshot.has_value() &&
                (is_battle_level_sync_excluded_context(snapshot->world) ||
                 is_battle_level_sync_excluded_context(snapshot->outer) ||
                 is_battle_level_sync_excluded_context(snapshot->object));
    }

    auto current_fixed_battle_level(UObject* pawn_override = nullptr) const -> std::optional<uint64_t>
    {
        UObject* const pawn = pawn_override ? pawn_override : trusted_current_playable_sora_pawn();
        if (!is_uobject_valid_fast(pawn) || !is_uobject_a_fast(pawn, AActor::StaticClass()))
        {
            return std::nullopt;
        }
        // ew_04 gameflow can also stream into ew_01. Use Sora's owning world
        // rather than the last script snapshot so only the requested map is fixed.
        UObject* const world = reinterpret_cast<UObject*>(static_cast<AActor*>(pawn)->GetWorld());
        if (!is_uobject_valid_fast(world))
        {
            return std::nullopt;
        }
        return fixed_battle_level_for_map(world->GetPathName());
    }

    auto resolve_configured_battle_level(
            uint64_t sora_level,
            std::wstring_view source,
            UObject* pawn_override = nullptr) -> std::optional<uint64_t>
    {
        const auto unavailable = [this, source](std::wstring_view reason) -> std::optional<uint64_t> {
            const std::wstring signature = std::format(STR("unavailable|{}"), reason);
            if (signature != m_battle_level_progress_signature)
            {
                m_battle_level_progress_signature = signature;
                Output::send<LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] BattleLevelProgress unavailable source={} reason={}\n"),
                        source,
                        reason);
            }
            return std::nullopt;
        };
        const RuntimeGoaSettingsStatus settings =
                config_.goa_settings.status();
        const auto fixed_level = current_fixed_battle_level(pawn_override);
        if (!fixed_level.has_value() && !settings.battle_level_sync_enabled)
        {
            return unavailable(STR("disabled_by_goa_options"));
        }
        const auto target_level = fixed_level.has_value()
                ? fixed_level
                : kh3ap::resolve_battle_level_target(sora_level, settings.battle_level_offset);
        if (!target_level.has_value())
        {
            return unavailable(STR("invalid_sora_level"));
        }
        const std::wstring signature = std::format(
                STR("{}|{}|{}|{}"),
                sora_level,
                settings.battle_level_sync_enabled,
                settings.battle_level_offset,
                *target_level);
        if (signature != m_battle_level_progress_signature)
        {
            m_battle_level_progress_signature = signature;
            Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] BattleLevelProgress source={} sora_level={} sync_enabled={} offset={} target_level={} setting_source={}\n"),
                    source,
                    sora_level,
                    settings.battle_level_sync_enabled,
                    settings.battle_level_offset,
                    *target_level,
                    fixed_level.has_value() ? STR("fixed_map_battle_level") : STR("goa_runtime_settings"));
        }
        return target_level;
    }

    auto defer_battle_level_sync_during_cinematic(
            uint64_t level,
            std::wstring_view source,
            std::chrono::steady_clock::time_point now) -> bool
    {
        const bool cinematic_or_map_entry = kh3ap::runtime_sora_state_matches_kind(
                kh3ap::RuntimeSoraStateKind::Cinematic,
                config_.sora_level_monitor.cached_runtime_sora_current_state_id,
                config_.sora_level_monitor.cached_runtime_sora_state_class);
        if (!cinematic_or_map_entry)
        {
            m_battle_level_sync_cinematic_gate_logged = false;
            return false;
        }

        constexpr auto retry_delay = std::chrono::milliseconds(250);
        m_pending_battle_level = level;
        m_pending_battle_level_sync_at = now + retry_delay;
        m_pending_battle_level_source = std::wstring{source};
        if (!m_battle_level_sync_cinematic_gate_logged)
        {
            m_battle_level_sync_cinematic_gate_logged = true;
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] BattleLevelSync deferred source={} level={} reason=cinematic_or_map_entry state_class={} state_id={} retry_ms={}\n"),
                    source,
                    level,
                    config_.sora_level_monitor.cached_runtime_sora_state_class.empty()
                            ? std::wstring{STR("<blank>")}
                            : config_.sora_level_monitor.cached_runtime_sora_state_class,
                    config_.sora_level_monitor.cached_runtime_sora_current_state_id.has_value()
                            ? std::to_wstring(*config_.sora_level_monitor.cached_runtime_sora_current_state_id)
                            : std::wstring{STR("<none>")},
                    static_cast<uint64_t>(retry_delay.count()));
        }
        return true;
    }

    auto try_read_current_battle_level(
            UObject* world_context_object,
            std::wstring_view source) -> std::optional<uint64_t>
    {
        const RuntimeHotPathProfileScope native_read_profile{STR("battle_level_sync:native_read")};
        RuntimeBattleLevelFunction resolved{};
        {
            const RuntimeHotPathProfileScope lookup_profile{STR("battle_level_sync:get_lookup")};
            resolved = m_battle_level_functions.resolve(true);
        }
        auto* const self_object = resolved.library;
        auto* const function_object = resolved.function;
        if (!self_object || !kh3ap::is_uobject_valid_fast(self_object) ||
            !function_object)
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] BattleLevelSync native snapshot failed "
                        "source={} reason=missing_function_or_default_object "
                        "self={} function={}\n"),
                    source,
                    format_object_debug_name(self_object),
                    function_object
                            ? function_object->GetFullName()
                            : std::wstring{STR("<null>")});
            return std::nullopt;
        }

        std::vector<std::byte> params(
                static_cast<size_t>(function_object->GetParmsSize()));
        std::fill(params.begin(), params.end(), std::byte{0});
        FProperty* world_property = find_function_param_by_name(
                function_object,
                STR("WorldContextObject"));
        if (!world_property)
        {
            world_property = find_function_param_by_name(
                    function_object,
                    STR("__WorldContext"));
        }
        if (world_property && !write_object_function_param(
                                      function_object,
                                      params.data(),
                                      world_property->GetName(),
                                      world_context_object))
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] BattleLevelSync native snapshot failed "
                        "source={} reason=world_param_write_failed param={}\n"),
                    source,
                    world_property->GetName());
            return std::nullopt;
        }

        try
        {
            const RuntimeHotPathProfileScope call_profile{STR("battle_level_sync:get_process_event")};
            self_object->ProcessEvent(function_object, params.data());
        }
        catch (...)
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] BattleLevelSync native snapshot failed "
                        "source={} reason=process_event_exception\n"),
                    source);
            return std::nullopt;
        }

        const auto value = read_function_int_param(
                function_object,
                params.data(),
                STR("ReturnValue"));
        if (!value.has_value() || *value < 1 || *value > max_native_battle_level)
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] BattleLevelSync native snapshot failed "
                        "source={} reason=invalid_return value={}\n"),
                    source,
                    value.value_or(-1));
            return std::nullopt;
        }
        return static_cast<uint64_t>(*value);
    }

    auto try_sync_battle_level_to_sora(
            uint64_t level,
            std::wstring_view source,
            UObject* sora_pawn_override = nullptr,
            bool force = false,
            bool sora_pawn_already_classified = false,
            bool bypass_cinematic_gate = false) -> bool
    {
        const auto profile_started_at = battle_level_profile_now();
        if (!plausible_runtime_battle_level(level))
        {
            return false;
        }

        const auto snapshot = config_.runtime_state.context_subsystem_state().context_state().current_snapshot();
        if (snapshot.has_value() &&
            (is_battle_level_sync_excluded_context(snapshot->world) ||
             is_battle_level_sync_excluded_context(snapshot->outer) ||
             is_battle_level_sync_excluded_context(snapshot->object)))
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] BattleLevelSync skipped source={} level={} reason=excluded_map_early context={} {}\n"),
                    source,
                    level,
                    truncate_for_log(snapshot->world, 120),
                    truncate_for_log(snapshot->outer, 120));
            return false;
        }
        const auto context_check_completed_at = battle_level_profile_now();

        const RuntimeGoaSettingsStatus battle_level_settings =
                config_.goa_settings.status();
        const auto fixed_level = current_fixed_battle_level(sora_pawn_override);
        if (fixed_level.has_value())
        {
            // Also covers delayed writes and native-level restoration when
            // sync is disabled: neither may raise this arena above level 1.
            level = *fixed_level;
        }
        if (!fixed_level.has_value() && !battle_level_settings.battle_level_sync_enabled)
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] BattleLevelSync skipped source={} level={} offset={} reason=disabled_by_goa_options setting_source=goa_runtime_settings\n"),
                    source,
                    level,
                    battle_level_settings.battle_level_offset);
            return false;
        }
        const auto setting_check_completed_at = battle_level_profile_now();

        UObject* const sora_pawn = sora_pawn_override
                ? sora_pawn_override
                : config_.runtime_state.grant_state().pawn_state().object();
        if (!sora_pawn ||
            (!sora_pawn_already_classified &&
             (!kh3ap::is_uobject_valid_fast(sora_pawn) ||
              !kh3ap::RuntimeObjectResolver::is_sora_ability_pawn_candidate(sora_pawn))))
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] BattleLevelSync skipped source={} level={} reason=missing_validated_sora_pawn\n"),
                    source,
                    level);
            return false;
        }
        const auto pawn_check_completed_at = battle_level_profile_now();

        const std::wstring pawn_name = sora_pawn_already_classified
                ? std::format(STR("SoraLevelOwner @0x{:x}"), reinterpret_cast<uintptr_t>(sora_pawn))
                : format_object_debug_name(sora_pawn);
        const std::wstring context_text = snapshot.has_value()
                ? std::format(STR("{} {} {} {}"), snapshot->world, snapshot->outer, snapshot->object, pawn_name)
                : pawn_name;
        if (is_battle_level_sync_excluded_context(context_text))
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] BattleLevelSync skipped source={} level={} reason=excluded_map context={}\n"),
                    source,
                    level,
                    truncate_for_log(context_text, 240));
            return false;
        }

        const std::wstring map_identity = snapshot.has_value()
                ? snapshot->world + STR("|") + snapshot->outer
                : pawn_name;
        const std::wstring signature = std::format(
                STR("0x{:x}|{}|{}"),
                reinterpret_cast<uintptr_t>(sora_pawn),
                map_identity,
                level);
        if (!force && config_.sora_level_monitor.last_battle_level_sync_signature == signature)
        {
            return true;
        }
        if (!bypass_cinematic_gate && defer_battle_level_sync_during_cinematic(
                    level,
                    source,
                    std::chrono::steady_clock::now()))
        {
            return false;
        }
        const auto validation_completed_at = battle_level_profile_now();

        const auto resolved = m_battle_level_functions.resolve(false);
        auto* const self_object = resolved.library;
        auto* const function_object = resolved.function;
        if (!self_object || !kh3ap::is_uobject_valid_fast(self_object) || !function_object)
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] BattleLevelSync failed source={} level={} reason=missing_function_or_default_object self={} function={}\n"),
                    source,
                    level,
                    format_object_debug_name(self_object),
                    function_object ? function_object->GetFullName() : std::wstring{STR("<null>")});
            return false;
        }
        const auto lookup_completed_at = battle_level_profile_now();

        if (!m_native_battle_level.has_value() ||
            m_native_battle_level_map_identity != map_identity)
        {
            if (const auto native_level = try_read_current_battle_level(
                        sora_pawn,
                        source))
            {
                m_native_battle_level = *native_level;
                m_native_battle_level_map_identity = map_identity;
                Output::send<LogLevel::Normal>(
                        STR("[KH3APProbeCpp] BattleLevelSync native snapshot "
                            "captured source={} level={} map={}\n"),
                        source,
                        *native_level,
                        truncate_for_log(map_identity, 180));
            }
        }
        const auto native_snapshot_completed_at = battle_level_profile_now();

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        std::fill(params.begin(), params.end(), std::byte{0});
        const bool wrote_world = write_object_function_param(
                function_object,
                params.data(),
                STR("WorldContextObject"),
                sora_pawn);
        const bool wrote_level = write_integral_function_param(
                find_function_param_by_name(function_object, STR("InBattleLevel")),
                params.data(),
                level);
        if (!wrote_world || !wrote_level)
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] BattleLevelSync failed source={} level={} reason=param_write_failed wrote_world={} wrote_level={}\n"),
                    source,
                    level,
                    wrote_world,
                    wrote_level);
            return false;
        }
        const auto params_completed_at = battle_level_profile_now();

        if constexpr (!s_enable_bp_tres_set_battle_level)
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] BattleLevelSync test-disabled source={} level={} pawn={}\n"),
                    source,
                    level,
                    pawn_name);
            return false;
        }

        try
        {
            const RuntimeHotPathProfileScope call_profile{STR("battle_level_sync:set_process_event")};
            self_object->ProcessEvent(function_object, params.data());
        }
        catch (...)
        {
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] BattleLevelSync failed source={} level={} reason=process_event_exception\n"),
                    source,
                    level);
            return false;
        }
        const auto process_event_completed_at = battle_level_profile_now();

        if (fixed_level.has_value())
        {
            const auto observed_level = try_read_current_battle_level(sora_pawn, source);
            if (observed_level != fixed_level)
            {
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] FixedMapBattleLevel readback failed "
                            "source={} expected={} observed={}\n"),
                        source,
                        *fixed_level,
                        observed_level.value_or(0));
                return false;
            }
            Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] FixedMapBattleLevel verified source={} level={}\n"),
                    source,
                    *observed_level);
        }

        config_.sora_level_monitor.last_battle_level_sync_signature = signature;
        if constexpr (s_enable_runtime_sora_level_profiling)
        {
            const auto readback_started_at = battle_level_profile_now();
            const auto observed_level = try_read_current_battle_level(sora_pawn, source);
            const auto readback_completed_at = battle_level_profile_now();
            const auto elapsed_us = [](auto begin, auto end) -> uint64_t {
                return static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count());
            };
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] SoraLevelProfile stage=battle_level_sync source={} level={} total_us={} validation_us={} context_check_us={} setting_check_us={} pawn_check_us={} context_gate_us={} lookup_us={} native_snapshot_us={} params_us={} process_event_us={} observed_level={} readback_us={}\n"),
                    source,
                    level,
                    elapsed_us(profile_started_at, process_event_completed_at),
                    elapsed_us(profile_started_at, validation_completed_at),
                    elapsed_us(profile_started_at, context_check_completed_at),
                    elapsed_us(context_check_completed_at, setting_check_completed_at),
                    elapsed_us(setting_check_completed_at, pawn_check_completed_at),
                    elapsed_us(pawn_check_completed_at, validation_completed_at),
                    elapsed_us(validation_completed_at, lookup_completed_at),
                    elapsed_us(lookup_completed_at, native_snapshot_completed_at),
                    elapsed_us(native_snapshot_completed_at, params_completed_at),
                    elapsed_us(params_completed_at, process_event_completed_at),
                    observed_level.value_or(0),
                    elapsed_us(readback_started_at, readback_completed_at));
        }
        Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] BattleLevelSync applied source={} level={} pawn={}\n"),
                source,
                level,
                pawn_name);
        return true;
    }

    auto try_sync_battle_level_to_observed_sora_level(
            std::wstring_view source,
            bool force = false) -> bool
    {
        if (current_runtime_context_excludes_battle_level_sync())
        {
            return false;
        }
        if (!current_fixed_battle_level().has_value() &&
            !config_.sora_level_monitor.last_observed_runtime_sora_level.has_value())
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] BattleLevelSync skipped source={} reason=sora_level_not_observed\n"),
                    source);
            return false;
        }
        UObject* const trusted_sora_pawn = trusted_current_playable_sora_pawn();
        const auto target_level = resolve_configured_battle_level(
                config_.sora_level_monitor.last_observed_runtime_sora_level.value_or(0),
                source);
        if (!target_level.has_value())
        {
            return false;
        }
        return try_sync_battle_level_to_sora(
                *target_level,
                source,
                trusted_sora_pawn,
                force,
                trusted_sora_pawn != nullptr);
    }

    auto schedule_initial_battle_level_sync(uint64_t level, UObject* validated_sora_pawn) -> void
    {
        if (current_runtime_context_excludes_battle_level_sync())
        {
            m_pending_battle_level.reset();
            m_pending_battle_level_sync_at = {};
            m_pending_battle_level_source.clear();
            m_pending_battle_level_sora_pawn = nullptr;
            m_pending_battle_level_sora_pawn_already_classified = false;
            m_pending_battle_level_bypass_cinematic_gate = false;
            m_battle_level_sync_cinematic_gate_logged = false;
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] BattleLevelSync scheduling skipped source=sora_level_initialized level={} reason=excluded_map_early\n"),
                    level);
            return;
        }

        const auto target_level = resolve_configured_battle_level(
                level,
                STR("sora_level_initialized_delayed"),
                validated_sora_pawn);
        if (!target_level.has_value())
        {
            m_pending_battle_level.reset();
            m_pending_battle_level_sync_at = {};
            m_pending_battle_level_source.clear();
            m_pending_battle_level_sora_pawn = nullptr;
            m_pending_battle_level_sora_pawn_already_classified = false;
            m_pending_battle_level_bypass_cinematic_gate = false;
            return;
        }
        if (current_fixed_battle_level(validated_sora_pawn).has_value())
        {
            // Apply at pawn initialization as well as the delayed map-entry
            // retry so the opening encounter starts with its fixed level.
            try_sync_battle_level_to_sora(
                    *target_level, STR("fixed_map_battle_level_initialized"),
                    validated_sora_pawn, true, validated_sora_pawn != nullptr, true);
        }
        {
            // Prepare while processing the initial level event, before the
            // delayed map-entry write. No package loads or global function scans.
            const RuntimeHotPathProfileScope prepare_profile{STR("battle_level_sync:prepare")};
            const auto prepare_started_at = battle_level_profile_now();
            m_battle_level_functions.prepare();
            if constexpr (s_enable_runtime_sora_level_profiling)
            {
                const auto prepare_us = std::chrono::duration_cast<std::chrono::microseconds>(
                        battle_level_profile_now() - prepare_started_at).count();
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] SoraLevelProfile stage=battle_level_prepare total_us={}\n"),
                        prepare_us);
            }
        }
        m_pending_battle_level = *target_level;
        m_pending_battle_level_sync_at =
                std::chrono::steady_clock::now() + battle_level_initial_sync_delay;
        m_pending_battle_level_source = STR("sora_level_initialized_delayed");
        m_pending_battle_level_sora_pawn = validated_sora_pawn;
        m_pending_battle_level_sora_pawn_already_classified = validated_sora_pawn != nullptr;
        m_pending_battle_level_bypass_cinematic_gate = false;
        m_battle_level_sync_cinematic_gate_logged = false;
        Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] BattleLevelSync scheduled source=sora_level_initialized level={} delay_ms=3000\n"),
                *m_pending_battle_level);
    }

    auto schedule_he06_special_battle_level_sync(
            std::wstring_view frame_name,
            std::chrono::steady_clock::time_point now) -> bool
    {
        if (frame_name.find(STR("/Game/Levels/he/he_06/")) == std::wstring_view::npos ||
            !config_.sora_level_monitor.last_observed_runtime_sora_level.has_value())
        {
            return false;
        }

        auto& pawn_state = config_.runtime_state.grant_state().pawn_state();
        if (!pawn_state.object())
        {
            UObject* const controller = pawn_state.controller_object();
            if (controller && !pawn_state.controller_name().empty())
            {
                cache_pawn_from_controller(
                        controller,
                        STR("HE05ToHE06BattleLevelSync:cached_controller"),
                        pawn_state.controller_name(),
                        true);
            }
        }

        const auto target_level = resolve_configured_battle_level(
                *config_.sora_level_monitor.last_observed_runtime_sora_level,
                STR("he05_to_he06_special_battle_delayed"));
        if (!target_level.has_value())
        {
            return false;
        }
        m_pending_battle_level = *target_level;
        m_pending_battle_level_sync_at = now + battle_level_resync_delay;
        m_pending_battle_level_source = STR("he05_to_he06_special_battle_delayed");
        m_pending_battle_level_sora_pawn = pawn_state.object();
        m_pending_battle_level_sora_pawn_already_classified =
                m_pending_battle_level_sora_pawn && pawn_state.candidate_trusted();
        m_pending_battle_level_bypass_cinematic_gate = true;
        m_battle_level_sync_cinematic_gate_logged = false;
        Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] BattleLevelSync scheduled source=he05_to_he06_special_battle level={} delay_ms=1000 pawn={}\n"),
                *m_pending_battle_level,
                pawn_state.name().empty() ? std::wstring{STR("<none>")} : pawn_state.name());
        return true;
    }

    auto process_pending_initial_battle_level_sync(
            std::chrono::steady_clock::time_point now) -> void
    {
        if (!m_pending_battle_level.has_value() ||
            m_pending_battle_level_sync_at.time_since_epoch().count() == 0 ||
            now < m_pending_battle_level_sync_at)
        {
            return;
        }

        // This phase shares the broader level_write_flush hitch label with
        // queue processing, but only runs once a delayed sync is actually due.
        const RuntimeBattleLevelProfileScope pending_sync_profile{
                config_.profile_sample_recorder,
                STR("level_write_flush:pending_battle_sync")};

        const uint64_t level = *m_pending_battle_level;
        const std::wstring source = m_pending_battle_level_source.empty()
                ? std::wstring{STR("pending_battle_level_sync")}
                : m_pending_battle_level_source;
        if (!m_pending_battle_level_bypass_cinematic_gate &&
            defer_battle_level_sync_during_cinematic(level, source, now))
        {
            return;
        }

        UObject* sora_pawn = m_pending_battle_level_sora_pawn;
        bool sora_pawn_already_classified = m_pending_battle_level_sora_pawn_already_classified;
        const bool bypass_cinematic_gate = m_pending_battle_level_bypass_cinematic_gate;

        // Cinematic transitions can replace Sora's pawn after the original
        // level-write owner scheduled this sync. The controller refresh at the
        // special-battle edge updates the cached pawn without requiring another
        // global UObject scan, so follow that newly validated pawn instead of
        // discarding the only pending battle-level write. Map travel clears the
        // cached pawn, which still rejects a genuinely stale level-write owner.
        if (sora_pawn_already_classified)
        {
            const auto& pawn_state = config_.runtime_state.grant_state().pawn_state();
            UObject* const cached_sora_pawn = pawn_state.object();
            if (cached_sora_pawn != sora_pawn)
            {
                if (!cached_sora_pawn || !pawn_state.candidate_trusted())
                {
                    Output::send<LogLevel::Verbose>(
                            STR("[KH3APProbeCpp] BattleLevelSync skipped source={} level={} reason=cached_pawn_lifecycle_mismatch\n"),
                            source,
                            level);
                    m_pending_battle_level.reset();
                    m_pending_battle_level_sync_at = {};
                    m_pending_battle_level_source.clear();
                    m_pending_battle_level_sora_pawn = nullptr;
                    m_pending_battle_level_sora_pawn_already_classified = false;
                    m_pending_battle_level_bypass_cinematic_gate = false;
                    return;
                }

                Output::send<LogLevel::Verbose>(
                        STR("[KH3APProbeCpp] BattleLevelSync retargeted source={} level={} reason=cinematic_pawn_replaced pawn={}\n"),
                        source,
                        level,
                        pawn_state.name());
                sora_pawn = cached_sora_pawn;
                sora_pawn_already_classified = true;
            }
        }

        m_pending_battle_level.reset();
        m_pending_battle_level_sync_at = {};
        m_pending_battle_level_source.clear();
        m_pending_battle_level_sora_pawn = nullptr;
        m_pending_battle_level_sora_pawn_already_classified = false;
        m_pending_battle_level_bypass_cinematic_gate = false;
        try_sync_battle_level_to_sora(
                level,
                source,
                sora_pawn,
                true,
                sora_pawn_already_classified,
                bypass_cinematic_gate);
    }

    };

    RuntimeBattleLevelSyncService::RuntimeBattleLevelSyncService(
            RuntimeBattleLevelSyncConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeBattleLevelSyncService::~RuntimeBattleLevelSyncService() = default;
    RuntimeBattleLevelSyncService::RuntimeBattleLevelSyncService(
            RuntimeBattleLevelSyncService&&) noexcept = default;
    auto RuntimeBattleLevelSyncService::operator=(
            RuntimeBattleLevelSyncService&&) noexcept
            -> RuntimeBattleLevelSyncService& = default;

    auto RuntimeBattleLevelSyncService::reset_for_map_lifecycle() -> void
    {
        impl_->reset_for_map_lifecycle();
    }

    auto RuntimeBattleLevelSyncService::status() const
            -> RuntimeBattleLevelSyncStatus
    {
        return impl_->status();
    }

    auto RuntimeBattleLevelSyncService::set_enabled(bool enabled) -> bool
    {
        return impl_->set_enabled(enabled);
    }

    auto RuntimeBattleLevelSyncService::set_offset(int32_t offset) -> bool
    {
        return impl_->set_offset(offset);
    }

    auto RuntimeBattleLevelSyncService::process_pending(
            std::chrono::steady_clock::time_point now) -> void
    {
        impl_->process_pending_initial_battle_level_sync(now);
    }

    auto RuntimeBattleLevelSyncService::schedule_initial(
            uint64_t level,
            UObject* validated_sora_pawn) -> void
    {
        impl_->schedule_initial_battle_level_sync(level, validated_sora_pawn);
    }

    auto RuntimeBattleLevelSyncService::schedule_he06_special(
            std::wstring_view frame_name,
            std::chrono::steady_clock::time_point now) -> bool
    {
        return impl_->schedule_he06_special_battle_level_sync(frame_name, now);
    }

    auto RuntimeBattleLevelSyncService::sync_observed(
            std::wstring_view source,
            bool force) -> bool
    {
        return impl_->try_sync_battle_level_to_observed_sora_level(source, force);
    }

    auto RuntimeBattleLevelSyncService::sync_sora_level(
            uint64_t sora_level,
            std::wstring_view source,
            UObject* sora_pawn,
            bool force,
            bool sora_pawn_already_classified,
            bool bypass_cinematic_gate) -> bool
    {
        const auto target_level = impl_->resolve_configured_battle_level(sora_level, source, sora_pawn);
        return target_level.has_value() &&
               impl_->try_sync_battle_level_to_sora(
                       *target_level,
                       source,
                       sora_pawn,
                       force,
                       sora_pawn_already_classified,
                       bypass_cinematic_gate);
    }

    auto RuntimeBattleLevelSyncService::sync_after_controller_reacquire(
            std::wstring_view source) -> bool
    {
        return impl_->sync_after_controller_reacquire(source);
    }
}
