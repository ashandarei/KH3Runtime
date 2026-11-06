#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/grants/messages/ap_message_call_helpers.h"

#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/features/player/context/context_helpers.h"
#include "kh3ap/features/player/objects/object_cache.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/features/grants/delivery/item_delivery_binding.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#define NOMINMAX
#include <Windows.h>

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        std::mutex s_processed_runtime_ap_message_mutex{};
        std::wstring s_last_processed_runtime_ap_message_signature{};
        auto runtime_ap_message_signature(const RuntimeApMessageRequest& request) -> std::wstring
        {
            return request.request_id + STR("|") + request.bridge_key + STR("|") + request.message;
        }

        auto is_duplicate_processed_runtime_ap_message(const RuntimeApMessageRequest& request) -> bool
        {
            const std::wstring signature = runtime_ap_message_signature(request);
            std::lock_guard<std::mutex> lock{s_processed_runtime_ap_message_mutex};
            return !signature.empty() && s_last_processed_runtime_ap_message_signature == signature;
        }

        auto mark_processed_runtime_ap_message(const RuntimeApMessageRequest& request) -> void
        {
            const std::wstring signature = runtime_ap_message_signature(request);
            if (signature.empty())
            {
                return;
            }

            std::lock_guard<std::mutex> lock{s_processed_runtime_ap_message_mutex};
            s_last_processed_runtime_ap_message_signature = signature;
        }

        auto try_resolve_tres_game_blueprint_library_self_object(
                RuntimeState& runtime_state,
                UFunction* fallback_function,
                std::wstring_view blueprint_library_default_object_path) -> UObject*
        {
            UObject* self_object = RuntimeObjectResolver::try_get_tres_game_blueprint_library_default_object(
                    runtime_state.object_cache(),
                    blueprint_library_default_object_path);
            if (!self_object)
            {
                self_object = try_get_function_default_object(fallback_function);
            }
            return self_object && kh3ap::is_uobject_valid_fast(self_object) ? self_object : nullptr;
        }

        auto try_call_tresui_clear_information_text_cache(
                RuntimeState& runtime_state,
                std::wstring_view function_path,
                std::wstring_view blueprint_library_default_object_path) -> bool
        {
            if (function_path.empty())
            {
                return false;
            }

            UFunction* const function_object = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, function_path);
            UObject* const self_object = try_resolve_tres_game_blueprint_library_self_object(
                    runtime_state,
                    function_object,
                    blueprint_library_default_object_path);
            if (!function_object || !self_object)
            {
                return false;
            }

            const auto params_size = static_cast<size_t>(function_object->GetParmsSize());
            std::vector<std::byte> params(params_size);
            if (!params.empty())
            {
                std::fill(params.begin(), params.end(), std::byte{0});
            }
            self_object->ProcessEvent(function_object, params.empty() ? nullptr : params.data());
            return true;
        }

    }

    auto try_call_tresui_open_information(
            const RuntimeApMessageRequest& request,
            RuntimeState& runtime_state,
            std::wstring_view function_path,
            std::wstring_view blueprint_library_default_object_path,
            std::wstring* failure_reason,
            std::wstring_view clear_information_text_cache_path) -> bool
    {
        UFunction* const function_object = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, function_path);
        UObject* const self_object = try_resolve_tres_game_blueprint_library_self_object(
                runtime_state,
                function_object,
                blueprint_library_default_object_path);

        if (!function_object || !self_object)
        {
            if (failure_reason)
            {
                *failure_reason = STR("TresUI_OpenInformation missing function/default object");
            }
            return false;
        }

        try_call_tresui_clear_information_text_cache(
                runtime_state,
                clear_information_text_cache_path,
                blueprint_library_default_object_path);

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        if (params.empty())
        {
            if (failure_reason)
            {
                *failure_reason = STR("TresUI_OpenInformation has no params slab");
            }
            return false;
        }
        std::fill(params.begin(), params.end(), std::byte{0});

        FProperty* text_property = find_function_param_by_name(function_object, STR("InformationText"));
        if (!text_property)
        {
            text_property = find_first_function_param_by_class(function_object, {STR("StructProperty"), STR("TextProperty")});
        }
        const bool wrote_text =
                write_tres_loc_text_function_param(
                        text_property,
                        params.data(),
                        request.message,
                        runtime_state.ap_message_param_storage()) ||
                write_name_or_string_function_param(text_property, params.data(), request.message);
        if (!wrote_text)
        {
            if (failure_reason)
            {
                *failure_reason = STR("TresUI_OpenInformation InformationText param missing or unsupported");
            }
            return false;
        }

        write_bool_function_param(find_function_param_by_name(function_object, STR("AutoClose")), params.data(), true);
        write_float_function_param(find_function_param_by_name(function_object, STR("OptionalDisplayTime")), params.data(), request.duration_seconds);

        self_object->ProcessEvent(function_object, params.data());
        retain_runtime_ap_message_params(runtime_state.ap_message_param_storage(), std::move(params));
        return true;
    }

    auto try_call_kismet_print_string(
            const RuntimeApMessageRequest& request,
            UObject* world_context_object,
            std::wstring_view function_path,
            std::wstring_view default_object_path,
            std::wstring* failure_reason) -> bool
    {
        UFunction* const function_object = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, function_path);
        if (!function_object)
        {
            if (failure_reason)
            {
                *failure_reason = STR("PrintString UFunction not found");
            }
            return false;
        }

        UObject* const self_object = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, default_object_path);
        if (!self_object || !kh3ap::is_uobject_valid_fast(self_object))
        {
            if (failure_reason)
            {
                *failure_reason = STR("Default__KismetSystemLibrary not found");
            }
            return false;
        }

        std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
        if (params.empty())
        {
            if (failure_reason)
            {
                *failure_reason = STR("PrintString has no params slab");
            }
            return false;
        }

        if (FProperty* const world_property = find_function_param_by_name(function_object, STR("WorldContextObject")); world_property)
        {
            if (UObject** const world_value = world_property->ContainerPtrToValuePtr<UObject*>(params.data()))
            {
                *world_value = world_context_object && kh3ap::is_uobject_valid_fast(world_context_object) ? world_context_object : nullptr;
            }
        }
        FProperty* message_property = find_function_param_by_name(function_object, STR("InString"));
        if (!message_property)
        {
            message_property = find_first_function_param_by_class(function_object, {STR("StrProperty"), STR("TextProperty"), STR("NameProperty")});
        }
        if (!write_name_or_string_function_param(message_property, params.data(), request.message))
        {
            if (failure_reason)
            {
                *failure_reason = STR("PrintString InString param missing or unsupported");
            }
            return false;
        }

        write_bool_function_param(find_function_param_by_name(function_object, STR("bPrintToScreen")), params.data(), true);
        write_bool_function_param(find_function_param_by_name(function_object, STR("bPrintToLog")), params.data(), true);
        write_float_function_param(find_function_param_by_name(function_object, STR("Duration")), params.data(), request.duration_seconds);
        write_linear_color_function_param(find_function_param_by_name(function_object, STR("TextColor")), params.data(), 0.20f, 0.85f, 1.0f, 1.0f);

        self_object->ProcessEvent(function_object, params.data());
        return true;
    }

    auto process_pending_runtime_ap_message(
            RuntimeState& runtime_state,
            const std::optional<std::filesystem::path>& bridge_dir,
            const std::optional<std::filesystem::path>& request_path,
            bool request_file_exists,
            std::wstring_view result_filename,
            std::wstring_view protocol_name,
            int64_t protocol_version,
            std::wstring_view tresui_open_information_path,
            std::wstring_view tresui_clear_information_text_cache_path,
            std::wstring_view tres_game_blueprint_library_default_object_path,
            std::wstring_view kismet_print_string_path,
            std::wstring_view kismet_system_library_default_object_path,
            std::wstring_view runtime_context_summary) -> void
    {
        if (!request_file_exists)
        {
            return;
        }

        const auto native = reconciliation::view();
        const bool native_request = native->message && native->ready;
        static auto next_native_message_at = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now();
        if (native_request && now < next_native_message_at) return;
        if (native_request)
        {
            if (!bridge_dir || native->epoch != reconciliation::epoch()) return;
            const auto identity = item_delivery::installed_delivery_seed_identity();
            const auto status = read_text_file(*bridge_dir / channels::ap_status).value_or("");
            if (!item_delivery::delivery_binding_error(L"kh3ap.item_delivery.v1", GetCurrentProcessId(), GetCurrentProcessId(),
                native->seed, native->slot, JsonFieldReader::string_field(status, "seed_name").value_or(L""),
                JsonFieldReader::int64_field(status, "slot").value_or(0), identity.seed, identity.slot).empty()) return;
        }
        const auto request = native_request ? std::optional{*native->message} : read_runtime_ap_message_request(request_path);
        if (!request.has_value())
        {
            return;
        }
        const auto consume = [&](bool shown) {
            if (native_request)
            {
                reconciliation::message_completed(request->request_id, native->epoch);
                if (shown) next_native_message_at = now + std::chrono::milliseconds(static_cast<int64_t>(request->duration_seconds * 1000));
            }
            else if (request_path) try_remove_file(*request_path);
        };
        if (is_duplicate_processed_runtime_ap_message(*request))
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Runtime AP message skipped duplicate processed request_id={} message={}\n"),
                    request->request_id,
                    truncate_for_log(request->message, 160));
            write_runtime_ap_message_result(
                    bridge_dir,
                    result_filename,
                    *request,
                    STR("duplicate_processed"),
                    STR(""),
                    protocol_name,
                    protocol_version);
            consume(false);
            return;
        }

        UObject* const last_world_context_object = runtime_state.object_cache().last_world_context_object();
        UObject* const world_context_object = last_world_context_object && kh3ap::is_uobject_valid_fast(last_world_context_object)
                                                       ? last_world_context_object
                                                       : nullptr;
        std::wstring failure_reason{};
        if (try_call_tresui_open_information(
                    *request,
                    runtime_state,
                    tresui_open_information_path,
                    tres_game_blueprint_library_default_object_path,
                    &failure_reason,
                    tresui_clear_information_text_cache_path))
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Runtime AP message information request_id={} message={} | world={}\n"),
                    request->request_id,
                    truncate_for_log(request->message, 160),
                    std::wstring{runtime_context_summary});
            write_runtime_ap_message_result(
                    bridge_dir,
                    result_filename,
                    *request,
                    STR("information"),
                    STR(""),
                    protocol_name,
                    protocol_version);
            mark_processed_runtime_ap_message(*request);
            consume(true);
            return;
        }

        const std::wstring information_failure_reason = failure_reason;
        failure_reason.clear();
        if (try_call_kismet_print_string(
                    *request,
                    world_context_object,
                    kismet_print_string_path,
                    kismet_system_library_default_object_path,
                    &failure_reason))
        {
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Runtime AP message printed fallback request_id={} message={} information_error={} | world={}\n"),
                    request->request_id,
                    truncate_for_log(request->message, 160),
                    information_failure_reason,
                    std::wstring{runtime_context_summary});
            write_runtime_ap_message_result(
                    bridge_dir,
                    result_filename,
                    *request,
                    STR("printed"),
                    STR(""),
                    protocol_name,
                    protocol_version);
            mark_processed_runtime_ap_message(*request);
            consume(true);
            return;
        }

        Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Runtime AP message failed request_id={} error={}\n"),
                request->request_id,
                std::format(STR("information={} | print={}"), information_failure_reason, failure_reason));
        write_runtime_ap_message_result(
                bridge_dir,
                result_filename,
                *request,
                STR("failed"),
                std::format(STR("information={} | print={}"), information_failure_reason, failure_reason),
                protocol_name,
                protocol_version);
        consume(false);
    }
}
