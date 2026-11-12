#include <memory>
#include "kh3ap/features/world/ui_control/service_impl.h"
#include "kh3ap/core/integer.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/features/world/ui_control/helpers.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/utilities.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace controller_input;
    using namespace ui_control_detail;
    RuntimeUiControlService::Impl::Impl(RuntimeUiControlConfig value) : config(std::move(value))
    {
    }

    void RuntimeUiControlService::Impl::advance()
    {
        if (selection_key_down)
        {
            selection_key_down = false;
            if (auto* controller = selection_controller.Get(); is_uobject_valid_fast(controller))
            {
                runtime_controller_action_input(
                    controller, player_sora_index == 1 ? L"right_trigger" : L"left_trigger", false);
            }
        }
        if (unix_ms() >= expires)
        {
            finish(L"error", L"timeout_inspect_before_retry");
            return;
        }
        auto* dialog = sample();
        switch (command_kind)
        {
        case ui_control_detail::CommandKind::event_benchmark:
            advance_event_benchmark();
            return;
        case ui_control_detail::CommandKind::ingredient_benchmark:
            advance_ingredient_benchmark();
            return;
        case ui_control_detail::CommandKind::kh02_remote:
            advance_kh02_remote();
            return;
        case ui_control_detail::CommandKind::voice_language:
            advance_voice_language();
            return;
        case ui_control_detail::CommandKind::bootstrap:
            advance_bootstrap();
            return;
        case ui_control_detail::CommandKind::viewport:
            advance_viewport();
            return;
        case ui_control_detail::CommandKind::frozen_ground:
            advance_frozen_ground();
            return;
        case ui_control_detail::CommandKind::garden_travel:
            advance_garden_travel();
            return;
        case ui_control_detail::CommandKind::garden_dialog:
            advance_garden_dialog(dialog);
            return;
        case ui_control_detail::CommandKind::dark_world_travel:
            advance_dark_world_travel();
            return;
        case ui_control_detail::CommandKind::computer_space:
            advance_computer_space();
            return;
        case ui_control_detail::CommandKind::space_dialogue:
            advance_space_dialogue();
            return;
        case ui_control_detail::CommandKind::tutorial:
            advance_tutorial();
            return;
        case ui_control_detail::CommandKind::magic_fixture:
            advance_magic_fixture();
            return;
        case ui_control_detail::CommandKind::command_step:
            advance_command_step();
            return;
        case ui_control_detail::CommandKind::command_inspect:
            advance_command_inspect();
            return;
        case ui_control_detail::CommandKind::player_selection:
            (void)advance_player_selection();
            return;
        case ui_control_detail::CommandKind::workshop_materials:
            advance_workshop_materials();
            return;
        case ui_control_detail::CommandKind::workshop:
            advance_workshop();
            return;
        case ui_control_detail::CommandKind::notice:
            advance_notice(dialog);
            return;
        case ui_control_detail::CommandKind::dismiss_message:
            advance_dismiss_message(dialog);
            return;
        case ui_control_detail::CommandKind::dismiss_key_item:
            advance_dismiss_key_item();
            return;
        case ui_control_detail::CommandKind::retry:
            advance_retry();
            return;
        case ui_control_detail::CommandKind::space_game_over:
            advance_space_game_over();
            return;
        case ui_control_detail::CommandKind::game_over:
            advance_game_over();
            return;
        case ui_control_detail::CommandKind::inspect:
            advance_inspect();
            return;
        case ui_control_detail::CommandKind::cutscene:
            advance_cutscene();
            return;
        case ui_control_detail::CommandKind::invalid:
            finish(L"error", L"unknown_command");
            return;
        }
    }

    void RuntimeUiControlService::Impl::accept()
    {
        dir = config.bridge_dir();
        if (!dir)
        {
            return;
        }
        const auto path = *dir / request_filename;
        if (!path_points_to_regular_file(path))
        {
            return;
        }
        auto text = read_text_file(path);
        if (!text)
        {
            return;
        }
        try_remove_file(path);
        id = JsonFieldReader::string_field(*text, "request_id").value_or(L"");
        if (id.empty() || std::find(consumed.begin(), consumed.end(), id) != consumed.end())
        {
            return;
        }
        consumed.push_back(id);
        if (consumed.size() > ui_command_history_limit)
        {
            consumed.pop_front();
        }
        command = JsonFieldReader::string_field(*text, "command").value_or(L"");
        expires = JsonFieldReader::int64_field(*text, "expires_unix_ms").value_or(0);
        expected_pawn = JsonFieldReader::string_field(*text, "expected_pawn").value_or(L"");
        expected_message_id =
            checked_native_integer(
                JsonFieldReader::int64_field(*text, "expected_message_id").value_or(-1))
                .value_or(-1);
        message_dismissed = false;
        key_item_open = key_item_ready = key_item_dismissed = false;
        notice_last_page = -1;
        notice_advances = 0;
        notice_next = {};
        player_select_open = player_select_ready = player_selection_observed = selection_key_down =
            false;
        player_selected = player_sora_index = -1;
        selection_controller = nullptr;
        workshop_json = "null";
        command_json = "null";
        viewport_json = "null";
        bootstrap_json = "null";
        remote_json = "null";
        const auto source_pid = JsonFieldReader::int64_field(*text, "source_pid").value_or(0);
        remote_pid = source_pid > 0 && source_pid <= std::numeric_limits<uint32_t>::max()
                         ? static_cast<uint32_t>(source_pid)
                         : 0;
        remote_executable = JsonFieldReader::string_field(*text, "source_executable").value_or(L"");
        texture_action = JsonFieldReader::string_field(*text, "action").value_or(L"inspect");
        viewport_request = {
            checked_native_integer(JsonFieldReader::int64_field(*text, "width").value_or(0))
                .value_or(0),
            checked_native_integer(JsonFieldReader::int64_field(*text, "height").value_or(0))
                .value_or(0),
            checked_native_integer(
                JsonFieldReader::int64_field(*text, "fullscreen_mode").value_or(-1))
                .value_or(-1)};
        apply_viewport = JsonFieldReader::bool_field(*text, "apply_resolution").value_or(true);
        command_action = L"command_" + JsonFieldReader::string_field(*text, "action").value_or(L"");
        expected_command_pawn_identity =
            JsonFieldReader::string_field(*text, "expected_pawn_identity").value_or(L"");
        command_key_down = command_released = false;
        command_samples.clear();
        active = true;
        opened = dispatched = skip_transition_observed = game_over_observed =
            retry_transition_observed = continue_transition_observed = false;
        selected = -1;
        scene_before.clear();
        scene_key.clear();
        manager_ref = nullptr;
        pause_ref = nullptr;
        instance_ref = nullptr;
        sora_ref = nullptr;
        build_ok = supported(base, death_ui);
        if (JsonFieldReader::string_field(*text, "schema") != L"kh3ap.ui_control.v1" ||
            JsonFieldReader::int64_field(*text, "process_id") != GetCurrentProcessId() ||
            expires <= unix_ms() || expires > unix_ms() + maximum_remote_capture_lifetime_ms)
        {
            finish(L"error", L"invalid_request_identity_or_expiry");
            return;
        }
        if (!build_ok)
        {
            finish(L"error", L"unsupported_executable_build");
            return;
        }
        const auto* definition = ui_control_detail::find_command(command);
        command_kind = definition ? definition->kind : ui_control_detail::CommandKind::invalid;
        if (death_ui.steam && (!definition || !definition->supports_steam))
        {
            finish(L"error", L"command_not_supported_on_steam");
            return;
        }
        if (!definition)
        {
            finish(L"error", L"unknown_command");
            return;
        }
        if (command == L"command_step" && command_action != L"command_up" &&
            command_action != L"command_down" && command_action != L"command_confirm" &&
            command_action != L"command_cancel")
        {
            finish(L"error", L"unknown_command_action");
            return;
        }
        if (definition->requires_message && expected_message_id < 0)
        {
            finish(L"error", L"expected_message_id_required");
            return;
        }
        if (definition->requires_pawn && expected_pawn.empty())
        {
            finish(L"error", L"expected_pawn_required");
            return;
        }
        report(L"accepted", L"validated_request");
        advance();
    }

    void RuntimeUiControlService::Impl::tick()
    {
        if (Clock::now() < next_poll)
        {
            return;
        }
        next_poll = Clock::now() + ui_request_update_interval;
        try
        {
            if (active)
            {
                advance();
            }
            else if (requests.update(config.bridge_dir(), true))
            {
                accept();
            }
        }
        catch (const std::exception& e)
        {
            const std::string reason = e.what();
            finish(L"error", std::wstring(reason.begin(), reason.end()));
        }
    }

    RuntimeUiControlService::RuntimeUiControlService(RuntimeUiControlConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }
    RuntimeUiControlService::~RuntimeUiControlService() = default;
    void RuntimeUiControlService::tick(bool initialized)
    {
        if (initialized)
        {
            impl_->tick();
        }
    }
} // namespace kh3ap
