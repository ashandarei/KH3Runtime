#include <array>
#include <chrono>
#include <utility>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/core/ui/ui_layout.h"
#include "kh3ap/features/world/space/space_service_impl.h"
#include "kh3ap/features/world/space/space_profile.h"
#include "kh3ap/features/world/space/space_policy.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/session/command_guard.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <algorithm>
#include <cstring>
#include <sstream>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        constexpr auto embark_confirm_button_id = 3;
        constexpr auto embark_cancel_button_id = 28;
        constexpr auto space_region_count = 3;
        constexpr auto landing_selected_index_offset = 0x3c8;
        constexpr auto landing_selection_locked_offset = 0x548;
        constexpr auto mission_menu_state_offset = 0x3d8;
        constexpr auto mission_list_menu_state = 3;
        constexpr auto mission_list_offset = 0x3e8;
        constexpr auto embark_ready_overview_state = 0x89;
        constexpr auto embark_selection_index_offset = 0x458;
        constexpr auto boss_cursor_kind = 3;
        constexpr auto schwarzgeist_cursor_id = 6;

        constexpr auto max_mission_notice_text_chars = 4096;
        constexpr auto max_mission_notice_text_capacity = 65536;
        constexpr auto max_space_diagnostic_rows = 128;
        constexpr auto space_mission_row_count = 46;
        constexpr auto max_space_mission_row_capacity = 256;
        constexpr auto max_landing_selection_index = 512;
        constexpr auto shadow_test_readback_delay = std::chrono::seconds(8);
        constexpr auto shadow_test_timeout = std::chrono::seconds(15);
        constexpr auto max_space_jump_tag_characters = 128;
        constexpr auto max_space_jump_map_characters = 256;
        constexpr auto minimum_space_jump_parameter_bytes = 17;
        constexpr auto maximum_space_jump_parameter_bytes = 24;
    } // namespace

    using space::native_profile;

    namespace
    {
        struct MissionNotice
        {
            UObject* panel{};
            int32_t id{-1}, type{-1}, pages{};
            bool ready{};
            std::wstring prompt;
        };
        MissionNotice mission_notice(UObject* menu)
        {
            MissionNotice result;
            auto* owner = try_read_reflected_object_property_value(menu, L"m_pUIManager");
            if (!is_uobject_valid_fast(owner))
            {
                return result;
            }
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            std::vector<UObject*> messages;
            UObjectGlobals::FindAllOf(L"TresUIP_Message", messages);
            for (auto* panel : messages)
            {
                if (!is_uobject_valid_fast(panel) ||
                    panel->GetPathName().find(L"Default__") != std::wstring::npos ||
                    try_read_reflected_object_property_value(panel, L"m_pUIManager") != owner)
                {
                    continue;
                }
                auto* bytes = reinterpret_cast<uint8_t*>(panel);
                if (!bytes[native_ui::panel_open_offset])
                {
                    continue;
                }
                if (result.panel ||
                    *reinterpret_cast<uintptr_t*>(bytes) != base + native_profile().message_vtable)
                {
                    return {};
                }
                result.panel = panel;
                result.id = *reinterpret_cast<int32_t*>(bytes + native_ui::message_id_offset);
                result.type = *reinterpret_cast<int32_t*>(bytes + native_ui::message_type_offset);
                result.pages =
                    *reinterpret_cast<int32_t*>(bytes + native_ui::message_page_count_offset);
                result.ready = bytes[native_ui::panel_ready_offset] != 0;
                const auto* text =
                    *reinterpret_cast<const wchar_t**>(bytes + native_ui::message_text_offset);
                const auto count = *reinterpret_cast<int32_t*>(bytes + 0x2a8),
                           capacity = *reinterpret_cast<int32_t*>(bytes + 0x2ac);
                if (!text || count < 1 || count > max_mission_notice_text_chars ||
                    capacity < count || capacity > max_mission_notice_text_capacity ||
                    text[count - 1] != L'\0')
                {
                    return {};
                }
                result.prompt.assign(text, count - 1);
            }
            return result;
        }
    } // namespace

    void RuntimeSpaceService::Impl::shadow_test_result(const wchar_t* status,
                                                       const std::wstring& reason)
    {
        if (!shadow_test)
        {
            return;
        }
        std::ostringstream result;
        result << "{\"request_id\":\"" << json_escape(shadow_test->request)
               << "\",\"process_id\":" << GetCurrentProcessId() << ",\"status\":\""
               << json_escape(status) << "\",\"reason\":\"" << json_escape(reason)
               << "\",\"presentation_only\":true,\"shadow_observed\":"
               << (shadow_test->shadow_observed ? "true" : "false") << ",\"before\":";
        write_flight_symbols(result, shadow_test->before);
        result << ",\"after\":";
        write_flight_symbols(result, flight_symbols);
        result << "}\n";
        write_text_file_atomic_verified(access.bridge / L"runtime_space_result.json", result.str());
    }

    void RuntimeSpaceService::Impl::poll_shadow_test(std::chrono::steady_clock::time_point now)
    {
        if (!shadow_test)
        {
            return;
        }
        auto& test = *shadow_test;
        if (test.world != current_world || test.seed != access.seed ||
            !is_uobject_valid_fast(test.actor.Get()) || now > test.deadline)
        {
            shadow_test_result(L"failed", L"shadow_test_context_changed_or_timed_out");
            shadow_test.reset();
            flight_refresh.request();
            return;
        }
        if (now >= test.release && !test.release_requested)
        {
            test.release_requested = true;
            flight_refresh.request();
            return;
        }
        const auto found = std::find_if(flight_symbols.begin(), flight_symbols.end(),
                                        [&](const auto& s)
                                        {
                                            return s.actor.Get() == test.actor.Get();
                                        });
        if (found == flight_symbols.end())
        {
            return;
        }
        if (now < test.release)
        {
            if (!test.shadow_observed && found->locked && !found->icon_visible &&
                found->mist_visible)
            {
                test.shadow_observed = true;
                shadow_test_result(L"running", L"shadow_visible_waiting_for_live_refresh");
            }
            return;
        }
        bool unchanged = flight_symbols.size() == test.before.size();
        for (const auto& before : test.before)
        {
            const auto after = std::find_if(flight_symbols.begin(), flight_symbols.end(),
                                            [&](const auto& s)
                                            {
                                                return s.actor.Get() == before.actor.Get();
                                            });
            unchanged = unchanged && after != flight_symbols.end() && before.actor.Get() != nullptr;
            if (after != flight_symbols.end())
            {
                unchanged =
                    unchanged && before.boss_locked == after->boss_locked &&
                    before.locked == after->locked && before.icon_visible == after->icon_visible &&
                    before.mist_visible == after->mist_visible && before.radius == after->radius;
            }
        }
        const bool passed = test.shadow_observed && !found->locked && found->icon_visible &&
                            !found->mist_visible && unchanged;
        shadow_test_result(
            passed ? L"completed" : L"failed",
            passed ? L"same_actor_revealed_without_travel_other_worlds_and_boss_locks_preserved"
                   : L"shadow_refresh_readback_failed");
        shadow_test.reset();
    }

    void RuntimeSpaceService::Impl::diagnostic_inspect_ships(DiagnosticRequest& request)
    {
        auto* context = ship_inventory_context();
        if (request.reason.empty() && (!context || !ships.inspect(context)))
        {
            request.reason = L"ready_ship_inventory_required";
        }
        std::ostringstream result;
        result << "{\"request_id\":\"" << json_escape(request.id)
               << "\",\"process_id\":" << GetCurrentProcessId() << ",\"status\":\""
               << (request.reason.empty() ? "completed" : "failed") << "\",\"reason\":\""
               << json_escape(request.reason) << "\",\"world\":\"" << json_escape(current_world)
               << "\",\"ships\":";
        ships.write_status(result, access);
        result << "}\n";
        write_text_file_atomic_verified(access.bridge / L"runtime_space_result.json", result.str());
        return;
    }
    void RuntimeSpaceService::Impl::publish_overview_result(DiagnosticRequest& request,
                                                            const OverviewSnapshot& snapshot)
    {
        auto* manager = snapshot.manager;
        auto* menu = snapshot.menu;
        auto* bytes = reinterpret_cast<uint8_t*>(manager);
        const int state = snapshot.initial_state;
        const int mode = snapshot.initial_mode;
        const bool dispatched = snapshot.dispatched;
        std::ostringstream result;
        const auto notice = state == 0x24 && mode == 3 && is_uobject_valid_fast(menu)
                                ? mission_notice(menu)
                                : MissionNotice{};
        constexpr GfxProbeFunctionPaths entry_gfx{L"/Script/ScaleformUI.GFxObject:GetMemberValue",
                                                  L"/Script/ScaleformUI.GFxObject:Invoke",
                                                  L"/Script/ScaleformUI.GFxObject:SetMemberValue"};
        auto* commands = try_gfx_get_member_value_object(
            entry_gfx, object_field(menu, L"m_pMenuCmdSet"), L"menuCmdSetObj");
        auto* embark_button = try_gfx_get_member_value_object(entry_gfx, commands, L"cmd1");
        result << "{\"request_id\":\"" << json_escape(request.id)
               << "\",\"process_id\":" << GetCurrentProcessId() << ",\"status\":\""
               << (request.reason.empty() ? "completed" : "failed") << "\",\"reason\":\""
               << json_escape(request.reason)
               << "\",\"dispatched\":" << (dispatched ? "true" : "false")
               << ",\"manager_state\":" << state << ",\"menu_mode\":" << mode
               << ",\"embark_help_hooked\":" << (embark_help.hook ? "true" : "false")
               << ",\"embark_selector_hooked\":" << (embark_selector.hook ? "true" : "false")
               << ",\"denied_embark_attempts\":" << denied_embark_attempts
               << ",\"selector_help_text\":\"" << json_escape(selector_help_text) << "\""
               << ",\"mission_menu_state\":" << native_profile().mission_menu_state
               << ",\"help_open\":"
               << (is_uobject_valid_fast(menu) &&
                           reinterpret_cast<uint8_t*>(menu)[landing_selection_locked_offset]
                       ? "true"
                       : "false")
               << ",\"mission_notice\":{\"id\":" << notice.id << ",\"type\":" << notice.type
               << ",\"pages\":" << notice.pages
               << ",\"ready\":" << (notice.ready ? "true" : "false") << ",\"prompt\":\""
               << json_escape(notice.prompt) << "\"}"
               << ",\"embark_selected\":" << (embark_selected(menu) ? "true" : "false")
               << ",\"embark_button_enabled\":\""
               << json_escape(try_gfx_get_member_value(entry_gfx, embark_button, L"enabled"))
               << "\""
               << ",\"help_text\":\"" << json_escape(read_embark_help(menu)) << "\""
               << ",\"region\":"
               << (is_uobject_valid_fast(manager)
                       ? *reinterpret_cast<int32_t*>(bytes + native_profile().overview_region)
                       : -1)
               << ",\"records\":[";
        if (is_uobject_valid_fast(manager))
        {
            auto* data =
                *reinterpret_cast<uint8_t**>(bytes + native_profile().overview_cursor_data);
            const auto count =
                *reinterpret_cast<int32_t*>(bytes + native_profile().overview_cursor_count);
            if (data && count > 0 && count <= max_space_diagnostic_rows)
            {
                for (int i = 0; i < count; ++i)
                {
                    auto* row = data + i * native_profile().cursor_stride;
                    if (i)
                    {
                        result << ',';
                    }
                    result << "{\"kind\":" << int(row[native_profile().cursor_kind]) << ",\"id\":"
                           << *reinterpret_cast<int32_t*>(row + native_profile().cursor_id)
                           << ",\"visible\":" << int(row[native_profile().cursor_visible])
                           << ",\"available\":" << int(row[native_profile().cursor_available])
                           << ",\"selectable\":" << int(row[native_profile().cursor_selectable])
                           << '}';
                }
            }
        }
        result << "],\"ui_components\":{";
        constexpr GfxProbeFunctionPaths gfx{L"/Script/ScaleformUI.GFxObject:GetMemberValue",
                                            L"/Script/ScaleformUI.GFxObject:Invoke",
                                            L"/Script/ScaleformUI.GFxObject:SetMemberValue"};
        const std::array<std::pair<const wchar_t*, const wchar_t*>, 3> components{
            {{L"commands", L"m_pMenuCmdSet"},
             {L"help", L"m_pMessage"},
             {L"missions", L"m_pMissionList"}}};
        for (size_t i = 0; i < components.size(); ++i)
        {
            if (i)
            {
                result << ',';
            }
            auto* component = object_field(menu, components[i].second);
            result << '\"' << json_escape(components[i].first) << "\":{\"visible\":\""
                   << json_escape(try_gfx_get_member_value(gfx, component, L"visible"))
                   << "\",\"alpha\":";
            const auto alpha = try_gfx_get_member_value_number(gfx, component, L"alpha");
            if (alpha)
            {
                result << *alpha;
            }
            else
            {
                result << "null";
            }
            result << '}';
        }
        auto* help_component = embark_help_field(menu);
        auto* help_field = try_gfx_get_member_value_object(gfx, help_component, L"textField");
        result << "},\"selector_help_layout\":{\"zone\":" << selected_space_zone(manager)
               << ",\"component\":\""
               << json_escape(try_gfx_get_member_value(gfx, help_component, L"textField")) << "\"";
        for (const auto* name : {L"apSpaceAccessTitle", L"apSpaceAccessRequirements"})
        {
            auto* line = try_gfx_get_member_value_object(gfx, help_component, name);
            result << ",\"" << json_escape(name)
                   << "\":{\"valid\":" << (is_uobject_valid_fast(line) ? "true" : "false");
            for (const auto* property :
                 {L"text", L"htmlText", L"visible", L"x", L"y", L"textHeight"})
            {
                result << ",\"" << json_escape(property) << "\":\""
                       << json_escape(try_gfx_get_member_value(gfx, line, property)) << "\"";
            }
            result << '}';
        }
        result << ",\"children\":\""
               << json_escape(try_gfx_get_member_value(gfx, help_component, L"numChildren"))
               << "\"";
        for (const auto* property :
             {L"visible", L"height", L"y", L"multiline", L"text", L"htmlText"})
        {
            result << ",\"" << json_escape(property) << "\":\""
                   << json_escape(try_gfx_get_member_value(gfx, help_field, property)) << "\"";
        }
        result << "},\"mission_display_indices\":[";
        // Native mission-list records are 96 bytes, sorted by their first
        // int (m_nDispIndex). No GFx placeholder is allocated for m_isUse=false.
        if (is_uobject_valid_fast(menu) &&
            *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(menu) +
                                        mission_menu_state_offset) == mission_list_menu_state)
        {
            auto* list = reinterpret_cast<uint8_t*>(menu) + mission_list_offset;
            auto* data = *reinterpret_cast<uint8_t**>(list);
            const int count = *reinterpret_cast<int32_t*>(list + 8),
                      capacity = *reinterpret_cast<int32_t*>(list + 12);
            if (data && count >= 0 && count <= space_mission_row_count && capacity >= count &&
                capacity <= max_space_mission_row_capacity)
            {
                for (int i = 0; i < count; ++i)
                {
                    if (i)
                    {
                        result << ',';
                    }
                    result << *reinterpret_cast<int32_t*>(data +
                                                          i * space::mission_display_record_stride);
                }
            }
        }
        result << "]}\n";
        write_text_file_atomic_verified(access.bridge / L"runtime_space_result.json", result.str());
        return;
    }
    void RuntimeSpaceService::Impl::diagnostic_overview(DiagnosticRequest& request)
    {
        auto* manager = overview_manager();
        auto* menu = object_field(object_field(manager, L"m_pGummiHUD"), L"m_pGummiMenu");
        auto* bytes = reinterpret_cast<uint8_t*>(manager);
        const int state = is_uobject_valid_fast(manager)
                              ? *reinterpret_cast<int32_t*>(bytes + native_profile().overview_state)
                              : -1;
        const int mode = is_uobject_valid_fast(menu)
                             ? *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(menu) + 0x3d8)
                             : -1;
        bool dispatched = false;
        if (request.reason.empty() &&
            (current_world != L"World /Game/Levels/wm/wm_01/wm_01.wm_01" ||
             !delivery_context(true) || !is_uobject_valid_fast(manager) ||
             !is_uobject_valid_fast(menu)))
        {
            request.reason = L"ready_overview_required";
        }
        if (request.reason.empty() && request.operation == L"overview_input")
        {
            const auto action = JsonFieldReader::string_field(request.text, "action").value_or(L"");
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const std::array<uint8_t, 24> signature{0x48, 0x89, 0x5c, 0x24, 0x18, 0x57, 0x48, 0x83,
                                                    0xec, 0x20, 0x8b, 0x42, 0x10, 0x48, 0x8b, 0xfa,
                                                    0x83, 0xc0, 0xfd, 0x48, 0x8b, 0xd9, 0x83, 0xf8};
            const bool open = action == L"embark" && state == 0x14 && mode == 0;
            const bool focus = (action == L"focus_embark" || action == L"focus_editor") &&
                               !native_profile().steam && state == 0x14 && mode == 0;
            const bool navigate =
                action == L"next_region" && state == native_profile().overview_idle_state &&
                mode == 9 &&
                *reinterpret_cast<int32_t*>(bytes + native_profile().overview_region_count) == 3;
            const bool information = action == L"information" && state == 0x14 && mode == 0;
            const bool missions = action == L"missions" && state == 0x1d && mode == 2;
            const bool cancel = action == L"cancel" &&
                                ((state == native_profile().mission_menu_state && mode == 3) ||
                                 (state == 0x1d && mode == 2));
            const bool notice = action == L"mission_notice" && state == 0x24 && mode == 3;
            const bool selector_input = !native_profile().steam && embark_selector.hook &&
                                        state == 55 && mode == 9 &&
                                        (action == L"confirm_embark" || action == L"cancel_embark");
            if ((!open && !navigate && !focus && !information && !missions && !cancel && !notice &&
                 !selector_input) ||
                std::memcmp(reinterpret_cast<void*>(base + native_profile().menu_input_rva),
                            signature.data(), signature.size()))
            {
                request.reason = L"expected_overview_menu_state_required";
            }
            else
            {
                if (selector_input)
                {
                    if (action == L"confirm_embark" && !locked_embark_selection(manager))
                    {
                        request.reason = L"locked_embark_destination_required_for_confirm_test";
                    }
                    else
                    {
                        const auto attempts = denied_embark_attempts;
                        embark_test_button = action == L"confirm_embark" ? embark_confirm_button_id
                                                                         : embark_cancel_button_id;
                        embark_selector_hook(manager);
                        embark_test_button = -1;
                        dispatched = action == L"confirm_embark"
                                         ? denied_embark_attempts == attempts + 1
                                         : *reinterpret_cast<int32_t*>(
                                               bytes + native_profile().overview_state) != state;
                    }
                }
                else if (notice)
                {
                    const auto active = mission_notice(menu);
                    const auto expected =
                        JsonFieldReader::int64_field(request.text, "expected_message_id")
                            .value_or(-1);
                    if (!active.panel || !active.ready || active.id != expected ||
                        active.type != 0 || active.pages != 1 || active.prompt.empty() ||
                        (*reinterpret_cast<uintptr_t**>(
                            active.panel))[native_ui::input_handler_vtable_index] !=
                            base + native_profile().message_input_rva ||
                        std::memcmp(
                            reinterpret_cast<void*>(base + native_profile().message_input_rva),
                            native_profile().message_input_bytes.data(),
                            native_profile().message_input_bytes.size()))
                    {
                        request.reason = L"expected_owned_mission_notice_required";
                    }
                    else
                    {
                        native_ui::InputEvent event{};
                        event.set_kind(native_ui::decide_event);
                        dispatched = reinterpret_cast<bool (*)(UObject*, void*)>(
                                         base + native_profile().message_input_rva)(active.panel,
                                                                                    event.data()) &&
                                     event.handled();
                    }
                }
                else if (navigate)
                {
                    // Region navigation is consumed by the manager, not
                    // the GFx tab event. Use its normal cache/UI refresh
                    // with the next already-unlocked region as a fixture.
                    const int previous =
                        *reinterpret_cast<int32_t*>(bytes + native_profile().overview_region);
                    if (previous < 0 || previous >= space_region_count ||
                        std::memcmp(
                            reinterpret_cast<void*>(base + native_profile().refresh_overview_rva),
                            native_profile().refresh_overview_bytes.data(),
                            native_profile().refresh_overview_bytes.size()))
                    {
                        request.reason = L"overview_region_layout_mismatch";
                    }
                    else
                    {
                        *reinterpret_cast<int32_t*>(bytes + native_profile().overview_region) =
                            (previous + 1) % space_region_count;
                        dispatched =
                            refresh_overview_native(manager) &&
                            *reinterpret_cast<int32_t*>(bytes + native_profile().overview_region) ==
                                (previous + 1) % space_region_count;
                        refresh_selector_help(manager);
                    }
                }
                else
                {
                    native_ui::InputEvent event{};
                    // Main Information (3), then its Gummi Missions (0),
                    // use the same confirm/cancel events as GFx input.
                    // Let the owning manager hide parents, run animations,
                    // and enter the state that consumes the panel's Cancel.
                    const int index = information ? 3 : action == L"focus_editor" ? 1 : 0;
                    event.set_kind(focus    ? native_ui::focus_event
                                   : cancel ? native_ui::cancel_event
                                            : native_ui::decide_event);
                    event.set_parameter(index);
                    const bool handled = reinterpret_cast<bool (*)(UObject*, void*)>(
                        base + native_profile().menu_input_rva)(menu, event.data());
                    dispatched = focus ? handled && *reinterpret_cast<int32_t*>(
                                                        reinterpret_cast<uint8_t*>(menu) +
                                                        landing_selected_index_offset) == index
                                       : event.handled();
                }
                if (!dispatched)
                {
                    request.reason = L"native_overview_event_not_handled";
                }
            }
        }
        publish_overview_result(request, {manager, menu, state, mode, dispatched});
    }
    void RuntimeSpaceService::Impl::diagnostic_landing(DiagnosticRequest& request)
    {
        auto* manager = landing_manager();
        auto* hud = object_field(manager, L"m_pGummiSpaceHUD");
        auto* menu = object_field(hud, L"m_pGummiMenu");
        const auto code = JsonFieldReader::string_field(request.text, "world_code").value_or(L"");
        bool dispatched = false;
        if (request.reason.empty() && request.operation == L"open_landing")
        {
            const auto symbol = std::find_if(flight_symbols.begin(), flight_symbols.end(),
                                             [&](const auto& s)
                                             {
                                                 return s.code == code;
                                             });
            if (!delivery_context(true) || !is_uobject_valid_fast(manager) ||
                *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(manager) +
                                            native_profile().overview_state) !=
                    embark_ready_overview_state ||
                symbol == flight_symbols.end() || !can_land(code) || symbol->locked ||
                symbol->boss_locked)
            {
                request.reason = L"owned_world_in_idle_flight_required";
            }
            else
            {
                dispatched = landing_hook(symbol->actor.Get());
            }
            if (request.reason.empty() && !dispatched)
            {
                request.reason = L"native_landing_not_opened";
            }
        }
        if (request.reason.empty() && request.operation == L"landing_input")
        {
            const auto action = JsonFieldReader::string_field(request.text, "action").value_or(L"");
            const int state =
                is_uobject_valid_fast(manager)
                    ? *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(manager) +
                                                  native_profile().overview_state)
                    : -1;
            const auto index = JsonFieldReader::int64_field(request.text, "index").value_or(0);
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const std::array<uint8_t, 24> signature{0x48, 0x89, 0x5c, 0x24, 0x18, 0x57, 0x48, 0x83,
                                                    0xec, 0x20, 0x8b, 0x42, 0x10, 0x48, 0x8b, 0xfa,
                                                    0x83, 0xc0, 0xfd, 0x48, 0x8b, 0xd9, 0x83, 0xf8};
            const int mode =
                is_uobject_valid_fast(menu)
                    ? *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(menu) + 0x3d8)
                    : -1;
            const bool list = state == 0x92 && mode == 10;
            const bool destination_confirmation =
                (state == 0x97 && mode == 16) || (state == 0x8f && mode == 15);
            if ((action != L"confirm" && action != L"cancel") ||
                (!list && !destination_confirmation) || index < 0 ||
                index > max_landing_selection_index ||
                (list && action == L"confirm" &&
                 index >= *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(menu) +
                                                      embark_selection_index_offset)) ||
                std::memcmp(reinterpret_cast<void*>(base + native_profile().menu_input_rva),
                            signature.data(), signature.size()) != 0)
            {
                request.reason = L"active_save_point_list_and_valid_choice_required";
            }
            else
            {
                // The same semantic event consumed by the native GFx panel.
                // Area-list confirmation carries its visible row at +0x28.
                native_ui::InputEvent event{};
                if (destination_confirmation)
                {
                    // Both landing confirmations use a TresUIP_Dialog child,
                    // whose real completion delegate carries No=0 / Yes=1.
                    UObject* choice = nullptr;
                    std::vector<UObject*> dialogs;
                    UObjectGlobals::FindAllOf(L"TresUIP_Dialog", dialogs);
                    for (auto* dialog : dialogs)
                    {
                        if (!is_uobject_valid_fast(dialog) ||
                            object_field(dialog, L"m_pUIManager") !=
                                object_field(menu, L"m_pUIManager"))
                        {
                            continue;
                        }
                        auto* bytes = reinterpret_cast<uint8_t*>(dialog);
                        if (!bytes[native_ui::panel_open_offset] ||
                            !bytes[native_ui::panel_ready_offset])
                        {
                            continue;
                        }
                        if (choice)
                        {
                            choice = nullptr;
                            break;
                        }
                        choice = dialog;
                    }
                    const std::array<uint8_t, 10> dialog_signature{0x40, 0x57, 0x48, 0x83, 0xec,
                                                                   0x20, 0x83, 0x7a, 0x10, 0x07};
                    if (choice &&
                        (*reinterpret_cast<uintptr_t**>(
                            choice))[native_ui::input_handler_vtable_index] ==
                            base + native_profile().choice_input_rva &&
                        !std::memcmp(
                            reinterpret_cast<void*>(base + native_profile().choice_input_rva),
                            dialog_signature.data(), dialog_signature.size()))
                    {
                        event.set_kind(native_ui::decide_event);
                        event.set_parameter(action == L"confirm" ? 1 : 0);
                        reinterpret_cast<bool (*)(UObject*, void*)>(
                            base + native_profile().choice_input_rva)(choice, event.data());
                    }
                }
                else
                {
                    event.set_kind(action == L"cancel" ? native_ui::cancel_event
                                                       : native_ui::decide_event);
                    event.set_parameter(static_cast<int32_t>(index));
                    event.set_selection(static_cast<int32_t>(index));
                    reinterpret_cast<bool (*)(UObject*, void*)>(
                        base + native_profile().menu_input_rva)(menu, event.data());
                }
                dispatched = event.handled();
                if (!dispatched)
                {
                    request.reason = L"native_menu_event_not_handled";
                }
            }
        }
        std::ostringstream result;
        result << "{\"request_id\":\"" << json_escape(request.id)
               << "\",\"process_id\":" << GetCurrentProcessId() << ",\"status\":\""
               << (request.reason.empty() ? "completed" : "failed") << "\",\"reason\":\""
               << json_escape(request.reason)
               << "\",\"dispatched\":" << (dispatched ? "true" : "false") << ",\"manager\":\""
               << (is_uobject_valid_fast(manager) ? json_escape(manager->GetFullName()) : "")
               << "\",\"manager_state\":"
               << (is_uobject_valid_fast(manager)
                       ? *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(manager) +
                                                     native_profile().overview_state)
                       : -1)
               << ",\"menu\":\""
               << (is_uobject_valid_fast(menu) ? json_escape(menu->GetFullName()) : "")
               << "\",\"landing\":";
        landing_ui.write_status(result);
        result << "}\n";
        write_text_file_atomic_verified(access.bridge / L"runtime_space_result.json", result.str());
        return;
    }
    void RuntimeSpaceService::Impl::diagnostic_flight_shadow(DiagnosticRequest& request)
    {
        const auto code = JsonFieldReader::string_field(request.text, "world_code").value_or(L"");
        const auto found = std::find_if(flight_symbols.begin(), flight_symbols.end(),
                                        [&](const auto& s)
                                        {
                                            return s.code == code;
                                        });
        if (request.reason.empty() &&
            (shadow_test || !delivery_context(true) || !can_land(code) ||
             found == flight_symbols.end() || !is_uobject_valid_fast(found->actor.Get()) ||
             found->locked || found->boss_locked || !found->icon_visible || found->mist_visible))
        {
            request.reason = L"owned_revealed_world_in_ready_flight_required";
        }
        if (request.reason.empty())
        {
            const auto now = std::chrono::steady_clock::now();
            // Presentation-only fixture: the AP access set and receipt
            // ledger remain authoritative and are never changed here.
            shadow_test = ShadowTest{request.id,
                                     current_world,
                                     access.seed,
                                     code,
                                     found->actor,
                                     flight_symbols,
                                     now + shadow_test_readback_delay,
                                     now + shadow_test_timeout,
                                     false};
            refresh_flight_symbols(now);
            shadow_test_result(L"running", L"waiting_for_shadow_readback");
            poll_shadow_test(now);
        }
        else
        {
            std::ostringstream result;
            result << "{\"request_id\":\"" << json_escape(request.id)
                   << "\",\"process_id\":" << GetCurrentProcessId()
                   << ",\"status\":\"failed\",\"reason\":\"" << json_escape(request.reason)
                   << "\"}\n";
            write_text_file_atomic_verified(access.bridge / L"runtime_space_result.json",
                                            result.str());
        }
        return;
    }
    void RuntimeSpaceService::Impl::diagnostic_boss_cursor(DiagnosticRequest& request)
    {
        auto* manager = overview_manager();
        auto* bytes = reinterpret_cast<uint8_t*>(manager);
        if (request.reason.empty() &&
            (!is_uobject_valid_fast(manager) || !access.allowed.contains(L"bx") ||
             current_world != L"World /Game/Levels/wm/wm_01/wm_01.wm_01" ||
             *reinterpret_cast<int32_t*>(bytes + native_profile().overview_state) !=
                 native_profile().overview_idle_state ||
             *reinterpret_cast<int32_t*>(bytes + native_profile().overview_region) != 1))
        {
            request.reason = L"owned_bx_in_idle_misty_stream_required";
        }
        auto bx_record = [&]() -> uint8_t*
        {
            auto* data =
                *reinterpret_cast<uint8_t**>(bytes + native_profile().overview_cursor_data);
            const auto count =
                *reinterpret_cast<int32_t*>(bytes + native_profile().overview_cursor_count);
            if (!data || count < 1 || count > max_space_diagnostic_rows)
            {
                return nullptr;
            }
            for (int i = 0; i < count; ++i)
            {
                auto* row = data + native_profile().cursor_stride * i;
                if (row[native_profile().cursor_kind] == boss_cursor_kind &&
                    *reinterpret_cast<int32_t*>(row + native_profile().cursor_id) ==
                        schwarzgeist_cursor_id)
                {
                    return row;
                }
            }
            return nullptr;
        };
        bool restored = false, cursor_preserved = false;
        if (request.reason.empty())
        {
            auto* row = bx_record();
            if (!row || row[native_profile().cursor_visible] != 1 ||
                row[native_profile().cursor_available] != 1 ||
                row[native_profile().cursor_selectable] != 1)
            {
                request.reason = L"bx_cursor_record_unavailable";
            }
            else
            {
                const auto selected =
                    *reinterpret_cast<int32_t*>(bytes + native_profile().overview_selected_cursor);
                // Reproduce only the stale presentation cache. Inventory,
                // AP receipts, discovery and save state are untouched.
                row[native_profile().cursor_visible] = row[native_profile().cursor_available] =
                    row[native_profile().cursor_selectable] = 0;
                const auto before = overview_refreshes;
                refresh_open_overview(manager);
                row = bx_record();
                restored = row && row[native_profile().cursor_visible] == 1 &&
                           row[native_profile().cursor_available] == 1 &&
                           row[native_profile().cursor_selectable] == 1 &&
                           overview_refreshes == before + 1;
                cursor_preserved =
                    *reinterpret_cast<int32_t*>(bytes + native_profile().overview_region) == 1 &&
                    *reinterpret_cast<int32_t*>(
                        bytes + native_profile().overview_selected_cursor) == selected &&
                    *reinterpret_cast<int32_t*>(bytes + native_profile().overview_state) ==
                        native_profile().overview_idle_state;
                if (row && !restored)
                {
                    row[native_profile().cursor_visible] = row[native_profile().cursor_available] =
                        row[native_profile().cursor_selectable] = 1;
                }
                if (!restored || !cursor_preserved)
                {
                    request.reason = L"cursor_refresh_readback_failed";
                }
            }
        }
        std::ostringstream result;
        result << "{\"request_id\":\"" << json_escape(request.id)
               << "\",\"process_id\":" << GetCurrentProcessId() << ",\"status\":\""
               << (request.reason.empty() ? "completed" : "failed") << "\",\"reason\":\""
               << json_escape(request.reason)
               << "\",\"stale_cache_restored\":" << (restored ? "true" : "false")
               << ",\"region_and_cursor_preserved\":" << (cursor_preserved ? "true" : "false")
               << "}\n";
        write_text_file_atomic_verified(access.bridge / L"runtime_space_result.json", result.str());
        return;
    }
    void RuntimeSpaceService::Impl::diagnostic_preview_notification(DiagnosticRequest& request)
    {
        if (request.reason.empty() &&
            (!access.items.contains(L"Nano Gear") || !delivery_context(true)))
        {
            request.reason = L"owned_nano_gear_in_ready_space_required";
        }
        if (request.reason.empty())
        {
            notifications.enqueue(L"WEP_KEYBLADE_SO_08");
        }
        std::ostringstream result;
        result << "{\"request_id\":\"" << json_escape(request.id)
               << "\",\"process_id\":" << GetCurrentProcessId() << ",\"status\":\""
               << (request.reason.empty() ? "completed" : "failed") << "\",\"reason\":\""
               << json_escape(request.reason) << "\",\"presentation_only\":true}\n";
        write_text_file_atomic_verified(access.bridge / L"runtime_space_result.json", result.str());
        return;
    }
    void RuntimeSpaceService::Impl::diagnostic_jump(DiagnosticRequest& request)
    {

        const auto code = space::destination_code(request.map);
        if (request.reason.empty() &&
            (request.tag.empty() || request.tag.size() > max_space_jump_tag_characters ||
             request.map.size() > max_space_jump_map_characters ||
             (request.map != space::garden && code.empty())))
        {
            request.reason = L"invalid_destination";
        }
        if (request.reason.empty())
        {
            auto* library = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary");
            auto* function = is_uobject_valid_fast(library)
                                 ? library->GetFunctionByNameInChain(L"TresMapJump")
                                 : nullptr;
            if (!is_uobject_valid_fast(function) ||
                function->GetParmsSize() < minimum_space_jump_parameter_bytes ||
                function->GetParmsSize() > maximum_space_jump_parameter_bytes)
            {
                request.reason = L"jump_layout_unavailable";
            }
            else
            {
                alignas(uint64_t) std::array<std::byte, maximum_space_jump_parameter_bytes>
                    params{};
                if (!write_name_or_string_function_param(
                        find_function_param_by_name(function, L"MapName"), params.data(),
                        request.map) ||
                    !write_name_or_string_function_param(
                        find_function_param_by_name(function, L"TargetTag"), params.data(),
                        request.tag) ||
                    !write_integral_function_param(
                        find_function_param_by_name(function, L"InFadeKind"), params.data(), 1))
                {
                    request.reason = L"jump_layout_mismatch";
                }
                else
                {
                    const auto denied_before = denied_landings;
                    const auto ship_denied_before = denied_ship_travel;
                    library->ProcessEvent(function, params.data());
                    if (denied_ship_travel != ship_denied_before)
                    {
                        request.reason = L"ap_ship_blueprint_required";
                    }
                    else if (denied_landings != denied_before)
                    {
                        request.reason = L"ap_world_access_denied";
                    }
                }
            }
        }
        std::ostringstream result;
        result << "{\"request_id\":\"" << json_escape(request.id)
               << "\",\"process_id\":" << GetCurrentProcessId() << ",\"status\":\""
               << (request.reason.empty() ? "completed" : "failed") << "\",\"reason\":\""
               << json_escape(request.reason) << "\"}\n";
        write_text_file_atomic_verified(access.bridge / L"runtime_space_result.json", result.str());
    }
    void RuntimeSpaceService::Impl::process_command()
    {
        const auto path = access.bridge / L"runtime_space_request.json";
        const auto text = read_text_file(path);
        if (!text)
        {
            return;
        }
        const auto request = JsonFieldReader::string_field(*text, "request_id").value_or(L"");
        const auto map = JsonFieldReader::string_field(*text, "map_name").value_or(L"");
        const auto tag = JsonFieldReader::string_field(*text, "target_tag").value_or(L"");
        const auto expected = JsonFieldReader::string_field(*text, "expected_world").value_or(L"");
        std::wstring reason(
            runtime_command_request_failure(*text, GetCurrentProcessId(),
                                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::system_clock::now().time_since_epoch())
                                                .count()));
        if (reason.empty() &&
            (request.empty() || !JsonFieldReader::int64_field(*text, "process_id")))
        {
            reason = L"request_identity_required";
        }
        const auto operation = JsonFieldReader::string_field(*text, "command").value_or(L"");
        if (reason.empty() &&
            (current_world != expected ||
             (operation != L"inspect_ships" &&
              (!space::is_space(current_world) || !controller_ready || !loading_closed))))
        {
            reason = L"expected_ready_space_required";
        }
        DiagnosticRequest command_request{*text, request, operation, map, tag, path, reason};
        if (operation == L"inspect_ships")
        {
            diagnostic_inspect_ships(command_request);
        }
        else if (operation == L"battle_test")
        {
            battle_command(*text, request, reason);
        }
        else if (operation == L"overview_input" || operation == L"inspect_overview")
        {
            diagnostic_overview(command_request);
        }
        else if (operation == L"open_landing" || operation == L"landing_input" ||
                 operation == L"inspect_landing")
        {
            diagnostic_landing(command_request);
        }
        else if (operation == L"test_flight_world_shadow")
        {
            diagnostic_flight_shadow(command_request);
        }
        else if (operation == L"test_bx_cursor_refresh")
        {
            diagnostic_boss_cursor(command_request);
        }
        else if (operation == L"preview_nano_gear_notification")
        {
            diagnostic_preview_notification(command_request);
        }
        else
        {
            diagnostic_jump(command_request);
        }
        try_remove_file(path);
    }

} // namespace kh3ap
#endif
