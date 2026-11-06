#pragma once
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/world/space/space_service.h"
#include "kh3ap/features/world/space/space_access.h"
#include "kh3ap/features/world/space/space_ships.h"
#include "kh3ap/features/world/space/space_policy.h"
#include "kh3ap/features/world/space/space_refresh_state.h"
#include "kh3ap/features/world/space/space_rewards.h"
#include "kh3ap/features/world/space/space_notifications.h"
#include "kh3ap/features/world/space/space_landing.h"
#include "kh3ap/features/world/space/save_space_policy.h"
#include "kh3ap/features/story/progression/game_flag_events.h"
#include <Unreal/FString.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/NameTypes.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <set>
#include <iosfwd>
#include <vector>

namespace RC::Unreal { class AActor; }
namespace kh3ap
{
    using RC::Unreal::FFrame;
    using RC::Unreal::FName;
    using RC::Unreal::FString;
    using RC::Unreal::FWeakObjectPtr;
    using RC::Unreal::UObject;
    struct SpaceDetour
    {
        uint64_t original{};
        std::unique_ptr<PLH::x64Detour> hook;
        void reset()
        {
            if (hook)
            {
                hook->unHook();
            }
            hook.reset();
            original = 0;
        }
        bool install(uintptr_t address, uintptr_t replacement)
        {
            hook = std::make_unique<PLH::x64Detour>(address, replacement, &original);
            if (hook->hook() && original)
            {
                return true;
            }
            if (hook->isHooked())
            {
                hook->unHook();
            }
            hook.reset();
            original = 0;
            return false;
        }
        ~SpaceDetour()
        {
            reset();
        }
    };
    struct RuntimeSpaceService::Impl
    {
        using Save = void (*)(void*, const FString&, const FString&, int32_t, FName);
        using Jump = bool (*)(void*, FName, FName, int32_t);
        using Landing = bool (*)(UObject*);
        using Lock = void (*)(UObject*, bool);
        using Mission = bool (*)(UObject*, uint8_t, void*);
        using Overview = bool (*)(UObject*);
        using IconState = void (*)(UObject*, FWeakObjectPtr*, uint8_t*);
        using TravelQuery = void (*)(UObject*, FFrame&, void*);
        inline static Impl* active{};
        SpaceDetour save, jump, landing, lock, mission, overview, icon_state, travel_query;
        SpaceDetour travel_begin;
        static void travel_begin_hook(UObject* actor);
        SpaceDetour landing_list;
        SpaceDetour first_embark;
        static bool first_embark_hook(UObject* dataset, int32_t world_code, void* result);
        SpaceDetour overview_default;
        static void overview_default_hook(UObject* manager);
        SpaceDetour embark_help;
        SpaceDetour embark_focus_help;
        SpaceDetour embark_selector, embark_input;
        static void embark_selector_hook(UObject* manager);
        static bool embark_input_hook(void* input, int32_t button);
        static bool embark_steam_input_hook(void* input, int32_t button, bool filtered);
        bool observe_embark_input(bool pressed, int32_t button);
        FWeakObjectPtr embark_input_manager;
        FWeakObjectPtr selector_menu;
        bool selector_help_visible{};
        std::wstring selector_help_text;
        int selector_help_zone{-1};
        uint64_t denied_embark_attempts{};
        std::chrono::steady_clock::time_point embark_denied_until{};
        std::wstring stock_selector_help;
        int selected_space_zone(UObject* manager) const;
        bool locked_embark_selection(UObject* manager) const;
        bool note_locked_embark(UObject* manager);
        void refresh_selector_help(UObject* manager);
        void restore_selector_help(UObject* menu);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        int embark_test_button{-1};
#endif
        bool embark_selected(UObject* menu);
        UObject* embark_help_field(UObject* menu);
        std::wstring read_embark_help(UObject* menu);
        static void embark_help_hook(UObject* menu, FString* message);
        static void embark_focus_help_hook(UObject* menu, FString* message);
        static bool embark_steam_focus_help_hook(UObject* menu, void* event);
        void render_embark_help(UObject* menu, FString* message, uint64_t original);
        RuntimeSpaceLanding landing_ui;
        static void landing_list_hook(UObject* menu, int32_t world_code);
        FWeakObjectPtr engine;
        std::wstring current_world;
        bool installed{}, attempted{};
        RuntimeSpaceAccess access;
        std::function<UObject*()> sora_pawn;
        RuntimeSpaceShips ships;
        bool ships_refresh_pending{};
        UObject* ship_inventory_context();
        bool can_travel(int zone = -1) const
        {
            return ships.usable(access, zone);
        }
        RuntimeSpaceRewards rewards;
        RuntimeSpaceNotifications notifications;
        bool space_journey{};
        space::SaveMenuReturn save_menu_return;
        std::array<bool, space::mission_count> pending_missions{};
        uint64_t mission_events{};
        uint64_t redirected_saves{}, redirected_jumps{}, denied_landings{};
        uint64_t denied_ship_travel{};
        space::RefreshRequest context_refresh;
        std::chrono::steady_clock::time_point context_deadline{};
        bool status_pending{true};
        std::string published_status;
        uint64_t context_samples{}, presentation_attempts{}, status_publications{};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        HANDLE command_changes{INVALID_HANDLE_VALUE};
#endif
        bool access_refresh_pending{true};
        uint64_t consumed_input_generation{};
        space::RefreshRequest flight_refresh, overview_refresh;
        FWeakObjectPtr presentation_world;
        bool presentation_ready{};
        struct FlightSymbol
        {
            FWeakObjectPtr actor;
            std::wstring code, path;
            bool locked{}, boss_locked{}, icon_visible{}, mist_visible{};
            float radius{};
        };
        std::vector<FlightSymbol> flight_symbols;
        uint64_t flight_visibility_updates{};
        uint64_t flight_discoveries{};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        struct ShadowTest
        {
            std::wstring request, world, seed, code;
            FWeakObjectPtr actor;
            std::vector<FlightSymbol> before;
            std::chrono::steady_clock::time_point release, deadline;
            bool shadow_observed{};
            bool release_requested{};
        };
        std::optional<ShadowTest> shadow_test;
#endif
        FWeakObjectPtr space_controller;
        bool loading_closed{}, controller_ready{};
        FWeakObjectPtr delivery_controller;
        uint64_t travel_points_enabled{};
        uint64_t world_fast_travel_enabled{};
        FWeakObjectPtr observed_overview;
        UObject* landing_manager();
        uint64_t overview_refreshes{};

        static std::optional<bool> bool_field(UObject* object, const wchar_t* name);
        std::optional<FlightSymbol> read_flight_symbol(UObject* actor);
        bool refresh_flight_symbols(std::chrono::steady_clock::time_point now);
        bool refresh_story_encounters();
        VBonusFlagEvents encounter_flags;
        uint64_t eclipse_unlock_recoveries{};
        void install_encounter_events();
        void poll_encounter_events(std::chrono::steady_clock::time_point now);
        bool refresh_eclipse_unlock();
        static void write_flight_symbols(std::ostream& json,
                                         const std::vector<FlightSymbol>& symbols);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        void shadow_test_result(const wchar_t* status, const std::wstring& reason);
        void poll_shadow_test(std::chrono::steady_clock::time_point now);
#endif

        UObject* overview_manager();
        bool refresh_open_overview(UObject* manager);
        bool refresh_overview_native(UObject* manager);

        static bool is_unlocked_travel_icon(UObject* icon);
        static int travel_icon_zone(UObject* icon);
        static void icon_state_hook(UObject* manager, FWeakObjectPtr* icon, uint8_t* record);
        static void travel_query_hook(UObject* icon, FFrame& stack, void* result);

        void apply_overview(UObject* manager);
        static bool overview_hook(UObject* manager);

        static UObject* object_field(UObject* object, const wchar_t* name);
        bool library_bool(const wchar_t* name);
        void sample_space();
        bool delivery_observed(UObject* controller) const;
        UObject* delivery_context(bool require_observed, bool recheck_loading = true);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        // Borrowed only while processing one verified diagnostic request.
        struct DiagnosticRequest
        {
            const std::string& text;
            const std::wstring& id;
            const std::wstring& operation;
            const std::wstring& map;
            const std::wstring& tag;
            const std::filesystem::path& path;
            std::wstring& reason;
        };
        void diagnostic_inspect_ships(DiagnosticRequest& request);
        struct OverviewSnapshot
        {
            UObject* manager;
            UObject* menu;
            int initial_state;
            int initial_mode;
            bool dispatched;
        };
        void publish_overview_result(DiagnosticRequest& request, const OverviewSnapshot& snapshot);
        void diagnostic_overview(DiagnosticRequest& request);
        void diagnostic_landing(DiagnosticRequest& request);
        void diagnostic_flight_shadow(DiagnosticRequest& request);
        void diagnostic_boss_cursor(DiagnosticRequest& request);
        void diagnostic_preview_notification(DiagnosticRequest& request);
        void diagnostic_jump(DiagnosticRequest& request);
        void process_command();
        // Borrowed for one command only; pending leases store weak references.
        struct BattleCommandContext
        {
            const std::wstring& action;
            UObject* pawn;
            UObject* manager;
            UObject* active;
            UObject* selected;
            const std::vector<RC::Unreal::AActor*>& starters;
        };
        void apply_battle_action(const std::string& text, const BattleCommandContext& context, std::ostream& body);
        void battle_protect(const std::string& text, const BattleCommandContext& context);
        void battle_restore();
        void battle_enable_fixture(const BattleCommandContext& context, std::ostream& body);
        void battle_approach(const BattleCommandContext& context, std::ostream& body);
        void battle_input(const std::string& text);
        void append_battle_snapshot(const BattleCommandContext& context, std::ostream& body);
        void append_battle_enemies(const BattleCommandContext& context, std::ostream& body);
        void append_battle_parts(const BattleCommandContext& context, std::ostream& body);
        void battle_command(const std::string& text, const std::wstring& request,
                            std::wstring reason);
        FWeakObjectPtr protected_ship;
        std::vector<FWeakObjectPtr> enabled_battle_fixtures;
        FWeakObjectPtr battle_input_controller;
        std::wstring battle_input_key;
        std::chrono::steady_clock::time_point battle_input_release{};
        bool ship_original_invincible{};
        std::chrono::steady_clock::time_point ship_protection_deadline{};
        void poll_ship_protection(std::chrono::steady_clock::time_point now);
#endif

        Impl(RuntimeSpaceAccess::BridgeResolver value, std::function<UObject*()> pawn_resolver)
            : access(std::move(value)), sora_pawn(std::move(pawn_resolver))
        {
        }
        ~Impl()
        {
            unsubscribe_game_flags(encounter_flags);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            if (command_changes != INVALID_HANDLE_VALUE)
            {
                FindCloseChangeNotification(command_changes);
            }
#endif
            if (active == this)
            {
                active = nullptr;
            }
        }

        auto world() -> UObject*;
        auto world_name() -> std::wstring;
        bool redirect(std::wstring_view source, std::wstring_view target) const;
        bool can_land(std::wstring_view code) const;
        std::wstring_view actor_world(UObject* actor);

        static void save_hook(void* owner, const FString& map, const FString& tag,
                              int32_t world_code, FName display);

        static bool jump_hook(void* owner, FName map, FName tag, int32_t fade);
        static bool landing_hook(UObject* actor);
        static void lock_hook(UObject* actor, bool locked);
        static bool mission_hook(UObject* manager, uint8_t id, void* counters);

        void install_once();

        void tick(std::chrono::steady_clock::time_point now);
        void publish_status();

        void flush_presentation(std::chrono::steady_clock::time_point now);
    };
} // namespace kh3ap
