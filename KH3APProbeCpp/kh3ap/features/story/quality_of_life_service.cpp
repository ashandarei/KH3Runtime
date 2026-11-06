#include "kh3ap/app/config.h"
#include "kh3ap/core/settings/pak_seed_settings.h"
#include "kh3ap/core/settings/seed_slot_options.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/story/quality_of_life_service.h"

#include "kh3ap/core/conditional_profile_scope.h"
#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/features/story/quality_of_life.h"
#include "kh3ap/features/story/progression/game_flag_events.h"
#include "kh3ap/app/state.h"
#include "kh3ap/bridge/catalog/update_catalog_state.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/NameTypes.hpp>

#include <array>
#include <cstring>
#include <string>
#include <utility>

namespace kh3ap
{
    using namespace RC;

    namespace
    {
        using ::kh3ap::settings::s_enable_runtime_hot_path_profiling;
    }

    struct RuntimeQualityOfLifeService::Impl
    {
        explicit Impl(RuntimeQualityOfLifeConfig value)
            : config(std::move(value))
        {
        }

        ~Impl() { unsubscribe_game_flags(flag_events); }

        auto initialize_events() -> bool
        {
            try {
                std::set<uint64_t> watched;
                bool integer=false, boolean=false;
                for(size_t index=0;index<runtime_quality_of_life_rules.size();++index) {
                    if(!rule_enabled[index])continue;
                    const auto& rule=runtime_quality_of_life_rules[index];
                    const RC::Unreal::FName name(rule.flag_name.data(),RC::Unreal::FNAME_Add);
                    static_assert(sizeof(name)==sizeof(flag_names[index]));
                    std::memcpy(&flag_names[index],&name,sizeof(name));
                    watched.insert(flag_names[index]);
                    if(rule.flag_kind==RuntimeQualityOfLifeFlagKind::IntegerTransition)integer=true;
                    else boolean=true;
                }
                flag_events.configure(std::move(watched));
                if(integer)subscribe_game_flags(flag_events,true);
                if(boolean)subscribe_game_flags(flag_events);
                events_ready=true;
                return true;
            } catch(const std::exception& error) {
                unsubscribe_game_flags(flag_events);
                events_failed=true;
                const std::string reason=error.what();
                Output::send<LogLevel::Error>(STR("[KH3APProbeCpp] NativeQualityOfLife event subscription failed reason={}\n"),
                    std::wstring(reason.begin(),reason.end()));
                return false;
            }
        }

        auto log_settings_failure_once(std::wstring_view reason) -> void
        {
            if (settings_failure_logged)
            {
                return;
            }
            settings_failure_logged = true;
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] NativeQualityOfLife waiting for "
                        "seed options reason={}\n"),
                    reason);
        }

        auto load_settings() -> bool
        {
            const auto& slot_data = installed_seed_slot_data();
            if (!slot_data.valid())
            {
                log_settings_failure_once(STR("installed_seed_options_unavailable"));
                return false;
            }

            for (size_t index = 0;
                 index < runtime_quality_of_life_rules.size();
                 ++index)
            {
                const RuntimeQualityOfLifeRule& rule =
                        runtime_quality_of_life_rules[index];
                rule_enabled[index] = seed_bool_option(slot_data, rule.slot_option);
            }

            settings_loaded = true;
            settings_failure_logged = false;
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] NativeQualityOfLife seed options "
                        "loaded {}={} {}={} source={}\n"),
                    runtime_quality_of_life_rules[0].feature_name,
                    rule_enabled[0] ? STR("true") : STR("false"),
                    runtime_quality_of_life_rules[1].feature_name,
                    rule_enabled[1] ? STR("true") : STR("false"),
                    STR("installed_seed_pak"));
            return true;
        }

        auto log_rule_failure_once(
                size_t index,
                std::wstring_view operation,
                std::wstring_view failure) -> void
        {
            if (rule_failure_logged[index])
            {
                return;
            }
            rule_failure_logged[index] = true;
            const RuntimeQualityOfLifeRule& rule =
                    runtime_quality_of_life_rules[index];
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] NativeQualityOfLife {} failed "
                        "feature={} flag={} failure={}\n"),
                    operation,
                    rule.feature_name,
                    rule.flag_name,
                    failure.empty() ? STR("<none>") : failure);
        }

        auto process_integer_rule(size_t index) -> bool
        {
            const RuntimeHotPathProfileScope profile{L"quality_of_life:integer_rule"};
            const RuntimeQualityOfLifeRule& rule =
                    runtime_quality_of_life_rules[index];
            int32_t current_value{};
            std::wstring failure{};
            if (!try_call_get_game_flag(
                        rule.flag_name,
                        current_value,
                        config.runtime_state,
                        config.get_game_flag_path,
                        config.blueprint_library_default_object_path,
                        config.debug_logging,
                        &failure))
            {
                log_rule_failure_once(index, STR("read"), failure);
                return false;
            }
            if (!should_apply_runtime_quality_of_life_rule(
                        rule,
                        rule_enabled[index],
                        current_value,
                        false))
            {
                rule_failure_logged[index] = false;
                return true;
            }

            failure.clear();
            if (!try_call_set_game_flag(
                        rule.flag_name,
                        rule.target_value,
                        config.runtime_state,
                        config.set_game_flag_path,
                        config.blueprint_library_default_object_path,
                        config.debug_logging,
                        &failure))
            {
                log_rule_failure_once(index, STR("write"), failure);
                return false;
            }

            rule_failure_logged[index] = false;
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] NativeQualityOfLife applied "
                        "feature={} flag={} before={} target={}\n"),
                    rule.feature_name,
                    rule.flag_name,
                    current_value,
                    rule.target_value);
            return true;
        }

        auto process_boolean_rule(size_t index) -> bool
        {
            const RuntimeHotPathProfileScope profile{L"quality_of_life:boolean_rule"};
            const RuntimeQualityOfLifeRule& rule =
                    runtime_quality_of_life_rules[index];
            bool current_value{};
            std::wstring failure{};
            if (!try_call_get_game_flag_boolean(
                        rule.flag_name,
                        current_value,
                        config.runtime_state,
                        config.get_game_flag_boolean_path,
                        config.blueprint_library_default_object_path,
                        config.debug_logging,
                        &failure))
            {
                log_rule_failure_once(index, STR("read"), failure);
                return false;
            }
            if (!should_apply_runtime_quality_of_life_rule(
                        rule,
                        rule_enabled[index],
                        0,
                        current_value))
            {
                rule_failure_logged[index] = false;
                return true;
            }

            failure.clear();
            if (!try_call_set_game_flag_boolean(
                        rule.flag_name,
                        true,
                        config.runtime_state,
                        config.set_game_flag_boolean_path,
                        config.blueprint_library_default_object_path,
                        config.debug_logging,
                        &failure))
            {
                log_rule_failure_once(index, STR("write"), failure);
                return false;
            }

            rule_failure_logged[index] = false;
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] NativeQualityOfLife applied "
                        "feature={} flag={} value=true\n"),
                    rule.feature_name,
                    rule.flag_name);
            return true;
        }

        auto tick(std::chrono::steady_clock::time_point now) -> void
        {
            if (!config.enabled || events_failed)
            {
                return;
            }
            if (!settings_loaded)
            {
                if (!should_run_periodic_update(
                            last_settings_attempt,
                            now,
                            config.retry_interval) ||
                    !load_settings())
                {
                    return;
                }
            }
            if(!events_ready&&!initialize_events())return;
            if(refresh_pending) {
                refresh_pending=false;
                rule_pending=rule_enabled;
                retry_at={};
            }
            if(flag_events.pending()) {
                const auto changed=flag_events.take();
                for(size_t i=0;i<flag_names.size();++i)if(rule_enabled[i]&&changed.contains(flag_names[i])) {
                    rule_pending[i]=true;retry_at[i]={};
                }
            }
            bool ready=false;
            for(size_t i=0;i<rule_pending.size();++i)ready=ready||(rule_pending[i]&&now>=retry_at[i]);
            if(!ready)return;
            RuntimeConditionalProfileScope<s_enable_runtime_hot_path_profiling> profile{
                    STR("on_update:quality_of_life_events")};

            for (size_t index = 0;
                 index < runtime_quality_of_life_rules.size();
                 ++index)
            {
                if (!rule_pending[index] || now<retry_at[index])
                {
                    continue;
                }
                // Setter callbacks enqueue another observation for a later
                // update; never process rules recursively inside a setter.
                rule_pending[index]=false;
                const bool complete=runtime_quality_of_life_rules[index].flag_kind ==
                    RuntimeQualityOfLifeFlagKind::IntegerTransition
                    ? process_integer_rule(index) : process_boolean_rule(index);
                if(!complete) {
                    rule_pending[index]=true;
                    retry_at[index]=now+config.retry_interval;
                }
            }
        }

        RuntimeQualityOfLifeConfig config;
        std::array<bool, runtime_quality_of_life_rules.size()> rule_enabled{};
        std::array<bool, runtime_quality_of_life_rules.size()>
                rule_failure_logged{};
        std::chrono::steady_clock::time_point last_settings_attempt{};
        VBonusFlagEvents flag_events{true};
        std::array<uint64_t,runtime_quality_of_life_rules.size()> flag_names{};
        std::array<bool,runtime_quality_of_life_rules.size()> rule_pending{};
        std::array<std::chrono::steady_clock::time_point,runtime_quality_of_life_rules.size()> retry_at{};
        bool refresh_pending{true},events_ready{},events_failed{};
        bool settings_loaded{false};
        bool settings_failure_logged{false};
    };

    RuntimeQualityOfLifeService::RuntimeQualityOfLifeService(
            RuntimeQualityOfLifeConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeQualityOfLifeService::~RuntimeQualityOfLifeService() = default;
    RuntimeQualityOfLifeService::RuntimeQualityOfLifeService(
            RuntimeQualityOfLifeService&&) noexcept = default;
    auto RuntimeQualityOfLifeService::operator=(
            RuntimeQualityOfLifeService&&) noexcept
            -> RuntimeQualityOfLifeService& = default;

    auto RuntimeQualityOfLifeService::tick(
            std::chrono::steady_clock::time_point now) -> void
    {
        impl_->tick(now);
    }

    auto RuntimeQualityOfLifeService::request_refresh() -> void
    {
        impl_->refresh_pending=true;
    }
}
