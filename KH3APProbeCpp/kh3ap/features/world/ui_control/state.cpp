#include "service_impl.h"
#include "helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/core/ui/ui_layout.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/world/space/space_service.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/NameTypes.hpp>
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
    void RuntimeUiControlService::Impl::report(std::wstring_view status, std::wstring_view reason)
    {
        if (!dir)
        {
            return;
        }
        std::ostringstream out;
        out << std::boolalpha << "{\"schema\":\"kh3ap.ui_control.v1\",\"request_id\":\""
            << json_escape(id) << "\",\"command\":\"" << json_escape(command)
            << "\",\"process_id\":" << GetCurrentProcessId() << ",\"expires_unix_ms\":" << expires
            << ",\"status\":\"" << json_escape(status) << "\",\"reason\":\"" << json_escape(reason)
            << "\",\"at\":\"" << now_iso8601_utc() << "\",\"supported_build\":" << build_ok
            << ",\"pawn\":\"" << json_escape(pawn_name) << "\",\"kh02_pawn\":\""
            << json_escape(is_uobject_valid_fast(Kh02Story::current_hero())
                               ? Kh02Story::current_hero()->GetFullName()
                               : L"")
            << "\",\"death_ui_only\":" << death_ui.steam << ",\"pause_type\":" << pause_type
            << ",\"menu_type\":" << menu_type << ",\"pause_open\":" << pause_open
            << ",\"pause_ready\":" << pause_ready << ",\"pause_open_dispatched\":" << opened
            << ",\"selection_dispatched\":" << dispatched << ",\"selected_action\":" << selected
            << ",\"skip_transition_observed\":" << skip_transition_observed
            << ",\"game_over_observed\":" << game_over_observed << ",\"space_pawn\":\""
            << json_escape(space_pawn) << "\",\"space_hp\":" << space_hp
            << ",\"retry_transition_observed\":" << retry_transition_observed
            << ",\"continue_transition_observed\":" << continue_transition_observed
            << ",\"message_id\":" << message_id << ",\"message_type\":" << message_type
            << ",\"message_pages\":" << message_pages << ",\"message_page\":" << message_page
            << ",\"notice_advances\":" << notice_advances << ",\"message_ready\":" << message_ready
            << ",\"message_dismissed\":" << message_dismissed << ",\"message_text\":\""
            << json_escape(message_text) << "\""
            << ",\"player_select_open\":" << player_select_open
            << ",\"player_select_ready\":" << player_select_ready
            << ",\"player_selected\":" << player_selected
            << ",\"player_selection_observed\":" << player_selection_observed
            << ",\"player_sora_index\":" << player_sora_index << ",\"workshop\":" << workshop_json
            << ",\"magic_fixture\":" << magic_fixture_json << ",\"command_menu\":" << command_json
            << ",\"viewport\":" << viewport_json << ",\"bootstrap\":" << bootstrap_json
            << ",\"remote_files\":" << remote_json
            << ",\"command_input_released\":" << command_released << ",\"command_observations\":[";
        for (size_t i = 0; i < command_samples.size(); ++i)
        {
            if (i)
            {
                out << ',';
            }
            out << "{\"state\":\"" << json_escape(command_samples[i].first)
                << "\",\"mp\":" << command_samples[i].second << '}';
        }
        out << "]"
            << ",\"scene_before\":\"" << json_escape(scene_before) << "\",\"scene_key\":\""
            << json_escape(scene_key) << "\"}\n";
        write_text_file_atomic_verified(*dir / L"runtime_ui_control_result.json", out.str());
        append_text_file(*dir / L"runtime_ui_control_events.jsonl", out.str());
    }

    void RuntimeUiControlService::Impl::finish(std::wstring_view status, std::wstring_view reason)
    {
        if (command_key_down)
        {
            command_key_down = false;
            if (auto* controller = selection_controller.Get(); is_uobject_valid_fast(controller))
            {
                runtime_controller_action_input(controller, command_action, false);
                command_released = true;
            }
        }
        if (selection_key_down)
        {
            selection_key_down = false;
            if (auto* controller = selection_controller.Get(); is_uobject_valid_fast(controller))
            {
                runtime_controller_action_input(
                    controller, player_sora_index == 1 ? L"right_trigger" : L"left_trigger", false);
            }
        }
        active = false;
        report(status, reason);
    }

    std::wstring RuntimeUiControlService::Impl::playing_scene(UObject* instance)
    {
        // The game owns this movie roster. Include each Matinee's play
        // generation, so Stop/Play in one tick is also distinguishable.
        struct Array
        {
            UObject** data;
            int32_t count, capacity;
        };
        auto* property = instance->GetPropertyByNameInChain(L"m_MatineeActorsForMovie");
        if (!property || property->GetClass().GetName() != L"ArrayProperty" ||
            property->GetElementSize() != sizeof(TArray<UObject*>))
        {
            throw std::runtime_error("movie_roster_layout_mismatch");
        }
        auto list = field<Array>(property->ContainerPtrToValuePtr<void>(instance), 0);
        if (list.count < 0 || list.count > max_movie_roster_entries || list.capacity < list.count)
        {
            throw std::runtime_error("invalid_movie_roster");
        }
        std::vector<std::wstring> playing;
        for (int i = 0; i < list.count; ++i)
        {
            auto* actor = field<UObject*>(list.data, static_cast<size_t>(i) * sizeof(UObject*));
            if (!is_uobject_valid_fast(actor))
            {
                continue;
            }
            auto* flag = actor->GetPropertyByNameInChain(L"bIsPlaying");
            if (!flag || flag->GetClass().GetName() != L"BoolProperty")
            {
                throw std::runtime_error("movie_actor_layout_mismatch");
            }
            if (!static_cast<FBoolProperty*>(flag)->GetPropertyValueInContainer(actor))
            {
                continue;
            }
            auto* generation = actor->GetPropertyByNameInChain(L"ReplicationForceIsPlaying");
            const auto count =
                generation && generation->GetElementSize() == 1
                    ? field<uint8_t>(generation->ContainerPtrToValuePtr<void>(actor), 0)
                    : 0;
            playing.push_back(actor->GetPathName() + L"#" + std::to_wstring(count));
        }
        std::sort(playing.begin(), playing.end());
        std::wstring result;
        for (const auto& value : playing)
        {
            result += value + L";";
        }
        return result;
    }

    UObject* RuntimeUiControlService::Impl::cutscene_pawn()
    {
        if (auto* hero = Kh02Story::current_hero(); is_uobject_valid_fast(hero))
        {
            return hero;
        }
        // The retained-pawn trial can leave a valid, unpossessed Sora in
        // the cache. Prefer the owned, currently possessed alternate.
        if (auto* hero = character_swap::current_hero(); character_swap::owns_swap_pawn(hero))
        {
            return hero;
        }
        auto* pawn = config.sora();
        if (is_uobject_valid_fast(pawn))
        {
            return pawn;
        }
        auto* hero = character_swap::current_hero();
        return character_swap::owns_swap_pawn(hero) ? hero : nullptr;
    }

    UObject* RuntimeUiControlService::Impl::sample()
    {
        // Some UI objects have no allocated weak serial. Resolve the
        // message afresh each tick and use it only within this game-thread
        // call; assigning it to FWeakObjectPtr can immediately yield null.
        UObject* active_message = nullptr;
        auto* manager = manager_ref.Get();
        if (!is_uobject_valid_fast(manager))
        {
            manager = unique(L"TresUIManager");
            manager_ref = manager;
        }
        if (!is_uobject_valid_fast(manager))
        {
            throw std::runtime_error("ui_manager_unavailable");
        }
        if (field<uintptr_t>(manager, 0) != base + death_ui.manager_vtable)
        {
            throw std::runtime_error("ui_manager_build_mismatch");
        }
        offset(manager, L"m_Pause", death_ui.manager_pause);
        offset(manager, L"m_pGameInstance", death_ui.manager_instance);
        auto* instance = try_read_reflected_object_property_value(manager, L"m_pGameInstance");
        auto* pause = try_read_reflected_object_property_value(manager, L"m_Pause");
        if (!is_uobject_valid_fast(instance) || !is_uobject_valid_fast(pause))
        {
            throw std::runtime_error("pause_unavailable");
        }
        if (field<uintptr_t>(pause, 0) != base + death_ui.pause_vtable)
        {
            throw std::runtime_error("pause_build_mismatch");
        }
        offset(pause, L"m_PauseRoot", pause_root_offset);
        offset(pause, L"m_PauseButton", pause_resume_button_offset);
        if (instance_ref.Get() && instance_ref.Get() != instance)
        {
            throw std::runtime_error("game_instance_changed");
        }
        instance_ref = instance;
        pause_ref = pause;
        scene_key = playing_scene(instance);
        if (config.cutscene_identity)
        {
            scene_key += L"|" + config.cutscene_identity();
        }
        pause_type = field<int32_t>(instance, game_instance_pause_type_offset);
        menu_type = field<int32_t>(pause, pause_menu_type_offset);
        pause_open = field<uint8_t>(pause, native_ui::panel_open_offset) != 0;
        pause_ready = field<uint8_t>(pause, native_ui::panel_ready_offset) != 0;
        const bool cutscene_command =
            command == L"inspect" || command == L"skip_cutscene" || command == L"pause_cutscene" ||
            command == L"resume_cutscene" || command == L"garden_dark_world_travel" ||
            command == L"garden_dark_world_inspect" || command == L"garden_dark_world_enter" ||
            (command == L"kh02_remote" &&
             (texture_action == L"portal_inspect" || texture_action == L"portal_interact" ||
              texture_action == L"portal_button"));
        const bool notice_command =
            command == L"dismiss_message" || command == L"acknowledge_notice";
        // Camera milestones can display their normal notices while an
        // owned alternate is possessed. Match the same current pawn that
        // inspection reports, retaining the expected-pawn/message guards.
        const bool battle_command = command == L"command_step" || command == L"command_inspect";
        const bool death_command =
            command == L"game_over" || command == L"retry" || command == L"continue";
        auto* pawn = cutscene_command || notice_command || battle_command || death_command
                         ? cutscene_pawn()
                         : config.sora();
        pawn_name = is_uobject_valid_fast(pawn) ? pawn->GetFullName() : L"";
        auto* flight = RuntimeSpaceService::diagnostic_flight_pawn();
        space_pawn = flight ? flight->GetFullName() : L"";
        space_hp = flight_hp(flight);
        if (!death_ui.steam && (command == L"inspect" || command == L"dismiss_message" ||
                                command == L"acknowledge_notice"))
        {
            message_id = message_type = message_page = -1;
            message_pages = 0;
            message_ready = false;
            message_text.clear();
            std::vector<UObject*> messages;
            UObjectGlobals::FindAllOf(L"TresUIP_Message", messages);
            for (auto* candidate : messages)
            {
                if (!is_uobject_valid_fast(candidate) ||
                    candidate->GetPathName().find(L"Default__") != std::wstring::npos ||
                    try_read_reflected_object_property_value(candidate, L"m_pUIManager") !=
                        manager ||
                    field<uint8_t>(candidate, native_ui::panel_open_offset) != 1)
                {
                    continue;
                }
                if (active_message)
                {
                    throw std::runtime_error("ambiguous_active_message");
                }
                if (field<uintptr_t>(candidate, 0) != base + message_dialog_vtable_rva)
                {
                    throw std::runtime_error("message_build_mismatch");
                }
                active_message = candidate;
                message_id = field<int32_t>(candidate, native_ui::message_id_offset);
                message_type = field<int32_t>(candidate, native_ui::message_type_offset);
                message_pages = field<int32_t>(candidate, native_ui::message_page_count_offset);
                message_page = field<int32_t>(candidate, native_ui::message_current_page_offset);
                message_ready = field<uint8_t>(candidate, native_ui::panel_ready_offset) == 1;
                const auto count = field<int32_t>(candidate, 0x2a8),
                           capacity = field<int32_t>(candidate, 0x2ac);
                if (count < 0 || count > max_message_text_characters || capacity < count)
                {
                    throw std::runtime_error("message_text_layout");
                }
                const auto* data = field<const wchar_t*>(candidate, native_ui::message_text_offset);
                for (int i = 0; i < count; ++i)
                {
                    message_text += field<wchar_t>(data, i * sizeof(wchar_t));
                }
                if (!message_text.empty() && message_text.back() == L'\0')
                {
                    message_text.pop_back();
                }
            }
        }
        return active_message;
    }

} // namespace kh3ap
