#include "kh3ap/app/config.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "startup_service.h"

#include "game_thread_driver_service.h"
#include "enums.h"
#include "kh3ap/hooks/hook_lifecycle_service.h"
#include "kh3ap/diagnostics/reflection/reflection_diagnostics.h"
#include "state.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>

#include <chrono>
#include <cstdint>
#include <string_view>
#include <utility>

using namespace RC;

namespace
{
    using ::kh3ap::settings::s_bp_ability_add_path;
    using ::kh3ap::settings::s_bp_ability_get_level_path;
    using ::kh3ap::settings::s_bp_get_item_path;
    using ::kh3ap::settings::s_bp_get_vbonus_path;
    using ::kh3ap::settings::s_bp_is_ability_enable_path;
    using ::kh3ap::settings::s_enable_runtime_grant_signature_dump;
    using ::kh3ap::settings::s_enable_runtime_hot_path_profiling;
    using ::kh3ap::settings::s_enable_startup_signature_dump;
    using ::kh3ap::settings::s_kismet_print_string_path;
    using ::kh3ap::settings::s_remy_final_milestone_reward_path;
    using ::kh3ap::settings::s_tres_get_vbonus_path;
    using ::kh3ap::settings::s_tresui_map_in_notice_message_path;
    using ::kh3ap::settings::s_tresui_open_center_information_path;
    using ::kh3ap::settings::s_tresui_open_information_path;
    using ::kh3ap::settings::s_tresui_open_message_path;
    using ::kh3ap::settings::s_tresui_show_get_bonus_point_snow_curling_path;
    using ::kh3ap::settings::s_tresui_show_get_treasure_prize_snow_curling_path;
    using ::kh3ap::settings::s_uip_map_name_on_callback_path;
    using ::kh3ap::settings::s_uip_multi_item_get_on_callback_path;
    using ::kh3ap::settings::s_wait_bp_get_multiitem_path;
}

namespace kh3ap
{
    RuntimeStartupService::RuntimeStartupService(RuntimeStartupConfig config)
        : config_(std::move(config))
    {
    }

    auto RuntimeStartupService::initialize() -> void
    {
        Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] on_unreal_init build_stamp={} performance_diagnostics={} automation_handlers={}\n"),
                config_.build_stamp,
                s_enable_runtime_hot_path_profiling ? STR("on") : STR("off"),
                KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS ? STR("on") : STR("off"));
        config_.game_thread_driver.begin_unreal_init();
        const auto enum_prewarm_started_at = std::chrono::steady_clock::now();
        const RuntimeEnumPrewarmResult enum_prewarm =
                maybe_initialize_runtime_enum_value_maps();
        const auto enum_prewarm_elapsed =
                std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() -
                        enum_prewarm_started_at);
        Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Runtime enum maps prewarmed elapsed_us={} vbonus_count={} ability_count={} vbonus_reflected={} ability_reflected={}\n"),
                static_cast<uint64_t>(enum_prewarm_elapsed.count()),
                static_cast<uint64_t>(enum_prewarm.victory_bonus_count),
                static_cast<uint64_t>(enum_prewarm.ability_count),
                enum_prewarm.victory_bonus_reflected ? STR("true") : STR("false"),
                enum_prewarm.ability_reflected ? STR("true") : STR("false"));
        initialize_runtime_startup_diagnostics(
                config_.runtime_state,
                s_enable_startup_signature_dump,
                s_enable_runtime_grant_signature_dump,
                s_tres_get_vbonus_path,
                s_bp_get_vbonus_path,
                s_bp_ability_add_path,
                s_bp_ability_get_level_path,
                s_bp_is_ability_enable_path,
                s_kismet_print_string_path,
                s_tresui_open_information_path,
                s_tresui_open_center_information_path,
                s_tresui_open_message_path,
                s_tresui_map_in_notice_message_path,
                s_bp_get_item_path,
                s_wait_bp_get_multiitem_path,
                s_remy_final_milestone_reward_path,
                s_tresui_show_get_treasure_prize_snow_curling_path,
                s_tresui_show_get_bonus_point_snow_curling_path,
                s_uip_map_name_on_callback_path,
                s_uip_multi_item_get_on_callback_path);
        config_.native_hook_lifecycle.install();
        if (config_.initialize_kh02)
        {
            config_.initialize_kh02();
        }
        config_.game_thread_driver.complete_unreal_init();
    }
}
