#pragma once
#include <optional>
#include <utility>

#include "kh3ap/features/world/ui_control/ui_control_service.h"
#include "kh3ap/core/input/runtime_controller_input_profile.h"
#include "command_catalog.h"
#include "kh3ap/bridge/transport/request_file_watcher.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <array>
#include <chrono>
#include <deque>
#include <vector>

namespace kh3ap
{
    using controller_input::DeathUiProfile;
    using RC::Unreal::FWeakObjectPtr;
    using RC::Unreal::UObject;
    using Clock = std::chrono::steady_clock;
    struct RuntimeUiControlService::Impl
    {

        RuntimeUiControlConfig config;
        inline static constexpr auto request_filename = L"runtime_ui_control_request.json";
        RequestFileWatcher requests{request_filename};
        ui_control_detail::CommandKind command_kind{ui_control_detail::CommandKind::invalid};
        std::optional<std::array<uint32_t, 2>> magic_fixture_backup;
        std::string magic_fixture_json = "null";
        uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        std::optional<std::filesystem::path> dir;
        std::wstring id, command, expected_pawn, pawn_name, scene_key, scene_before;
        std::deque<std::wstring> consumed;
        int64_t expires{};
        FWeakObjectPtr manager_ref{}, pause_ref{}, instance_ref{}, sora_ref{};
        int message_id{-1}, message_type{-1}, message_pages{}, expected_message_id{-1};
        int message_page{-1}, notice_last_page{-1}, notice_advances{};
        Clock::time_point notice_next{};
        bool message_ready{}, message_dismissed{};
        bool player_select_open{}, player_select_ready{}, player_selection_observed{},
            selection_key_down{};
        int player_selected{-1}, player_sora_index{-1};
        FWeakObjectPtr selection_controller{};
        std::wstring message_text;
        std::string workshop_json{"null"};
        std::string command_json{"null"};
        std::string viewport_json{"null"};
        std::string bootstrap_json{"null"};

        std::wstring texture_action;
        uint32_t remote_pid{};
        std::wstring remote_executable;
        std::string remote_json{"null"};
        std::array<int, 3> viewport_request{};
        bool apply_viewport{};
        std::wstring command_action;
        std::wstring expected_command_pawn_identity;
        bool command_key_down{}, command_released{};
        Clock::time_point command_done{};
        std::vector<std::pair<std::wstring, int32_t>> command_samples;
        bool active{}, build_ok{}, opened{}, dispatched{}, skip_transition_observed{},
            game_over_observed{}, retry_transition_observed{}, continue_transition_observed{};
        uint64_t tutorial_count_before{}, tutorial_completed_before{};
        int pause_type{-1}, menu_type{-1}, selected{-1};
        int space_hp{-1};
        std::wstring space_pawn;
        bool pause_open{}, pause_ready{};
        Clock::time_point next_poll{};
        explicit Impl(RuntimeUiControlConfig value);
        void report(std::wstring_view status, std::wstring_view reason);
        void finish(std::wstring_view status, std::wstring_view reason);
        std::wstring playing_scene(UObject* instance);
        DeathUiProfile death_ui;
        UObject* cutscene_pawn();
        UObject* sample();
        void advance_event_benchmark();
        void advance_ingredient_benchmark();
        void advance_kh02_remote();
        void advance_voice_language();
        void advance_bootstrap();
        void advance_viewport();
        void advance_frozen_ground();
        void advance_garden_travel();
        void advance_garden_dialog(UObject* dialog);
        void advance_dark_world_travel();
        void advance_computer_space();
        void advance_space_dialogue();
        void advance_tutorial();
        void advance_magic_fixture();
        void advance_command_step();
        void advance_command_inspect();
        bool advance_player_selection();
        void advance_workshop_materials();
        void advance_workshop();
        void advance_notice(UObject* dialog);
        void advance_dismiss_message(UObject* dialog);
        void advance_retry();
        void advance_space_game_over();
        void advance_game_over();
        void advance_inspect();
        void advance_cutscene();
        void advance();
        void accept();
        void tick();
    };
} // namespace kh3ap
