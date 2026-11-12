#include "service_impl.h"
#include "helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/ui/ui_layout.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/features/world/space/space_service.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace controller_input;
    using namespace ui_control_detail;
    bool RuntimeUiControlService::Impl::advance_player_selection()
    {
        auto* selector = unique(L"TresUIP_HudPcSelect");
        player_select_open = player_select_ready = false;
        if (is_uobject_valid_fast(selector))
        {
            // Both retail selectors consume the ordinary left/right
            // trigger and run their own selection delegate/close path.
            const uintptr_t selector_tick = death_ui.steam ? 0x164b5d0 : 0x19be2f0;
            const bool selector_signature =
                death_ui.steam ? signature(base + selector_tick,
                                           {0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec,
                                            0x20, 0xf3, 0x0f, 0x10, 0x81, 0x70, 0x02, 0x00, 0x00})
                               : signature(base + selector_tick, {0x48, 0x89, 0x5c, 0x24, 0x18,
                                                                  0x57, 0x48, 0x83, 0xec, 0x20});
            offset(selector, L"m_pPCInfoL", player_selector_left_info_offset);
            offset(selector, L"m_pUIManager", player_selector_manager_offset);
            if (try_read_reflected_object_property_value(selector, L"m_pUIManager") !=
                    manager_ref.Get() ||
                field<uintptr_t>(reinterpret_cast<void*>(field<uintptr_t>(selector, 0)),
                                 player_selector_tick_vtable_offset) != base + selector_tick ||
                !selector_signature)
            {
                throw std::runtime_error("player_select_build_or_owner_mismatch");
            }
            player_select_open = field<uint8_t>(selector, native_ui::panel_open_offset) != 0;
            player_select_ready = field<uint8_t>(selector, native_ui::panel_ready_offset) != 0;
            player_selected = field<int32_t>(selector, player_selector_selected_index_offset);
            if (!dispatched && player_select_open)
            {
                player_sora_index = sora_selection_index(pawn_name);
            }
        }
        if (command == L"select_sora")
        {
            if (dispatched)
            {
                if (!player_select_open && player_selected == player_sora_index)
                {
                    player_selection_observed = true;
                    finish(L"completed", L"native_sora_selection_observed");
                }
                return true;
            }
            if (pawn_name != expected_pawn || !player_select_open || !player_select_ready ||
                player_selected != -1 || player_sora_index < 0 ||
                !is_uobject_valid_fast(selector) ||
                field<uint8_t>(selector,
                               player_selector_enabled_flags_offset + player_sora_index) == 0)
            {
                throw std::runtime_error("expected_verified_remind_sora_selection_required");
            }
            auto* controller =
                try_read_reflected_object_property_value(config.sora(), L"Controller");
            if (!is_uobject_valid_fast(controller))
            {
                throw std::runtime_error("selection_controller_unavailable");
            }
            selection_controller = controller;
            if (selection_controller.Get() != controller)
            {
                throw std::runtime_error("selection_controller_identity_unavailable");
            }
            dispatched = true;
            report(L"accepted", L"pressing_native_sora_selection_trigger");
            runtime_controller_action_input(
                controller, player_sora_index == 1 ? L"right_trigger" : L"left_trigger", true);
            selection_key_down = true;
            return true;
        }

        return false;
    }

    void RuntimeUiControlService::Impl::advance_notice(UObject* dialog)
    {
        if (dispatched && message_id != expected_message_id)
        {
            message_dismissed = true;
            finish(L"completed", L"native_notice_pages_advanced_and_closed");
            return;
        }
        if (pawn_name != expected_pawn || message_id != expected_message_id || message_type != 0 ||
            message_pages < 1 || message_pages > max_notice_pages || message_text.empty())
        {
            throw std::runtime_error("expected_ready_informational_notice_required");
        }
        if (!message_ready)
        {
            if (!dispatched)
            {
                throw std::runtime_error("notice_not_ready");
            }
            return;
        }
        if (message_page == notice_last_page || Clock::now() < notice_next)
        {
            return;
        }
        if (message_page < 0 || message_page >= message_pages || notice_advances >= message_pages)
        {
            throw std::runtime_error("notice_page_progress_invalid");
        }
        if (!signature(base + egs_message_confirm_rva,
                       {0x48, 0x8b, 0xc4, 0x57, 0x48, 0x81, 0xec, 0x80, 0, 0, 0}) ||
            !signature(base + egs_message_page_check_rva,
                       {0x8b, 0x8b, 0x70, 0x02, 0, 0, 0x85, 0xc9}))
        {
            throw std::runtime_error("notice_page_handler_build_mismatch");
        }
        if (!is_uobject_valid_fast(dialog) ||
            field<uintptr_t>(dialog, 0) != base + message_dialog_vtable_rva ||
            field<int32_t>(dialog, native_ui::message_id_offset) != expected_message_id)
        {
            throw std::runtime_error("notice_dialog_unavailable");
        }
        // Event 5 is the native confirm/page action. Event 7 reports a
        // completed Flash selection and cannot advance several pages.
        native_ui::InputEvent event{};
        const int32_t kind = native_ui::page_advance_event;
        event.set_kind(kind);
        const auto previous = message_page;
        using Confirm = bool(__fastcall*)(UObject*, void*);
        if (!reinterpret_cast<Confirm>(base + egs_message_confirm_rva)(dialog, event.data()) ||
            field<int32_t>(dialog, native_ui::message_current_page_offset) != previous + 1 ||
            !event.handled())
        {
            throw std::runtime_error("notice_page_advance_not_observed");
        }
        notice_last_page = previous;
        ++notice_advances;
        notice_next = Clock::now() + notice_advance_interval;
        dispatched = true;
        selected = 0;
        report(L"accepted", L"native_notice_page_advanced");
        return;
    }

    void RuntimeUiControlService::Impl::advance_dismiss_message(UObject* dialog)
    {
        if (dispatched)
        {
            if (message_id != expected_message_id)
            {
                message_dismissed = true;
                finish(L"completed", L"native_message_closed");
            }
            return;
        }
        if (pawn_name != expected_pawn || message_id != expected_message_id || message_type != 0 ||
            message_pages != 1 || !message_ready || message_text.empty())
        {
            finish(L"error", L"expected_single_page_notice_required");
            return;
        }
        if (!signature(base + egs_message_confirm_rva,
                       {0x48, 0x8b, 0xc4, 0x57, 0x48, 0x81, 0xec, 0x80, 0, 0, 0}))
        {
            throw std::runtime_error("message_handler_build_mismatch");
        }
        if (!is_uobject_valid_fast(dialog) ||
            field<uintptr_t>(dialog, 0) != base + message_dialog_vtable_rva ||
            field<int32_t>(dialog, native_ui::message_id_offset) != expected_message_id)
        {
            throw std::runtime_error("notice_dialog_unavailable");
        }
        native_ui::InputEvent event{};
        const int32_t kind = native_ui::decide_event, index = 0;
        event.set_kind(kind);
        event.set_parameter(index);
        dispatched = true;
        selected = 0;
        report(L"accepted", L"dispatching_native_notice_ok");
        using Decide = bool(__fastcall*)(UObject*, void*);
        if (!reinterpret_cast<Decide>(base + egs_message_confirm_rva)(dialog, event.data()) ||
            field<int32_t>(dialog, message_completion_state_offset) != 0 || !event.handled())
        {
            throw std::runtime_error("notice_selection_not_observed");
        }
        return;
    }

    void RuntimeUiControlService::Impl::advance_dismiss_key_item()
    {
        auto* manager = manager_ref.Get();
        if (!is_uobject_valid_fast(manager))
        {
            throw std::runtime_error("key_item_manager_unavailable");
        }
        auto* panel = sample_key_item(manager);
        if (!panel)
        {
            throw std::runtime_error("key_item_panel_unavailable");
        }
        if (dispatched)
        {
            if (!key_item_open)
            {
                key_item_dismissed = true;
                finish(L"completed", L"native_key_item_closed");
            }
            return;
        }
        if (pawn_name != expected_pawn || pause_open || !key_item_ready ||
            !is_uobject_valid_fast(try_read_reflected_object_property_value(panel, L"m_pMoviePlayer")))
        {
            throw std::runtime_error("expected_ready_key_item_required");
        }
        const auto input = base + (death_ui.steam ? steam_key_item_input_rva : egs_key_item_input_rva);
        const auto vtable = field<uintptr_t>(panel, 0);
        if (field<uintptr_t>(reinterpret_cast<void*>(vtable),
                             native_ui::input_handler_vtable_index * sizeof(void*)) != input ||
            !signature(input, {0x40, 0x53, 0x48, 0x83, 0xec, 0x60, 0x48, 0xc7, 0x44, 0x24, 0x20,
                               0xfe, 0xff, 0xff, 0xff}))
        {
            throw std::runtime_error("key_item_handler_build_mismatch");
        }
        native_ui::InputEvent event{key_item_confirm_event};
        dispatched = true;
        selected = 0;
        report(L"accepted", L"dispatching_native_key_item_confirm");
        using Confirm = bool(__fastcall*)(UObject*, void*);
        if (!reinterpret_cast<Confirm>(input)(panel, event.data()) ||
            !is_uobject_valid_fast(panel) || field<uint8_t>(panel, native_ui::panel_ready_offset) != 0)
        {
            throw std::runtime_error("key_item_confirmation_not_observed");
        }
    }

    void RuntimeUiControlService::Impl::advance_retry()
    {
        const bool flying = command == L"space_retry";
        const bool continuing = command == L"continue";
        int32_t requested_action = continuing ? 0 : game_over_retry_action;
        auto* pause = pause_ref.Get();
        offset(pause, L"m_GameOver", pause_game_over_offset);
        auto* game_over = try_read_reflected_object_property_value(pause, L"m_GameOver");
        if (!is_uobject_valid_fast(game_over) ||
            field<uintptr_t>(game_over, 0) != base + death_ui.game_over_vtable)
        {
            throw std::runtime_error("game_over_layout_mismatch");
        }
        if (field<uintptr_t>(reinterpret_cast<void*>(field<uintptr_t>(game_over, 0)),
                             game_over_retry_vtable_index * sizeof(void*)) !=
            base + death_ui.retry_handler)
        {
            throw std::runtime_error("game_over_handler_mismatch");
        }
        offset(game_over, L"m_Button", game_over_buttons_offset);
        offset(game_over, L"m_Owner", game_over_owner_offset);
        if (try_read_reflected_object_property_value(game_over, L"m_Owner") != pause)
        {
            throw std::runtime_error("game_over_owner_mismatch");
        }
        const bool game_over_open = field<uint8_t>(game_over, 0xe8) == 1;
        if (dispatched)
        {
            const bool recovered =
                flying
                    ? (space_hp > 0 && RuntimeSpaceService::item_delivery_context() != nullptr)
                    : (is_uobject_valid_fast(cutscene_pawn()) && sora_ref.Get() != cutscene_pawn());
            if (!game_over_open && !pause_open && recovered)
            {
                if (continuing)
                {
                    continue_transition_observed = true;
                }
                else
                {
                    retry_transition_observed = true;
                }
                finish(L"completed", flying       ? L"native_retry_restored_flight"
                                     : continuing ? L"native_continue_replaced_sora"
                                                  : L"native_retry_replaced_sora");
            }
            return;
        }
        if ((flying ? space_pawn : pawn_name) != expected_pawn || !game_over_open || !pause_open ||
            !pause_ready || (flying && space_hp > 0))
        {
            finish(L"error", continuing ? L"continue_requires_expected_death_menu"
                                        : L"retry_requires_expected_death_menu");
            return;
        }
        const int type = field<int32_t>(game_over, 0x2b0);
        // Native Gummi free-flight restart (7) and battle Retry (6)
        // have distinct menu types and delegates from Sora Retry (2).
        if (flying)
        {
            if (type != gummi_free_flight_game_over_type && type != gummi_battle_game_over_type)
            {
                throw std::runtime_error("unsupported_flight_retry_menu");
            }
            requested_action = type == gummi_free_flight_game_over_type
                                   ? gummi_free_flight_retry_action
                                   : gummi_battle_retry_action;
        }
        if (type < 0 || type >= death_ui_kind_count ||
            !signature(base + death_ui.retry_handler,
                       {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x83, 0x7a, 0x10, 0x07}))
        {
            throw std::runtime_error("unsupported_retry_menu");
        }
        // Shipped GameOver actions: 0 loads Continue, 2 invokes the
        // Retry delegate. Select only the requested enabled action.
        int index = -1;
        for (int i = 0; i < game_over_buttons_per_type; ++i)
        {
            if (field<int32_t>(reinterpret_cast<void*>(base + death_ui.action_table),
                               type * game_over_action_row_bytes + i * sizeof(int32_t)) ==
                requested_action)
            {
                index = i;
            }
        }
        if (index < 0)
        {
            finish(L"error", continuing ? L"continue_not_offered" : L"retry_not_offered");
            return;
        }
        auto* button =
            field<UObject*>(game_over, game_over_buttons_offset + index * sizeof(UObject*));
        auto* as_object = try_read_reflected_object_property_value(button, L"m_ASObject");
        if (!is_uobject_valid_fast(as_object))
        {
            throw std::runtime_error("retry_button_unavailable");
        }
        using Enabled = bool(__fastcall*)(UObject*);
        if (!reinterpret_cast<Enabled>(base + death_ui.enabled_handler)(as_object))
        {
            finish(L"error", L"retry_button_disabled");
            return;
        }
        native_ui::InputEvent event{};
        const int32_t kind = native_ui::decide_event, action = requested_action;
        event.set_sender(as_object);
        event.set_kind(kind);
        event.set_anchor(index);
        event.set_parameter(action);
        auto* retry_pawn = cutscene_pawn();
        sora_ref = retry_pawn;
        if (!flying && (!is_uobject_valid_fast(retry_pawn) || sora_ref.Get() != retry_pawn))
        {
            throw std::runtime_error("retry_pawn_identity_unavailable");
        }
        dispatched = true;
        selected = requested_action;
        report(L"accepted", continuing ? L"dispatching_native_continue_selection"
                                       : L"dispatching_native_retry_selection");
        using Decide = bool(__fastcall*)(UObject*, void*);
        if (!reinterpret_cast<Decide>(base + death_ui.retry_handler)(game_over, event.data()) ||
            !event.handled())
        {
            throw std::runtime_error("retry_selection_not_observed");
        }
        return;
    }

    void RuntimeUiControlService::Impl::advance_space_game_over()
    {
        if (dispatched)
        {
            if (space_hp == 0 && pause_open && pause_ready)
            {
                game_over_observed = true;
                finish(L"completed", L"native_flight_game_over_observed");
            }
            return;
        }
        auto* controller = RuntimeSpaceService::item_delivery_context();
        auto* pawn = RuntimeSpaceService::diagnostic_flight_pawn();
        if (!controller || !pawn || space_pawn != expected_pawn || space_hp <= 0 || pause_open ||
            pause_type != 0)
        {
            throw std::runtime_error("flight_game_over_requires_expected_ready_pawn");
        }
        dispatched = true;
        report(L"accepted", L"dispatching_normal_flight_damage");
        damage_flight(pawn, controller);
        return;
    }

    void RuntimeUiControlService::Impl::advance_game_over()
    {
        if (dispatched)
        {
            if ((config.game_over_active() || runtime_sora_game_over(sora_ref.Get())) &&
                pause_open && pause_ready)
            {
                game_over_observed = true;
                finish(L"completed", L"native_game_over_observed");
            }
            return;
        }
        auto* pawn = cutscene_pawn();
        if (pawn_name != expected_pawn || !is_uobject_valid_fast(pawn) ||
            !config.gameplay_ready() || config.game_over_active() || pause_open || pause_type == 1)
        {
            finish(L"error", L"game_over_requires_expected_playable_sora");
            return;
        }
        sora_ref = pawn;
        dispatched = true;
        report(L"accepted", L"dispatching_normal_game_over");
        if (!config.request_game_over(pawn))
        {
            finish(L"error", L"game_over_dispatch_failed");
        }
        return;
    }

    void RuntimeUiControlService::Impl::advance_inspect()
    {
        if (!advance_player_selection())
        {
            finish(L"completed", L"ui_snapshot");
        }
    }

    void RuntimeUiControlService::Impl::advance_cutscene()
    {
        auto* pause = pause_ref.Get();
        if (dispatched)
        {
            if (command == L"resume_cutscene")
            {
                if (!pause_open)
                {
                    cutscene_engine_pause(cutscene_pawn(), false);
                    finish(L"completed", L"native_resume_observed");
                }
                return;
            }
            // A selected Skip can finish by travelling to another map/Sora.
            if (!pause_open && (pawn_name != expected_pawn || pause_type != 1 ||
                                (!scene_before.empty() && scene_key != scene_before)))
            {
                skip_transition_observed = true;
                finish(L"completed", L"native_skip_transition_observed");
            }
            return;
        }
        if (pawn_name != expected_pawn)
        {
            finish(L"error", L"pawn_changed");
            return;
        }
        if (pause_type != 1)
        {
            finish(L"error", L"cutscene_pause_not_available");
            return;
        }
        if (command == L"resume_cutscene" && !pause_open)
        {
            finish(L"error", L"cutscene_not_paused");
            return;
        }
        if (!pause_open)
        {
            if (opened)
            {
                return;
            }
            scene_before = scene_key;
            opened = true;
            report(L"accepted", L"opening_native_cutscene_pause");
            // Steam adds a force-open argument. False preserves the normal
            // game-instance pause rules; EGS has the original one-arg ABI.
            if (death_ui.steam)
            {
                using Open = void(__fastcall*)(UObject*, bool);
                reinterpret_cast<Open>(base + death_ui.open_pause)(manager_ref.Get(), false);
            }
            else
            {
                using Open = void(__fastcall*)(UObject*);
                reinterpret_cast<Open>(base + death_ui.open_pause)(manager_ref.Get());
            }
            return;
        }
        if (menu_type != 1)
        {
            finish(L"error", L"unexpected_pause_menu");
            return;
        }
        if (!pause_ready)
        {
            return;
        }
        if (command == L"pause_cutscene")
        {
            cutscene_engine_pause(cutscene_pawn(), true);
            finish(L"completed", L"native_cutscene_pause_observed");
            return;
        }
        if (!opened)
        {
            scene_before = scene_key;
        }
        // The game's cutscene menu table contains Resume=0, Skip=1. Use
        // its real button and enabled predicate, then the same
        // semantic Decide event as the menu. Animation completion owns
        // the skip delegate; never synthesize its OnCallback notification.
        const bool resume = command == L"resume_cutscene";
        auto* button =
            field<UObject*>(pause, resume ? pause_resume_button_offset : pause_other_button_offset);
        if (!is_uobject_valid_fast(button))
        {
            throw std::runtime_error("cutscene_button_unavailable");
        }
        auto* as_object = try_read_reflected_object_property_value(button, L"m_ASObject");
        if (!is_uobject_valid_fast(as_object))
        {
            throw std::runtime_error("cutscene_button_presentation_unavailable");
        }
        using Enabled = bool(__fastcall*)(UObject*);
        if (!reinterpret_cast<Enabled>(base + death_ui.enabled_handler)(as_object))
        {
            finish(L"error", L"skip_button_disabled");
            return;
        }
        native_ui::InputEvent event{};
        // Native Decide reads the semantic action at +0x18; +0x14 is
        // only the button index. Continue=0 and Skip=1 need both fields.
        const int32_t kind = native_ui::decide_event, index = resume ? 0 : 1,
                      action = resume ? 0 : 1;
        event.set_sender(as_object);
        event.set_kind(kind);
        event.set_anchor(index);
        event.set_parameter(action);
        dispatched = true;
        report(L"accepted", resume ? L"dispatching_native_resume_selection"
                                   : L"dispatching_native_skip_selection");
        using Decide = bool(__fastcall*)(UObject*, void*);
        if (field<uintptr_t>(reinterpret_cast<void*>(field<uintptr_t>(pause, 0)),
                             native_ui::input_handler_vtable_index * sizeof(void*)) !=
            base + death_ui.pause_decide)
        {
            throw std::runtime_error("skip_handler_build_mismatch");
        }
        const bool handled =
            reinterpret_cast<Decide>(base + death_ui.pause_decide)(pause, event.data());
        selected = field<int32_t>(pause, pause_selected_index_offset);
        if (!handled || selected != index || !event.handled())
        {
            throw std::runtime_error("skip_selection_not_observed");
        }
        report(L"accepted", resume ? L"resume_selected_waiting_for_native_close"
                                   : L"skip_selected_waiting_for_native_transition");
    }

} // namespace kh3ap
