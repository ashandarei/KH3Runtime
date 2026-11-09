#include "kh3ap/features/player/player_action_commands.h"
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/features/world/startup/new_game_service.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/player/state/sora_pawn_catalog.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/features/world/space/space_policy.h"
#include "kh3ap/features/world/space/space_service.h"
#include "kh3ap/features/enemies/enemy_clear_service.h"
#include "kh3ap/features/grants/vbonus/vbonus_grant_call_helpers.h"
#include "kh3ap/features/grants/keyblades/progressive_keyblade_level_helpers.h"
#include "kh3ap/bridge/utilities.h"

#include "kh3ap/features/kh02/story/kh02_story.h"

#include <Unreal/AActor.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/World.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr auto maximum_new_game_request_lifetime_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::minutes(5)).count();

        constexpr auto new_game_ready_stability_duration = std::chrono::seconds(2);
        constexpr auto new_game_update_interval = std::chrono::milliseconds(250);
        constexpr auto new_game_report_interval = std::chrono::seconds(1);
        constexpr auto new_game_choice_read_interval = std::chrono::seconds(1);
        constexpr auto max_new_game_parameter_bytes = 4096;
        constexpr auto max_new_game_array_entries = 1024;
        constexpr auto max_new_game_array_capacity = 65536;
        constexpr auto max_precollected_item_token_characters = 64;
        constexpr auto max_test_potion_count = 99;
        constexpr auto new_game_request_history_limit = 128;
    }

    using namespace RC::Unreal;
    namespace
    {
        using Clock = std::chrono::steady_clock;
        constexpr auto request_file = L"runtime_new_game_request.json";
        constexpr auto result_file = L"runtime_new_game_result.json";
        constexpr auto events_file = L"runtime_new_game_events.jsonl";
        struct DialogLayout { uintptr_t vtable, decide, after_event; bool steam; };
        constexpr DialogLayout egs_dialog{0x7c5c398, 0x1a49950, 0x1a498d0, false};
        // Steam PE 66743F34/A969000: the same semantic event and dialog fields,
        // with vtable slots 89/90 resolving to Steam's decision/close handlers.
        constexpr DialogLayout steam_dialog{0x6b1cb28, 0x16c7110, 0x16d0780, true};
        constexpr auto mirror_class=L"/Game/Maps/StationOfAwakening/uasset/Actor/Mirror/Mirror_Base.Mirror_Base_C";
        constexpr auto mirror_handler=L"BndEvt__TresReactor_K2Node_ComponentBoundEvent_0_TresReactorDoCommandSignature__DelegateSignature";
        constexpr std::array prompts{
            L"Are you sure you want the path of Balance?", L"Is this who you are?"};

        int64_t unix_ms()
        { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

        template<class T> bool read_native(uintptr_t address, T& value) noexcept
        {
            __try { std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T)); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        template<class T> T field(const void* object, size_t offset)
        {
            T value{};
            if (!object || !read_native(reinterpret_cast<uintptr_t>(object) + offset, value))
                throw std::runtime_error("unreadable_native_field");
            return value;
        }
        bool signature(uintptr_t address, std::initializer_list<uint8_t> bytes)
        {
            for (auto expected : bytes)
            { uint8_t actual{}; if (!read_native(address++, actual) || actual != expected) return false; }
            return true;
        }
        const DialogLayout* supported(uintptr_t base)
        {
            IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
            if (!read_native(base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew >= 0x1000 ||
                !read_native(base + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE) return nullptr;
            const DialogLayout* layout = nullptr;
            if (kh3ap::native_build::egs.matches(nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage)) layout = &egs_dialog;
            if (kh3ap::native_build::steam.matches(nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage)) layout = &steam_dialog;
            if (!layout || !signature(base + layout->decide, {0x40,0x57,0x48,0x83,0xec,0x20,0x83,0x7a,0x10,0x07}) ||
                field<uintptr_t>(reinterpret_cast<void*>(base + layout->vtable), 89 * 8) != base + layout->decide ||
                field<uintptr_t>(reinterpret_cast<void*>(base + layout->vtable), 90 * 8) != base + layout->after_event) return nullptr;
            if (layout->steam ? !signature(base + layout->after_event,
                    {0x40,0x53,0x48,0x83,0xec,0x20,0x80,0xb9,0x62,0x02,0,0,0}) :
                    !signature(base + layout->after_event, {0x40,0x53,0x48,0x83,0xec,0x30})) return nullptr;
            return layout;
        }

        struct Call
        {
            UObject* object; UFunction* function; std::vector<std::byte> params;
            Call(UObject* target, const wchar_t* name) : object(target)
            {
                if (!is_uobject_valid_fast(object)) throw std::runtime_error("invalid_call_target");
                function = object->GetFunctionByNameInChain(name);
                if (!is_uobject_valid_fast(function) || function->GetParmsSize() > max_new_game_parameter_bytes)
                    throw std::runtime_error("missing_function_or_layout");
                params.resize(std::max<int>(1, function->GetParmsSize()));
            }
            FProperty* param(const wchar_t* name)
            {
                auto* p = find_function_param_by_name(function, name);
                if (!p || p->GetOffset_Internal() < 0 || p->GetOffset_Internal() + p->GetElementSize() > function->GetParmsSize())
                    throw std::runtime_error("missing_parameter_or_layout");
                return p;
            }
            void object_param(const wchar_t* name, UObject* value)
            {
                param(name);
                if (!write_object_function_param(function, params.data(), name, value))
                    throw std::runtime_error("unsupported_object_parameter");
            }
            void invoke() { object->ProcessEvent(function, params.data()); }
        };
        bool boolean(UObject* object, const wchar_t* name)
        {
            Call c(object, name); auto* p = c.param(L"ReturnValue");
            if (p->GetClass().GetName() != L"BoolProperty") throw std::runtime_error("unsupported_bool_return");
            c.invoke(); return static_cast<FBoolProperty*>(p)->GetPropertyValueInContainer(c.params.data());
        }
        int inventory_count(UObject* pawn, const std::wstring& token)
        {
            auto* library = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary");
            Call c(library, L"BP_TresGetBackyardItemNum");
            c.object_param(L"WorldContextObject", pawn);
            if (!write_name_or_string_function_param(c.param(L"InItemKeyName"), c.params.data(), token))
                throw std::runtime_error("inventory_token_layout");
            auto* result = c.param(L"ReturnValue");
            if (result->GetClass().GetName() != L"IntProperty" || result->GetElementSize() != sizeof(int32_t))
                throw std::runtime_error("inventory_count_layout");
            c.invoke();
            return *result->ContainerPtrToValuePtr<int32_t>(c.params.data());
        }
        bool inventory_has(UObject* pawn, const std::wstring& token)
        {
            auto* library = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary");
            Call c(library, L"BP_TresHaveItem");
            c.object_param(L"WorldContextObject", pawn);
            if (!write_name_or_string_function_param(c.param(L"InItemKeyName"), c.params.data(), token))
                throw std::runtime_error("inventory_token_layout");
            auto* result = c.param(L"ReturnValue");
            if (result->GetClass().GetName() != L"BoolProperty") throw std::runtime_error("inventory_have_layout");
            const RuntimeGrantInternalCallDepth::Scope internal_call;
            c.invoke();
            return static_cast<FBoolProperty*>(result)->GetPropertyValueInContainer(c.params.data());
        }
        bool bool_property(UObject* object, const wchar_t* name)
        {
            auto* p = object->GetPropertyByNameInChain(name);
            if (!p || p->GetClass().GetName() != L"BoolProperty") throw std::runtime_error("missing_choice_bool");
            return static_cast<FBoolProperty*>(p)->GetPropertyValueInContainer(object);
        }
        int byte_property(UObject* object, const wchar_t* name)
        {
            auto* p = object->GetPropertyByNameInChain(name);
            if (!p || p->GetElementSize() != 1 || (p->GetClass().GetName() != L"ByteProperty" && p->GetClass().GetName() != L"EnumProperty"))
                throw std::runtime_error("missing_choice_enum");
            return *p->ContainerPtrToValuePtr<uint8_t>(object);
        }
        std::wstring native_string(const void* object, size_t offset)
        {
            const auto* p = field<const wchar_t*>(object, offset);
            const auto count = field<int32_t>(object, offset + 8);
            const auto capacity = field<int32_t>(object, offset + 12);
            if (count < 0 || count > max_new_game_array_entries || capacity < count || capacity > max_new_game_array_capacity)
                throw std::runtime_error("invalid_dialog_string");
            if (!count) return {};
            if (!p) throw std::runtime_error("invalid_dialog_string");
            std::wstring value(static_cast<size_t>(count), L'\0');
            for (int i = 0; i < count; ++i)
                if (!read_native(reinterpret_cast<uintptr_t>(p + i), value[i])) throw std::runtime_error("unreadable_dialog_string");
            if (value.back() != L'\0') throw std::runtime_error("unterminated_dialog_string");
            value.pop_back(); return value;
        }
        bool in_map(UObject* pawn, const wchar_t* map)
        {
            return is_uobject_valid_fast(pawn) && pawn->GetPathName().starts_with(std::wstring{map} + L".");
        }
        UObject* game_instance(UObject* pawn)
        {
            auto* library = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, L"/Script/Engine.Default__GameplayStatics");
            Call c(library, L"GetGameInstance"); c.object_param(L"WorldContextObject", pawn); c.invoke();
            return try_read_function_object_param(c.function, c.params.data(), L"ReturnValue");
        }
        struct Choices
        {
            bool known{}, warrior{}, ez{}, balance{}, ez_code{}, pro_code{};
            int growth{-1}, ability{-1};
            int prefix() const
            {
                if (!known || (balance && ability != 2))
                    throw std::runtime_error("unexpected_existing_mirror_choices");
                return balance ? 1 : 0;
            }
        };
        Choices read_choices()
        {
            auto* library = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, L"/Script/Engine.Default__GameplayStatics");
            Call c(library, L"LoadGameFromSlot"); auto* slot = c.param(L"SlotName");
            if (slot->GetClass().GetName() != L"StrProperty" || slot->GetElementSize() != sizeof(FString))
                throw std::runtime_error("unsupported_savegame_string");
            // Only the randomizer-owned ChoicesMade sidecar. Borrow the string
            // for this synchronous read; never touch KHIII_slot/system files.
            FString name{L"ChoicesMade"};
            std::memcpy(slot->ContainerPtrToValuePtr<void>(c.params.data()), &name, sizeof(name));
            if (!write_integral_function_param(c.param(L"UserIndex"), c.params.data(), 0))
                throw std::runtime_error("unsupported_savegame_user");
            c.invoke(); auto* save = try_read_function_object_param(c.function, c.params.data(), L"ReturnValue");
            if (!is_uobject_valid_fast(save)) return {};
            if (save->GetClassPrivate()->GetPathName() != L"/Game/Blueprints/GoA/SaveData/StationPathway.StationPathway_C")
                throw std::runtime_error("unexpected_choice_save_class");
            return {true, bool_property(save,L"Choice2"), bool_property(save,L"Choice3"), bool_property(save,L"Choice1"),
                bool_property(save,L"EzCode"), bool_property(save,L"ProCode"), byte_property(save,L"GrowthPath"), byte_property(save,L"AbilityPath")};
        }
    }

    auto runtime_station_sora() -> UObject* try
    {
        std::vector<UObject*> controllers;
        UObjectGlobals::FindAllOf(L"TresPlayerControllerSora", controllers);
        UObject* found = nullptr;
        for (auto* controller : controllers)
        {
            if (!is_uobject_valid_fast(controller) || controller->GetPathName().find(L"Default__") != std::wstring::npos) continue;
            auto* pawn = try_read_reflected_object_property_value(controller, L"Pawn");
            if (!in_map(pawn, station_map) || try_read_reflected_object_property_value(pawn, L"Controller") != controller ||
                !is_uobject_valid_fast(pawn->GetClassPrivate()) ||
                !runtime_sora_pawn_catalog_matches_any(pawn->GetPathName(), pawn->GetClassPrivate()->GetName()) ||
                !boolean(controller, L"IsLocalController")) continue;
            if (found) return nullptr;
            found = pawn;
        }
        return found;
    }
    catch (const std::exception&) { return nullptr; }

    struct RuntimeNewGameService::Impl
    {
        RuntimeNewGameConfig config;
        std::optional<std::filesystem::path> dir;
        std::wstring id, command, seed, player, world_name, pawn_name, prompt, last_reason;
        std::deque<std::wstring> consumed;
        FWeakObjectPtr pawn_ref{}, world_ref{}, game_instance_ref{}, mirror_ref{};
        uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        int64_t expires{};
        Clock::time_point next_poll{}, next_choice_read{}, stable_since{}, last_report{};
        Choices choices;
        std::array<bool, 2> interactions{}, decisions{}, choice_proven{};
        int step{}, initial_step{}, selected{-1};
        bool active{}, supported_build{}, final_dispatched{};
        const DialogLayout* dialog_layout{};
        std::wstring item_token, grant_context;
        int count_before{-1}, count_after{-1};
        int staging_count{-1}, magic_command{-1};
        bool had_item{}, has_item{}, grant_dispatched{}, grant_returned{};
        explicit Impl(RuntimeNewGameConfig c) : config(std::move(c)) {}

        void report(std::wstring_view status, std::wstring_view reason, bool terminal = false)
        {
            if (!dir) return;
            const auto now = Clock::now();
            if (!terminal && reason == last_reason && now - last_report < new_game_report_interval) return;
            last_reason = reason; last_report = now;
            std::ostringstream out;
            out << std::boolalpha << "{\"schema\":\"kh3ap.new_game.v1\",\"request_id\":\"" << json_escape(id)
                << "\",\"command\":\"" << json_escape(command) << "\",\"process_id\":" << GetCurrentProcessId()
                << ",\"expires_unix_ms\":" << expires << ",\"expected_seed\":\"" << json_escape(seed)
                << "\",\"expected_player\":\"" << json_escape(player) << "\",\"status\":\"" << json_escape(status)
                << "\",\"reason\":\"" << json_escape(reason) << "\",\"at\":\"" << now_iso8601_utc()
                << "\",\"supported_build\":" << supported_build << ",\"world\":\"" << json_escape(world_name)
                << "\",\"pawn\":\"" << json_escape(pawn_name) << "\",\"prompt\":\"" << json_escape(prompt)
                << "\",\"selected_index\":" << selected << ",\"step\":" << step << ",\"initial_step\":" << initial_step
                << ",\"final_confirmation_dispatched\":" << final_dispatched
                << ",\"choices\":{\"known\":" << choices.known << ",\"warrior\":" << choices.warrior
                << ",\"ez\":" << choices.ez << ",\"balance\":" << choices.balance << ",\"growth\":" << choices.growth
                << ",\"ability\":" << choices.ability << ",\"ez_code\":" << choices.ez_code << ",\"pro_code\":" << choices.pro_code << "},\"steps\":[";
            for (int i=0;i<2;++i)
            { if(i) out << ','; out << "{\"interaction_dispatched\":" << interactions[i] << ",\"yes_dispatched\":" << decisions[i]
                << ",\"selection_proven\":" << choice_proven[i] << '}'; }
            out << "],\"item_token\":\"" << json_escape(item_token) << "\",\"count_before\":" << count_before
                << ",\"count_after\":" << count_after << ",\"had_item\":" << had_item << ",\"has_item\":" << has_item
                << ",\"grant_dispatched\":" << grant_dispatched << ",\"grant_returned\":" << grant_returned
                << ",\"staging_count\":" << staging_count << ",\"magic_command\":" << magic_command
                << ",\"grant_context\":\"" << json_escape(grant_context) << "\"}\n";
            write_text_file_atomic_verified(*dir / result_file, out.str()); append_text_file(*dir / events_file, out.str());
            if (terminal) active = false;
        }
        void verify_seed()
        {
            auto status = read_text_file(*dir / channels::ap_status_file);
            if (!status || JsonFieldReader::string_field(*status, "seed_name") != seed ||
                JsonFieldReader::string_field(*status, "player_name") != player || seed.empty() || player.empty())
                throw std::runtime_error("bridge_seed_identity_changed");
        }
        void space_inventory_probe(const std::string& text)
        {
            // Explicit diagnostic experiment only. Never consume an AP receipt
            // or relax the ordinary possessed-Sora grant pipeline here.
            item_token=JsonFieldReader::string_field(text,"item_token").value_or(L"");
            if (item_token!=L"ITEM_POTION") throw std::runtime_error("space_probe_requires_potion");
            auto* engine=UObjectGlobals::StaticFindObject<UObject*>(nullptr,nullptr,L"/Engine/Transient.TresGameEngine_0");
            auto* viewport=try_read_reflected_object_property_value(engine,L"GameViewport");
            auto* world=try_read_reflected_object_property_value(viewport,L"World");
            if (!is_uobject_valid_fast(world) || !space::is_space(world->GetPathName()))
                throw std::runtime_error("space_world_required");
            world_name=world->GetPathName();
            if (JsonFieldReader::string_field(text,"expected_world")!=world_name)
                throw std::runtime_error("world_identity_mismatch");
            std::vector<UObject*> controllers;
            UObjectGlobals::FindAllOf(world_name.find(L"/wm/")!=std::wstring::npos ?
                L"TresWorldMapPlayerController" : L"TresGumiShipPlayerControllerDefault",controllers);
            UObject* context=nullptr;
            for (auto* candidate:controllers) {
                if (!is_uobject_valid_fast(candidate) || candidate->GetPathName().find(L"Default__")!=std::wstring::npos ||
                    static_cast<AActor*>(candidate)->GetWorld()!=world || !boolean(candidate,L"IsLocalController")) continue;
                auto* pawn=try_read_reflected_object_property_value(candidate,L"Pawn");
                if (!is_uobject_valid_fast(pawn) || static_cast<AActor*>(pawn)->GetWorld()!=world ||
                    try_read_reflected_object_property_value(candidate,L"AcknowledgedPawn")!=pawn ||
                    try_read_reflected_object_property_value(pawn,L"Controller")!=candidate ||
                    !is_uobject_valid_fast(try_read_reflected_object_property_value(candidate,L"PlayerInput"))) continue;
                if (context) throw std::runtime_error("ambiguous_space_controller");
                context=candidate; pawn_name=pawn->GetPathName();
            }
            if (!context) throw std::runtime_error("possessed_space_controller_required");
            grant_context=context->GetFullName();
            auto* instance=game_instance(context);
            auto* library=UObjectGlobals::StaticFindObject<UObject*>(nullptr,nullptr,L"/Script/TresGame.Default__TresGameBlueprintLibrary");
            if (!is_uobject_valid_fast(instance) || field<int32_t>(instance,0x8d0)!=0 ||
                !boolean(library,L"TresUI_IsClosedLoadingScreen")) throw std::runtime_error("unpaused_loaded_space_required");
            count_before=count_after=inventory_count(context,item_token);
            had_item=has_item=inventory_has(context,item_token);
            if (command==L"inspect_space_inventory") { report(L"completed",L"space_inventory_snapshot",true); return; }
            if (count_before<0 || count_before>=max_test_potion_count) throw std::runtime_error("potion_count_not_testable");
            if (command==L"queue_space_inventory") {
                // Exercise the production poll/apply/ack path, not this
                // diagnostic service's direct ProcessEvent grant. Negative
                // index and manual_test never represent an AP receipt.
                const auto path=*dir/channels::runtime_item_grant_file;
                if (path_points_to_regular_file(path)) throw std::runtime_error("runtime_grant_lane_busy");
                std::ostringstream request;
                request << "{\"protocol_name\":\"kh3_ap_file_bridge\",\"protocol_version\":1,\"request_id\":\""
                    << "local-regrant-" << json_escape(id) << "-delivery\",\"index\":-1,\"item_name\":\"Potion\","
                    << "\"kh3_runtime_token\":\"ITEM_POTION\",\"kh3_grant_method\":\"bp_tres_get_item\","
                    << "\"action_kind\":\"manual_test\",\"show_info\":true}";
                if (!write_text_file_atomic_verified(path,request.str())) throw std::runtime_error("runtime_grant_enqueue_failed");
                report(L"completed",L"space_inventory_queued",true); return;
            }
            Call c(library,L"BP_TresGetItem");
            c.object_param(L"WorldContextObject",context);
            if (!write_name_or_string_function_param(c.param(L"InItemKeyName"),c.params.data(),item_token) ||
                !write_integral_function_param(c.param(L"InNum"),c.params.data(),1) ||
                !write_bool_function_param(c.param(L"bShowInfo"),c.params.data(),true) ||
                c.function->GetNumParms()!=4 || find_function_param_by_name(c.function,L"ReturnValue"))
                throw std::runtime_error("space_grant_layout");
            grant_dispatched=true; report(L"waiting",L"dispatching_space_potion");
            { const RuntimeGrantInternalCallDepth::Scope internal_call; c.invoke(); }
            grant_returned=true;
            count_after=inventory_count(context,item_token); has_item=inventory_has(context,item_token);
            report(count_after==count_before+1 && has_item ? L"completed":L"error",L"space_grant_readback",true);
        }
        void capture_factory_retry(const std::string& text)
        {
            auto* pawn=config.playable_sora();
            if (!dialog_layout || !is_uobject_valid_fast(pawn))
                throw std::runtime_error("factory_sora_required");
            auto* world=static_cast<AActor*>(pawn)->GetWorld();
            if (!is_uobject_valid_fast(world) || world->GetPathName()!=L"/Game/Levels/mi/mi_02/mi_02.mi_02" ||
                JsonFieldReader::string_field(text,"expected_world")!=world->GetPathName() ||
                JsonFieldReader::string_field(text,"expected_pawn")!=pawn->GetPathName())
                throw std::runtime_error("factory_fixture_identity_mismatch");
            auto* controller=try_read_reflected_object_property_value(pawn,L"Controller");
            if (!is_uobject_valid_fast(controller) || try_read_reflected_object_property_value(controller,L"Pawn")!=pawn ||
                boolean(controller,L"IsMoveInputIgnored")) throw std::runtime_error("factory_fixture_requires_control");
            UObject* flow=nullptr; UObject* start=nullptr;
            auto* owning_world=try_read_reflected_object_property_value(pawn->GetOuterPrivate(),L"OwningWorld");
            if (!is_uobject_valid_fast(owning_world)) throw std::runtime_error("factory_owning_world_unavailable");
            const auto belongs_to_world=[&](UObject* object) {
                return try_read_reflected_object_property_value(object->GetOuterPrivate(),L"OwningWorld")==owning_world;
            };
            std::vector<UObject*> objects;
            UObjectGlobals::FindAllOf(L"mi_02_gameflow_C",objects);
            for (auto* object:objects)
                if (is_uobject_valid_fast(object) && object->GetPathName().starts_with(L"/Game/Levels/mi/mi_02/umap/mi_02_gameflow.") &&
                    belongs_to_world(object))
                { if (flow) throw std::runtime_error("ambiguous_factory_flow"); flow=object; }
            objects.clear(); UObjectGlobals::FindAllOf(L"TresPlayerStart",objects);
            for (auto* object:objects)
                if (is_uobject_valid_fast(object) && object->GetPathName().ends_with(L":PersistentLevel.mi_02_Lv_Rtry_01") &&
                    belongs_to_world(object))
                { if (start) throw std::runtime_error("ambiguous_factory_retry_start"); start=object; }
            if (!flow || !start) throw std::runtime_error("factory_retry_owners_unavailable");
            // The original mi_02 script calls this owning function with this
            // PlayerStart and false. Invoke it normally; never hook or patch it.
            Call capture(flow,L"CreateRetryPoint");
            capture.object_param(L"RetryPoint",start);
            pawn_name=pawn->GetPathName(); world_name=world->GetPathName();
            grant_context=flow->GetFullName()+L" | "+start->GetPathName();
            grant_dispatched=true; capture.invoke(); grant_returned=true;
            report(L"completed",L"factory_retry_checkpoint_dispatched",true);
        }
        void character_reward_probe(const std::string& text)
        {
            const bool queue=command==L"queue_character_reward";
            item_token=JsonFieldReader::string_field(text,"item_token").value_or(L"");
            const bool ability=item_token.starts_with(L"ETresAbilityKind::");
            const bool party=item_token==L"KH3AP_PARTY_MEMBER_UNLOCK::PartyMember_003";
            const bool style=item_token==L"KH3AP_KEYBLADE_STYLE::WEP_KEYBLADE00";
            const bool unlock=item_token==L"KH3AP_CONTROLLER_UNLOCK::magic";
            const bool level=item_token.starts_with(L"KH3AP_PROGRESSIVE_KEYBLADE_LEVEL::KEYBLADE::0::");
            if (!ability && !party && !style && !unlock && !level && !item_token.starts_with(L"ETresVictoryBonusKind::"))
                throw std::runtime_error("character_reward_enum_required");
            auto* context=RuntimeSpaceService::is_space_active() ? RuntimeSpaceService::item_delivery_context() :
                (persistent_player_reward_context() ? persistent_player_reward_context() : (queue ? nullptr : config.playable_sora()));
            if (!is_uobject_valid_fast(context)) throw std::runtime_error("ready_character_reward_context_required");
            auto* current=static_cast<AActor*>(context)->GetWorld();
            if (!is_uobject_valid_fast(current)) throw std::runtime_error("reward_world_required");
            world_name=current->GetPathName(); grant_context=context->GetFullName();
            if (JsonFieldReader::string_field(text,"expected_world")!=world_name)
                throw std::runtime_error("world_identity_mismatch");
            if (party) {
                count_after=staging_count=config.party_unlock_state ? config.party_unlock_state(L"PartyMember_003") : -1;
                if (count_after<0) throw std::runtime_error("party_roster_unavailable");
            } else if (level) {
                KeybladeLevelQueryResult result{};
                if (!try_query_keyblade_level(L"Kingdom Key",false,&result,nullptr))
                    throw std::runtime_error("keyblade_level_unavailable");
                count_after=staging_count=result.achieved_flag_count;
            } else {
                const auto read_token=unlock ? std::wstring{L"ETresAbilityKind::AP_MAGIC"} :
                    style ? std::wstring{L"ETresAbilityKind::AP_KEYBLADE_STYLE_00"} : item_token;
                if (!inspect_persistent_character_reward(context,read_token,count_after,staging_count,magic_command) || count_after!=staging_count)
                    throw std::runtime_error("character_reward_banks_unavailable_or_mismatched");
            }
            count_before=count_after; had_item=has_item=count_after>0;
            if (!queue) { report(L"completed",L"character_reward_snapshot",true); return; }
            const auto path=*dir/channels::runtime_item_grant_file;
            if (path_points_to_regular_file(path)) throw std::runtime_error("runtime_grant_lane_busy");
            const auto flag=JsonFieldReader::string_field(text,"runtime_flag").value_or(L"");
            std::ostringstream request;
            request << "{\"protocol_name\":\"kh3_ap_file_bridge\",\"protocol_version\":1,\"request_id\":\"local-regrant-"
                << json_escape(id) << "-delivery\",\"index\":-1,\"item_name\":\"Space character reward test\",\"kh3_runtime_token\":\""
                << json_escape(item_token) << "\",\"kh3_runtime_flag_name\":\"" << json_escape(flag)
                << "\",\"kh3_grant_method\":\"" << (unlock ? "controller_unlock" : party ? "party_member_unlock" : style ? "keyblade_style" :
                    level ? "progressive_keyblade_level" : ability ? "durable_sora_ability" : "durable_sora_vbonus")
                << "\",\"action_kind\":\"manual_test\",\"show_info\":true}";
            if (!write_text_file_atomic_verified(path,request.str())) throw std::runtime_error("runtime_grant_enqueue_failed");
            pipe::notify_grants();
            report(L"completed",L"character_reward_queued",true);
        }
        void accept()
        {
            dir = config.bridge_dir();
            if (!dir || !path_points_to_regular_file(*dir / request_file)) return;
            auto text = read_text_file(*dir / request_file); if (!text) return;
            try_remove_file(*dir / request_file);
            id = JsonFieldReader::string_field(*text,"request_id").value_or(L"");
            if (id.empty() || std::find(consumed.begin(), consumed.end(), id) != consumed.end()) return;
            consumed.push_back(id); if(consumed.size()>new_game_request_history_limit) consumed.pop_front();
            active = true; choices={}; interactions={}; decisions={}; choice_proven={};
            step=initial_step=0; selected=-1; final_dispatched=false; stable_since={}; next_choice_read={};
            pawn_ref=nullptr; world_ref=nullptr; game_instance_ref=nullptr; mirror_ref=nullptr;
            world_name.clear(); pawn_name.clear(); prompt.clear(); last_reason.clear();
            item_token.clear(); grant_context.clear(); count_before=count_after=-1;
            staging_count=magic_command=-1;
            had_item=has_item=grant_dispatched=grant_returned=false;
            command=JsonFieldReader::string_field(*text,"command").value_or(L"");
            seed=JsonFieldReader::string_field(*text,"expected_seed").value_or(L"");
            player=JsonFieldReader::string_field(*text,"expected_player").value_or(L"");
            expires=JsonFieldReader::int64_field(*text,"expires_unix_ms").value_or(0);
            dialog_layout=supported(base);
            supported_build=dialog_layout != nullptr;
            if (JsonFieldReader::string_field(*text, "schema") != L"kh3ap.new_game.v1" ||
                JsonFieldReader::int64_field(*text, "process_id") != GetCurrentProcessId() ||
                expires <= unix_ms() || expires > unix_ms() + maximum_new_game_request_lifetime_ms)
            {
                throw std::runtime_error("invalid_or_expired_identity");
            }
            if (!supported_build) throw std::runtime_error("unsupported_executable_build");
            // Read-only counters use the existing guarded Steam banks and
            // reflected inventory getters. Never admit diagnostic grants.
            const bool steam_ability_inspection = command == L"inspect_character_reward" &&
                (JsonFieldReader::string_field(*text,"item_token").value_or(L"").starts_with(L"ETresAbilityKind::") ||
                 JsonFieldReader::string_field(*text,"item_token").value_or(L"").starts_with(L"ETresVictoryBonusKind::"));
            if (dialog_layout->steam && command != L"inspect" && command != L"finish_mirrors" &&
                command != L"capture_factory_retry" && command != L"inspect_grant" && !steam_ability_inspection)
                throw std::runtime_error("command_not_supported_on_steam");
            if (command != L"inspect" && command != L"finish_mirrors" &&
                command != L"capture_factory_retry" && command != L"inspect_grant" &&
                command != L"space_grant" && command != L"inspect_space_inventory" &&
                command != L"queue_space_inventory" && command != L"inspect_character_reward" &&
                command != L"queue_character_reward")
            {
                throw std::runtime_error("unknown_command");
            }
            verify_seed();
            if (command==L"capture_factory_retry") { capture_factory_retry(*text); return; }
            if (command==L"inspect_character_reward" || command==L"queue_character_reward") {
                character_reward_probe(*text); return;
            }
            if (command==L"space_grant" || command==L"inspect_space_inventory" || command==L"queue_space_inventory")
            { space_inventory_probe(*text); return; }
            auto* pawn=runtime_station_sora();
            if (command == L"inspect_grant")
            {
                item_token=JsonFieldReader::string_field(*text,"item_token").value_or(L"");
                // Read-only replay evidence accepts native inventory row names.
                const bool valid_item_token =
                    !item_token.empty() &&
                    item_token.size() <= max_precollected_item_token_characters &&
                    std::all_of(item_token.begin(), item_token.end(),
                                [](wchar_t c)
                                {
                                    return (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') ||
                                           c == L'_';
                                });
                if (!valid_item_token)
                {
                    throw std::runtime_error("invalid_inventory_probe_item");
                }
                if (!pawn)
                {
                    pawn = persistent_player_reward_context();
                    if (!pawn)
                    {
                        pawn = config.playable_sora();
                    }
                }
                if (!is_uobject_valid_fast(pawn))
                {
                    throw std::runtime_error("inventory_sora_required");
                }
                pawn_name=pawn->GetPathName();
                auto* world=static_cast<AActor*>(pawn)->GetWorld();
                if (!is_uobject_valid_fast(world)) throw std::runtime_error("grant_world_unavailable");
                world_name=world->GetPathName();
                if (JsonFieldReader::string_field(*text,"expected_world") != world_name)
                    throw std::runtime_error("world_identity_mismatch");
                auto* controller=try_read_reflected_object_property_value(pawn,L"Controller");
                if (!is_uobject_valid_fast(controller) || try_read_reflected_object_property_value(controller,L"Pawn") != pawn ||
                    boolean(controller,L"IsMoveInputIgnored")) throw std::runtime_error("possessed_input_enabled_sora_required");
                count_before=count_after=inventory_count(pawn,item_token);
                had_item=has_item=inventory_has(pawn,item_token);
                report(L"completed", L"inventory_snapshot", true);
                return;
            }
            if (!pawn)
            {
                pawn=config.playable_sora();
                if (is_uobject_valid_fast(pawn)) pawn_name=pawn->GetPathName();
                if (command==L"inspect") { report(L"completed",L"outside_station",true); return; }
                throw std::runtime_error("possessed_station_sora_required");
            }
            pawn_ref=pawn; world_ref=static_cast<AActor*>(pawn)->GetWorld(); game_instance_ref=game_instance(pawn);
            if (!is_uobject_valid_fast(world_ref.Get()) || !is_uobject_valid_fast(game_instance_ref.Get()))
                throw std::runtime_error("startup_world_identity_unavailable");
            world_name=world_ref.Get()->GetPathName(); pawn_name=pawn->GetPathName();
            choices=read_choices();
            if (command==L"inspect") { report(L"completed",L"station_snapshot",true); return; }
            if (JsonFieldReader::string_field(*text,"expected_world") != world_name) throw std::runtime_error("world_identity_mismatch");
            step=initial_step=choices.prefix();
            for(int i=0;i<initial_step;++i) choice_proven[i]=true;
            report(L"accepted",L"verified_station_choices");
        }
        UObject* open_dialog()
        {
            std::vector<UObject*> objects; UObjectGlobals::FindAllOf(L"TresUIP_Dialog",objects);
            UObject* found=nullptr;
            for (auto* dialog:objects)
            {
                if (!is_uobject_valid_fast(dialog) || dialog->GetPathName().find(L"Default__")!=std::wstring::npos) continue;
                auto* manager=try_read_reflected_object_property_value(dialog,L"m_pUIManager");
                if (!is_uobject_valid_fast(manager) || try_read_reflected_object_property_value(manager,L"m_pGameInstance")!=game_instance_ref.Get()) continue;
                if (field<uintptr_t>(dialog,0)!=base+dialog_layout->vtable) throw std::runtime_error("dialog_vtable_mismatch");
                auto* root_property=dialog->GetPropertyByNameInChain(L"m_RootMain");
                auto* manager_property=dialog->GetPropertyByNameInChain(L"m_pUIManager");
                if(!root_property || root_property->GetOffset_Internal()!=0x250 ||
                    !manager_property || manager_property->GetOffset_Internal()!=0x70)
                    throw std::runtime_error("dialog_reflected_layout_mismatch");
                if (!field<uint8_t>(dialog,0xa1)) continue;
                if(found) throw std::runtime_error("multiple_open_choice_dialogs");
                found=dialog;
            }
            return found;
        }
        void interact(UObject* pawn)
        {
            auto* klass=UObjectGlobals::StaticFindObject<UClass*>(nullptr,nullptr,mirror_class);
            if(!is_uobject_valid_fast(klass)) throw std::runtime_error("mirror_class_unavailable");
            std::vector<AActor*> actors; UGameplayStatics::GetAllActorsOfClass(pawn,klass,actors);
            UObject* actor=nullptr;
            if(actors.size()!=3)throw std::runtime_error("expected_three_growth_mirrors");
            for(auto* candidate:actors)
            {
                if(!is_uobject_valid_fast(candidate))throw std::runtime_error("invalid_growth_mirror");
                if(candidate->GetName()==L"Mirror_Balance" && byte_property(candidate,L"GrowthType")==2)
                { if(actor)throw std::runtime_error("mirror_actor_not_unique");actor=candidate; }
            }
            if(!actor)throw std::runtime_error("balance_mirror_unavailable");
            auto* reactor=try_read_reflected_object_property_value(actor,L"TresReactor");
            if(!is_uobject_valid_fast(reactor) || !boolean(reactor,L"BP_IsGimmickComponentActive"))
            { report(L"waiting",L"waiting_for_active_mirror"); return; }
            Call c(actor,mirror_handler);
            {
                c.object_param(L"CommandCauser",pawn); c.object_param(L"ReactorComponent",reactor);
                // The reactor uses CHECK=4; the callback takes a player action command.
                if(byte_property(reactor,L"m_Command")!=4 || !write_integral_function_param(c.param(L"CommandID"),c.params.data(),player_action_command_value(PlayerActionCommand::CHECK)))
                    throw std::runtime_error("mirror_command_mismatch");
            }
            mirror_ref=actor; interactions[step]=true;
            report(L"waiting",L"dispatching_mirror_interaction"); c.invoke();
        }
        void decide(UObject* dialog)
        {
            prompt=native_string(dialog,0x268);
            if (prompt!=prompts[step]) throw std::runtime_error("unexpected_choice_prompt");
            if (field<int32_t>(dialog,0x258)!=2 || field<int32_t>(dialog,0x280)!=2)
                throw std::runtime_error("unexpected_choice_count");
            const auto* labels=field<const std::byte*>(dialog,0x278);
            const auto affirmative=native_string(labels,16);
            if(native_string(labels,0)!=L"No" || (affirmative!=L"Yes" && affirmative!=L"Yup!"))
                throw std::runtime_error("unexpected_choice_labels");
            if(!field<uint8_t>(dialog,0xa2)) { report(L"waiting",L"waiting_for_dialog_animation"); return; }
            auto* movie=try_read_reflected_object_property_value(dialog,L"m_pMoviePlayer");
            auto* root=try_read_reflected_object_property_value(dialog,L"m_RootMain");
            if(!is_uobject_valid_fast(movie) || !is_uobject_valid_fast(root)) throw std::runtime_error("dialog_presentation_unavailable");
            // Native semantic decide event, then its normal post-event close.
            // OnCallback(1,1) is NOT selection: that is an animation completion
            // callback and ignores Param. Never synthesize that callback.
            alignas(16) std::array<std::byte,0x30> event{};
            const int32_t kind=7,index=1;
            std::memcpy(event.data()+0x10,&kind,4); std::memcpy(event.data()+0x18,&index,4);
            if(step==1 && !RuntimeEnemyClearService::arm_opening_protection(600))
                throw std::runtime_error("opening_protection_not_armed");
            decisions[step]=true; if(step==1) final_dispatched=true;
            report(L"waiting",L"dispatching_yes_001");
            using Decide=bool(__fastcall*)(UObject*,void*); using After=void(__fastcall*)(UObject*);
            if(!reinterpret_cast<Decide>(base+dialog_layout->decide)(dialog,event.data())) throw std::runtime_error("native_choice_rejected");
            selected=field<int32_t>(dialog,0x264);
            if(selected!=1 || !(field<uint32_t>(event.data(),0x24)&2)) throw std::runtime_error("native_choice_not_observed");
            reinterpret_cast<After>(base+dialog_layout->after_event)(dialog);
            report(L"waiting",L"yes_selected_waiting_for_gameplay_result");
        }
        void advance()
        {
            if(unix_ms()>=expires) throw std::runtime_error("timeout_inspect_before_retry");
            verify_seed();
            auto* pawn=pawn_ref.Get();
            if(step==1 && final_dispatched)
            {
                auto* current=config.playable_sora();
                if(in_map(current,opening_darkside_map))
                {
                    if(stable_since==Clock::time_point{}) stable_since=Clock::now();
                    if(Clock::now()-stable_since>=new_game_ready_stability_duration)
                    {
                        auto* current_world=static_cast<AActor*>(current)->GetWorld();
                        if(!is_uobject_valid_fast(current_world)) throw std::runtime_error("darkside_world_unavailable");
                        pawn_name=current->GetPathName(); world_name=current_world->GetPathName();
                        choice_proven[1]=true; report(L"ready",L"confirmed_mirrors_entered_darkside",true);
                    }
                }
                else stable_since={};
                return;
            }
            if(!in_map(pawn,station_map) || !is_uobject_valid_fast(world_ref.Get()) || static_cast<AActor*>(pawn)->GetWorld()!=world_ref.Get())
                throw std::runtime_error("station_context_lost");
            auto* current_controller=try_read_reflected_object_property_value(pawn,L"Controller");
            if(!is_uobject_valid_fast(current_controller) || try_read_reflected_object_property_value(current_controller,L"Pawn")!=pawn)
                throw std::runtime_error("station_possession_lost");
            auto* dialog=open_dialog();
            if(step<1 && decisions[step])
            {
                if(Clock::now()<next_choice_read) return;
                next_choice_read=Clock::now()+new_game_choice_read_interval;
                choices=read_choices();
                const int prefix=choices.prefix();
                if(prefix>step+1 || prefix<step) throw std::runtime_error("unexpected_choice_progress");
                if(prefix==step+1 && (!dialog || (step==0 && native_string(dialog,0x268)==prompts[1])))
                { choice_proven[step]=true; ++step; mirror_ref=nullptr; prompt.clear(); report(L"waiting",L"mirror_selection_persisted"); }
                return;
            }
            if(dialog) { decide(dialog); return; }
            if(step<1 && !interactions[step])
            {
                auto* controller=try_read_reflected_object_property_value(pawn,L"Controller");
                if(boolean(controller,L"IsMoveInputIgnored")) { report(L"waiting",L"waiting_for_mirror_sequence"); return; }
                interact(pawn); return;
            }
            report(L"waiting",step==1 ? L"waiting_for_final_confirmation" : L"waiting_for_mirror_dialog");
        }
        void tick(bool initialized)
        {
            if(!initialized || Clock::now()<next_poll) return;
            next_poll=Clock::now()+new_game_update_interval;
            try { if(!active) accept(); if(active) advance(); }
            catch(const std::exception& e)
            { const std::string text=e.what(); report(L"error",std::wstring(text.begin(),text.end()),true); }
        }
    };
    RuntimeNewGameService::RuntimeNewGameService(RuntimeNewGameConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
    RuntimeNewGameService::~RuntimeNewGameService()=default;
    void RuntimeNewGameService::tick(bool initialized) { impl_->tick(initialized); }
}
