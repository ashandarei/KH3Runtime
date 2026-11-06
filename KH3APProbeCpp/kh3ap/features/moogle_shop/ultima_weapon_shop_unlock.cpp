#include "ultima_weapon_shop_unlock.h"

#include "kh3ap/features/grants/keyblades/progressive_keyblade_level_helpers.h"
#include "moogle_shop_catalog.h"
#include "moogle_shop_scan_state.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>

namespace kh3ap
{
    namespace
    {
        constexpr int max_retry_attempts_per_event = 10;
        constexpr auto shop_setup_retry_delay = std::chrono::seconds(1);
    } // namespace

    using namespace RC;

    UltimaWeaponShopUnlock::UltimaWeaponShopUnlock(RuntimeMoogleShopScanState& scan_state,
                                                   bool debug_logging)
        : scan_state_(scan_state), debug_logging_(debug_logging)
    {
    }

    auto UltimaWeaponShopUnlock::request_refresh(std::wstring_view source) -> void
    {
        const bool newly_requested = scan_state_.request_ultima_refresh();
        reset_retry_window();
        if (newly_requested && debug_logging_)
        {
            Output::send<LogLevel::Verbose>(
                STR("[KH3APProbeCpp] Ultima Weapon shop refresh requested source={}\n"), source);
        }
    }

    auto UltimaWeaponShopUnlock::reset_retry_window() -> void
    {
        retry_attempts_ = 0;
        retry_not_before_ = {};
    }

    auto UltimaWeaponShopUnlock::reset() -> void
    {
        reset_retry_window();
        failure_logged_ = false;
    }

    auto UltimaWeaponShopUnlock::defer_retry(std::chrono::steady_clock::time_point now) -> void
    {
        if (retry_attempts_ >= max_retry_attempts_per_event)
        {
            scan_state_.complete_ultima_refresh();
            retry_not_before_ = {};
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Ultima Weapon shop refresh paused after {} event "
                    "retries; next lifecycle event will rearm it\n"),
                retry_attempts_);
        }
        else
        {
            retry_not_before_ = now + shop_setup_retry_delay;
        }
    }

    auto UltimaWeaponShopUnlock::process(std::chrono::steady_clock::time_point now,
                                         bool seed_loaded, const UltimaWeaponShopOptions& options)
        -> void
    {
        if (!scan_state_.ultima_refresh_requested() || !seed_loaded || now < retry_not_before_)
        {
            return;
        }
        if (!options.shop_mode)
        {
            scan_state_.complete_ultima_refresh();
            return;
        }
        if (retry_attempts_ >= max_retry_attempts_per_event)
        {
            scan_state_.complete_ultima_refresh();
            return;
        }
        ++retry_attempts_;

        UltimaWeaponShopPrerequisiteQueryResult keyblade_query{};
        std::wstring failure_details{};
        if (options.keyblade_levels_start_at_max)
        {
            keyblade_query.required_flag_count =
                static_cast<int>(options.required_keyblades.size()) * 10;
            keyblade_query.maxed_flag_count = keyblade_query.required_flag_count;
            keyblade_query.all_required_keyblades_maxed = true;
        }
        else if (!try_query_ultima_weapon_shop_prerequisites(options.required_keyblades,
                                                             &keyblade_query, &failure_details))
        {
            if (!failure_logged_)
            {
                failure_logged_ = true;
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Ultima Weapon shop unlock query unavailable failure={}\n"),
                    failure_details.empty() ? STR("<none>") : failure_details);
            }
            defer_retry(now);
            return;
        }

        constexpr int locked_shop_level = 0;
        constexpr int unlocked_shop_level = 1;
        UltimaWeaponShopLevelUpdateResult update{};
        failure_details.clear();
        const int desired_shop_level =
            keyblade_query.all_required_keyblades_maxed ? unlocked_shop_level : locked_shop_level;
        if (!try_update_ultima_weapon_shop_level(desired_shop_level, row_cache_, &update,
                                                 &failure_details))
        {
            if (!failure_logged_)
            {
                failure_logged_ = true;
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Ultima Weapon shop row update unavailable failure={}\n"),
                    failure_details.empty() ? STR("<none>") : failure_details);
            }
            defer_retry(now);
            return;
        }

        failure_logged_ = false;
        reset_retry_window();
        scan_state_.complete_ultima_refresh();
        if (update.changed)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Ultima Weapon shop availability changed previous_level={} "
                    "new_level={} maxed_flags={}/{} unlocked={}\n"),
                update.previous_shop_level, update.desired_shop_level,
                keyblade_query.maxed_flag_count, keyblade_query.required_flag_count,
                keyblade_query.all_required_keyblades_maxed ? STR("true") : STR("false"));
        }
    }
} // namespace kh3ap
