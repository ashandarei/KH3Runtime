#include "moogle_shop_service.h"

#include "moogle_material_enum_augmentation.h"
#include "moogle_shop_catalog.h"
#include "moogle_shop_scan_state.h"
#include "ultima_weapon_shop_unlock.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/app/state.h"
#include "kh3ap/features/player/characters/character_exploration.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;

    struct RuntimeMoogleShopService::Impl
    {
        explicit Impl(RuntimeMoogleShopConfig value)
            : config(std::move(value)),
              material_enum({.enabled = config.material_enum_augmentation_enabled,
                             .material_item_index_min = config.material_item_index_min,
                             .material_item_index_max = config.material_item_index_max,
                             .build_stamp = config.build_stamp,
                             .bridge_dir_resolver = config.bridge_dir_resolver,
                             .unreal_init_time_resolver = config.unreal_init_time_resolver}),
              ultima_unlock(scan_state, config.debug_logging)
        {
        }

        auto runtime_context() const -> std::wstring
        {
            return config.runtime_context_resolver ? config.runtime_context_resolver()
                                                   : std::wstring{};
        }

        auto shop_inventory_context() const -> UObject*
        {
            if (inventory_work_gate.active())
            {
                if (auto* alternate = character_exploration::shop_context(
                        scan_state.shop_session_active() ? nullptr : alternate_shop_pawn.Get()))
                {
                    return alternate;
                }
            }
            auto& pawn_state = config.runtime_state.grant_state().pawn_state();
            UObject* const cached_pawn = pawn_state.object();
            if (!cached_pawn || !pawn_state.candidate_trusted())
            {
                return nullptr;
            }
            return cached_pawn;
        }

        auto request_reconciliation(std::wstring_view source) -> void
        {
            if (!config.enabled)
            {
                return;
            }

            const bool newly_requested = scan_state.request_reconciliation();
            if (newly_requested && config.debug_logging)
            {
                Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] MoogleShop reconciliation requested source={}\n"), source);
            }
        }

        auto defer_inventory_work_until_shop(std::wstring_view source) -> void
        {
            if (!config.enabled)
            {
                return;
            }

            scan_state.set_shop_session_active(false);
            alternate_shop_pawn = {};
            scan_state.cancel_reconciliation_for_retry();
            scan_state.cancel_purchase_scan_for_retry();
            inventory_work_gate.reset(false);
            if (config.debug_logging)
            {
                Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] MoogleShop inventory work "
                                                    "deferred until ShopMenu source={}\n"),
                                                source);
            }
        }

        auto request_purchase_scan(std::wstring_view source) -> void
        {
            if (!config.enabled)
            {
                return;
            }

            const bool newly_requested = scan_state.request_purchase_scan();
            if (newly_requested && config.debug_logging)
            {
                Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] MoogleShop purchase scan requested source={}\n"), source);
            }
        }

        auto reset_scan_work_state() -> void
        {
            const bool shop_session_active = scan_state.shop_session_active();
            scan_state.reset();
            ultima_unlock.reset_retry_window();
            if (shop_session_active)
            {
                scan_state.set_shop_session_active(true);
            }
        }

        auto reset() -> void
        {
            catalog.reset();
            reported_at.clear();
            delivery_state.clear();
            reset_scan_work_state();
            inventory_query_not_before = {};
            failure_logged = false;
            ultima_unlock.reset();
            bridge_refresh_gate.reset(scan_state.shop_session_active());
            inventory_work_gate.reset(scan_state.shop_session_active());
        }

        auto refresh_bridge_files(const std::optional<std::filesystem::path>& bridge_dir) -> void
        {
            if (!config.enabled)
            {
                return;
            }
            if (bridge_dir != current_bridge_dir)
            {
                reset();
                current_bridge_dir = bridge_dir;
            }
            if (!bridge_dir.has_value())
            {
                return;
            }

            const std::filesystem::path slot_data_path = *bridge_dir / STR("slot_data.json");
            JsonObject slot_document{};
            if (!catalog.seed_loaded())
            {
                if (!catalog.try_load_seed(slot_data_path, slot_document))
                {
                    return;
                }

                reported_at.clear();
                reset_scan_work_state();
                if (!catalog.options().checks_enabled)
                {
                    inventory_work_gate.reset(false);
                }
            }

            if (!catalog.seed_loaded() || !catalog.options().checks_enabled)
            {
                return;
            }
            if (!bridge_refresh_gate.active())
            {
                return;
            }

            if (!catalog.locations_loaded())
            {
                if (!slot_document.valid())
                {
                    slot_document = JsonObject::read_seed(slot_data_path);
                }
                if (!slot_document.valid())
                {
                    return;
                }

                catalog.load_locations(slot_document, config.max_catalog_locations);
                reported_at.clear();
                reset_scan_work_state();
                if (scan_state.shop_session_active())
                {
                    inventory_work_gate.activate();
                }
            }

            const auto checked_refresh = catalog.refresh_checked_locations(*bridge_dir);
            if (!checked_refresh.updated)
            {
                return;
            }
            const std::wstring pending_report_key{delivery_state.pending_bridge_key()};
            if (delivery_state.report_in_flight() &&
                catalog.checked_bridge_keys().contains(pending_report_key))
            {
                Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] MoogleShop purchase acknowledged check={}\n"),
                    pending_report_key);
                delivery_state.acknowledge(pending_report_key);
            }
            if (checked_refresh.changed)
            {
                request_reconciliation(STR("game_state_checked_locations"));
            }
        }

        auto complete_inventory_work_if_idle() -> void
        {
            if (!scan_state.shop_session_active() && !scan_state.reconciliation_requested() &&
                !scan_state.reconciliation_active() && !scan_state.purchase_scan_requested() &&
                !scan_state.purchase_scan_active() && !delivery_state.report_in_flight())
            {
                inventory_work_gate.reset(false);
                alternate_shop_pawn = {};
            }
        }

        auto process_inventory_events(std::chrono::steady_clock::time_point now) -> void
        {
            if (!config.enabled || !catalog.options().checks_enabled ||
                !catalog.locations_loaded() || !inventory_work_gate.active() ||
                !catalog.seed_loaded() || !catalog.checked_locations_valid())
            {
                return;
            }

            const auto& locations = catalog.locations();
            const auto& checked_bridge_keys = catalog.checked_bridge_keys();
            scan_state.begin_reconciliation(locations.size());
            if (!scan_state.shop_session_active() && !delivery_state.report_in_flight())
            {
                scan_state.begin_purchase_scan(locations.size());
            }
            if ((!scan_state.reconciliation_active() &&
                 (delivery_state.report_in_flight() || !scan_state.purchase_scan_active())) ||
                now < inventory_query_not_before)
            {
                complete_inventory_work_if_idle();
                return;
            }

            const auto blocking_ui =
                config.runtime_state.ui_diagnostic_state().blocking_ui_state().snapshot();
            if (config.runtime_state.grant_state().special_battle_mode_active() ||
                blocking_ui.active)
            {
                return;
            }

            UObject* const sora_pawn = shop_inventory_context();
            if (!sora_pawn)
            {
                return;
            }

            while (scan_state.reconciliation_active() ||
                   (!scan_state.shop_session_active() && !delivery_state.report_in_flight() &&
                    scan_state.purchase_scan_active()))
            {
                bool reconciliation_query = false;
                std::optional<size_t> location_index{};
                if (scan_state.reconciliation_active())
                {
                    location_index = scan_state.next_reconciliation_index(
                        locations.size(),
                        [&checked_bridge_keys, &locations](size_t index)
                        {
                            return checked_bridge_keys.contains(locations[index].bridge_key);
                        });
                    reconciliation_query = location_index.has_value();
                }

                if (!location_index.has_value() && !scan_state.shop_session_active() &&
                    !delivery_state.report_in_flight() && scan_state.purchase_scan_active())
                {
                    location_index = scan_state.next_purchase_index(
                        locations.size(),
                        [this, now, &locations, &checked_bridge_keys](size_t index)
                        {
                            const auto& location = locations[index];
                            if (checked_bridge_keys.contains(location.bridge_key))
                            {
                                return false;
                            }

                            const auto reported = reported_at.find(location.bridge_key);
                            return reported == reported_at.end() ||
                                   has_elapsed_since(reported->second, now,
                                                     config.report_retry_interval);
                        });
                }
                if (!location_index.has_value())
                {
                    continue;
                }

                const auto& location = locations[*location_index];
                bool has_item = false;
                std::wstring failure_reason{};
                if (!try_call_bp_tres_have_item(location.item_token, sora_pawn, has_item,
                                                config.runtime_state, config.have_item_path,
                                                config.blueprint_library_default_object_path,
                                                config.debug_logging, &failure_reason))
                {
                    inventory_query_not_before = now + config.inventory_query_retry_interval;
                    if (reconciliation_query)
                    {
                        scan_state.cancel_reconciliation_for_retry();
                    }
                    else
                    {
                        scan_state.cancel_purchase_scan_for_retry();
                    }
                    if (!failure_logged)
                    {
                        failure_logged = true;
                        Output::send<LogLevel::Warning>(
                            STR("[KH3APProbeCpp] MoogleShop inventory query failed token={} "
                                "failure={} | {}\n"),
                            location.item_token,
                            failure_reason.empty() ? STR("<none>") : failure_reason,
                            runtime_context());
                    }
                    return;
                }
                failure_logged = false;

                if (reconciliation_query)
                {
                    if (has_item)
                    {
                        continue;
                    }

                    const int32_t munny_delta = location.shop_price > 0 ? -location.shop_price : 0;
                    failure_reason.clear();
                    const bool reconciled =
                        munny_delta != 0
                            ? try_call_bp_tres_get_item_and_add_munny(
                                  location.item_token, munny_delta, sora_pawn, false,
                                  config.runtime_state, config.get_item_path, config.add_munny_path,
                                  config.blueprint_library_default_object_path,
                                  config.debug_logging, &failure_reason)
                            : try_call_bp_tres_get_item(
                                  location.item_token, sora_pawn, false, config.runtime_state,
                                  config.get_item_path,
                                  config.blueprint_library_default_object_path,
                                  config.debug_logging, &failure_reason);
                    if (reconciled)
                    {
                        Output::send<LogLevel::Warning>(
                            STR("[KH3APProbeCpp] MoogleShop reconciled sold-out marker check={} "
                                "token={} restored_cost={} munny_delta={} | {}\n"),
                            location.bridge_key, location.item_token, location.shop_price,
                            munny_delta, runtime_context());
                        continue;
                    }

                    failure_logged = true;
                    inventory_query_not_before = now + config.inventory_query_retry_interval;
                    scan_state.cancel_reconciliation_for_retry();
                    Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] MoogleShop marker reconcile failed check={} token={} "
                            "restored_cost={} munny_delta={} failure={} | {}\n"),
                        location.bridge_key, location.item_token, location.shop_price, munny_delta,
                        failure_reason.empty() ? STR("<none>") : failure_reason, runtime_context());
                    return;
                }

                if (!has_item)
                {
                    continue;
                }

                RuntimeLootPayload payload{};
                payload.type = STR("shop");
                payload.bridge_key = location.bridge_key;
                payload.actor_name = STR("Moogle Shop Buy Menu");
                payload.source_id = location.source_id;
                payload.source = STR("BP_TresHaveItem inventory marker");
                const std::string opened_at = now_iso8601_utc();
                payload.opened_at = std::wstring{opened_at.begin(), opened_at.end()};
                payload.grant_context = STR("moogle_shop_purchase");
                payload.mapping_reason = STR("resolved from slot_data.json shop_item_token");
                payload.resolution_kind = STR("inventory_marker");
                payload.token_field_name = STR("InItemKeyName");
                payload.token_value = location.item_token;
                if (config.payload_emitter && config.payload_emitter(payload))
                {
                    reported_at[location.bridge_key] = now;
                    delivery_state.mark_report_in_flight(location.bridge_key);
                    Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] MoogleShop purchase "
                                                        "reported check={} token={} | {}\n"),
                                                    location.bridge_key, location.item_token,
                                                    runtime_context());
                }
                else
                {
                    inventory_query_not_before = now + config.inventory_query_retry_interval;
                    scan_state.cancel_purchase_scan_for_retry();
                    Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] MoogleShop purchase "
                                                        "report deferred check={} token={} | {}\n"),
                                                    location.bridge_key, location.item_token,
                                                    runtime_context());
                }

                // The bridge payload uses a single replaceable file. Keep this
                // report in flight until game_state.json confirms its check;
                // only then may the next purchased marker replace the file.
                return;
            }
            complete_inventory_work_if_idle();
        }

        auto process_pending_work(std::chrono::steady_clock::time_point now) -> void
        {
            material_enum.process(now, catalog.seed_loaded() && catalog.options().checks_enabled,
                                  current_bridge_dir);
            ultima_unlock.process(now, catalog.seed_loaded(), catalog.options().ultima);
            process_inventory_events(now);
        }

        auto initialize_runtime() -> void
        {
            process_pending_work(std::chrono::steady_clock::now());
        }

        auto observe_shop_session(bool active) -> void
        {
            const bool was_active = scan_state.shop_session_active();
            if (!scan_state.set_shop_session_active(active))
            {
                return;
            }

            if (!was_active && active)
            {
                alternate_shop_pawn = make_uobject_weak(character_exploration::shop_context());
                const bool newly_activated = bridge_refresh_gate.activate();
                const auto bridge_dir =
                    config.bridge_dir_resolver ? config.bridge_dir_resolver() : current_bridge_dir;
                refresh_bridge_files(bridge_dir);
                if (catalog.options().checks_enabled)
                {
                    inventory_work_gate.activate();
                    request_reconciliation(STR("ShopMenu_enter"));
                }
                if (catalog.options().ultima.shop_mode)
                {
                    ultima_unlock.request_refresh(STR("ShopMenu_enter"));
                }
                if (catalog.options().checks_enabled || catalog.options().ultima.shop_mode)
                {
                    process_pending_work(std::chrono::steady_clock::now());
                }
                if (newly_activated && config.debug_logging)
                {
                    Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] MoogleShop bridge refresh "
                                                        "activated source=ShopMenu_enter\n"));
                }
                return;
            }

            if (was_active && !active)
            {
                if (catalog.options().checks_enabled)
                {
                    request_purchase_scan(STR("ShopMenu_exit"));
                }
                if (catalog.options().ultima.shop_mode)
                {
                    ultima_unlock.request_refresh(STR("ShopMenu_exit"));
                }
                if (catalog.options().checks_enabled || catalog.options().ultima.shop_mode)
                {
                    process_pending_work(std::chrono::steady_clock::now());
                }
            }
        }

        auto observe_progressive_keyblade_level_applied() -> void
        {
            if (catalog.options().ultima.shop_mode)
            {
                ultima_unlock.request_refresh(STR("progressive_keyblade_level_applied"));
            }
        }

        auto observe_player_started(std::wstring_view source) -> void
        {
            defer_inventory_work_until_shop(source);
            if (catalog.options().ultima.shop_mode)
            {
                ultima_unlock.request_refresh(source);
                process_pending_work(std::chrono::steady_clock::now());
            }
        }

        auto refresh(const std::optional<std::filesystem::path>& bridge_dir) -> void
        {
            refresh_bridge_files(bridge_dir);
            if (catalog.options().checks_enabled || catalog.options().ultima.shop_mode)
            {
                process_pending_work(std::chrono::steady_clock::now());
            }
        }

        RuntimeMoogleShopConfig config;
        MoogleMaterialEnumAugmentation material_enum;
        std::optional<std::filesystem::path> current_bridge_dir{};
        MoogleShopCatalog catalog{};
        std::unordered_map<std::wstring, std::chrono::steady_clock::time_point> reported_at{};
        RuntimeMoogleShopDeliveryState delivery_state{};
        RuntimeMoogleShopScanState scan_state{};
        UltimaWeaponShopUnlock ultima_unlock;
        RuntimeMoogleShopActivationGate bridge_refresh_gate{};
        RuntimeMoogleShopActivationGate inventory_work_gate{};
        FWeakObjectPtr alternate_shop_pawn{};
        std::chrono::steady_clock::time_point inventory_query_not_before{};
        bool failure_logged{false};
    };

    RuntimeMoogleShopService::RuntimeMoogleShopService(RuntimeMoogleShopConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeMoogleShopService::~RuntimeMoogleShopService() = default;
    RuntimeMoogleShopService::RuntimeMoogleShopService(RuntimeMoogleShopService&&) noexcept =
        default;
    auto RuntimeMoogleShopService::operator=(RuntimeMoogleShopService&&) noexcept
        -> RuntimeMoogleShopService& = default;

    auto RuntimeMoogleShopService::request_reconciliation(std::wstring_view source) -> void
    {
        impl_->defer_inventory_work_until_shop(source);
    }

    auto RuntimeMoogleShopService::initialize_runtime() -> void
    {
        impl_->initialize_runtime();
    }

    auto RuntimeMoogleShopService::observe_shop_session(bool active) -> void
    {
        impl_->observe_shop_session(active);
    }

    auto RuntimeMoogleShopService::observe_progressive_keyblade_level_applied() -> void
    {
        impl_->observe_progressive_keyblade_level_applied();
    }

    auto RuntimeMoogleShopService::observe_player_started(std::wstring_view source) -> void
    {
        impl_->observe_player_started(source);
    }

    auto RuntimeMoogleShopService::reset() -> void
    {
        impl_->reset();
    }

    auto RuntimeMoogleShopService::refresh(const std::optional<std::filesystem::path>& bridge_dir)
        -> void
    {
        impl_->refresh(bridge_dir);
    }

    auto RuntimeMoogleShopService::has_pending_work() const -> bool
    {
        const auto& state = impl_->scan_state;
        return impl_->config.enabled &&
               (state.ultima_refresh_requested() ||
                (impl_->inventory_work_gate.active() &&
                 (state.reconciliation_requested() || state.reconciliation_active() ||
                  state.purchase_scan_requested() || state.purchase_scan_active())) ||
                (state.shop_session_active() && impl_->material_enum.has_pending_work()));
    }

} // namespace kh3ap
