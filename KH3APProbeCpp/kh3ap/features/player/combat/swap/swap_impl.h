#pragma once
#include "../character_combat_swap.h"
#include "attack_input_buffer.h"
#include "camera_state.h"
#include "swing_hit_history.h"
#include "kh3ap/core/input/options_input_ownership.h"
#include "kh3ap/core/input/runtime_controller_input.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/NameTypes.hpp>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include <deque>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>
#endif

namespace PLH
{
    class x64Detour;
}

namespace kh3ap
{
    struct CharacterCombatSwap::Impl
    {
        using Clock = std::chrono::steady_clock;
        using Select = int (*)(RC::Unreal::UObject*);
        inline static Impl* active{};
        Player player;
        Change change;
        std::unique_ptr<PLH::x64Detour> sora_hook, aqua_hook, run_hook, camera_hook, attack_hook,
            prepare_hook;
        std::array<std::unique_ptr<PLH::x64Detour>, 3> alternate_hooks;
        std::array<uint64_t, 3> original_alternates{};
        uint64_t original_sora{}, original_aqua{}, original_run{}, original_camera{},
            original_attack{}, original_prepare{};
        bool attempted{}, installed{}, queued{}, dispatched{}, input_seeded{}, pose_pending{},
            run_pose_pending{};
        bool waiting_for_preload{};
        RC::Unreal::FWeakObjectPtr outgoing, request_world, incoming, positioned;
        RC::Unreal::FWeakObjectPtr first_movement_pawn;
        bool first_movement_pending{};
        std::array<float, 3> first_movement_location{};
        Clock::time_point first_movement_deadline{};
        int target{-1}, next_index{-1};
        bool air{}, falling{}, grounded{};
        bool air_jump_available{};
        uint32_t floating_mode{};
        std::array<float, 3> location{}, rotation{}, velocity{};
        std::array<float, 3> control_rotation{};
        RuntimeControllerStickState stick_input{}, look_input{};
        std::array<float, 3> movement_input{}, last_movement_input{}, acceleration{};
        float analog_modifier{}, control_modifier{};
        float movement_max_speed{}, movement_max_acceleration{};
        bool run_handoff{}, run_loop{};
        uint8_t run_walk_active{}, run_walk_input{};
        float run_walk_grace{};
        float run_heading{}, run_elapsed{}, run_phase{}, run_play_rate{1.0f};
        int run_step{};
        uint32_t run_flags{};
        RC::Unreal::FName run_animation{};
        bool swing_pending{}, resuming_swing{};
        int swing_index{-1};
        float swing_phase{}, swing_progress_rate{};
        bool swing_air{};
        bool swing_input_ready{}, swing_change_ready{};
        RC::Unreal::FWeakObjectPtr swing_target, swing_homing, swing_owner, swing_state;
        SwingHitHistory swing_hits;
        AttackInputBuffer attack_buffer;
        RuntimeOptionsInputOwnership attack_input;
        RC::Unreal::FWeakObjectPtr attack_press_controller;
        Clock::time_point attack_input_until{}, attack_release_at{};
        bool attack_press_held{}, replaying_attack{};
        unsigned long game_thread{};
        Clock::time_point run_until{};
        combat_swap_detail::CameraView rendered_view{};
        float rendered_timestamp{};
        combat_swap_detail::CameraDistance zoom{};
        combat_swap_detail::CameraOrbit orbit{};
        RC::Unreal::FWeakObjectPtr transition_camera;
        float camera_frame_delta{1.0f / 60.0f};
        int camera_transition_frames{};
        Clock::time_point requested_at{}, combo_until{};
        std::string error;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        uint64_t swaps{};
        uint64_t first_movement_updates{};
        std::deque<nlohmann::json> selections;
        bool camera_control_ready{}, camera_ui_current{};
        nlohmann::json camera_deltas = nlohmann::json::array();
        nlohmann::json swing_resume;
        nlohmann::json pose_handoff;
        uint64_t duplicate_hits_blocked{};
        uint64_t buffered_presses{}, replayed_presses{}, accepted_presses{};
#endif

        Impl(Player get_player, Change change_player);
        ~Impl();
        bool install();
        void remove_hooks();
        static bool available(RC::Unreal::UObject* pawn);
        static int select_riku(RC::Unreal::UObject* current);
        static int select_kairi(RC::Unreal::UObject* current);
        static int select_roxas(RC::Unreal::UObject* current);
        static int select_sora(RC::Unreal::UObject* current);
        static int select_aqua(RC::Unreal::UObject* current);
        static void update_camera(RC::Unreal::UObject* camera, float delta);
        void restore_movement_mode(RC::Unreal::UObject* pawn, RC::Unreal::UObject* movement);
        bool position_incoming_pawn(RC::Unreal::UObject* pawn);
        void restore_movement(RC::Unreal::UObject* pawn);
        static void enter_run(RC::Unreal::UObject* current);
        static void prepare_attack(RC::Unreal::UObject* current);
        static void enter_attack(RC::Unreal::UObject* current);
        void resume_swing(RC::Unreal::UObject* pawn);
        bool resume_run(RC::Unreal::UObject* pawn);
        void refresh_handoff_pose(RC::Unreal::UObject* pawn);
        int select(RC::Unreal::UObject* current, uint64_t original);
        std::optional<bool> attack_command_selected(RC::Unreal::UObject* pawn);
        bool attack_key(int controller, std::wstring_view key, int event);
        void release_attack_press();
        void tick_attack_buffer(Clock::time_point now);
        void capture_frame(RC::Unreal::UObject* pawn);
        bool request(RC::Unreal::UObject* pawn);
        void complete_first_movement(Clock::time_point now);
        auto tick(Clock::time_point now) -> std::optional<int>;
        bool swing_damage(RC::Unreal::UObject* victim, RC::Unreal::UObject* instigator,
                          RC::Unreal::UObject* causer, const void* event, bool record);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        struct TestSession
        {
            Impl& swap;
            RC::Unreal::FWeakObjectPtr test_world, test_controller;
            Clock::time_point test_until{}, test_next{}, test_release{};
            int test_phase{}, test_target{}, test_expected_index{};
            int test_input_mode{}, test_origin{};
            float test_swing_phase{0.35f};
            bool test_air{}, test_expire{}, test_held{}, test_jump{};
            bool test_camera{}, test_movement{}, test_rotate{}, test_menu{};
            bool test_locomotion{};
            bool test_camera_relative{};
            bool test_release_at_swap{};
            bool test_capture_gait{};
            std::map<int, std::vector<std::wstring>> test_gait_bones;
            int test_swaps_remaining{};
            float test_strength{};
            Clock::time_point test_started{};
            std::wstring test_action;
            std::string test_status = "idle";
            nlohmann::json test_result;

            void release_test_input();
            void test_press(RC::Unreal::UObject* pawn, const wchar_t* action,
                            Clock::time_point now);
            bool start_test(bool airborne, bool expire, float phase = 0.35f);
            bool start_menu_test();
            bool start_input_test(bool swap_back);
            bool start_camera_test();
            bool start_movement_test(bool rotate);
            auto movement_snapshot(RC::Unreal::UObject* pawn) -> nlohmann::json;
            bool start_locomotion_test(int count, int warmup_ms, int strength_percent,
                                       bool release_at_swap, bool camera_relative,
                                       bool capture_gait);
            void sample_gait(RC::Unreal::UObject* pawn, RC::Unreal::UObject* anim,
                             nlohmann::json& sample);
            void tick_locomotion_test(Clock::time_point now);
            void tick_movement_test(RC::Unreal::UObject* pawn, Clock::time_point now);
            void tick_camera_test(RC::Unreal::UObject* pawn, Clock::time_point now);
            void tick_menu_test(RC::Unreal::UObject* pawn, Clock::time_point now);
            void tick_test(Clock::time_point now);
        };
        TestSession session{*this};
#endif
    };
} // namespace kh3ap
