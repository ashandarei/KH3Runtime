#include "kh3ap/bridge/transport/request_file_watcher.h"
#include <memory>
#include <optional>
#include "kh3ap/features/world/save_slot_policy.h"
#include "kh3ap/features/ui/title_state_ids.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/world/startup/autoload_service.h"
#include "kh3ap/features/world/loading_runtime.h"
#include "kh3ap/features/world/startup/new_game_service.h"
#include "kh3ap/features/world/startup/fast_new_game_service.h"
#include "kh3ap/features/enemies/enemy_clear_service.h"
#include "kh3ap/features/world/ui_control/ui_control_service.h"
#include "kh3ap/features/world/space/space_service.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/bridge/transport/bridge_paths.h"

#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/features/kh02/runtime/kh02_entry_service.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/utilities.h"

#include <Unreal/FProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr uint8_t unknown_native_flag_byte = 0xff;

        enum StartupSetupStep : int
        {
            setup_not_started,
            setup_waiting_for_notice,
            setup_waiting_for_notice_close,
            setup_waiting_for_settings,
            setup_complete,
        };
        constexpr auto load_completion_storage_bytes = 0x40;
        constexpr auto maximum_game_difficulty = 3;
        constexpr auto title_ui_manager_offset = 0x548;
        constexpr auto title_movie_offset = 0x540;
        constexpr auto title_ready_state = 5;
        constexpr auto title_new_game_busy_offset = 0x5a5;
        constexpr auto title_new_game_confirmation_offset = 0x5d0;
        constexpr auto title_new_game_difficulty_offset = 0x588;
        constexpr auto title_difficulty_confirmed_navigation_state = 20;

        constexpr auto save_catalog_entry_stride = 0xf88;
        constexpr auto save_catalog_slot_offset = 0x60;
        constexpr auto save_catalog_mode_offset = 0x2b;
        constexpr auto save_catalog_special_flag_shift = 26;
        constexpr auto title_load_kind_offset = 0x574;
        constexpr auto title_load_mode_offset = 0x578;
        constexpr auto title_load_special_offset = 0x579;
        constexpr auto title_navigation_state_offset = 0x570;
        constexpr auto title_notice_state_offset = 0x57c;
        constexpr auto title_settings_confirmed_offset = 0x5e8;
        constexpr auto steam_load_completion_vtable_rva = 0x6a57a08;
        constexpr auto steam_load_completion_handle_rva = 0x2cf02a0;
        constexpr auto completion_title_offset = 8;
        constexpr auto completion_handle_offset = 0x18;
        constexpr auto completion_inline_units_offset = 0x30;
        constexpr auto completion_destructor_vtable_offset = 0x48;
        constexpr auto completion_heap_allocation_offset = 0x20;
        constexpr auto save_manager_load_state_offset = 0x69d0;
        constexpr auto save_manager_load_result_offset = 0x6a20;
        constexpr auto save_manager_load_slot_offset = 0x6a30;
        constexpr auto open_startup_notice_rva = 0x18c4670;
        constexpr auto open_startup_settings_rva = 0x18ca640;
        constexpr auto max_autoload_request_lifetime_ms = 600000;
        constexpr auto autoload_report_interval = std::chrono::seconds(1);
        constexpr auto autoload_title_stability_duration = std::chrono::seconds(2);
        constexpr auto autoload_gameplay_stability_duration = std::chrono::milliseconds(2000);
        constexpr auto autoload_update_interval = std::chrono::milliseconds(250);
        constexpr auto autoload_control_stability_duration = std::chrono::milliseconds(250);
    } // namespace

    using namespace RC::Unreal;
    using namespace loading_runtime;
    namespace
    {
        using Clock = std::chrono::steady_clock;
        constexpr auto request_name = L"runtime_autoload_request.json";
        constexpr auto result_name = L"runtime_autoload_result.json";
        constexpr auto events_name = L"runtime_autoload_events.jsonl";
        constexpr int ready_title_menu_state = title_state_ids::menu_ready;

        void dispatch_normal_continue(uintptr_t base, UObject* title)
        {
            if (!character_native::verify_sites({character_native::Site::load_continue_selector}))
            {
                throw std::runtime_error("native_continue_selector_profile_mismatch");
            }
            // Call the ordinary newest-save selector. It owns metadata choice,
            // completion construction, and slot dispatch. Deliberately perform
            // no KH0.2 preparation here: the common native load gate must hold it.
            using Continue = void(__fastcall*)(UObject*);
            reinterpret_cast<Continue>(
                base +
                character_native::rva(character_native::Site::load_continue_selector))(title);
        }

        struct AutosaveMetadata
        {
            bool found{};
            uint32_t flags{};
            uint8_t mode{};
            int32_t catalog_count{};
        };

        AutosaveMetadata autosave_metadata(uintptr_t base, int requested_slot)
        {
            AutosaveMetadata out{};
            uintptr_t entries{};
            if (!read_native(base + native_rva(catalog_rva), entries) ||
                !read_native(base + native_rva(catalog_count_rva), out.catalog_count) || !entries ||
                out.catalog_count < 1 || out.catalog_count > save_slots::catalog_capacity)
            {
                return {};
            }
            // Read only the game's already-loaded save catalog, never save files.
            for (int i = 0; i < out.catalog_count; ++i)
            {
                const uintptr_t entry =
                    entries + static_cast<uintptr_t>(i) * save_catalog_entry_stride;
                uint8_t slot{};
                if (!read_native(entry + save_catalog_slot_offset, slot))
                {
                    return {};
                }
                if (slot == requested_slot)
                {
                    out.found = read_native(entry, out.flags) &&
                                read_native(entry + save_catalog_mode_offset, out.mode);
                    return out;
                }
            }
            return out;
        }

        UObject* live_title()
        {
            std::vector<UObject*> objects;
            UObjectGlobals::FindAllOf(STR("TresHUDTitle"), objects);
            for (auto* object : objects)
            {
                std::wstring name;
                if (is_uobject_valid_fast(object) && try_get_uobject_full_name_fast(object, name) &&
                    name.find(STR("Default__")) == std::wstring::npos &&
                    name.find(STR("PersistentLevel")) != std::wstring::npos)
                {
                    return object;
                }
            }
            return nullptr;
        }

        void dispatch_slot_load(uintptr_t base, UObject* title, UObject* manager,
                                const AutosaveMetadata& metadata, int slot)
        {
            // Use KH3's own delegate allocation/copy/destruction ABI. The game
            // retains its callback as usual; our service retains only weak refs.
            alignas(16) std::array<std::byte, load_completion_storage_bytes> completion{};
            using Make = void*(__fastcall*)(void*, UObject**);
            using Load = void(__fastcall*)(UObject*, int32_t, void*, bool, bool, bool);
            using Destroy = void(__fastcall*)(void*);
            auto* title_bytes = reinterpret_cast<std::byte*>(title);
            const int32_t load_kind = 0;
            const int32_t idle = 0;
            const uint8_t special = (metadata.flags >> save_catalog_special_flag_shift) & 1;
            std::memcpy(title_bytes + title_load_kind_offset, &load_kind, sizeof(load_kind));
            std::memcpy(title_bytes + title_load_mode_offset, &metadata.mode,
                        sizeof(metadata.mode));
            std::memcpy(title_bytes + title_load_special_offset, &special, sizeof(special));
            // Suspend only title navigation while its native load callback owns
            // progress. Success resumes KH3's normal load-to-world state machine.
            std::memcpy(title_bytes + title_navigation_state_offset, &idle, sizeof(idle));
            if (startup_store == AutoloadStore::steam)
            {
                const uintptr_t vtable = base + steam_load_completion_vtable_rva;
                const uint64_t handle = reinterpret_cast<uint64_t(__fastcall*)()>(
                    base + steam_load_completion_handle_rva)();
                const int32_t inline_units = 2;
                std::memcpy(completion.data(), &vtable, sizeof(vtable));
                std::memcpy(completion.data() + completion_title_offset, &title, sizeof(title));
                std::memcpy(completion.data() + completion_handle_offset, &handle, sizeof(handle));
                std::memcpy(completion.data() + completion_inline_units_offset, &inline_units,
                            sizeof(inline_units));
            }
            else
            {
                reinterpret_cast<Make>(base + native_rva(make_completion_rva))(completion.data(),
                                                                               &title);
            }
            reinterpret_cast<Load>(base + native_rva(load_slot_rva))(
                manager, slot, completion.data(), true, true, false);
            if (startup_store == AutoloadStore::steam)
            {
                // The loader clones the inline delegate into its own storage.
                // Destroy only our inline instance; no heap allocation is ours.
                const auto vtable = read_field<uintptr_t>(completion.data(), 0);
                reinterpret_cast<void(__fastcall*)(void*, uint8_t)>(read_field<uintptr_t>(
                    reinterpret_cast<void*>(vtable), completion_destructor_vtable_offset))(
                    completion.data(), 0);
                return;
            }
            reinterpret_cast<Destroy>(base + native_rva(destroy_delegate_rva))(completion.data());
            void* remaining_allocation =
                read_field<void*>(completion.data(), completion_heap_allocation_offset);
            if (remaining_allocation)
            {
                reinterpret_cast<Destroy>(base + native_rva(free_rva))(remaining_allocation);
            }
        }

        int64_t unix_ms()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }

        UObject* live_save_manager()
        {
            std::vector<UObject*> objects;
            UObjectGlobals::FindAllOf(STR("TresUIManager"), objects);
            UObject* found = nullptr;
            for (auto* object : objects)
            {
                std::wstring name;
                if (!is_uobject_valid_fast(object) ||
                    !try_get_uobject_full_name_fast(object, name) ||
                    name.find(STR("Default__")) != std::wstring::npos)
                {
                    continue;
                }
                if (found)
                {
                    return nullptr; // Never choose between competing worlds.
                }
                found = object;
            }
            return found;
        }

        bool movement_input_enabled(UObject* pawn)
        {
            auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
            if (!is_uobject_valid_fast(controller) ||
                try_read_reflected_object_property_value(controller, L"Pawn") != pawn)
            {
                return false;
            }
            auto* function = controller->GetFunctionByNameInChain(L"IsMoveInputIgnored");
            if (!is_uobject_valid_fast(function) || function->GetParmsSize() != 1)
            {
                return false;
            }
            uint8_t ignored = 1;
            controller->ProcessEvent(function, &ignored);
            return ignored == 0;
        }

        void dispatch_slot_save(uintptr_t base, UObject* manager, int slot)
        {
            // Normal TresUIManager manual-save path, also used by TresUISaveLoad
            // at 0x1a90cec. Empty external delegate; KH3 retains its own internal
            // callback. Completion is independently observed below.
            alignas(16) std::array<std::byte, load_completion_storage_bytes> completion{};
            using Save = void(__fastcall*)(UObject*, int32_t, void*, bool, bool);
            reinterpret_cast<Save>(base + native_rva(save_slot_rva))(
                manager, slot, completion.data(), true, false);
        }
    } // namespace

    struct RuntimeAutoloadService::Impl
    {
        explicit Impl(RuntimeAutoloadConfig value) : config(std::move(value))
        {
        }
        RuntimeAutoloadConfig config;
        RequestFileWatcher requests{request_name};
        std::optional<std::filesystem::path> active_dir;
        std::wstring request_id;
        std::wstring command;
        std::wstring phase;
        std::wstring last_reason;
        std::wstring last_request_id;
        std::wstring expected_map;
        std::wstring expected_seed, expected_player;
        std::string starter_options{"null"};
        int setup_step{};
        Clock::time_point next_poll{};
        Clock::time_point stable_since{};
        Clock::time_point last_report{};
        FWeakObjectPtr title_ref{};
        FWeakObjectPtr manager_ref{};
        FWeakObjectPtr ready_pawn_ref{};
        std::wstring observed_pawn_name;
        int64_t expires{};
        bool active{};
        bool dispatched{};
        bool supported{};
        bool title_active{};
        bool map_ready{};
        bool station_ready{};
        bool blocking_ui{};
        bool idle{};
        bool input_enabled{};
        bool control_ready{};
        bool allow_control_ready{};
        bool observe_continue_only{};
        bool defer_kh02_preparation{};
        bool allow_space{}, space_ready{};
        bool loading_screen_closed{};
        bool loading_continue_dispatched{};
        bool opening_continue_dispatched{};
        bool loading_transition_complete{};
        bool loading_prompt_ready{};
        int loading_screen_mode{-1};
        float loading_screen_progress{-1.0f};
        bool save_closed{};
        int native_save_result{};
        int requested_slot{};
        int difficulty{-1};
        uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        int title_state{-1};
        int load_state{-1};
        int load_result{-1};
        int observed_slot{-1};
        uint64_t load_gate_failure_sequence{};
        AutosaveMetadata metadata{};

        void report(std::wstring_view status, std::wstring_view reason, bool terminal = false)
        {
            if (!active_dir)
            {
                return;
            }
            const auto now = Clock::now();
            if (!terminal && phase == status && last_reason == reason &&
                now - last_report < autoload_report_interval)
            {
                return;
            }
            phase = status;
            last_reason = reason;
            last_report = now;
            std::ostringstream out;
            out << "{\"schema\":\"kh3ap.runtime_autoload.v1\",\"request_id\":\""
                << json_escape(request_id) << "\",\"command\":\"" << json_escape(command)
                << "\",\"status\":\"" << json_escape(status) << "\",\"reason\":\""
                << json_escape(reason) << "\",\"at\":\"" << now_iso8601_utc()
                << "\",\"process_id\":" << GetCurrentProcessId()
                << ",\"expires_unix_ms\":" << expires << ",\"slot\":" << requested_slot
                << ",\"dispatched\":" << (dispatched ? "true" : "false")
                << ",\"observed_slot\":" << observed_slot
                << ",\"supported_build\":" << (supported ? "true" : "false") << ",\"storefront\":\""
                << (startup_store == AutoloadStore::steam ? "steam" : "egs") << "\""
                << ",\"title_active\":" << (title_active ? "true" : "false")
                << ",\"title_state\":" << title_state << ",\"load_state\":" << load_state
                << ",\"load_result\":" << load_result
                << ",\"catalog_count\":" << metadata.catalog_count
                << ",\"autosave_available\":" << (metadata.found ? "true" : "false")
                << ",\"map_ready\":" << (map_ready ? "true" : "false")
                << ",\"space_ready\":" << (space_ready ? "true" : "false")
                << ",\"station_ready\":" << (station_ready ? "true" : "false")
                << ",\"difficulty\":" << difficulty << ",\"expected_seed\":\""
                << json_escape(expected_seed) << "\",\"expected_player\":\""
                << json_escape(expected_player) << "\""
                << ",\"blocking_ui\":" << (blocking_ui ? "true" : "false")
                << ",\"sora_idle\":" << (idle ? "true" : "false")
                << ",\"input_enabled\":" << (input_enabled ? "true" : "false")
                << ",\"control_ready\":" << (control_ready ? "true" : "false")
                << ",\"loading_screen_closed\":" << (loading_screen_closed ? "true" : "false")
                << ",\"loading_continue_dispatched\":"
                << (loading_continue_dispatched ? "true" : "false")
                << ",\"opening_continue_dispatched\":"
                << (opening_continue_dispatched ? "true" : "false")
                << ",\"loading_transition_complete\":"
                << (loading_transition_complete ? "true" : "false")
                << ",\"loading_prompt_ready\":" << (loading_prompt_ready ? "true" : "false")
                << ",\"loading_screen_mode\":" << loading_screen_mode
                << ",\"loading_screen_progress\":" << loading_screen_progress
                << ",\"save_closed\":" << (save_closed ? "true" : "false")
                << ",\"native_save_result\":" << native_save_result
                << ",\"setup_step\":" << setup_step << ",\"starter_options\":" << starter_options
                << ",\"pawn\":\"" << json_escape(observed_pawn_name) << "\"}\n";
            write_text_file_atomic_verified(*active_dir / result_name, out.str());
            append_text_file(*active_dir / events_name, out.str());
            if (terminal)
            {
                active = false;
            }
        }

        void accept()
        {
            const auto dir = config.bridge_dir();
            if (!dir)
            {
                return;
            }
            const auto path = *dir / request_name;
            if (!path_points_to_regular_file(path))
            {
                return;
            }
            const auto text = read_text_file(path);
            if (!text)
            {
                return;
            }
            try_remove_file(path);
            request_id = JsonFieldReader::string_field(*text, "request_id").value_or(L"");
            if (request_id.empty() || request_id == last_request_id)
            {
                return;
            }
            last_request_id = request_id;
            active_dir = dir;
            active = true;
            dispatched = false;
            title_ref = nullptr;
            // The UI manager belongs to GameInstance and normally survives
            // travel. Keep its weak identity between requests; Get() rejects
            // a collected/replaced manager without repeating a global scan.
            ready_pawn_ref = nullptr;
            observed_pawn_name.clear();
            starter_options = "null";
            setup_step = setup_not_started;
            stable_since = {};
            metadata = {};
            map_ready = station_ready = blocking_ui = false;
            idle = input_enabled = save_closed = false;
            loading_screen_closed = loading_continue_dispatched = false;
            opening_continue_dispatched = false;
            loading_transition_complete = loading_prompt_ready = false;
            loading_screen_mode = -1;
            loading_screen_progress = -1.0f;
            native_save_result = 0;
            observed_slot = -1;
            load_gate_failure_sequence = 0;
            title_state = load_state = load_result = -1;
            command = JsonFieldReader::string_field(*text, "command").value_or(L"");
            control_ready = false;
            allow_control_ready =
                JsonFieldReader::bool_field(*text, "allow_control_ready").value_or(false);
            observe_continue_only =
                JsonFieldReader::bool_field(*text, "observe_continue_only").value_or(false);
            defer_kh02_preparation =
                JsonFieldReader::bool_field(*text, "defer_kh02_preparation").value_or(false);
            allow_space = JsonFieldReader::bool_field(*text, "allow_space").value_or(false);
            space_ready = false;
            expected_map = JsonFieldReader::string_field(*text, "expected_map").value_or(L"");
            expected_seed = JsonFieldReader::string_field(*text, "expected_seed").value_or(L"");
            expected_player = JsonFieldReader::string_field(*text, "expected_player").value_or(L"");
            const auto raw_difficulty =
                JsonFieldReader::int64_field(*text, "difficulty").value_or(-1);
            difficulty = raw_difficulty >= 0 && raw_difficulty <= maximum_game_difficulty
                             ? static_cast<int>(raw_difficulty)
                             : -1;
            const auto raw_slot =
                JsonFieldReader::int64_field(*text, "slot").value_or(save_slots::autosave);
            requested_slot = save_slots::is_native_slot(raw_slot) ? static_cast<int>(raw_slot) : -1;
            expires = JsonFieldReader::int64_field(*text, "expires_unix_ms").value_or(0);
            supported = supported_image(base);
            const auto pid = JsonFieldReader::int64_field(*text, "process_id").value_or(0);
            if (pid && pid != GetCurrentProcessId())
            {
                report(L"error", L"process_id_mismatch", true);
                return;
            }
            if (expires <= unix_ms() || expires > unix_ms() + max_autoload_request_lifetime_ms)
            {
                report(L"error", L"invalid_or_expired_request", true);
                return;
            }
            if (command != L"inspect_startup" && command != L"load_autosave" &&
                command != L"load_slot" && command != L"continue_native" &&
                command != L"save_slot" && command != L"wait_gameplay" && command != L"new_game" &&
                command != L"setup_startup")
            {
                report(L"error", L"unknown_command", true);
                return;
            }
            if (command == L"wait_gameplay" &&
                (pid != GetCurrentProcessId() ||
                 (!expected_map.empty() && !expected_map.starts_with(L"/Game/"))))
            {
                report(L"error", L"wait_requires_current_process_and_valid_map", true);
                return;
            }
            // Default to reserved test slots. A user-named ordinary slot must
            // be explicitly repeated in the request; slot zero is never allowed.
            const auto authorized_save_slot =
                JsonFieldReader::int64_field(*text, "authorized_save_slot");
            if (command == L"save_slot" &&
                (!save_slots::is_manual_slot(requested_slot) ||
                 (authorized_save_slot && *authorized_save_slot != requested_slot) ||
                 (requested_slot < save_slots::first_agent_test &&
                  (!authorized_save_slot || *authorized_save_slot != requested_slot))))
            {
                report(L"error", L"test_slot_must_be_50_to_99", true);
                return;
            }
            if (command == L"load_slot" && !save_slots::is_manual_slot(requested_slot))
            {
                report(L"error", L"manual_slot_must_be_1_to_99", true);
                return;
            }
            if (command == L"load_autosave" && requested_slot != save_slots::autosave)
            {
                report(L"error", L"autosave_slot_must_be_zero", true);
                return;
            }
            if (defer_kh02_preparation && command != L"load_autosave" && command != L"load_slot")
            {
                report(L"error", L"native_load_gate_requires_selected_slot_load", true);
                return;
            }
            if (defer_kh02_preparation &&
                (startup_store == AutoloadStore::unsupported || !native_load_gate_installed()))
            {
                report(L"error", L"native_load_gate_not_installed_for_this_runtime", true);
                return;
            }
            if (command == L"save_slot" && pid != GetCurrentProcessId())
            {
                report(L"error", L"save_requires_current_process_id", true);
                return;
            }
            if (command == L"setup_startup" &&
                (pid != GetCurrentProcessId() || expected_seed.empty() || expected_player.empty()))
            {
                report(L"error", L"setup_requires_process_and_seed", true);
                return;
            }
            if (startup_store == AutoloadStore::steam &&
                (command == L"new_game" || command == L"setup_startup" || command == L"save_slot"))
            {
                report(L"error", L"command_not_supported_on_steam", true);
                return;
            }
            if (command == L"new_game")
            {
                if (pid != GetCurrentProcessId() || difficulty < 0 || expected_map != station_map ||
                    expected_seed.empty() || expected_player.empty() ||
                    !matches(base, new_game_confirmed_rva,
                             {0x89, 0x91, 0x88, 0x05, 0, 0, 0xc7, 0x81, 0x70, 0x05, 0, 0, 0x14, 0,
                              0, 0, 0xc3}))
                {
                    report(L"error", L"invalid_new_game_request_or_build", true);
                    return;
                }
            }
            if (!supported)
            {
                report(L"error", L"unsupported_executable_build", true);
                return;
            }
            report(L"accepted", L"waiting_for_runtime");
        }

        bool flush_native_load_failure()
        {
            if (!active || !dispatched ||
                (command != L"load_slot" && command != L"load_autosave" &&
                 command != L"continue_native"))
            {
                return false;
            }
            const auto failure = native_load_gate_failure();
            if (!native_load_gate_failure_is_new(dispatched, load_gate_failure_sequence,
                                                 failure.sequence, failure.reason))
            {
                return false;
            }
            report(L"error",
                   L"native_load_gate_failed: " +
                       std::wstring(failure.reason.begin(), failure.reason.end()),
                   true);
            return true;
        }

        // Native objects are borrowed only within this tick, after ownership/layout validation.
        struct TickContext
        {
            UObject* title;
            UObject* manager;
            UObject* pawn;
            Clock::time_point now;
            bool is_title;
            bool ignored_map;
            bool selected_load;
            bool expected_map_ready;
            bool space_context;
        };
        void advance_save_slot(const TickContext& context)
        {
            if (!dispatched)
            {
                if (ready_pawn_ref.Get() != context.pawn)
                {
                    ready_pawn_ref = context.pawn;
                    stable_since = {};
                }
                const bool local_story_ready =
                    control_ready && Kh02Story::native_save_ready(context.pawn);
                if ((!map_ready && !local_story_ready) || context.is_title || context.ignored_map ||
                    blocking_ui || !idle || !input_enabled)
                {
                    report(L"error", L"save_requires_playable_idle_sora", true);
                    return;
                }
                if (!is_uobject_valid_fast(context.manager) || load_state != 0)
                {
                    report(L"error", L"save_service_not_idle", true);
                    return;
                }
                if (stable_since == Clock::time_point{})
                {
                    stable_since = context.now;
                }
                if (context.now - stable_since < autoload_title_stability_duration)
                {
                    report(L"waiting", L"verifying_stable_playable_sora");
                    return;
                }
                dispatched = true;
                report(L"saving", L"dispatching_native_manual_save");
                dispatch_slot_save(base, context.manager, requested_slot);
                observed_slot = read_field<int32_t>(
                    context.manager, manager_offset(save_manager_load_slot_offset), -1);
                if (observed_slot != requested_slot)
                {
                    report(L"error", L"native_slot_selection_mismatch", true);
                }
                return;
            }
            if (!is_uobject_valid_fast(context.manager))
            {
                report(L"error", L"save_manager_lost_after_dispatch", true);
                return;
            }
            if (read_field<int32_t>(context.manager, manager_offset(save_manager_load_slot_offset),
                                    -1) == requested_slot &&
                load_result != 0)
            {
                native_save_result = load_result;
            }
            if (native_save_result > 1)
            {
                report(L"error", L"native_save_failed", true);
                return;
            }
            if (save_closed && native_save_result == 1 && load_state == 0)
            {
                report(L"saved", L"native_success_and_slot_write_closed", true);
                return;
            }
            report(L"saving", L"waiting_for_native_save_completion");
            return;
        }
        void advance_startup_setup(const TickContext& context)
        {
            if (!context.is_title || !is_uobject_valid_fast(context.title))
            {
                report(L"error", L"setup_requires_title", true);
                return;
            }
            try
            {
                if (setup_step == setup_not_started)
                {
                    if (title_state != title_state_ids::intro_movie &&
                        title_state != title_state_ids::menu_ready)
                    {
                        report(L"waiting", L"waiting_for_setup_title");
                        return;
                    }
                    if (!matches(
                            base, open_startup_notice_rva,
                            {0x48, 0x8b, 0xc4, 0x55, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57}) ||
                        !matches(base, open_startup_settings_rva, {0x48, 0x8b, 0xc4}))
                    {
                        report(L"error", L"setup_open_build_mismatch", true);
                        return;
                    }
                    // Present the actual first-run notice and bind its normal
                    // title completion delegate; do not edit title/save state.
                    using OpenNotice = bool(__fastcall*)(UObject*);
                    if (!reinterpret_cast<OpenNotice>(base +
                                                      open_startup_notice_rva)(context.title))
                    {
                        report(L"error", L"setup_notice_not_opened", true);
                        return;
                    }
                    setup_step = setup_waiting_for_notice;
                    report(L"waiting", L"setup_notice_opened");
                    return;
                }
                if (setup_step == setup_waiting_for_notice)
                {
                    if (runtime_starter_options_tick(context.title, starter_options, true))
                    {
                        setup_step = setup_waiting_for_notice_close;
                        report(L"waiting", L"setup_notice_accepted");
                    }
                    else
                    {
                        report(L"waiting", L"waiting_for_setup_notice");
                    }
                    return;
                }
                if (setup_step == setup_waiting_for_notice_close)
                {
                    if (read_field<int32_t>(context.title, title_notice_state_offset) != 0)
                    {
                        report(L"waiting", L"waiting_for_notice_close");
                        return;
                    }
                    using OpenSettings = void(__fastcall*)(UObject*);
                    reinterpret_cast<OpenSettings>(base + open_startup_settings_rva)(context.title);
                    setup_step = setup_waiting_for_settings;
                    report(L"waiting", L"setup_settings_opened");
                    return;
                }
                if (setup_step == setup_waiting_for_settings)
                {
                    if (!try_read_reflected_object_property_value(context.title,
                                                                  L"m_IniSettings") &&
                        read_field<int32_t>(context.title, title_settings_confirmed_offset) == 1)
                    {
                        setup_step = setup_complete;
                        report(L"ready", L"startup_settings_confirmed_and_closed", true);
                        return;
                    }
                    if (runtime_starter_options_tick(context.title, starter_options))
                    {
                        report(L"waiting", L"selected_native_starter_option");
                    }
                    else
                    {
                        report(L"waiting", L"waiting_for_settings_presentation");
                    }
                    return;
                }
            }
            catch (const std::exception& error)
            {
                const std::string reason = error.what();
                report(L"error", std::wstring(reason.begin(), reason.end()), true);
                return;
            }
        }
        void advance_new_game(const TickContext& context)
        {
            // The normal client's Fast New Game can win the title race.
            // Adopt only its verified in-process native dispatch at the
            // requested difficulty; never repeat the confirmation.
            const auto client_difficulty = RuntimeFastNewGameService::dispatched_difficulty();
            if (client_difficulty >= 0)
            {
                if (client_difficulty != difficulty)
                {
                    report(L"error", L"client_new_game_difficulty_mismatch", true);
                    return;
                }
                dispatched = true;
                report(L"loading", L"observing_client_fast_new_game");
                return;
            }
            if (is_uobject_valid_fast(context.pawn) && !context.is_title)
            {
                report(L"error", L"already_in_gameplay", true);
                return;
            }
            if (context.is_title && is_uobject_valid_fast(context.title) &&
                title_state >= title_ready_state && title_state < title_state_ids::intro_movie)
            {
                try
                {
                    if (runtime_starter_options_tick(context.title, starter_options))
                    {
                        report(L"waiting", L"selected_native_starter_option");
                        return;
                    }
                }
                catch (const std::exception& error)
                {
                    const std::string reason = error.what();
                    report(L"error", std::wstring(reason.begin(), reason.end()), true);
                    return;
                }
            }
            // State 14 has finished system initialization, just like the
            // native slot loader. The title's confirmation callback can
            // leave the startup movie through the normal state machine.
            if (!context.is_title || !is_uobject_valid_fast(context.title) ||
                !is_uobject_valid_fast(context.manager) ||
                (title_state != title_state_ids::intro_movie &&
                 title_state != title_state_ids::menu_ready) ||
                load_state != 0)
            {
                report(L"waiting", L"waiting_for_new_game_title_menu");
                return;
            }
            // Do not enter the loaded-game or New Game+ branch, and do not
            // replace an existing modal title operation.
            auto* menu = try_read_reflected_object_property_value(context.title, L"m_Title");
            if (!is_uobject_valid_fast(menu) ||
                read_field<uint8_t>(context.title, title_new_game_busy_offset,
                                    unknown_native_flag_byte) != 0 ||
                read_field<int32_t>(context.title, title_load_kind_offset, -1) == 1 ||
                read_field<int32_t>(context.title, title_new_game_confirmation_offset, -1) != 0)
            {
                report(L"waiting", L"waiting_for_idle_new_game_menu");
                return;
            }
            dispatched = true;
            report(L"loading", L"dispatching_native_new_game_confirmation");
            using NewGame = void(__fastcall*)(UObject*, int32_t);
            reinterpret_cast<NewGame>(base + native_rva(new_game_confirmed_rva))(context.title,
                                                                                 difficulty);
            if (read_field<int32_t>(context.title, title_navigation_state_offset, -1) !=
                    title_difficulty_confirmed_navigation_state ||
                read_field<int32_t>(context.title, title_new_game_difficulty_offset, -1) !=
                    difficulty)
            {
                report(L"error", L"new_game_dispatch_not_observed", true);
            }
            return;
        }
        void advance_slot_load(const TickContext& context)
        {
            if (is_uobject_valid_fast(context.pawn) && !context.is_title && !context.ignored_map)
            {
                report(L"error", L"already_in_gameplay", true);
                return;
            }
            if (command == L"continue_native")
            {
                // State 14 has completed system/catalog loading as well.
                // The selector needs that service, not title-menu input.
                if (!is_uobject_valid_fast(context.title) ||
                    !is_uobject_valid_fast(context.manager) ||
                    (title_state != title_state_ids::intro_movie &&
                     title_state != ready_title_menu_state) ||
                    load_state != 0)
                {
                    report(L"waiting", L"waiting_for_native_continue_menu");
                    return;
                }
                if (metadata.catalog_count < 1)
                {
                    report(L"error", L"native_continue_catalog_unavailable", true);
                    return;
                }
                load_gate_failure_sequence = native_load_gate_failure().sequence;
                dispatched = true;
                report(L"loading", L"dispatching_normal_native_continue");
                try
                {
                    dispatch_normal_continue(base, context.title);
                }
                catch (const std::exception& error)
                {
                    const std::string reason = error.what();
                    report(L"error", std::wstring(reason.begin(), reason.end()), true);
                }
                return;
            }
            // 14 is the startup movie, after the system/catalog read. The
            // load service is ready here, before the title menu (19).
            if (!is_uobject_valid_fast(context.title) || !is_uobject_valid_fast(context.manager) ||
                (title_state != title_state_ids::intro_movie &&
                 title_state != title_state_ids::menu_ready) ||
                load_state != 0)
            {
                report(L"waiting", L"waiting_for_title_save_service");
                return;
            }
            if (!metadata.found)
            {
                report(L"error", L"autosave_unavailable", true);
                return;
            }
            // EGS preparation belongs to the common native gate, which
            // inspects the selected save's destination. Other verified
            // startup profiles retain their explicit preparation path.
            if (!native_load_gate_installed() && !defer_kh02_preparation)
            {
                try
                {
                    if (!Kh02EntryService::prepare_startup(context.title))
                    {
                        report(L"waiting", L"preparing_local_kh02_story_provider");
                        return;
                    }
                }
                catch (const std::exception& error)
                {
                    const std::string reason = error.what();
                    report(L"error", std::wstring(reason.begin(), reason.end()), true);
                    return;
                }
            }
            load_gate_failure_sequence = native_load_gate_failure().sequence;
            dispatched = true;
            report(L"loading", L"dispatching_native_selected_slot");
            dispatch_slot_load(base, context.title, context.manager, metadata, requested_slot);
            // A readiness gate can retain this exact request. Validate its
            // selected slot when native loading actually starts, not while
            // the request is still waiting for its content prerequisites.
            return;
        }
        bool resolve_tick_owners(TickContext& context)
        {
            context.title = title_ref.Get();
            if (!is_uobject_valid_fast(context.title) && !dispatched && context.is_title)
            {
                context.title = live_title();
                title_ref = context.title;
            }
            context.manager = manager_ref.Get();
            // A departing world can remain alive through GC. A live weak
            // reference alone does not prove current UI ownership.
            if (auto* owned_manager = owned_save_manager(config.playable_sora()))
            {
                context.manager = owned_manager;
                manager_ref = context.manager;
            }
            if (is_uobject_valid_fast(context.title))
            {
                if (read_field<uintptr_t>(context.title, 0) !=
                        base + native_rva(title_vtable_rva) ||
                    !property_offset(context.title, L"m_UIManager", title_ui_manager_offset) ||
                    !property_offset(context.title, L"m_Title", title_movie_offset))
                {
                    report(L"error", L"title_layout_mismatch", true);
                    return false;
                }
                title_state = read_field<int32_t>(context.title, title_navigation_state_offset, -1);
                context.manager =
                    try_read_reflected_object_property_value(context.title, L"m_UIManager");
                manager_ref = context.manager;
            }
            if (!is_uobject_valid_fast(context.manager) &&
                ((command == L"save_slot" && !dispatched) || command == L"inspect_startup" ||
                 command == L"wait_gameplay" || (dispatched && command != L"save_slot")))
            {
                context.manager = live_save_manager();
                manager_ref = context.manager;
            }
            if (is_uobject_valid_fast(context.manager))
            {
                const auto* manager_class = context.manager->GetClassPrivate();
                if (!is_uobject_valid_fast(manager_class) ||
                    manager_class->GetName() != STR("TresUIManager"))
                {
                    report(L"error", L"save_manager_class_mismatch", true);
                    return false;
                }
                load_state = read_field<int32_t>(
                    context.manager, manager_offset(save_manager_load_state_offset), -1);
                load_result = read_field<int32_t>(
                    context.manager, manager_offset(save_manager_load_result_offset), -1);
                // Keep the first authoritative selection: native completion
                // resets the manager's operation slot during world travel.
                if (command == L"load_slot" || command == L"load_autosave" ||
                    command == L"continue_native")
                {
                    const auto manager_slot = read_field<int32_t>(
                        context.manager, manager_offset(save_manager_load_slot_offset), -1);
                    const auto selection = observe_native_load_selection(
                        observed_slot, dispatched, context.is_title, load_state, title_state,
                        manager_slot, command == L"continue_native" ? -1 : requested_slot);
                    if (selection == NativeLoadSelection::mismatch)
                    {
                        report(L"error", L"native_slot_selection_mismatch", true);
                        return false;
                    }
                }
            }
            if (!dispatched && command != L"wait_gameplay" && command != L"new_game" &&
                command != L"setup_startup")
            {
                metadata = autosave_metadata(base, requested_slot);
            }

            return true;
        }
        bool sample_pawn_context(TickContext& context)
        {
            const bool is_title = context.is_title;
            UObject* pawn = config.playable_sora();
            if ((command == L"inspect" || (allow_control_ready && command == L"wait_gameplay")) &&
                !is_uobject_valid_fast(pawn))
            {
                auto* alternate = character_swap::current_hero();
                if (character_swap::owns_swap_pawn(alternate))
                {
                    pawn = alternate;
                }
            }
            const bool space_context = allow_space && command != L"save_slot" &&
                                       command != L"new_game" &&
                                       RuntimeSpaceService::is_space_active();
            if (space_context)
            {
                pawn = RuntimeSpaceService::diagnostic_flight_pawn(false);
            }
            space_ready =
                space_context && pawn && RuntimeSpaceService::item_delivery_context() != nullptr;
            observed_pawn_name.clear();

            auto& pawn_name = observed_pawn_name;
            map_ready = is_uobject_valid_fast(pawn) &&
                        try_get_uobject_full_name_fast(pawn, pawn_name) && !pawn_name.empty() &&
                        config.map_ready_context().find(pawn_name) != std::wstring_view::npos;
            station_ready = false;
            if (command == L"new_game")
            {
                auto status = read_text_file(*active_dir / channels::ap_status_file);
                if (!status ||
                    JsonFieldReader::string_field(*status, "seed_name") != expected_seed ||
                    JsonFieldReader::string_field(*status, "player_name") != expected_player)
                {
                    report(L"error", L"new_game_seed_identity_changed", true);
                    return false;
                }
                if (auto* station = !is_title ? runtime_station_sora() : nullptr)
                {
                    if (!dispatched)
                    {
                        report(L"error", L"already_in_station_use_mirror_resume", true);
                        return false;
                    }
                    pawn = station;
                    station_ready = try_get_uobject_full_name_fast(station, pawn_name);
                }
            }
            blocking_ui = config.blocking_ui_active();
            if (space_context && config.space_blocking_ui_active)
            {
                blocking_ui = config.space_blocking_ui_active();
            }
            idle = is_uobject_valid_fast(pawn) && config.sora_idle && config.sora_idle();
            input_enabled = is_uobject_valid_fast(pawn) && movement_input_enabled(pawn);
            const bool selected_load =
                dispatched &&
                (((command == L"load_autosave" || command == L"load_slot") &&
                  observed_slot == requested_slot) ||
                 (command == L"continue_native" && save_slots::is_native_slot(observed_slot)));
            const bool expected_map_ready =
                expected_map.empty() || pawn_name.find(expected_map + L".") != std::wstring::npos;
            context.pawn = pawn;
            context.space_context = space_context;
            context.selected_load = selected_load;
            context.expected_map_ready = expected_map_ready;
            return true;
        }

        bool sample_loading_context(TickContext& context)
        {
            auto* pawn = context.pawn;
            auto* manager = context.manager;
            const bool is_title = context.is_title;
            const bool ignored_map = context.ignored_map;
            const bool space_context = context.space_context;
            const bool selected_load = context.selected_load;
            const bool expected_map_ready = context.expected_map_ready;
            const auto& pawn_name = observed_pawn_name;
            const auto screen = loading_screen_snapshot(base, manager);
            loading_screen_closed = screen.known && screen.closed;
            loading_screen_mode = screen.mode;
            loading_screen_progress = screen.progress;
            if (screen.layout_mismatch)
            {
                report(L"error", L"loading_screen_layout_mismatch", true);
                return false;
            }
            // Native New Game first loads the stock opening map. GoA's
            // cutscene_bt50_bt051 Blueprint opens Station after Continue.
            // Give that loading task its own one-shot; Station can have a
            // second task, and only Station satisfies the ready proof.
            const bool opening_map =
                command == L"new_game" && dispatched && is_uobject_valid_fast(pawn) &&
                (pawn_name.find(L"/Game/Levels/bt/bt_50/bt_50.") != std::wstring::npos ||
                 pawn_name.find(L"/Game/Levels/bt_DLC/bt_51/bt_51.") != std::wstring::npos);
            // Death-screen Continue can reach its native ready prompt before
            // the restored map emits MapName. Keep final gameplay readiness
            // separate from permission to complete this validated load task.
            const bool waiting_for_loaded_pawn = command == L"wait_gameplay" &&
                                                 is_uobject_valid_fast(pawn) &&
                                                 !pawn_name.empty() && expected_map_ready;
            const bool can_continue =
                !observe_continue_only &&
                (selected_load || waiting_for_loaded_pawn ||
                 (command == L"new_game" && dispatched && station_ready && expected_map_ready) ||
                 opening_map) &&
                !(opening_map ? opening_continue_dispatched : loading_continue_dispatched) &&
                !is_title && (!ignored_map || space_context) && load_state == 0 &&
                load_result >= 0 && load_result <= 1 &&
                // This is a presentation gauge, not the native completion
                // predicate: GoA can show Continue with progress still 0.75.
                // loading_transition validates the finished native thread,
                // ready flags and exact one-shot predicate/action below.
                screen.known && screen.open && !screen.closed && screen.mode == 1 &&
                screen.progress >= 0.0f && screen.progress <= 1.0f &&
                is_uobject_valid_fast(
                    try_read_reflected_object_property_value(screen.object, L"m_pMoviePlayer"));
            const auto transition = loading_transition(base, can_continue);
            loading_transition_complete = transition.complete;
            control_ready = !is_title && !ignored_map && !blocking_ui && loading_screen_closed &&
                            loading_transition_complete && runtime_sora_control_ready(pawn);
            loading_prompt_ready = transition.prompt_ready;
            if (transition.layout_mismatch)
            {
                report(L"error", L"loading_transition_layout_mismatch", true);
                return false;
            }
            if (transition.dispatched)
            {
                if (opening_map)
                {
                    opening_continue_dispatched = true;
                }
                else
                {
                    loading_continue_dispatched = true;
                }
                if (transition.retire_failed)
                {
                    report(L"error", L"loading_continue_task_retire_failed", true);
                    return false;
                }
                report(L"loading", L"dispatched_native_loading_continue");
                return false;
            }
            return true;
        }

        std::optional<TickContext> sample_tick_context(bool is_title, bool ignored_map,
                                                       Clock::time_point now)
        {
            TickContext context{};
            context.now = now;
            context.is_title = is_title;
            context.ignored_map = ignored_map;
            if (!resolve_tick_owners(context))
            {
                return std::nullopt;
            }
            if (!sample_pawn_context(context) || !sample_loading_context(context))
            {
                return std::nullopt;
            }
            return context;
        }
        void tick(bool initialized, bool is_title, bool ignored_map)
        {
            if (!initialized)
            {
                return;
            }
            if (flush_native_load_failure())
            {
                return;
            }
            const auto now = Clock::now();
            // Observe native completion every game tick so a short-lived result
            // cannot be lost when KH3 resets its manager to idle.
            if (now < next_poll && !(active && dispatched && command == L"save_slot"))
            {
                return;
            }
            next_poll = now + autoload_update_interval;
            title_active = is_title;
            if (!active && requests.update(config.bridge_dir(), true))
            {
                accept();
            }
            if (!active)
            {
                return;
            }
            if (unix_ms() >= expires)
            {
                report(L"error", L"timeout", true);
                return;
            }

            const auto context = sample_tick_context(is_title, ignored_map, now);
            if (!context)
            {
                return;
            }
            auto* pawn = context->pawn;
            const bool selected_load = context->selected_load;
            const bool expected_map_ready = context->expected_map_ready;
            if (command == L"inspect_startup")
            {
                ready_pawn_ref = pawn;
                report(L"completed", L"startup_snapshot", true);
                return;
            }
            if (command == L"save_slot")
            {
                advance_save_slot(*context);
                return;
            }
            if (command == L"setup_startup")
            {
                advance_startup_setup(*context);
                return;
            }
            if (!dispatched && command == L"new_game")
            {
                advance_new_game(*context);
                return;
            }
            if (!dispatched && command != L"wait_gameplay")
            {
                advance_slot_load(*context);
                return;
            }
            // Native completion uses 1 for success; 2..6 are save-service
            // failure results. Leave error handling to KH3, never retry a load.
            if (is_title && load_result > 1)
            {
                report(L"error", L"native_save_load_failed", true);
                return;
            }
            // A pawn and the reflected "closed" flag alone are insufficient:
            // the native Continue operation can still own the fullscreen UI.
            if ((map_ready || station_ready || space_ready ||
                 (allow_control_ready && control_ready)) &&
                expected_map_ready && input_enabled && loading_screen_closed &&
                loading_transition_complete && !blocking_ui && !is_title &&
                (!ignored_map || space_ready))
            {
                if ((command == L"load_slot" || command == L"load_autosave" ||
                     command == L"continue_native") &&
                    observed_slot < 0)
                {
                    report(L"error", L"native_slot_selection_unobserved", true);
                    return;
                }
                if (ready_pawn_ref.Get() != pawn)
                {
                    ready_pawn_ref = pawn;
                    stable_since = now;
                }
                if (now - stable_since >= (allow_control_ready
                                               ? autoload_control_stability_duration
                                               : autoload_gameplay_stability_duration))
                {
                    report(L"ready",
                           space_ready ? L"stable_space_controller"
                           : command == L"new_game"
                               ? L"new_game_station_ready"
                               : (command == L"wait_gameplay"
                                      ? L"stable_playable_sora"
                                      : (map_ready ? L"map_ready_after_selected_slot_load"
                                                   : L"control_ready_after_selected_slot_load")),
                           true);
                    return;
                }
            }
            else
            {
                ready_pawn_ref = nullptr;
                stable_since = {};
            }
            report(L"loading", L"waiting_for_playable_sora");
        }
    };

    RuntimeAutoloadService::RuntimeAutoloadService(RuntimeAutoloadConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }
    RuntimeAutoloadService::~RuntimeAutoloadService() = default;
    void RuntimeAutoloadService::tick(bool initialized, bool title, bool ignored_map)
    {
        impl_->tick(initialized, title, ignored_map);
    }
    void RuntimeAutoloadService::flush_native_load_failure()
    {
        impl_->flush_native_load_failure();
    }
    void RuntimeAutoloadService::manual_save_completed(int32_t slot)
    {
        if (impl_->active && impl_->dispatched && impl_->command == L"save_slot" &&
            impl_->requested_slot == slot)
        {
            impl_->save_closed = true;
        }
    }
} // namespace kh3ap
