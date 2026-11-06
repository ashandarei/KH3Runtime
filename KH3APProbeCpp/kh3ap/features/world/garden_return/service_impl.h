#pragma once
#ifdef S
#pragma push_macro("S")
#undef S
#define KH3AP_GARDEN_RESTORE_S_MACRO
#endif
#include <polyhook2/Detour/x64Detour.hpp>
#ifdef KH3AP_GARDEN_RESTORE_S_MACRO
#pragma pop_macro("S")
#undef KH3AP_GARDEN_RESTORE_S_MACRO
#endif
#include "kh3ap/features/world/garden_return/garden_return_service.h"
#include "runtime_context.h"
#include "alternate_interaction_service.h"
#include "character_restoration.h"
#include "layout.h"
#include "kh3ap/core/input/options_input_ownership.h"
#include "kh3ap/features/kh02/combat/kh02_combat_swap.h"
#include "kh3ap/features/kh02/combat/kh02_combo_master.h"
#include "kh3ap/features/kh02/combat/kh02_companion_handoff.h"
#include "kh3ap/features/kh02/combat/kh02_mickey_link.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/UObject.hpp>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/bridge/transport/request_file_watcher.h"
#include <nlohmann/json.hpp>
#endif
namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace garden_return_detail;
    struct RuntimeGardenReturnService::Impl
    {

        using Input = bool (*)(UObject*, void*);
        using Close = void (*)(UObject*, bool);
        inline static Impl* active{};
        std::function<std::optional<std::filesystem::path>()> bridge;
        uintptr_t base{};
        bool steam{};
        uintptr_t input_rva{egs_camp_input_rva};
        uintptr_t controller_vtable{egs_camp_controller_vtable_rva};
        uintptr_t menu_vtable{egs_camp_menu_vtable_rva};
        size_t submenu_offset{egs_camp_submenu_offset};
        size_t busy_offset{egs_camp_busy_offset};

        void close_camp(UObject* menu);

        uint64_t original{};
        std::unique_ptr<PLH::x64Detour> hook;
        bool attempted{}, installed{}, pending{}, editing{};
        std::optional<bool> row_enabled;
        std::optional<bool> row_space;
        std::optional<bool> row_kh02;
        Clock::time_point selected_at{};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        RequestFileWatcher diagnostic_requests{L"runtime_garden_return_request.json"};
        std::function<std::string(int)> gummiphone_diagnostic;
        FWeakObjectPtr phone_key_controller;
        std::wstring phone_key_id, phone_key_completed, phone_key_action, phone_key_error;
        bool phone_key_down{};
        Clock::time_point phone_key_release{};
        FWeakObjectPtr save_press_controller;
        Clock::time_point save_press_release{};
        std::optional<bool> spellweaver_fixture_original;
#endif
        RuntimeOptionsInputOwnership character_input;
        Kh02ComboMaster combo_master;
        Kh02CompanionHandoff companion_handoff;
        Kh02MickeyLink mickey_link;
        Kh02CombatSwap combat_swap{[this]
                                   {
                                       return player();
                                   },
                                   [this](UObject* pawn, int id)
                                   {
                                       return change_player(pawn, id);
                                   }};
        garden_return_detail::CharacterRestoration restoration{
            garden_return_detail::CharacterRestorationConfig{
                [this]
                {
                    return pending;
                },
                [this]
                {
                    return interactions.save_pending() || interactions.chest_pending();
                },
                [this]
                {
                    return combat_swap.busy();
                },
                [this]
                {
                    return ready(camp());
                },
                [this](UObject* pawn, int id)
                {
                    return change_player(pawn, id);
                },
                [this](std::wstring_view value)
                {
                    phase.assign(value);
                }}};
        garden_return_detail::AlternateInteractionService interactions{
            garden_return_detail::AlternateInteractionConfig{[this]
                                                             {
                                                                 return ready(camp());
                                                             },
                                                             [this]
                                                             {
                                                                 return pending;
                                                             },
                                                             [this]
                                                             {
                                                                 restoration.select_aqua(true);
                                                             },
                                                             [this](std::wstring_view value)
                                                             {
                                                                 phase.assign(value);
                                                             },
                                                             [this](Clock::time_point deadline)
                                                             {
                                                                 restoration.defer_until(deadline);
                                                             }}};
        int pending_character = -1;
        bool character_dispatched{};
        uint64_t character_switches{};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        std::wstring challenge_hold_key, challenge_hold_world;
        bool challenge_hold_gamepad{true};
        Clock::time_point challenge_hold_release{};
#endif

        bool change_player(UObject* pawn, int id);

        bool character_key(int controller, std::wstring_view key, int event, bool);

        std::wstring source, phase = L"idle";
        uint64_t selections{};
        int32_t last_kind{}, last_anchor{}, last_parameter{};

        explicit Impl(decltype(bridge) resolver);

        ~Impl();

        UObject* camp();

        bool ready(UObject* menu);

        bool eligible();

        UObject* stock(UObject* menu, int index);

        UObject* body(UObject* menu);

        UObject* button(UObject* menu);

        UObject* clone_child(UObject* source_row, UObject* source_child, UObject* row);

        void install_button(UObject* menu);

        bool select(UObject* menu);

        static bool input(UObject* menu, void* event);

        void install();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        enum class DiagnosticProgress
        {
            Finished,
            Pending,
        };
        // References are borrowed for this call only; pending input copies its identity into owned
        // state.
        struct DiagnosticCommand
        {
            const std::string& text;
            UObject* menu;
            const std::wstring& id;
            const std::wstring& action;
            std::wstring& reason;
            nlohmann::json& scope_probe;
        };
        DiagnosticProgress diagnostic_character_style(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_cancel(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_experimental_pool(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_spellweaver_fixture(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_king_mickey_trap(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_experimental_select(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_alternate_wall_run_move(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_experimental_idle_battle(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_character_aim_race(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_character_aim_scope(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_experimental_swap(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_challenges(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_character_save(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_mickey_link_offer(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_ship_chart_cancel(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_saved_alternate_return(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_capture_scene(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_character_interact(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_costume(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_character_aqua(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_combat_swap(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_combat_test_ground(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_combat_test_camera(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_character_effect_suppress(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_character_effect_preview(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_combat_test_input(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_combat_test_movement(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_combat_test_locomotion(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_combat_give_combo_master(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_challenge_next(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_challenge_controller_hold_dpad(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_challenge_controller_burst(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_open(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_focus(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_gummiphone(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_gummiphone_inspect(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_gummiphone_confirm(DiagnosticCommand& request);
        DiagnosticProgress diagnostic_select(DiagnosticCommand& request);
        DiagnosticProgress dispatch_diagnostic(DiagnosticCommand& request);
        void diagnostic();
#endif

        void tick(Clock::time_point now);
    };
} // namespace kh3ap
