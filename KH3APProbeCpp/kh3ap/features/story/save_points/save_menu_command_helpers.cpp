#include "kh3ap/bridge/session/command_sources.h"
#include "kh3ap/features/story/save_points/save_menu_commands.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "save_menu_command_helpers.h"

#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/session/command_guard.h"
#include "kh3ap/features/player/context/context_helpers.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include <nlohmann/json.hpp>

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/Property/FMulticastDelegateProperty.hpp>

#include <algorithm>
#include <chrono>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        using save_menu_commands::is_get_game_flag_boolean_command;
        using save_menu_commands::is_get_game_flag_command;
        using save_menu_commands::is_save_point_dispatch_command;
        using save_menu_commands::is_set_game_flag_boolean_command;
        using save_menu_commands::is_set_game_flag_command;
        using save_menu_commands::is_set_game_flag_label_command;
        using save_menu_commands::is_valid_game_flag_name;
        using save_menu_commands::is_world_map_progress_command;
        using save_menu_commands::lower_copy;
        struct SaveMenuCommandRequest
        {
            std::wstring request_id{};
            std::wstring command{};
            int32_t progress_value{9999};
            std::wstring flag_name{};
            std::wstring flag_bank{};
            std::wstring flag_label{};
            int32_t flag_value{};
            bool flag_value_provided{};
            bool flag_boolean_value{};
            bool flag_boolean_value_provided{};
        };

        struct SaveMenuVector
        {
            double x{};
            double y{};
            double z{};
        };

        struct SavePointDispatchResult
        {
            bool invoked{};
            bool set_action_command_invoked{};
            bool start_action_command_invoked{};
            bool action_key_press_invoked{};
            bool action_key_release_invoked{};
            std::wstring reason{};
            UObject* save_point{};
            UObject* reactor{};
            double distance{};
            int binding_count{};
        };


        auto parse_request(const std::string& text) -> SaveMenuCommandRequest
        {
            SaveMenuCommandRequest request{};
            request.request_id = JsonFieldReader::string_field(text, "request_id").value_or(std::wstring{});
            request.command =
                JsonFieldReader::string_field(text, "command").value_or(std::wstring{});
            const int64_t progress_value = JsonFieldReader::int64_field(text, "progress_value").value_or(9999);
            request.progress_value = static_cast<int32_t>(std::clamp<int64_t>(
                    progress_value,
                    std::numeric_limits<int32_t>::min(),
                    std::numeric_limits<int32_t>::max()));
            request.flag_name = JsonFieldReader::string_field(text, "flag_name").value_or(std::wstring{});
            request.flag_bank = JsonFieldReader::string_field(text, "flag_bank").value_or(std::wstring{});
            request.flag_label = JsonFieldReader::string_field(text, "flag_label").value_or(std::wstring{});
            const auto flag_value = JsonFieldReader::int64_field(text, "flag_value");
            request.flag_value_provided = flag_value.has_value();
            if (flag_value.has_value())
            {
                request.flag_value = static_cast<int32_t>(std::clamp<int64_t>(
                        *flag_value,
                        std::numeric_limits<int32_t>::min(),
                        std::numeric_limits<int32_t>::max()));
            }
            const auto flag_boolean_value = JsonFieldReader::bool_field(text, "flag_value");
            request.flag_boolean_value_provided = flag_boolean_value.has_value();
            request.flag_boolean_value = flag_boolean_value.value_or(false);
            return request;
        }

        auto append_json_string_field(std::ostringstream& json, std::string_view key, std::wstring_view value) -> void
        {
            json << ",\"" << key << "\":\"" << json_escape(value) << "\"";
        }

        auto write_result(const RuntimeSaveMenuCommandDeps& deps,
                          const SaveMenuCommandRequest& request, std::wstring_view status,
                          std::wstring_view reason, UObject* pawn, UObject* controller,
                          bool open_save_menu_invoked, UObject* save_point = nullptr,
                          UObject* reactor = nullptr, double save_point_distance = 0.0,
                          int delegate_binding_count = 0, bool set_action_command_invoked = false,
                          bool start_action_command_invoked = false,
                          bool action_key_press_invoked = false,
                          bool action_key_release_invoked = false,
                          std::wstring_view game_flag_name = {}, int32_t game_flag_value = 0,
                          bool game_flag_set = false, bool game_flag_value_is_boolean = false,
                          bool game_flag_boolean_value = false, bool game_flag_read = false,
                          bool game_cleared_value_provided = false, bool game_cleared_value = false,
                          std::string_view extra_json = {}) -> void
        {
            if (!deps.bridge_dir.has_value())
            {
                return;
            }

            std::ostringstream json;
            json << "{";
            json << "\"schema\":\"kh3ap.runtime_save_menu_result.v1\"";
            json << ",\"protocol_name\":\"" << json_escape(deps.protocol_name) << "\"";
            json << ",\"protocol_version\":" << deps.protocol_version;
            json << ",\"event\":\"runtime_save_menu_result\"";
            append_json_string_field(json, "status", status);
            append_json_string_field(json, "reason", reason);
            append_json_string_field(json, "request_id", request.request_id);
            json << ",\"process_id\":" << GetCurrentProcessId();
            append_json_string_field(json, "command", request.command);
            json << ",\"progress_value\":" << request.progress_value;
            append_json_string_field(json, "world_map_progress_flag", deps.world_map_progress_flag);
            append_json_string_field(json, "flag_name", game_flag_name);
            if (game_flag_value_is_boolean)
            {
                json << ",\"flag_value\":" << (game_flag_boolean_value ? "true" : "false");
            }
            else
            {
                json << ",\"flag_value\":" << game_flag_value;
            }
            append_json_string_field(
                    json,
                    "flag_value_type",
                    game_flag_name.empty()
                            ? STR("none")
                            : (game_flag_value_is_boolean ? STR("boolean") : STR("integer")));
            json << ",\"game_flag_set\":" << (game_flag_set ? "true" : "false");
            json << ",\"game_flag_read\":" << (game_flag_read ? "true" : "false");
            if (game_cleared_value_provided)
            {
                json << ",\"is_game_cleared\":" << (game_cleared_value ? "true" : "false");
            }
            const std::string observed_at = now_iso8601_utc();
            append_json_string_field(json, "observed_at_utc", std::wstring{observed_at.begin(), observed_at.end()});
            append_json_string_field(json, "build_stamp", deps.build_stamp);
            append_json_string_field(json, "pawn", format_object_debug_name(pawn));
            append_json_string_field(json, "controller", format_object_debug_name(controller));
            append_json_string_field(json, "save_point", format_object_debug_name(save_point));
            append_json_string_field(json, "reactor", format_object_debug_name(reactor));
            json << ",\"save_point_distance\":" << save_point_distance;
            json << ",\"delegate_binding_count\":" << delegate_binding_count;
            json << ",\"open_save_menu_invoked\":" << (open_save_menu_invoked ? "true" : "false");
            json << ",\"set_action_command_invoked\":" << (set_action_command_invoked ? "true" : "false");
            json << ",\"start_action_command_invoked\":" << (start_action_command_invoked ? "true" : "false");
            json << ",\"action_key_press_invoked\":" << (action_key_press_invoked ? "true" : "false");
            json << ",\"action_key_release_invoked\":" << (action_key_release_invoked ? "true" : "false");
            json << ",\"world_map_progress_set\":"
                 << (game_flag_set && is_world_map_progress_command(request.command) ? "true" : "false");
            append_json_string_field(json, "runtime_context", runtime_context_summary(deps.runtime_state, deps.bridge_dir));
            if (!extra_json.empty())
            {
                json << "," << extra_json;
            }
            json << "}\n";

            write_text_file(*deps.bridge_dir / std::filesystem::path{std::wstring{deps.result_filename}}, json.str());
            append_text_file(*deps.bridge_dir / std::filesystem::path{std::wstring{deps.events_filename}}, json.str());
        }

        auto resolve_blueprint_library_self(const RuntimeSaveMenuCommandDeps& deps, UFunction* function_object) -> UObject*
        {
            UObject* self_object = RuntimeObjectResolver::try_get_tres_game_blueprint_library_default_object(
                    deps.runtime_state.object_cache(),
                    deps.blueprint_library_default_object_path);
            if (!self_object)
            {
                self_object = try_get_function_default_object(function_object);
            }
            return self_object;
        }

        auto invoke_game_flag_int_getter(
                const RuntimeSaveMenuCommandDeps& deps,
                std::wstring_view flag_name,
                int32_t& value,
                std::wstring& failure_reason) -> bool
        {
            return try_call_get_game_flag(
                    flag_name,
                    value,
                    deps.runtime_state,
                    deps.get_game_flag_function_path,
                    deps.blueprint_library_default_object_path,
                    deps.debug_logging,
                    &failure_reason);
        }

        auto invoke_game_flag_label_setter(
                const RuntimeSaveMenuCommandDeps& deps,
                std::wstring_view flag_name,
                std::wstring_view flag_label,
                std::wstring& failure_reason) -> bool
        {
            UFunction* const function_object = UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr,
                    nullptr,
                    deps.set_flag_label_function_path);
            UObject* const self_object = resolve_blueprint_library_self(deps, function_object);
            if (!function_object || !self_object)
            {
                failure_reason = STR("missing_set_flag_label_prereq");
                return false;
            }

            std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
            std::fill(params.begin(), params.end(), std::byte{0});
            FProperty* flag_property = find_function_param_by_name(function_object, STR("Flag"));
            if (!flag_property)
            {
                flag_property = find_function_param_by_name(function_object, STR("FlagName"));
            }
            FProperty* label_property = find_function_param_by_name(function_object, STR("label"));
            if (!label_property)
            {
                label_property = find_function_param_by_name(function_object, STR("FlagLabelName"));
            }
            if (!write_name_or_string_function_param(flag_property, params.data(), flag_name) ||
                !write_name_or_string_function_param(label_property, params.data(), flag_label))
            {
                failure_reason = STR("failed_set_flag_label_param_setup");
                return false;
            }

            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            self_object->ProcessEvent(function_object, params.data());
            return true;
        }

        auto invoke_bool_getter(
                const RuntimeSaveMenuCommandDeps& deps,
                std::wstring_view function_path,
                std::wstring_view flag_name,
                bool& value,
                std::wstring& failure_reason) -> bool
        {
            UFunction* const function_object = UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr,
                    nullptr,
                    function_path);
            UObject* const self_object = resolve_blueprint_library_self(deps, function_object);
            if (!function_object || !self_object)
            {
                failure_reason = STR("missing_bool_getter_prereq");
                return false;
            }

            std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
            std::fill(params.begin(), params.end(), std::byte{0});
            if (!flag_name.empty())
            {
                FProperty* flag_property = find_function_param_by_name(function_object, STR("FlagName"));
                if (!flag_property)
                {
                    flag_property = find_first_function_param_by_class(function_object, {STR("NameProperty")});
                }
                if (!write_name_or_string_function_param(flag_property, params.data(), flag_name))
                {
                    failure_reason = STR("failed_bool_getter_flag_param_setup");
                    return false;
                }
            }
            FProperty* const return_property = find_function_param_by_name(function_object, STR("ReturnValue"));
            if (!return_property)
            {
                failure_reason = STR("missing_bool_getter_return_property");
                return false;
            }
            {
                const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                self_object->ProcessEvent(function_object, params.empty() ? nullptr : params.data());
            }
            bool* const return_value = return_property->ContainerPtrToValuePtr<bool>(params.data());
            if (!return_value)
            {
                failure_reason = STR("missing_bool_getter_return_value");
                return false;
            }
            value = *return_value;
            return true;
        }

        auto try_get_actor_location(
                UObject* object,
                std::wstring_view actor_location_function_path) -> std::optional<SaveMenuVector>
        {
            if (!object || !kh3ap::is_uobject_valid_fast(object))
            {
                return std::nullopt;
            }
            UFunction* const function_object = UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr,
                    nullptr,
                    std::wstring{actor_location_function_path}.c_str());
            if (!function_object)
            {
                return std::nullopt;
            }
            FProperty* const return_property = find_function_param_by_name(function_object, STR("ReturnValue"));
            if (!return_property)
            {
                return std::nullopt;
            }

            std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
            std::fill(params.begin(), params.end(), std::byte{0});
            {
                const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                object->ProcessEvent(function_object, params.data());
            }
            void* const value_ptr = return_property->ContainerPtrToValuePtr<void>(params.data());
            if (!value_ptr)
            {
                return std::nullopt;
            }

            SaveMenuVector result{};
            if (return_property->GetElementSize() >= static_cast<int>(sizeof(double) * 3))
            {
                double values[3]{};
                std::memcpy(values, value_ptr, sizeof(values));
                result = {values[0], values[1], values[2]};
            }
            else if (return_property->GetElementSize() >= static_cast<int>(sizeof(float) * 3))
            {
                float values[3]{};
                std::memcpy(values, value_ptr, sizeof(values));
                result = {values[0], values[1], values[2]};
            }
            else
            {
                return std::nullopt;
            }
            return result;
        }

        auto vector_distance(const SaveMenuVector& left, const SaveMenuVector& right) -> double
        {
            const double dx = left.x - right.x;
            const double dy = left.y - right.y;
            const double dz = left.z - right.z;
            return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
        }

        auto write_object_function_param(FProperty* property, void* params, UObject* value) -> bool
        {
            if (!property || !params || property->GetElementSize() < static_cast<int>(sizeof(UObject*)))
            {
                return false;
            }
            void* const value_ptr = property->ContainerPtrToValuePtr<void>(params);
            if (!value_ptr)
            {
                return false;
            }
            std::memcpy(value_ptr, &value, sizeof(value));
            return true;
        }

        auto invoke_set_action_command(
                UObject* controller,
                std::wstring_view function_path,
                UObject* save_point,
                UObject* reactor) -> bool
        {
            if (!controller || !kh3ap::is_uobject_valid_fast(controller) || !save_point || !reactor)
            {
                return false;
            }
            UFunction* const function_object = UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr,
                    nullptr,
                    std::wstring{function_path}.c_str());
            if (!function_object)
            {
                return false;
            }

            std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
            std::fill(params.begin(), params.end(), std::byte{0});
            const bool wrote_command = write_integral_function_param(
                    find_function_param_by_name(function_object, STR("inCommand")),
                    params.data(),
                    76);
            const bool wrote_timer = write_float_function_param(
                    find_function_param_by_name(function_object, STR("inTimer")),
                    params.data(),
                    30.0);
            const bool wrote_actor = write_object_function_param(
                    find_function_param_by_name(function_object, STR("inActor")),
                    params.data(),
                    save_point);
            const bool wrote_component = write_object_function_param(
                    find_function_param_by_name(function_object, STR("InComponent")),
                    params.data(),
                    reactor);
            if (!wrote_command || !wrote_timer || !wrote_actor || !wrote_component)
            {
                return false;
            }

            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            controller->ProcessEvent(function_object, params.data());
            return true;
        }

        auto invoke_simulated_action_key(
                UObject* controller,
                std::wstring_view function_path,
                int input_event,
                double amount_depressed) -> bool
        {
            if (!controller || !kh3ap::is_uobject_valid_fast(controller))
            {
                return false;
            }
            UFunction* const function_object = UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr,
                    nullptr,
                    std::wstring{function_path}.c_str());
            if (!function_object)
            {
                return false;
            }

            std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
            std::fill(params.begin(), params.end(), std::byte{0});
            FProperty* const key_property = find_function_param_by_name(function_object, STR("Key"));
            void* const key_value = key_property ? key_property->ContainerPtrToValuePtr<void>(params.data()) : nullptr;
            if (!key_property || !key_value || key_property->GetElementSize() < static_cast<int>(sizeof(FName)))
            {
                return false;
            }
            const FName action_key{STR("Gamepad_FaceButton_Top")};
            std::memcpy(key_value, &action_key, sizeof(action_key));

            const bool wrote_event = write_integral_function_param(
                    find_function_param_by_name(function_object, STR("EventType")),
                    params.data(),
                    input_event);
            const bool wrote_amount = write_float_function_param(
                    find_function_param_by_name(function_object, STR("AmountDepressed")),
                    params.data(),
                    amount_depressed);
            const bool wrote_gamepad = write_bool_function_param(
                    find_function_param_by_name(function_object, STR("bGamepad")),
                    params.data(),
                    true);
            if (!wrote_event || !wrote_amount || !wrote_gamepad)
            {
                return false;
            }

            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            controller->ProcessEvent(function_object, params.data());
            return true;
        }

        auto dispatch_nearest_save_point_command(
                UObject* pawn,
                UObject* controller,
                std::wstring_view actor_location_function_path,
                std::wstring_view reactor_command_delegate_signature_path,
                std::wstring_view set_action_command_function_path,
                std::wstring_view start_action_command_function_path,
                std::wstring_view simulate_keypress_function_path) -> SavePointDispatchResult
        {
            SavePointDispatchResult result{};
            const auto pawn_location = try_get_actor_location(pawn, actor_location_function_path);
            if (!pawn_location.has_value())
            {
                result.reason = STR("missing_pawn_location");
                return result;
            }

            std::vector<UObject*> candidates{};
            UObjectGlobals::FindAllOf(STR("TresSavePointActor"), candidates);
            std::vector<UObject*> blueprint_candidates{};
            UObjectGlobals::FindAllOf(STR("g_ex_SavePoint_C"), blueprint_candidates);
            for (UObject* object : blueprint_candidates)
            {
                if (std::find(candidates.begin(), candidates.end(), object) == candidates.end())
                {
                    candidates.push_back(object);
                }
            }

            double nearest_distance = std::numeric_limits<double>::max();
            for (UObject* object : candidates)
            {
                if (!object || !kh3ap::is_uobject_valid_fast(object) || object->GetFullName().find(STR("Default__")) != std::wstring::npos)
                {
                    continue;
                }
                const auto object_location = try_get_actor_location(object, actor_location_function_path);
                if (!object_location.has_value())
                {
                    continue;
                }
                const double distance = vector_distance(*pawn_location, *object_location);
                if (distance < nearest_distance)
                {
                    nearest_distance = distance;
                    result.save_point = object;
                }
            }
            if (!result.save_point)
            {
                result.reason = STR("missing_loaded_save_point");
                return result;
            }
            result.distance = nearest_distance;

            result.reactor = try_read_reflected_object_property_value(result.save_point, STR("MyReactor"));
            if (!result.reactor)
            {
                result.reason = STR("missing_save_point_reactor");
                return result;
            }

            FProperty* const property = result.reactor->GetPropertyByNameInChain(STR("OnReactorDoCommand"));
            void* const delegate_value = result.reactor->GetValuePtrByPropertyNameInChain(STR("OnReactorDoCommand"));
            if (!property || !delegate_value || property->GetClass().GetName() != STR("MulticastDelegateProperty"))
            {
                result.reason = STR("missing_reactor_command_delegate");
                return result;
            }

            UFunction* const signature_function = UObjectGlobals::StaticFindObject<UFunction*>(
                    nullptr,
                    nullptr,
                    std::wstring{reactor_command_delegate_signature_path}.c_str());
            auto* const script_delegate = static_cast<FMulticastScriptDelegate*>(delegate_value);
            if (!signature_function || !script_delegate)
            {
                result.reason = STR("invalid_reactor_command_delegate");
                return result;
            }
            result.binding_count = script_delegate->InvocationList.Num();
            if (result.binding_count <= 0)
            {
                result.reason = STR("unbound_reactor_command_delegate");
                return result;
            }

            result.set_action_command_invoked = invoke_set_action_command(
                    controller,
                    set_action_command_function_path,
                    result.save_point,
                    result.reactor);
            if (!result.set_action_command_invoked)
            {
                result.reason = STR("set_action_command_failed");
                return result;
            }

            (void)start_action_command_function_path;
            result.action_key_press_invoked = invoke_simulated_action_key(
                    controller,
                    simulate_keypress_function_path,
                    0,
                    1.0);
            if (!result.action_key_press_invoked)
            {
                result.reason = STR("action_key_press_failed");
                return result;
            }
            result.action_key_release_invoked = invoke_simulated_action_key(
                    controller,
                    simulate_keypress_function_path,
                    1,
                    0.0);
            result.invoked = result.action_key_release_invoked;
            if (!result.action_key_release_invoked)
            {
                result.reason = STR("action_key_release_failed");
            }
            return result;
        }

    } // namespace

    auto process_pending_runtime_save_menu_command(const RuntimeSaveMenuCommandDeps& deps) -> void
    {
        if (!runtime_command_source_allowed(deps.request_filename ==
                                            channels::runtime_gameflow_command_file))
        {
            return;
        }

        if (!deps.bridge_dir.has_value())
        {
            return;
        }

        const auto request_path = *deps.bridge_dir / std::filesystem::path{std::wstring{deps.request_filename}};
        if (!path_points_to_regular_file(request_path))
        {
            return;
        }
        const auto request_text = read_text_file(request_path);
        if (!request_text.has_value())
        {
            return;
        }

        SaveMenuCommandRequest request = parse_request(*request_text);
        if (deps.request_filename == channels::runtime_gameflow_command_file &&
            request.command != L"get_game_flag" && request.command != L"set_game_flag" &&
            !(request.flag_bank == L"kh02" && request.command == L"reset_gameflow"))
        {
            write_result(deps, request, STR("failed"), STR("unsupported_recovery_command"), nullptr,
                         nullptr, false);
            try_remove_file(request_path);
            return;
        }
        const auto guard_failure = runtime_command_request_failure(*request_text, GetCurrentProcessId(),
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        if (!guard_failure.empty())
        {
            write_result(deps, request, STR("failed"), guard_failure, nullptr, nullptr, false);
            try_remove_file(request_path);
            return;
        }
        // Source flags share names with KH3 flags. An explicit bank must never
        // fall through to the ordinary game's Blueprint getters or setters.
        if (!request.flag_bank.empty())
        {
            using Json = nlohmann::json;
            Json result{{"status", "failed"}, {"flag_bank", to_utf8(request.flag_bank)}};
            try
            {
                if (request.flag_bank != L"kh02")
                {
                    throw std::runtime_error("Unknown gameflow bank");
                }
                if (request.command != L"get_game_flag" &&
                    deps.runtime_state.ui_diagnostic_state().blocking_ui_state().snapshot().active)
                {
                    throw std::runtime_error("Close the blocking game UI before changing the 0.2 story");
                }
                result = Json::parse(Kh02Story::gameflow_command(
                    deps.runtime_state.grant_state().pawn_state().object(), request.request_id,
                    request.command, request.flag_name,
                    request.flag_value_provided ? std::optional<int>{request.flag_value} : std::nullopt));
            }
            catch (const std::exception& error)
            {
                result["status"] = "failed";
                result["reason"] = error.what();
            }
            result["request_id"] = to_utf8(request.request_id);
            result["process_id"] = GetCurrentProcessId();
            result["schema"] = "kh3ap.runtime_gameflow_result.v1";
            const auto text = result.dump() + "\n";
            write_text_file(*deps.bridge_dir / std::filesystem::path{deps.result_filename}, text);
            if (result.at("status") != "waiting")
            {
                append_text_file(*deps.bridge_dir / std::filesystem::path{deps.events_filename}, text);
                try_remove_file(request_path);
            }
            return;
        }
        if (request.request_id.empty())
        {
            const std::string observed_at = now_iso8601_utc();
            request.request_id = STR("save-menu-") + std::wstring{observed_at.begin(), observed_at.end()};
        }

        if (!is_save_point_dispatch_command(request.command) &&
            !is_world_map_progress_command(request.command) &&
            !is_set_game_flag_command(request.command) &&
            !is_set_game_flag_boolean_command(request.command) &&
            !is_set_game_flag_label_command(request.command) &&
            !is_get_game_flag_command(request.command) &&
            !is_get_game_flag_boolean_command(request.command))
        {
            write_result(deps, request, STR("failed"), STR("unsupported_command"), nullptr, nullptr,
                         false);
            try_remove_file(request_path);
            return;
        }
        if (is_set_game_flag_boolean_command(request.command) &&
            (!is_valid_game_flag_name(request.flag_name) || !request.flag_boolean_value_provided))
        {
            write_result(deps, request, STR("failed"),
                         !request.flag_boolean_value_provided ? STR("missing_flag_value")
                                                              : STR("invalid_flag_name"),
                         nullptr, nullptr, false, nullptr, nullptr, 0.0, 0, false, false, false,
                         false, request.flag_name, 0, false, true, request.flag_boolean_value);
            try_remove_file(request_path);
            return;
        }

        if (is_set_game_flag_command(request.command) &&
            (!is_valid_game_flag_name(request.flag_name) || !request.flag_value_provided))
        {
            write_result(deps, request, STR("failed"),
                         !request.flag_value_provided ? STR("missing_flag_value")
                                                      : STR("invalid_flag_name"),
                         nullptr, nullptr, false, nullptr, nullptr, 0.0, 0, false, false, false,
                         false, request.flag_name, request.flag_value, false);
            try_remove_file(request_path);
            return;
        }

        if ((is_get_game_flag_command(request.command) || is_get_game_flag_boolean_command(request.command)) &&
            !is_valid_game_flag_name(request.flag_name))
        {
            write_result(deps, request, STR("failed"), STR("invalid_flag_name"), nullptr, nullptr,
                         false, nullptr, nullptr, 0.0, 0, false, false, false, false,
                         request.flag_name);
            try_remove_file(request_path);
            return;
        }

        if (is_set_game_flag_label_command(request.command) &&
            (!is_valid_game_flag_name(request.flag_name) || !is_valid_game_flag_name(request.flag_label)))
        {
            write_result(deps, request, STR("failed"),
                         request.flag_label.empty() ? STR("missing_flag_label")
                                                    : STR("invalid_flag_or_label"),
                         nullptr, nullptr, false, nullptr, nullptr, 0.0, 0, false, false, false,
                         false, request.flag_name);
            try_remove_file(request_path);
            return;
        }

        if (is_get_game_flag_command(request.command))
        {
            int32_t value = 0;
            std::wstring failure_reason{};
            const bool read = invoke_game_flag_int_getter(deps, request.flag_name, value, failure_reason);
            write_result(deps, request, read ? STR("completed") : STR("failed"), failure_reason,
                         nullptr, nullptr, false, nullptr, nullptr, 0.0, 0, false, false, false,
                         false, request.flag_name, value, false, false, false, read);
            try_remove_file(request_path);
            return;
        }

        if (is_get_game_flag_boolean_command(request.command))
        {
            bool value = false;
            std::wstring failure_reason{};
            const bool read = invoke_bool_getter(
                    deps,
                    deps.get_game_flag_boolean_function_path,
                    request.flag_name,
                    value,
                    failure_reason);
            write_result(deps, request, read ? STR("completed") : STR("failed"), failure_reason,
                         nullptr, nullptr, false, nullptr, nullptr, 0.0, 0, false, false, false,
                         false, request.flag_name, 0, false, true, value, read);
            try_remove_file(request_path);
            return;
        }

        if (deps.runtime_state.grant_state().special_battle_mode_active())
        {
            write_result(deps, request, STR("waiting"), STR("special_battle_mode_active"), nullptr,
                         nullptr, false);
            return;
        }

        auto& pawn_state = deps.runtime_state.grant_state().pawn_state();
        UObject* const pawn = pawn_state.object();
        if (!pawn || !kh3ap::is_uobject_valid_fast(pawn) || !RuntimeObjectResolver::is_sora_ability_pawn_candidate(pawn))
        {
            write_result(deps, request, STR("waiting"), STR("missing_safe_sora_pawn"), pawn,
                         nullptr, false);
            return;
        }

        if (is_world_map_progress_command(request.command) || is_set_game_flag_command(request.command))
        {
            const bool generic_flag_command = is_set_game_flag_command(request.command);
            const std::wstring_view flag_name = generic_flag_command
                    ? std::wstring_view{request.flag_name}
                    : deps.world_map_progress_flag;
            const int32_t flag_value = generic_flag_command ? request.flag_value : request.progress_value;
            std::wstring failure_reason{};
            const bool flag_set = try_call_set_game_flag(
                    flag_name,
                    flag_value,
                    deps.runtime_state,
                    deps.set_game_flag_function_path,
                    deps.blueprint_library_default_object_path,
                    deps.debug_logging,
                    &failure_reason);
            write_result(deps, request, flag_set ? STR("completed") : STR("failed"), failure_reason,
                         pawn, nullptr, false, nullptr, nullptr, 0.0, 0, false, false, false, false,
                         flag_name, flag_value, flag_set);
            try_remove_file(request_path);
            return;
        }

        if (is_set_game_flag_boolean_command(request.command))
        {
            std::wstring failure_reason{};
            const bool flag_set = try_call_set_game_flag_boolean(
                    request.flag_name,
                    request.flag_boolean_value,
                    deps.runtime_state,
                    deps.set_game_flag_boolean_function_path,
                    deps.blueprint_library_default_object_path,
                    deps.debug_logging,
                    &failure_reason);
            write_result(deps, request, flag_set ? STR("completed") : STR("failed"), failure_reason,
                         pawn, nullptr, false, nullptr, nullptr, 0.0, 0, false, false, false, false,
                         request.flag_name, 0, flag_set, true, request.flag_boolean_value);
            try_remove_file(request_path);
            return;
        }

        if (is_set_game_flag_label_command(request.command))
        {
            std::wstring failure_reason{};
            const bool flag_set = invoke_game_flag_label_setter(
                    deps,
                    request.flag_name,
                    request.flag_label,
                    failure_reason);
            const std::string extra_json = std::string{"\"flag_label\":\""} +
                                           json_escape(request.flag_label) + "\"";
            write_result(deps, request, flag_set ? STR("completed") : STR("failed"), failure_reason,
                         pawn, nullptr, false, nullptr, nullptr, 0.0, 0, false, false, false, false,
                         request.flag_name, 0, flag_set, false, false, false, false, false,
                         extra_json);
            try_remove_file(request_path);
            return;
        }

        UObject* controller = pawn_state.controller_object();
        if (!controller || !kh3ap::is_uobject_valid_fast(controller))
        {
            controller = try_read_object_property_object(pawn, STR("Controller"));
        }

        if (is_save_point_dispatch_command(request.command))
        {
            if (!controller || !kh3ap::is_uobject_valid_fast(controller))
            {
                write_result(deps, request, STR("waiting"), STR("missing_player_controller"), pawn,
                             controller, false);
                return;
            }
            const SavePointDispatchResult dispatch = dispatch_nearest_save_point_command(
                    pawn,
                    controller,
                    deps.actor_location_function_path,
                    deps.reactor_command_delegate_signature_path,
                    deps.set_action_command_function_path,
                    deps.start_action_command_function_path,
                    deps.simulate_keypress_function_path);
            write_result(deps, request, dispatch.invoked ? STR("completed") : STR("failed"),
                         dispatch.reason, pawn, controller, dispatch.invoked, dispatch.save_point,
                         dispatch.reactor, dispatch.distance, dispatch.binding_count,
                         dispatch.set_action_command_invoked, dispatch.start_action_command_invoked,
                         dispatch.action_key_press_invoked, dispatch.action_key_release_invoked);
            try_remove_file(request_path);
            return;
        }
    }
}
