#pragma once

#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/reflection/reflection_scalar.h"
#include "kh3ap/core/types.h"
#include "kh3ap/core/performance_build.h"

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace RC::Unreal
{
    struct FFrame;
    struct FName;
    class FProperty;
    class FStructProperty;
    class UClass;
    class UObject;
    class UFunction;
    class UStruct;
    class UScriptStruct;
}

namespace kh3ap
{
    class RuntimeApMessageParamStorage;

    auto record_runtime_hot_path_profile_sample(std::wstring_view scope_name, uint64_t elapsed_us) -> void;

    class RuntimeHotPathProfileScope
    {
    public:
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        explicit RuntimeHotPathProfileScope(std::wstring_view scope_name);
#else
        explicit RuntimeHotPathProfileScope(std::wstring_view) {}
#endif
        RuntimeHotPathProfileScope(const RuntimeHotPathProfileScope&) = delete;
        auto operator=(const RuntimeHotPathProfileScope&) -> RuntimeHotPathProfileScope& = delete;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        ~RuntimeHotPathProfileScope();

    private:
        std::wstring scope_name;
        std::chrono::steady_clock::time_point started_at{};
        bool enabled{false};
#endif
    };

    auto get_frame_node(RC::Unreal::FFrame& frame) -> RC::Unreal::UFunction*;

    auto get_frame_locals(RC::Unreal::FFrame& frame) -> void*;

    auto get_frame_object(RC::Unreal::FFrame& frame) -> RC::Unreal::UObject*;

    auto ue_realloc_for_script_array(void* original, size_t size, uint32_t alignment) -> void*;

    auto try_get_property_min_alignment_dynamic(const RC::Unreal::FProperty* property) -> uint32_t;

    auto initialize_property_value_dynamic(RC::Unreal::FProperty* property, void* value) -> bool;

    auto destroy_property_value_dynamic(RC::Unreal::FProperty* property, void* value) -> void;

    auto read_frame_local_dword(RC::Unreal::FFrame& frame, int slot_index) -> std::optional<uint32_t>;

    auto find_function_param_by_name(
            RC::Unreal::UFunction* function,
            std::wstring_view param_name) -> RC::Unreal::FProperty*;

    auto read_unsigned_param_value(const void* value_ptr, int element_size) -> uint64_t;

    auto decode_name_value(uint64_t raw_u64) -> DecodedNameValue;

    auto try_probe_pointer_bytes(uint64_t address, size_t size) -> std::optional<std::wstring>;

    auto try_read_utf16_string(uint64_t address, size_t max_wchars) -> std::optional<std::wstring>;

    auto looks_like_process_pointer(uint64_t value) -> bool;

    auto try_format_supported_map_entries(
            RC::Unreal::FProperty* property,
            const void* value_ptr) -> std::optional<std::wstring>;

    auto try_collect_supported_array_entries(
            RC::Unreal::FProperty* property,
            const void* value_ptr) -> std::optional<std::vector<std::wstring>>;

    auto try_collect_array_entry_diff_signatures(
            RC::Unreal::FProperty* property,
            const void* value_ptr,
            size_t max_entries) -> std::optional<std::vector<std::wstring>>;

    auto try_format_supported_array_entries(
            RC::Unreal::FProperty* property,
            const void* value_ptr) -> std::optional<std::wstring>;

    auto write_object_function_param(
            RC::Unreal::UFunction* function_object,
            void* params,
            std::wstring_view param_name,
            RC::Unreal::UObject* value) -> bool;

    auto write_bool_function_param(RC::Unreal::FProperty* property, void* params, bool value) -> bool;

    auto write_linear_color_function_param(
            RC::Unreal::FProperty* property,
            void* params,
            float r,
            float g,
            float b,
            float a) -> bool;

    auto write_name_or_string_function_param(
            RC::Unreal::FProperty* property,
            void* params,
            std::wstring_view value) -> bool;

    auto find_struct_field_by_name(
            RC::Unreal::UScriptStruct* script_struct,
            std::wstring_view field_name) -> RC::Unreal::FProperty*;

    auto write_string_struct_field(
            RC::Unreal::FProperty* field,
            void* struct_value,
            std::wstring_view value) -> bool;

    auto write_string_array_struct_field(
            RC::Unreal::FProperty* field,
            void* struct_value,
            std::wstring_view value,
            RuntimeApMessageParamStorage& storage) -> bool;

    auto write_tres_loc_text_function_param(
            RC::Unreal::FProperty* property,
            void* params,
            std::wstring_view localized_text,
            RuntimeApMessageParamStorage& storage) -> bool;

    auto retain_runtime_ap_message_params(
            RuntimeApMessageParamStorage& storage,
            std::vector<std::byte>&& params) -> void;

    auto find_first_function_param_by_class(
            RC::Unreal::UFunction* function,
            std::initializer_list<std::wstring_view> property_classes) -> RC::Unreal::FProperty*;

    auto summarize_param_flags(RC::Unreal::FProperty* property) -> std::wstring;

    auto describe_property_detail(RC::Unreal::FProperty* property) -> std::wstring;

    auto format_hook_param_value(
            RC::Unreal::FProperty* property,
            const void* value_ptr,
            int element_size) -> std::wstring;

    auto format_stable_ability_trace_value(
            RC::Unreal::FProperty* property,
            const void* value_ptr,
            int element_size,
            std::wstring_view param_name) -> std::wstring;

    auto summarize_stable_function_params(
            RC::Unreal::UFunction* function_object,
            void* params,
            bool include_returns) -> std::wstring;

    auto try_read_function_object_param(
            RC::Unreal::UFunction* function_object,
            void* params,
            std::wstring_view param_name) -> RC::Unreal::UObject*;

    auto try_read_reflected_object_property_value(
            RC::Unreal::UObject* object,
            std::wstring_view property_name) -> RC::Unreal::UObject*;

    auto join_wide_parts(const std::vector<std::wstring>& parts, std::wstring_view separator) -> std::wstring;

    auto try_read_object_string_property(RC::Unreal::UObject* object, const wchar_t* property_name) -> std::optional<std::wstring>;

    auto try_read_object_name_property(RC::Unreal::UObject* object, const wchar_t* property_name) -> std::optional<std::wstring>;

    auto try_read_object_bool_property(RC::Unreal::UObject* object, const wchar_t* property_name) -> std::optional<bool>;

    auto try_read_object_property_object(RC::Unreal::UObject* object, const wchar_t* property_name) -> RC::Unreal::UObject*;

    auto try_read_object_scalar_property_as_string(RC::Unreal::UObject* object, const wchar_t* property_name) -> std::optional<std::wstring>;

    auto function_param_signature(RC::Unreal::UFunction* function_object) -> std::wstring;

    auto read_function_param_value_as_string(
            RC::Unreal::UFunction* function,
            void* params,
            std::wstring_view param_name) -> std::wstring;

    auto read_function_int_param(
            RC::Unreal::UFunction* function_object,
            void* params,
            std::wstring_view param_name) -> std::optional<int64_t>;

    auto summarize_function_params_compact(
            RC::Unreal::UFunction* function_object,
            void* params) -> std::wstring;

    auto try_get_function_default_object(RC::Unreal::UFunction* function_object) -> RC::Unreal::UObject*;

    auto format_object_debug_name(RC::Unreal::UObject* object) -> std::wstring;

    auto summarize_object_class_chain(RC::Unreal::UObject* object, size_t max_depth = 8) -> std::wstring;

    auto format_object_outer_chain(RC::Unreal::UObject* object, int max_depth = 6) -> std::wstring;

    auto is_game_viewport_context(RC::Unreal::UObject* object) -> bool;

    auto append_world_context_candidate(std::vector<RC::Unreal::UObject*>& candidates, RC::Unreal::UObject* object) -> void;

}
