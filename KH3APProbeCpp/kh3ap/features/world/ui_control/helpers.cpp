#include "helpers.h"
#include "kh3ap/core/input/runtime_controller_input_profile.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/features/grants/keyblades/progressive_keyblade_level_helpers.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
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

namespace kh3ap::ui_control_detail
{
    using namespace RC::Unreal;
    using namespace controller_input;
    void UiProbeCall::run()
    {
        owner->ProcessEvent(function, params.empty() ? nullptr : params.data());
    }

    UiProbeCall::UiProbeCall(UObject* object, const wchar_t* name)
        : owner(object),
          function(is_uobject_valid_fast(object) ? object->GetFunctionByNameInChain(name) : nullptr)
    {
        if (!is_uobject_valid_fast(function) ||
            function->GetParmsSize() > max_ui_command_parameter_bytes)
        {
            throw std::runtime_error("garden_probe_function_missing:" + to_utf8(name));
        }
        params.resize(function->GetParmsSize());
    }

    void cutscene_engine_pause(UObject* pawn, bool paused)
    {
        auto* library = kh02_require_loaded_object(L"/Script/Engine.Default__GameplayStatics");
        Kh02ReflectedCall set(library, L"SetGamePaused", "cutscene_pause",
                              max_cutscene_pause_parameter_bytes);
        set.object(L"WorldContextObject", pawn);
        set.boolean(L"bPaused", paused);
        set.run();
        Kh02ReflectedCall read(library, L"IsGamePaused", "cutscene_pause",
                               max_cutscene_pause_parameter_bytes);
        read.object(L"WorldContextObject", pawn);
        read.run();
        if (read.boolean_result() != paused)
        {
            throw std::runtime_error("cutscene_engine_pause_not_observed");
        }
    }
    void remote_texture_screenshot(UObject* pawn)
    {
        auto* library = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, L"/Script/Engine.Default__KismetSystemLibrary");
        auto* function = is_uobject_valid_fast(library)
                             ? library->GetFunctionByNameInChain(L"ExecuteConsoleCommand")
                             : nullptr;
        if (!is_uobject_valid_fast(function) ||
            function->GetParmsSize() > max_remote_screenshot_parameter_bytes)
        {
            throw std::runtime_error("remote_screenshot_function");
        }
        std::vector<std::byte> params(function->GetParmsSize());
        if (!write_object_function_param(function, params.data(), L"WorldContextObject", pawn))
        {
            throw std::runtime_error("remote_screenshot_context");
        }
        auto* field = find_function_param_by_name(function, L"Command");
        struct Borrowed
        {
            const wchar_t* data;
            int32_t count, capacity;
        };
        if (!field || field->GetClass().GetName() != L"StrProperty" ||
            field->GetElementSize() != sizeof(Borrowed))
        {
            throw std::runtime_error("remote_screenshot_string");
        }
        const std::wstring command = L"HighResShot 1920x1080 "
                                     L"filename=kh02-remote-texture.png";
        const Borrowed value{command.c_str(), static_cast<int32_t>(command.size() + 1),
                             static_cast<int32_t>(command.size() + 1)};
        std::memcpy(field->ContainerPtrToValuePtr<void>(params.data()), &value, sizeof(value));
        library->ProcessEvent(function, params.data());
    }
    int flight_hp(UObject* pawn)
    {
        if (!is_uobject_valid_fast(pawn))
        {
            return -1;
        }
        auto* fn = pawn->GetFunctionByNameInChain(L"GetHP");
        if (!is_uobject_valid_fast(fn) || fn->GetParmsSize() != sizeof(int32_t))
        {
            throw std::runtime_error("flight_hp_layout");
        }
        int hp = -1;
        pawn->ProcessEvent(fn, &hp);
        return hp;
    }
    void damage_flight(UObject* pawn, UObject* controller)
    {
        auto* lib = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, L"/Script/Engine.Default__GameplayStatics");
        auto* fn =
            is_uobject_valid_fast(lib) ? lib->GetFunctionByNameInChain(L"ApplyDamage") : nullptr;
        if (!is_uobject_valid_fast(fn) || fn->GetParmsSize() > max_flight_damage_parameter_bytes)
        {
            throw std::runtime_error("flight_damage_layout");
        }
        std::vector<std::byte> params(fn->GetParmsSize());
        auto* amount = find_function_param_by_name(fn, L"BaseDamage");
        if (!amount ||
            !write_float_function_param(amount, params.data(),
                                        static_cast<double>(flight_hp(pawn)) + 1.0) ||
            !write_object_function_param(fn, params.data(), L"DamagedActor", pawn) ||
            !write_object_function_param(fn, params.data(), L"EventInstigator", controller) ||
            !write_object_function_param(fn, params.data(), L"DamageCauser", pawn))
        {
            throw std::runtime_error("flight_damage_parameters");
        }
        lib->ProcessEvent(fn, params.data());
    }
    int64_t unix_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }
    UObject* unique(const wchar_t* name)
    {
        std::vector<UObject*> objects;
        UObjectGlobals::FindAllOf(name, objects);
        UObject* found = nullptr;
        for (auto* object : objects)
        {
            if (!is_uobject_valid_fast(object) ||
                object->GetPathName().find(L"Default__") != std::wstring::npos)
            {
                continue;
            }
            if (found)
            {
                throw std::runtime_error("ambiguous_live_ui_object");
            }
            found = object;
        }
        return found;
    }
    std::wstring command_pawn_identity(UObject* pawn)
    {
        if (!is_uobject_valid_fast(pawn))
        {
            throw std::runtime_error("command_pawn_unavailable");
        }
        FWeakObjectPtr weak{};
        weak = pawn;
        if (weak.Get() != pawn)
        {
            throw std::runtime_error("command_pawn_identity_unavailable");
        }
        static_assert(sizeof(weak) == 8);
        std::array<int32_t, 2> identity{};
        std::memcpy(identity.data(), &weak, sizeof(weak));
        return std::to_wstring(identity[0]) + L":" + std::to_wstring(identity[1]);
    }
    std::string command_snapshot(UObject* manager, UObject* pawn)
    {
        auto* hud = unique(L"TresUIP_HudCommand");
        if (!is_uobject_valid_fast(hud) ||
            try_read_reflected_object_property_value(hud, L"m_pUIManager") != manager)
        {
            throw std::runtime_error("command_hud_unavailable");
        }
        offset(hud, L"m_pGFxMainCmdList", hud_main_command_list_offset);
        offset(hud, L"m_pGFxSubCmdName", hud_subcommand_names_offset);
        const GfxProbeFunctionPaths paths{L"/Script/ScaleformUI.GFxObject:GetMemberValue",
                                          L"/Script/ScaleformUI.GFxObject:Invoke",
                                          L"/Script/ScaleformUI.GFxObject:SetMemberValue"};
        std::ostringstream out;
        out << "{\"nodes\":[";
        bool first = true;
        size_t node_count = 0;
        const auto emit = [&](UObject* object, std::wstring label)
        {
            if (!is_uobject_valid_fast(object))
            {
                return;
            }
            if (++node_count > max_command_tree_nodes)
            {
                throw std::runtime_error("command_tree_exceeds_bound");
            }
            if (!first)
            {
                out << ',';
            }
            first = false;
            out << "{\"name\":\"" << json_escape(label) << "\",\"members\":{";
            bool member_first = true;
            for (auto key :
                 {L"text", L"htmlText", L"currentFrame", L"currentLabel", L"selectedIndex",
                  L"visible", L"enabled", L"alpha", L"name", L"numChildren"})
            {
                const auto value = try_gfx_get_member_value(paths, object, key);
                if (!member_first)
                {
                    out << ',';
                }
                member_first = false;
                out << '"' << json_escape(key) << "\":\"" << json_escape(value) << '"';
            }
            out << "}}";
        };
        const auto tree = [&](auto&& self, UObject* object, std::wstring label, int depth) -> void
        {
            if (!is_uobject_valid_fast(object))
            {
                return;
            }
            emit(object, label);
            if (depth >= max_gfx_diagnostic_depth)
            {
                return;
            }
            const auto count = try_gfx_get_member_value_number(paths, object, L"numChildren");
            if (!count || *count < 0 || *count > max_gfx_diagnostic_children)
            {
                return;
            }
            for (int i = 0; i < static_cast<int>(*count); ++i)
            {
                auto* child = try_gfx_invoke_get_child_at(paths, object, i);
                self(self, child, label + L"/" + std::to_wstring(i), depth + 1);
            }
        };
        for (int i = 0; i < hud_main_command_count; ++i)
        {
            tree(tree, field<UObject*>(hud, hud_main_command_list_offset + i * sizeof(UObject*)),
                 L"main_" + std::to_wstring(i), 0);
        }
        for (int i = 0; i < hud_subcommand_count; ++i)
        {
            tree(tree, field<UObject*>(hud, hud_subcommand_names_offset + i * sizeof(UObject*)),
                 L"sub_" + std::to_wstring(i), 0);
            emit(field<UObject*>(hud, hud_subcommand_rows_offset + i * sizeof(UObject*)),
                 L"sub_row_" + std::to_wstring(i));
        }
        emit(field<UObject*>(hud, hud_root_offset), L"root");
        emit(field<UObject*>(hud, hud_subcommand_rows_offset), L"sub_list");
        auto* state_comp = try_read_reflected_object_property_value(pawn, L"MyStateComp");
        auto* state = is_uobject_valid_fast(state_comp)
                          ? try_read_reflected_object_property_value(state_comp, L"m_pCurrentState")
                          : nullptr;
        offset(pawn, L"m_MagicPoint", sora_magic_point_offset);
        out << "],\"pawn_identity\":\"" << json_escape(command_pawn_identity(pawn))
            << "\",\"mp\":" << field<int32_t>(pawn, sora_magic_point_offset) << ",\"sora_state\":\""
            << json_escape(is_uobject_valid_fast(state) ? state->GetFullName() : L"") << "\"}";
        return out.str();
    }
    std::optional<int64_t> selection_game_flag(const wchar_t* name)
    {
        auto* library = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        auto* function = is_uobject_valid_fast(library)
                             ? library->GetFunctionByNameInChain(L"GetGameFlag")
                             : nullptr;
        auto* flag = function ? find_function_param_by_name(function, L"FlagName") : nullptr;
        auto* result = function ? find_function_param_by_name(function, L"ReturnValue") : nullptr;
        if (!flag || !result || function->GetParmsSize() != game_flag_parameter_bytes ||
            result->GetClass().GetName() != L"IntProperty" ||
            result->GetOffset_Internal() != game_flag_result_offset ||
            result->GetElementSize() != sizeof(int32_t))
        {
            return std::nullopt;
        }
        std::vector<std::byte> params(function->GetParmsSize());
        if (!write_name_or_string_function_param(flag, params.data(), name))
        {
            return std::nullopt;
        }
        library->ProcessEvent(function, params.data());
        return field<int32_t>(params.data(), game_flag_result_offset);
    }

    // Diagnostic-only resize fixture. Uses normal engine settings methods;
    // never saves settings. The driver restores both viewport and config.
    std::string viewport_fixture(UObject* pawn, const std::optional<std::array<int, 3>>& requested,
                                 bool apply)
    {
        auto* cdo = find_loaded_runtime_asset(L"/Script/Engine.Default__GameUserSettings");
        UiProbeCall get(cdo, L"GetGameUserSettings");
        auto* returned = find_function_param_by_name(get.function, L"ReturnValue");
        if (!returned || returned->GetClass().GetName() != L"ObjectProperty" ||
            returned->GetElementSize() != sizeof(ViewportSize) ||
            returned->GetOffset_Internal() != 0 || get.params.size() != sizeof(ViewportSize))
        {
            throw std::runtime_error("viewport_settings_layout");
        }
        get.run();
        auto* settings = *returned->ContainerPtrToValuePtr<UObject*>(get.params.data());
        auto pair_field = [](UiProbeCall& call, const wchar_t* name)
        {
            auto* field = find_function_param_by_name(call.function, name);
            if (!field || !field->IsA<FStructProperty>() ||
                field->GetElementSize() != sizeof(ViewportSize) ||
                field->GetOffset_Internal() != 0 || call.params.size() != sizeof(ViewportSize) ||
                static_cast<FStructProperty*>(field)->GetStruct()->GetName() != L"IntPoint")
            {
                throw std::runtime_error("viewport_resolution_layout");
            }
            return field;
        };
        auto mode_field = [](UiProbeCall& call, const wchar_t* name)
        {
            auto* field = find_function_param_by_name(call.function, name);
            if (!field || field->GetClass().GetName() != L"ByteProperty" ||
                field->GetElementSize() != 1 || field->GetOffset_Internal() != 0 ||
                call.params.size() != 1)
            {
                throw std::runtime_error("viewport_mode_layout");
            }
            return field;
        };
        if (requested)
        {
            const auto [width, height, mode] = *requested;
            if (width < minimum_viewport_width || width > maximum_viewport_width ||
                height < minimum_viewport_height || height > maximum_viewport_height || mode < 0 ||
                mode > maximum_window_mode)
            {
                throw std::runtime_error("viewport_request_range");
            }
            UiProbeCall set_size(settings, L"SetScreenResolution");
            auto* size = pair_field(set_size, L"Resolution");
            const std::array<int, 2> values{width, height};
            std::memcpy(size->ContainerPtrToValuePtr<void>(set_size.params.data()), values.data(),
                        sizeof(ViewportSize));
            UiProbeCall set_mode(settings, L"SetFullscreenMode");
            auto* kind = mode_field(set_mode, L"InFullscreenMode");
            *kind->ContainerPtrToValuePtr<uint8_t>(set_mode.params.data()) =
                static_cast<uint8_t>(mode);
            UiProbeCall apply_size(settings, L"ApplyResolutionSettings");
            if (!write_bool_function_param(
                    find_function_param_by_name(apply_size.function,
                                                L"bCheckForCommandLineOverrides"),
                    apply_size.params.data(), false))
            {
                throw std::runtime_error("viewport_apply_layout");
            }
            set_size.run();
            set_mode.run();
            if (apply)
            {
                apply_size.run();
            }
        }
        UiProbeCall size(settings, L"GetScreenResolution");
        auto* size_result = pair_field(size, L"ReturnValue");
        size.run();
        ViewportSize configured{};
        std::memcpy(configured.data(),
                    size_result->ContainerPtrToValuePtr<void>(size.params.data()),
                    sizeof(configured));
        UiProbeCall mode(settings, L"GetFullscreenMode");
        auto* mode_result = mode_field(mode, L"ReturnValue");
        mode.run();
        const int fullscreen = *mode_result->ContainerPtrToValuePtr<uint8_t>(mode.params.data());
        auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
        UiProbeCall view(controller, L"GetViewportSize");
        if (view.params.size() != sizeof(ViewportSize))
        {
            throw std::runtime_error("viewport_actual_layout");
        }
        for (const auto& [name, offset] : {std::pair{L"SizeX", 0}, std::pair{L"SizeY", 4}})
        {
            auto* field = find_function_param_by_name(view.function, name);
            if (!field || field->GetClass().GetName() != L"IntProperty" ||
                field->GetElementSize() != sizeof(int32_t) || field->GetOffset_Internal() != offset)
            {
                throw std::runtime_error("viewport_actual_fields");
            }
        }
        view.run();
        ViewportSize actual{};
        std::memcpy(actual.data(), view.params.data(), sizeof(actual));
        std::ostringstream json;
        json << "{\"width\":" << actual[0] << ",\"height\":" << actual[1]
             << ",\"configured_width\":" << configured[0]
             << ",\"configured_height\":" << configured[1] << ",\"fullscreen_mode\":" << fullscreen
             << '}';
        return json.str();
    }
    UObject* frozen_garden_portal(const wchar_t* story_flag)
    {
        std::vector<UObject*> objects;
        UObjectGlobals::FindAllOf(L"Portal_Base_C", objects);
        UObject* found = nullptr;
        for (auto* object : objects)
        {
            if (!is_uobject_valid_fast(object) ||
                object->GetPathName().find(L"GoA_01:PersistentLevel.") == std::wstring::npos)
            {
                continue;
            }
            const bool dark_world = _wcsicmp(story_flag, L"gameflow_DW") == 0;
            const auto flag = try_read_object_name_property(
                object, dark_world ? L"LockedKeyItem" : L"FinishedFlagName");
            if (!flag || _wcsicmp(flag->c_str(), dark_world ? L"WEP_KEYBLADE_SO_014" : story_flag))
            {
                continue;
            }
            if (found)
            {
                throw std::runtime_error("garden_frozen_portal_ambiguous");
            }
            found = object;
        }
        if (!found)
        {
            throw std::runtime_error("garden_frozen_portal_missing");
        }
        return found;
    }
    UObject* frozen_menu_widget()
    {
        std::vector<UObject*> objects;
        UObjectGlobals::FindAllOf(L"W_SaveChoiceUI_C", objects);
        UObject* found = nullptr;
        for (auto* object : objects)
        {
            if (!is_uobject_valid_fast(object) ||
                object->GetPathName().find(L"Default__") != std::wstring::npos)
            {
                continue;
            }
            UiProbeCall visible(object, L"IsInViewport");
            visible.run();
            auto* result = find_function_param_by_name(visible.function, L"ReturnValue");
            if (!result || !result->IsA<FBoolProperty>())
            {
                throw std::runtime_error("garden_widget_visibility_layout");
            }
            if (!static_cast<FBoolProperty*>(result)->GetPropertyValueInContainer(
                    visible.params.data()))
            {
                continue;
            }
            if (found)
            {
                throw std::runtime_error("garden_frozen_menu_ambiguous");
            }
            found = object;
        }
        return found;
    }
    std::vector<FName> frozen_menu_rows(UObject* widget)
    {
        // FilteredList is the world's candidate catalog, including rows the
        // evaluator rejects. Only Save Items contains displayed buttons.
        auto* property = widget->GetPropertyByNameInChain(L"Save Items");
        if (!property || !property->IsA<FArrayProperty>())
        {
            throw std::runtime_error("garden_filtered_list_layout");
        }
        struct Array
        {
            UObject** data;
            int32_t num, max;
        };
        const auto values = *property->ContainerPtrToValuePtr<Array>(widget);
        if (values.num < 0 || values.num > max_ui_value_entries || values.max < values.num ||
            (values.num && !values.data))
        {
            throw std::runtime_error("garden_filtered_list_bounds");
        }
        std::vector<FName> result;
        for (int i = 0; i < values.num; ++i)
        {
            auto* entry = values.data[i];
            auto* row = is_uobject_valid_fast(entry) ? entry->GetPropertyByNameInChain(L"AreaRow")
                                                     : nullptr;
            if (!row || !row->IsA<FStructProperty>() ||
                row->GetElementSize() != garden_selection_row_bytes)
            {
                throw std::runtime_error("garden_displayed_row_layout");
            }
            const auto tag = field<FName>(row->ContainerPtrToValuePtr<void>(entry), 0x10);
            result.push_back(tag.ToString() == L"fz_02_Lv_AP_Save_01" ? FName{L"FZ_SAVE08"} : tag);
        }
        return result;
    }
    void select_frozen_menu_row(UObject* widget, const wchar_t* row_name,
                                const wchar_t* player_start)
    {
        auto* table = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr,
            L"/Game/Blueprints/GoA/Data/KH3AreaSelectData_DT.KH3AreaSelectData_DT");
        if (!is_uobject_valid_fast(table))
        {
            throw std::runtime_error("garden_table_missing");
        }
        struct Entry
        {
            FName name;
            const void* row;
            uint64_t hash;
        };
        const auto data = field<Entry*>(table, 0x48);
        const int count = field<int32_t>(table, 0x50);
        if (!data || count < 1 || count > max_garden_selection_rows)
        {
            throw std::runtime_error("garden_table_bounds");
        }
        const void* row = nullptr;
        for (int i = 0; i < count; ++i)
        {
            if (data[i].name.ToString() == row_name)
            {
                row = data[i].row;
            }
        }
        if (!row ||
            field<FName>(row, garden_selection_player_start_offset).ToString() != player_start)
        {
            throw std::runtime_error("garden_frozen_player_start_changed");
        }
        UiProbeCall select(widget, L"Entry Button Selected");
        auto* parameter = find_function_param_by_name(select.function, L"AreaRow");
        if (!parameter || !parameter->IsA<FStructProperty>() ||
            parameter->GetElementSize() != garden_selection_row_bytes ||
            parameter->GetOffset_Internal() + parameter->GetElementSize() > select.params.size())
        {
            throw std::runtime_error("garden_selection_row_layout");
        }
        // Borrow the immutable table row only during the real widget event.
        std::memcpy(parameter->ContainerPtrToValuePtr<void>(select.params.data()), row,
                    garden_selection_row_bytes);
        select.run();
    }
    int sora_selection_index(std::wstring_view pawn_name)
    {
        const auto story = selection_game_flag(L"gameflow_KG_DLC");
        if (pawn_name.find(L"/Game/Levels/kg_DLC/kg_05/kg_05.") != std::wstring_view::npos)
        {
            // Cooked kg05 selections: Sora/Riku in A, Aqua/Sora in C.
            if (story == kg05_sora_riku_story_state &&
                selection_game_flag(L"gameflow_KG_DLC_13_A") == kg05_sora_riku_selection_state)
            {
                return 0;
            }
            if (story == kg05_aqua_sora_story_state &&
                selection_game_flag(L"gameflow_KG_DLC_13_C") == kg05_aqua_sora_selection_state)
            {
                return 1;
            }
        }
        if (pawn_name.find(L"/Game/Levels/kg_DLC/kg_06/kg_06.") != std::wstring_view::npos &&
            (story == kg06_initial_selection_story_state ||
             story == kg06_retry_selection_story_state ||
             story == kg06_final_selection_story_state))
        {
            // kg06's shared initial/Retry selector passes Sora (main_0010)
            // on the left and Kairi (main_0070) on the right. LAST_CHARSEL
            // records the previous choice; it does not reorder this UI.
            return 0;
        }
        return -1;
    }
    std::string workshop_probe(uintptr_t base, bool craft, bool deposit)
    {
        NativeWorkshopSnapshot before, after;
        if (!try_query_native_workshop(before, nullptr))
        {
            throw std::runtime_error("workshop_snapshot_unavailable");
        }
        auto* table = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr,
            L"/Game/Load/Tres/ItemSynthesis/TresItemSynthesisData.TresItemSynthesisData");
        if (!is_uobject_valid_fast(table))
        {
            throw std::runtime_error("synthesis_table_unavailable");
        }
        auto* structure = field<UScriptStruct*>(table, data_table_row_struct_offset);
        if (!is_uobject_valid_fast(structure) ||
            structure->GetName() != L"TresItemSynthesisDataTable")
        {
            throw std::runtime_error("synthesis_structure_mismatch");
        }
        for (auto [name, expected] :
             {std::pair{L"m_RewardItem", 0x70}, {L"m_Condition", 0x78}, {L"m_FlagIndex", 0x68}})
        {
            auto* prop = find_struct_field_by_name(structure, name);
            if (!prop || prop->GetOffset_Internal() != expected)
            {
                throw std::runtime_error("synthesis_layout_mismatch");
            }
        }
        const auto data = field<uintptr_t>(table, 0x48);
        const auto count = field<int>(table, 0x50);
        if (!data || count < 1 || count > max_synthesis_selection_rows)
        {
            throw std::runtime_error("synthesis_rows_invalid");
        }
        uintptr_t row{};
        FName recipe{L"IS_0"};
        for (int i = 0; i < count; ++i)
        {
            if (field<FName>(reinterpret_cast<void*>(data), i * data_table_row_map_entry_bytes)
                    .ToString() == L"IS_0")
            {
                row = field<uintptr_t>(reinterpret_cast<void*>(data),
                                       i * data_table_row_map_entry_bytes +
                                           data_table_row_pointer_offset);
            }
        }
        if (!row ||
            field<uint8_t>(reinterpret_cast<void*>(row), synthesis_requirement_flag_offset) != 0 ||
            field<int>(reinterpret_cast<void*>(row), synthesis_requirement_count_offset) != 0)
        {
            throw std::runtime_error("ether_recipe_not_unconditionally_unlocked");
        }
        const auto owner = before.save_data - 0x870;
        auto* itemman =
            field<UObject*>(reinterpret_cast<void*>(owner), game_instance_item_manager_offset);
        if (!is_uobject_valid_fast(itemman) ||
            itemman->GetClassPrivate()->GetName() != L"TresItemMan")
        {
            throw std::runtime_error("workshop_item_manager_unavailable");
        }
        if (!signature(base + egs_synthesis_craft_rva,
                       {0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57})
            // The AP item converter owns a six-byte detour at the entry.
            // Guard its unchanged tail; call the hooked entry so seeded
            // numeric AP markers retain the normal runtime mapping.
            || !signature(base + egs_item_convert_category_check_rva,
                          {0x8b, 0xd1, 0x0f, 0x84, 0x98, 0x00, 0x00, 0x00}))
        {
            throw std::runtime_error("synthesis_transaction_build_mismatch");
        }
        using Convert = int (*)(UObject*, FName);
        bool deposited;
        if (deposit)
        {
            if (!signature(base + egs_synthesis_deposit_rva,
                           {0x40, 0x55, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xe9}))
            {
                throw std::runtime_error("synthesis_deposit_build_mismatch");
            }
            using Deposit = bool (*)(void*);
            deposited = reinterpret_cast<Deposit>(base + egs_synthesis_deposit_rva)(
                reinterpret_cast<void*>(before.save_data));
        }
        std::array<int, synthesis_material_slot_count> material_ids{}, costs{}, stock_before{},
            stock_after{};
        for (int i = 0; i < synthesis_material_slot_count; ++i)
        {
            costs[i] =
                field<int>(reinterpret_cast<void*>(row),
                           synthesis_material_cost_offset + i * synthesis_material_slot_bytes);
            if (costs[i] == 0)
            {
                continue;
            }
            auto token =
                field<FName>(reinterpret_cast<void*>(row),
                             synthesis_material_token_offset + i * synthesis_material_slot_bytes);
            material_ids[i] =
                reinterpret_cast<Convert>(base + egs_item_token_conversion_rva)(itemman, token);
            if (material_ids[i] >> std::numeric_limits<uint8_t>::digits !=
                synthesis_material_item_category)
            {
                throw std::runtime_error("synthesis_material_category_mismatch");
            }
            stock_before[i] = field<int16_t>(
                reinterpret_cast<void*>(before.save_data),
                synthesis_stock_offset +
                    (material_ids[i] & std::numeric_limits<uint8_t>::max()) * sizeof(uint16_t));
        }
        const auto reward = field<FName>(reinterpret_cast<void*>(row), 0x70).ToString();
        bool success;
        if (craft)
        {
            using Craft = bool (*)(FName, void*, int, void*, UObject*);
            success = reinterpret_cast<Craft>(base + egs_synthesis_craft_rva)(
                recipe, reinterpret_cast<void*>(row), 1, reinterpret_cast<void*>(before.save_data),
                itemman);
        }
        if (!try_query_native_workshop(after, nullptr) || after.save_data != before.save_data)
        {
            throw std::runtime_error("workshop_context_changed");
        }
        bool consumed = success;
        for (int i = 0; i < synthesis_material_slot_count; ++i)
        {
            if (costs[i])
            {
                stock_after[i] = field<int16_t>(
                    reinterpret_cast<void*>(after.save_data),
                    synthesis_stock_offset +
                        (material_ids[i] & std::numeric_limits<uint8_t>::max()) * sizeof(uint16_t));
                consumed = consumed && stock_before[i] - stock_after[i] == costs[i];
            }
        }
        std::ostringstream json;
        json << std::boolalpha << "{\"recipe\":\"IS_0\",\"reward_token\":\"" << json_escape(reward)
             << "\",\"deposit_requested\":" << deposit << ",\"deposit_success\":" << deposited
             << ",\"craft_requested\":" << craft << ",\"native_success\":" << success
             << ",\"materials_consumed\":" << consumed
             << ",\"flag_before\":" << bool(before.synthesis[0] & 1)
             << ",\"flag_after\":" << bool(after.synthesis[0] & 1) << ",\"materials\":[";
        bool first = true;
        for (int i = 0; i < synthesis_material_slot_count; ++i)
        {
            if (costs[i])
            {
                if (!first)
                {
                    json << ',';
                }
                first = false;
                json << "{\"native_id\":" << material_ids[i] << ",\"cost\":" << costs[i]
                     << ",\"before\":" << stock_before[i] << ",\"after\":" << stock_after[i] << '}';
            }
        }
        json << "]}";
        return json.str();
    }
} // namespace kh3ap::ui_control_detail
