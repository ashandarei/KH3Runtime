#include "service_impl.h"
#include "helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/ui/ui_layout.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/features/kh02/runtime/kh02_runtime.h"
#include "kh3ap/features/kh02/core/kh02_profile.h"
#include "kh3ap/features/kh02/core/kh02_voice_language.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/transport/kh02_remote_files.h"
#include "kh3ap/features/kh02/transport/kh02_asset_companion.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UObjectGlobals.hpp>
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

    void RuntimeUiControlService::Impl::advance_kh02_remote()
    {
        if (texture_action == L"overlay_inspect" || texture_action == L"overlay_toggle")
        {
            auto* hero = Kh02Story::current_hero();
            if (!is_uobject_valid_fast(hero))
            {
                hero = config.sora();
            }
            if (!is_uobject_valid_fast(hero) || hero->GetFullName() != expected_pawn ||
                !config.map_overlay_probe)
            {
                throw std::runtime_error("map_overlay_expected_current_hero");
            }
            remote_json = config.map_overlay_probe(hero, texture_action == L"overlay_toggle");
            finish(L"completed", L"map_overlay_snapshot");
            return;
        }
        if (texture_action.starts_with(L"story_"))
        {
            const auto action =
                std::wstring_view(texture_action).substr(std::wstring_view{L"story_"}.size());
            auto* hero = Kh02Story::current_hero();
            if (action.starts_with(L"within_") &&
                (!is_uobject_valid_fast(hero) || hero->GetFullName() != expected_pawn))
            {
                throw std::runtime_error("kh02_expected_current_hero");
            }
            if (action.starts_with(L"flow_chest_") &&
                (!is_uobject_valid_fast(hero) || hero->GetFullName() != expected_pawn ||
                 pause_open))
            {
                throw std::runtime_error("kh02_chest_requires_current_unpaused_hero");
            }
            if (action.starts_with(L"flow_stars_") &&
                (!is_uobject_valid_fast(hero) || hero->GetFullName() != expected_pawn ||
                 pause_open))
            {
                throw std::runtime_error("kh02_stars_requires_current_unpaused_hero");
            }
            if (action.starts_with(L"flow_castle_trigger_") &&
                (!is_uobject_valid_fast(hero) || hero->GetFullName() != expected_pawn ||
                 pause_open))
            {
                throw std::runtime_error("kh02_trigger_requires_current_unpaused_hero");
            }
            if ((action == L"playtest_reset" || action == L"load" || action == L"start" ||
                 action == L"flow_prepare" || action.starts_with(L"flow_load") ||
                 action == L"flow_start" || action == L"flow_button") &&
                (pawn_name != expected_pawn || !config.gameplay_ready() || pause_open))
            {
                throw std::runtime_error("kh02_story_requires_idle_sora");
            }
            if (auto result = config.kh02_runtime.story_command(config.sora(), action))
            {
                remote_json = *result;
                finish(L"completed", L"kh02_story_snapshot");
            }
            return;
        }
        if (pawn_name != expected_pawn || !config.gameplay_ready() || pause_open)
        {
            throw std::runtime_error("remote_files_requires_idle_sora");
        }
        if (texture_action == L"enable")
        {
            remote_json = enable_kh02_remote_files(remote_pid);
        }
        else if (texture_action == L"inspect")
        {
            remote_json = kh02_remote_files_status();
        }
        else if (texture_action == L"stream_benchmark")
        {
            if (pawn_name.find(L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.") == std::wstring::npos)
            {
                throw std::runtime_error("kh02_stream_benchmark_requires_garden");
            }
            remote_json = kh02_remote_files_benchmark();
        }
        else if (texture_action == L"stream_cache_on" || texture_action == L"stream_cache_off")
        {
            if (pawn_name.find(L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.") == std::wstring::npos)
            {
                throw std::runtime_error("kh02_cache_configuration_requires_garden");
            }
            remote_json = configure_kh02_export_cache(texture_action == L"stream_cache_on");
        }
        else if (texture_action == L"stream_ranges_on" || texture_action == L"stream_ranges_off")
        {
            if (pawn_name.find(L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.") == std::wstring::npos)
            {
                throw std::runtime_error("kh02_cache_configuration_requires_garden");
            }
            remote_json = configure_kh02_range_cache(texture_action == L"stream_ranges_on");
        }
        else if (texture_action == L"stream_async_on" || texture_action == L"stream_async_off")
        {
            if (pawn_name.find(L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.") == std::wstring::npos)
            {
                throw std::runtime_error("kh02_async_configuration_requires_garden");
            }
            remote_json = configure_kh02_async_reads(texture_action == L"stream_async_on");
        }
        else if (texture_action == L"companion_start")
        {
            remote_json = start_kh02_asset_companion(remote_executable);
        }
        else if (texture_action == L"companion_inspect")
        {
            remote_json = kh02_asset_companion_status();
        }
        else if (texture_action == L"portal_inspect" || texture_action == L"portal_interact" ||
                 texture_action == L"portal_button")
        {
            remote_json = config.kh02_runtime.portal_command(cutscene_pawn(), texture_action);
        }
        else if (texture_action == L"companion_stop")
        {
            if (pawn_name.find(L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.") == std::wstring::npos)
            {
                throw std::runtime_error("companion_stop_requires_garden");
            }
            remote_json = stop_kh02_asset_companion();
        }
        else if (texture_action == L"screenshot")
        {
            remote_texture_screenshot(config.sora());
            remote_json = kh02_remote_files_status();
        }
        else
        {
            throw std::runtime_error("remote_files_invalid_action");
        }
        finish(L"completed", L"remote_files_snapshot");
        return;
    }

    void RuntimeUiControlService::Impl::advance_voice_language()
    {
        if (command != L"kh02_voice_inspect" && is_uobject_valid_fast(config.sora()))
        {
            finish(L"error", L"voice_fixture_requires_title");
            return;
        }
        const auto language = command == L"kh02_voice_en"   ? L"en"
                              : command == L"kh02_voice_ja" ? L"ja"
                                                            : L"inspect";
        remote_json = kh02_voice_language_fixture(language).dump();
        finish(L"completed", L"kh02_voice_language");
        return;
    }

    void RuntimeUiControlService::Impl::advance_frozen_ground()
    {
        if (pawn_name != expected_pawn ||
            pawn_name.find(L"/Game/Levels/fz/fz_02/fz_02.") == std::wstring::npos ||
            !config.gameplay_ready())
        {
            throw std::runtime_error("frozen_ground_requires_playable_labyrinth_sora");
        }
        auto* pawn = config.sora();
        auto* movement = try_read_reflected_object_property_value(pawn, L"MyMovement");
        std::ostringstream evidence;
        evidence << "{";
        for (const auto name : {L"IsMovingOnGround", L"IsFalling"})
        {
            UiProbeCall call(movement, name);
            call.run();
            auto* result = find_function_param_by_name(call.function, L"ReturnValue");
            if (!result || !result->IsA<FBoolProperty>())
            {
                throw std::runtime_error("frozen_movement_result_layout");
            }
            evidence << '"' << json_escape(name) << "\":" << std::boolalpha
                     << static_cast<FBoolProperty*>(result)->GetPropertyValueInContainer(
                            call.params.data())
                     << ',';
        }
        bool first = true;
        for (const auto name : {L"K2_GetActorLocation", L"GetVelocity"})
        {
            UiProbeCall call(pawn, name);
            call.run();
            auto* result = find_function_param_by_name(call.function, L"ReturnValue");
            if (!result || result->GetElementSize() != sizeof(std::array<float, 3>))
            {
                throw std::runtime_error("frozen_vector_result_layout");
            }
            std::array<float, 3> vector{};
            std::memcpy(vector.data(), result->ContainerPtrToValuePtr<void>(call.params.data()),
                        sizeof(std::array<float, 3>));
            if (!first)
            {
                evidence << ',';
            }
            first = false;
            evidence << '"' << json_escape(name) << "\":[" << vector[0] << ',' << vector[1] << ','
                     << vector[2] << ']';
        }
        evidence << '}';
        command_json = evidence.str();
        finish(L"completed", L"frozen_ground_snapshot");
        return;
    }

    void RuntimeUiControlService::Impl::advance_garden_travel()
    {
        const bool graveyard = command == L"garden_graveyard_travel";
        const wchar_t* player_start = graveyard ? L"kg_02_Lv_Save_01" : L"fz_02_Lv_AP_Save_01";
        if (pawn_name != expected_pawn ||
            pawn_name.find(L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.") == std::wstring::npos)
        {
            throw std::runtime_error("garden_probe_requires_current_garden_sora");
        }
        auto* widget = frozen_menu_widget();
        if (!opened)
        {
            if (!config.gameplay_ready() || pause_open || pause_type != 0 || widget)
            {
                throw std::runtime_error("garden_probe_requires_idle_gameplay");
            }
            auto* portal = frozen_garden_portal(graveyard ? L"gameflow_KG" : L"gameflow_FZ");
            UiProbeCall interact(portal, L"BndEvt__TresReactor_K2Node_ComponentBoundEvent_0_"
                                         L"TresReactorDoCommandSignature__DelegateSignature");
            if (!write_object_function_param(
                    interact.function, interact.params.data(), L"ReactorComponent",
                    try_read_reflected_object_property_value(portal, L"TresReactor")) ||
                !write_object_function_param(interact.function, interact.params.data(),
                                             L"CommandCauser", config.sora()))
            {
                throw std::runtime_error("garden_portal_interaction_layout");
            }
            interact.run();
            opened = true;
            return;
        }
        if (!is_uobject_valid_fast(widget))
        {
            return;
        }
        const auto prefix = try_read_object_name_property(widget, L"WorldPrefix");
        if (!prefix ||
            _wcsnicmp(prefix->c_str(), graveyard ? L"KG" : L"FZ", std::wstring_view{L"KG"}.size()))
        {
            throw std::runtime_error("garden_world_menu_prefix");
        }
        const auto rows = frozen_menu_rows(widget);
        std::ostringstream evidence;
        evidence << "{\"" << (graveyard ? "gameflow_KG" : "gameflow_FZ") << "\":"
                 << selection_game_flag(graveyard ? L"gameflow_KG" : L"gameflow_FZ").value_or(-1)
                 << ",\"rows\":[";
        bool first = true, found = false;
        for (const auto row : rows)
        {
            if (!first)
            {
                evidence << ',';
            }
            first = false;
            evidence << '"' << json_escape(row.ToString()) << '"';
            if (row.ToString() == (graveyard ? player_start : L"FZ_SAVE08"))
            {
                found = true;
            }
        }
        evidence << "],\"custom_present\":" << std::boolalpha << found << ",\"player_start\":\""
                 << json_escape(player_start) << "\"}";
        command_json = evidence.str();
        if (command == L"garden_frozen_travel" || graveyard)
        {
            if (!found)
            {
                throw std::runtime_error("garden_frozen_destination_not_listed");
            }
            select_frozen_menu_row(widget, graveyard ? L"KG_SAVE02" : L"FZ_SAVE08", player_start);
            dispatched = true;
            selected = graveyard ? graveyard_gate_command_index : arendelle_gate_command_index;
            finish(L"completed", graveyard ? L"garden_graveyard_widget_selection_dispatched"
                                           : L"garden_frozen_widget_selection_dispatched");
            return;
        }
        UiProbeCall close(widget, L"Close Menu");
        close.run();
        finish(L"completed", L"garden_frozen_menu_inspected");
        return;
    }

    void RuntimeUiControlService::Impl::advance_garden_dialog(UObject* dialog)
    {
        const bool dark_world =
            command == L"garden_dark_world_inspect" || command == L"garden_dark_world_enter";
        if (pawn_name != expected_pawn ||
            pawn_name.find(dark_world ? L"/Game/Maps/GardenOfAssemblage/umap/GoA_01."
                                      : L"/Game/Levels/kg/kg_02/kg_02.") == std::wstring::npos)
        {
            throw std::runtime_error("battlegate_zero_requires_normal_kg02_sora");
        }
        // Closed dialogs can remain alive until the next GC. Select
        // only the active dialog owned by this session's UI manager.
        UObject* choice = nullptr;
        std::vector<UObject*> dialogs;
        UObjectGlobals::FindAllOf(L"TresUIP_Dialog", dialogs);
        for (auto* dialog : dialogs)
        {
            if (!is_uobject_valid_fast(dialog) ||
                dialog->GetPathName().find(L"Default__") != std::wstring::npos ||
                !field<uint8_t>(dialog, native_ui::panel_open_offset) ||
                !field<uint8_t>(dialog, native_ui::panel_ready_offset) ||
                try_read_reflected_object_property_value(dialog, L"m_pUIManager") !=
                    manager_ref.Get())
            {
                continue;
            }
            if (choice)
            {
                throw std::runtime_error("ambiguous_active_garden_dialog");
            }
            choice = dialog;
        }
        if (!is_uobject_valid_fast(choice) ||
            !field<uint8_t>(choice, native_ui::panel_open_offset) ||
            !field<uint8_t>(choice, native_ui::panel_ready_offset) ||
            try_read_reflected_object_property_value(choice, L"m_pUIManager") != manager_ref.Get())
        {
            throw std::runtime_error("battlegate_zero_ready_choice_required");
        }
        const auto read_text = [](const void* data)
        {
            const auto count = field<int32_t>(data, 8), capacity = field<int32_t>(data, 12);
            const auto* chars = field<const wchar_t*>(data, 0);
            if (!chars || count < 1 || count > max_message_text_characters || capacity < count ||
                capacity > max_message_text_capacity)
            {
                throw std::runtime_error("battlegate_choice_text_layout");
            }
            std::wstring result;
            for (int i = 0; i < count; ++i)
            {
                result += field<wchar_t>(chars, i * sizeof(wchar_t));
            }
            if (result.back() != L'\0')
            {
                throw std::runtime_error("battlegate_choice_text_termination");
            }
            result.pop_back();
            return result;
        };
        const auto text =
            read_text(reinterpret_cast<const std::byte*>(choice) + choice_dialog_text_offset);
        const auto choices = field<uintptr_t>(choice, choice_dialog_choices_offset);
        const auto count = field<int32_t>(choice, choice_dialog_count_offset);
        const auto capacity = field<int32_t>(choice, choice_dialog_capacity_offset);
        if (!choices || count < 1 || count > max_garden_dialog_choices || capacity < count ||
            capacity > max_garden_dialog_choice_capacity)
        {
            throw std::runtime_error("garden_choice_count");
        }
        std::vector<std::wstring> rows;
        for (int i = 0; i < count; ++i)
        {
            rows.push_back(
                read_text(reinterpret_cast<void*>(choices + i * sizeof(RC::Unreal::FString))));
        }
        const std::vector<std::wstring> original{L"Exit", L"You bet!"};
        const std::vector<std::wstring> chapter{L"Enter Kingdom Hearts 0.2", L"Exit"};
        const std::vector<std::wstring> cleared{L"Return to Castle Town", L"Continue to Dark Aqua",
                                                L"Exit"};
        const bool chapter_menu = dark_world && text == L"Where would you like to go?" &&
                                  (rows == chapter || rows == cleared);
        const bool correct_text =
            dark_world ? (chapter_menu || (text == L"Are you sure you want to visit Dark World?" &&
                                           rows == original))
                       : text.find(L"<b(on)>Battlegate 0 - Difficulty ") == 0 &&
                             text.find(L"Enter and meet them in battle?") != std::wstring::npos &&
                             rows == std::vector<std::wstring>{L"No", L"Yes"};
        if (!correct_text)
        {
            throw std::runtime_error("battlegate_zero_choice_mismatch");
        }
        int selection = 1;
        if (dark_world && texture_action == L"castle" && !chapter_menu)
        {
            throw std::runtime_error("kh02_checks_not_enabled_for_portal");
        }
        if (dark_world && texture_action == L"exit")
        {
            selection = chapter_menu ? count - 1 : 0;
        }
        else if (chapter_menu && texture_action == L"castle")
        {
            selection = 0;
        }
        else if (chapter_menu && rows != cleared && command == L"garden_dark_world_enter")
        {
            throw std::runtime_error("dark_world_requires_completed_kh02");
        }
        const auto dialog_id = field<int32_t>(choice, 0xa8);
        std::ostringstream evidence;
        evidence << (dark_world ? "{\"world\":\"Dark World\"," : "{\"battlegate\":0,")
                 << "\"dialog_id\":" << dialog_id << ",\"text\":\"" << json_escape(text)
                 << "\",\"choices\":[";
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (i)
            {
                evidence << ',';
            }
            evidence << '\"' << json_escape(rows[i]) << '\"';
        }
        evidence << "]}";
        command_json = evidence.str();
        if (command == L"battlegate_zero_inspect" || command == L"garden_dark_world_inspect")
        {
            finish(L"completed", L"battlegate_zero_choice_observed");
            return;
        }
        const auto decide = base + kh02_native::rva(kh02_native::Site::garden_dialog_decide);
        if (dialog_id != expected_message_id ||
            field<uintptr_t>(reinterpret_cast<void*>(field<uintptr_t>(choice, 0)),
                             native_ui::input_handler_vtable_index * sizeof(void*)) != decide ||
            !kh02_native::verify_sites({kh02_native::Site::garden_dialog_decide}))
        {
            throw std::runtime_error("battlegate_zero_choice_identity_changed");
        }
        native_ui::InputEvent event{};
        const int32_t kind = native_ui::decide_event, index = selection;
        event.set_kind(kind);
        event.set_parameter(index);
        using Decide = bool (*)(UObject*, void*);
        dispatched = true;
        selected = index;
        if (!reinterpret_cast<Decide>(decide)(choice, event.data()) ||
            field<int32_t>(choice, choice_dialog_selected_index_offset) != index ||
            !event.handled())
        {
            throw std::runtime_error("battlegate_zero_selection_not_observed");
        }
        finish(L"completed", L"battlegate_zero_yes_selected_verify_arrival");
        return;
    }

    void RuntimeUiControlService::Impl::advance_dark_world_travel()
    {
        if (pawn_name != expected_pawn ||
            pawn_name.find(L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.") == std::wstring::npos ||
            !config.gameplay_ready() || pause_open || pause_type != 0 || frozen_menu_widget())
        {
            throw std::runtime_error("dark_world_portal_requires_idle_garden_player");
        }
        auto* portal = frozen_garden_portal(L"gameflow_DW");
        UiProbeCall interact(portal, L"BndEvt__TresReactor_K2Node_ComponentBoundEvent_0_"
                                     L"TresReactorDoCommandSignature__DelegateSignature");
        if (!write_object_function_param(
                interact.function, interact.params.data(), L"ReactorComponent",
                try_read_reflected_object_property_value(portal, L"TresReactor")) ||
            !write_object_function_param(interact.function, interact.params.data(),
                                         L"CommandCauser", cutscene_pawn()))
        {
            throw std::runtime_error("dark_world_portal_interaction_layout");
        }
        std::ostringstream evidence;
        evidence << "{\"portal\":\"" << json_escape(portal->GetPathName())
                 << "\",\"story_flag\":\"gameflow_DW\"}";
        command_json = evidence.str();
        interact.run();
        dispatched = true;
        finish(L"completed", L"dark_world_portal_interaction_dispatched_verify_arrival");
        return;
    }

    void RuntimeUiControlService::Impl::advance_computer_space()
    {
        // Choice menus use TresUIP_Dialog, not informational Message.
        auto* choice = unique(L"TresUIP_Dialog");
        const bool choice_open = is_uobject_valid_fast(choice) &&
                                 field<uint8_t>(choice, native_ui::panel_open_offset) != 0;
        if (pawn_name != expected_pawn ||
            pawn_name.find(L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.") == std::wstring::npos)
        {
            throw std::runtime_error("computer_test_requires_garden_sora");
        }
        auto* computer = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr,
            L"/Game/Maps/GardenOfAssemblage/umap/"
            L"GoA_gimmick.GoA_gimmick:PersistentLevel.g_rg_Computer_01");
        if (!is_uobject_valid_fast(computer))
        {
            throw std::runtime_error("garden_computer_unavailable");
        }
        offset(computer, L"Text_Choices_Menu", garden_computer_choices_offset);
        if (field<int32_t>(computer, garden_computer_choice_count_offset) !=
            garden_computer_choice_count)
        {
            throw std::runtime_error("computer_space_choice_missing");
        }
        const auto choices = field<uintptr_t>(computer, garden_computer_choices_offset);
        const std::array<std::wstring, 3> labels{L"Exit", L"Credits", L"Go to Space"};
        for (size_t i = 0; i < labels.size(); ++i)
        {
            const auto* data = reinterpret_cast<void*>(choices + garden_computer_choice_bytes * i +
                                                       garden_computer_choice_text_offset);
            const auto length = field<int32_t>(data, 8);
            if (length != static_cast<int32_t>(labels[i].size() + 1))
            {
                throw std::runtime_error("computer_choice_label_mismatch");
            }
            const auto* chars = field<const wchar_t*>(data, 0);
            for (size_t j = 0; j < labels[i].size(); ++j)
            {
                if (field<wchar_t>(chars, j * sizeof(wchar_t)) != labels[i][j])
                {
                    throw std::runtime_error("computer_choice_label_mismatch");
                }
            }
        }
        if (!opened)
        {
            if (!config.gameplay_ready() || pause_open || pause_type != 0 || choice_open)
            {
                throw std::runtime_error("computer_test_requires_idle_gameplay");
            }
            auto* function = computer->GetFunctionByNameInChain(
                L"BndEvt__TresReactor_K2Node_ComponentBoundEvent_1_ScriptMulticastDelegate_"
                L"_DelegateSignature");
            if (!is_uobject_valid_fast(function) || function->GetParmsSize() != 0)
            {
                throw std::runtime_error("computer_interaction_layout_mismatch");
            }
            computer->ProcessEvent(function, nullptr);
            opened = true;
            return;
        }
        if (!choice_open || !field<uint8_t>(choice, native_ui::panel_ready_offset))
        {
            return;
        }
        if (try_read_reflected_object_property_value(choice, L"m_pUIManager") !=
                manager_ref.Get() ||
            field<uintptr_t>(reinterpret_cast<void*>(field<uintptr_t>(choice, 0)),
                             native_ui::input_handler_vtable_index * sizeof(void*)) !=
                base + egs_computer_choice_decide_rva ||
            !signature(base + egs_computer_choice_decide_rva,
                       {0x40, 0x57, 0x48, 0x83, 0xec, 0x20, 0x83, 0x7a, 0x10, 0x07}))
        {
            throw std::runtime_error("expected_computer_choice_required");
        }
        // Exercise the real choice completion delegate and Blueprint
        // branch. Arrival is verified separately by space.py --wait.
        native_ui::InputEvent event{};
        const int32_t kind = native_ui::decide_event, index = 2;
        event.set_kind(kind);
        event.set_parameter(index);
        using Decide = bool (*)(UObject*, void*);
        dispatched = true;
        selected = index;
        if (!reinterpret_cast<Decide>(base + egs_computer_choice_decide_rva)(choice,
                                                                             event.data()) ||
            field<int32_t>(choice, choice_dialog_selected_index_offset) != index ||
            !event.handled())
        {
            throw std::runtime_error("computer_selection_not_observed");
        }
        finish(L"completed", L"computer_space_choice_selected");
        return;
    }

} // namespace kh3ap
