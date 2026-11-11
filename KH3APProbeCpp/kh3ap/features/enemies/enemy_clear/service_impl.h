#pragma once
#include "kh3ap/bridge/transport/request_file_watcher.h"

#include "kh3ap/features/enemies/enemy_clear_service.h"
#include "kh3ap/features/enemies/mom_barrier_service.h"
#include "kh3ap/features/enemies/sora_experience_test.h"
#include "kh3ap/features/grants/messages/ap_message_param_storage.h"
#include "kh3ap/core/events/keyblade_inventory_events.h"

#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/FFrame.hpp>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <vector>

namespace RC::Unreal
{
    class UClass;
}

namespace kh3ap
{
    // One owner retains request, restoration, weak-identity and callback state.
    // Implementations are grouped by operation; update order lives in update.cpp.
    struct RuntimeEnemyClearService::Impl
    {
        using UObject = RC::Unreal::UObject;
        using UClass = RC::Unreal::UClass;
        using FFrame = RC::Unreal::FFrame;
        using FWeakObjectPtr = RC::Unreal::FWeakObjectPtr;
        using Clock = std::chrono::steady_clock;
        struct Target
        {
            FWeakObjectPtr actor{}, entity{};
            std::wstring name, class_name, entity_name;
            int64_t hp_before{}, hp_after{}, max_hp{}, state_before{}, state_after{}, team{};
            int32_t kills_before{-1}, kills_after{-1};
            std::array<float, 3> location{};
            bool requested{}, death_observed{}, vanished{}, invincible{}, hp_floor{}, no_damage{};
            float damage_returned{}, damage_requested{};
            size_t entity_targets{};
            std::wstring proof;
        };
        RuntimeEnemyClearConfig config;
        inline static constexpr auto request_name = L"runtime_enemy_clear_request.json";
        RequestFileWatcher requests{request_name};
        bool mutation_context_ready{}, mutation_control_ready{};
        std::optional<std::filesystem::path> dir;
        std::wstring request_id, command, expected_world, world_name, pawn_name;
        std::deque<std::wstring> consumed;
        FWeakObjectPtr sora{}, world{};
        std::vector<Target> targets;
        std::string spawn_points{"[]"};
        std::string scene_objects{"[]"};
        std::string emblems{"[]"};
        std::string camera_snapshot{"null"};
        bool camera_look{};
        float look_x{}, look_y{};
        FWeakObjectPtr look_controller{};
        Clock::time_point look_until{};
        FWeakObjectPtr photo_movement{}, photo_pawn{}, photo_attachment{};
        FWeakObjectPtr photo_fov_component{};
        float photo_original_fov{};
        bool photo_tick_enabled{};
        Clock::time_point photo_hold_until{};
        std::string ingredient_counts{"{}"};
        std::string keyblade_ownership{"null"};
        std::string boss_setting{"null"};
        std::string boss_hp_setup_result{"null"};
        SoraExperienceSnapshot experience_before{}, experience_after{};
        int32_t experience_requested{};
        bool experience_dispatched{};
        std::wstring interaction_actor, interaction_treasure;
        bool interaction_dispatched{};
        FWeakObjectPtr input_controller{};
        FWeakObjectPtr union_input_ride{};
        std::wstring input_action;
        bool input_dispatched{}, input_released{};
        bool waiting_dive_window{};
        bool combo_active{}, combo_held{};
        int combo_requested{}, combo_pressed{}, combo_released{}, combo_interval_ms{};
        Clock::time_point combo_next{}, combo_last_press{};
        int64_t combo_max_gap_ms{};
        FWeakObjectPtr combo_controller{};
        FWeakObjectPtr container_ref{}, container_chest_ref{};
        std::wstring container_name;
        bool break_dispatched{}, container_destroyed{}, chest_exposed{};
        Clock::time_point release_at{};
        Clock::time_point next_poll{}, started{};
        int64_t expires{}, sora_hp_before{}, sora_hp_after{};
        std::array<float, 3> actor_rotation{};
        bool transform_captured{};
        std::array<float, 3> position_before{}, position_after{}, position_immediate{},
            requested_position{};
        bool waiting{}, position_dispatched{};
        size_t skipped_friendly{}, next_target{};
        RuntimeMoMBarrierService mom_barrier;
        double damage_percent{};
        Clock::time_point pulse_observe_at{};
        FWeakObjectPtr protected_sora{}, protected_world{};
        bool original_no_damage{}, original_hp_floor{};
        bool original_no_damage_response{}, protection_no_damage_response{};
        int64_t protection_expires{};
        FWeakObjectPtr protection_swap_source, protection_swap_incoming;
        int64_t protection_swap_until{};
        inline static Impl* instance{};
        int64_t opening_protection_expires{};
        bool opening_protection_seen{};
        FWeakObjectPtr opening_engine, opening_world, opening_controller;
        Clock::time_point opening_discovery_at{};
        std::wstring protection_reason{L"off"};
        FWeakObjectPtr centered_boss{}, center_world{}, center_sora{};
        std::array<float, 3> center_original{}, center_before{}, center_after{};
        std::wstring center_actor;
        int64_t center_expires{};
        FWeakObjectPtr anchor_boss{}, anchor_entity{}, anchor_pawn{}, anchor_world{};
        FWeakObjectPtr anchor_actor{}, anchor_original{};
        int64_t anchor_expires{};
        std::string anchor_snapshot{"null"};
        void accept_xemnas_anchor(UObject* pawn, const std::string& text);
        void restore_xemnas_anchor();
        void tick_xemnas_anchor();
        // Agent-only experiment. The supported EGS native getter copies this
        // non-reflected Vector; verify its image, instructions and reflected
        // return against storage before any game-thread write.
        std::array<float, 3> read_test_center(UObject* boss);
        void restore_test_center();
        void tick_test_center();
        void set_condition(UObject* pawn, const wchar_t* function, const wchar_t* param,
                           bool value);
        void remove_protection();
        void tick_protection();
        void tick_opening_protection();
        std::string mom_snapshot{"null"};
        explicit Impl(RuntimeEnemyClearConfig c);
        struct ChestGate
        {
            FWeakObjectPtr gate_chest, gate_world, gate_original_material;
            bool gate_enabled{}, gate_locked_observed{}, gate_grant_dispatched{}, gate_colored{};
            std::array<float, 4> gate_original_hsb{};
            uint64_t gate_inventory_revision{};
            size_t gate_inventory_checks{};
            bool gate_inventory_dirty{}, gate_owned{};
            FWeakObjectPtr notice_object, notice_reactor;
            bool gate_notification{};
            uint8_t gate_original_command{3};
            size_t notices_shown{};
            std::string notice_error;
            Clock::time_point notice_next{};
            RuntimeApMessageParamStorage notice_storage;

            std::wstring keyblade{L"WEP_KEYBLADE_SO_02"}, keyblade_name{L"Shooting Star"};
        };
        std::map<std::wstring, ChestGate> chest_gates;
        RuntimeKeybladeOwnershipCache keyblade_cache;
        FWeakObjectPtr cache_pawn, cache_world;
        Clock::time_point gate_next_poll{};
        std::string gate_snapshot{"null"}, gates_snapshot{"[]"};
        inline static Impl* notice_instance{};
        ~Impl();
        void ensure_cache_context(UObject* pawn);
        void refresh_keyblade_group(UObject* pawn, const std::wstring& token);
        static void chest_notice_thunk(UObject* context, FFrame& stack, void*);
        void remove_chest_notice(ChestGate& gate);
        void bind_chest_notice(UObject* pawn, ChestGate& gate);

        void update_chest_gate(UObject* pawn, ChestGate& gate);
        void process_chest_gate_events(ChestGate& gate);
        std::string inspect_chest_gate(UObject* pawn, ChestGate& gate);

        void report(std::wstring_view status, std::wstring_view reason);
        void finish(std::wstring_view status, std::wstring_view reason);

        UObject* controlled_combat_pawn();
        void accept_union_cross_input(const std::string& text);
        UObject* request_pawn();
        bool gameplay(UObject* pawn);
        std::vector<UObject*> owned_components(UObject* actor, UClass* component_class);
        std::array<float, 3> component_vector(UObject* component, const wchar_t* name);
        std::array<UObject*, 6> photo_friend_slots(UObject* pawn, UObject** manager_out = nullptr);
        void inspect_emblems(UObject* pawn);
        UObject* held_camera(UObject* pawn);
        void detach_photo_attachment();
        void attach_photo_marker(UObject* pawn, std::wstring_view path);
        void restore_photo_movement();
        void inspect_camera(UObject* pawn);
        void inspect_scene(UObject* pawn);
        UObject* resolve_chest(UObject* pawn, const std::wstring& requested_actor,
                               const std::wstring& requested_treasure);
        void break_container(UObject* pawn, const std::wstring& requested_actor,
                             const std::wstring& requested_treasure);
        void open_chest(UObject* pawn, const std::wstring& requested_actor,
                        const std::wstring& requested_treasure);
        void press_action(UObject* controller, std::wstring_view action);
        void enumerate(UObject* pawn);
        void sample();
        void set_gigas_position(UObject* pawn);
        void set_position(UObject* pawn);
        void damage(Target& target, UObject* actor, UObject* pawn);
        void accept_pooh_input(const std::string& text);
        void accept_photo_fov(UObject* pawn, const std::string& text);
        void accept_photo_party(UObject* pawn, const std::string& text);
        void accept_photo_position(UObject* pawn, const std::string& text);
        void accept_camera_look(UObject* pawn, const std::string& text);
        void accept_inspect_emblems(UObject* pawn, const std::string& text);
        void accept_boss_setting(UObject* pawn, const std::string& text);
        void accept_inspect_transform(UObject* pawn, const std::string& text);
        void accept_boss_hp_setup(UObject* pawn, const std::string& text);
        void accept_boss_center(UObject* pawn, const std::string& text);
        void accept_protection(UObject* pawn, const std::string& text);
        void accept_damage(UObject* pawn, const std::string& text);
        void accept_experience(UObject* pawn, const std::string& text);
        void accept_mom_barrier(UObject* pawn, const std::string& text);
        void accept_repair_chest_record(UObject* pawn, const std::string& text);
        void accept_chest_gate(UObject* pawn, const std::string& text);
        void accept_inspect_scene(UObject* pawn, const std::string& text);
        void accept_attack_combo(UObject* pawn, const std::string& text);
        void accept_press_action(UObject* pawn, const std::string& text);
        void accept_pooh_sign(UObject* pawn, const std::string& text);
        void accept_open_chest(UObject* pawn, const std::string& text);
        void accept_break_emblem_cover(UObject* pawn, const std::string& text);
        void accept_break_container(UObject* pawn, const std::string& text);
        struct RequestCommand
        {
            std::wstring_view name;
            void (Impl::*handler)(UObject*, const std::string&);
            bool requires_world;
            bool before_health;
        };
        static const RequestCommand* find_request_command(std::wstring_view name) noexcept;
        void accept_cactuar_spawn(UObject* pawn, const std::string& text);
        void accept();
        bool update_camera_input(Clock::time_point now);
        bool update_combo_input(Clock::time_point now);
        bool update_action_input(Clock::time_point now);
        void tick();
    };
} // namespace kh3ap
