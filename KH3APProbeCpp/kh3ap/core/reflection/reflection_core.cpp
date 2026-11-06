#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/diagnostics/profiling/reflection_profile.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/features/grants/messages/ap_message_param_storage.h"

#include <UnrealDef.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace kh3ap
{
    namespace
    {
        constexpr int frame_local_slab_headroom_bytes = 32;
        constexpr int minimum_frame_local_slab_bytes = 1;
        constexpr int maximum_frame_local_slab_bytes = 128;

        constexpr bool reflection_profiling_enabled = KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS != 0;
        auto get_ue4ss_module_handle() -> HMODULE
        {
            static HMODULE module = GetModuleHandleW(L"UE4SS.dll");
            return module;
        }

        template <typename Fn> auto resolve_ue4ss_export(const char* name) -> Fn
        {
            HMODULE const module = get_ue4ss_module_handle();
            if (!module)
            {
                return nullptr;
            }
            return reinterpret_cast<Fn>(GetProcAddress(module, name));
        }
    } // namespace

    auto record_runtime_hot_path_profile_sample(std::wstring_view scope_name, uint64_t elapsed_us)
        -> void
    {
        if constexpr (!reflection_profiling_enabled)
        {
            return;
        }
        record_runtime_reflection_profile_sample(scope_name, elapsed_us);
    }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    RuntimeHotPathProfileScope::RuntimeHotPathProfileScope(std::wstring_view scope_name)
        : scope_name(scope_name), started_at(std::chrono::steady_clock::now()),
          enabled(reflection_profiling_enabled)
    {
    }

    RuntimeHotPathProfileScope::~RuntimeHotPathProfileScope()
    {
        if (!enabled)
        {
            return;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started_at);
        record_runtime_hot_path_profile_sample(scope_name, static_cast<uint64_t>(elapsed.count()));
    }
#endif

    auto get_frame_node(RC::Unreal::FFrame& frame) -> RC::Unreal::UFunction*
    {
        auto* frame_impl = reinterpret_cast<RC::Unreal::FFrame_50_AndBelow*>(&frame);
        return frame_impl->Node;
    }

    auto get_frame_locals(RC::Unreal::FFrame& frame) -> void*
    {
        auto* frame_impl = reinterpret_cast<RC::Unreal::FFrame_50_AndBelow*>(&frame);
        return frame_impl->Locals;
    }

    auto get_frame_object(RC::Unreal::FFrame& frame) -> RC::Unreal::UObject*
    {
        auto* frame_impl = reinterpret_cast<RC::Unreal::FFrame_50_AndBelow*>(&frame);
        return frame_impl->Object;
    }

    auto ue_realloc_for_script_array(void* original, size_t size, uint32_t alignment) -> void*
    {
        using ReallocFn = void* (*)(void*, size_t, uint32_t);
        static ReallocFn realloc_fn =
            resolve_ue4ss_export<ReallocFn>("?Realloc@FMemory@Unreal@RC@@SAPEAXPEAX_KI@Z");
        if (!realloc_fn)
        {
            return nullptr;
        }
        return realloc_fn(original, size, alignment);
    }

    auto try_get_property_min_alignment_dynamic(const RC::Unreal::FProperty* property) -> uint32_t
    {
        using GetMinAlignmentFn = int (*)(const RC::Unreal::FProperty*);
        static GetMinAlignmentFn get_min_alignment = resolve_ue4ss_export<GetMinAlignmentFn>(
            "?GetMinAlignment@FProperty@Unreal@RC@@QEBAHXZ");
        if (!property || !get_min_alignment)
        {
            return 8;
        }
        return static_cast<uint32_t>(std::max(1, get_min_alignment(property)));
    }

    auto initialize_property_value_dynamic(RC::Unreal::FProperty* property, void* value) -> bool
    {
        using InitializeValueFn = void (*)(RC::Unreal::FProperty*, void*);
        static InitializeValueFn initialize_value = resolve_ue4ss_export<InitializeValueFn>(
            "?InitializeValue@FProperty@Unreal@RC@@QEAAXPEAX@Z");
        if (!property || !value || !initialize_value)
        {
            return false;
        }
        initialize_value(property, value);
        return true;
    }

    auto destroy_property_value_dynamic(RC::Unreal::FProperty* property, void* value) -> void
    {
        using DestroyValueFn = void (*)(RC::Unreal::FProperty*, void*);
        static DestroyValueFn destroy_value =
            resolve_ue4ss_export<DestroyValueFn>("?DestroyValue@FProperty@Unreal@RC@@QEAAXPEAX@Z");
        if (property && value && destroy_value)
        {
            destroy_value(property, value);
        }
    }

    auto read_frame_local_dword(RC::Unreal::FFrame& frame, int slot_index)
        -> std::optional<uint32_t>
    {
        if (slot_index < 0)
        {
            return std::nullopt;
        }

        RC::Unreal::UFunction* frame_node = get_frame_node(frame);
        void* frame_locals = get_frame_locals(frame);
        if (!frame_node || !frame_locals)
        {
            return std::nullopt;
        }

        const int slab_size =
            std::clamp(frame_node->GetParmsSize() + frame_local_slab_headroom_bytes,
                       minimum_frame_local_slab_bytes, maximum_frame_local_slab_bytes);
        const int offset = slot_index * static_cast<int>(sizeof(uint32_t));
        if (offset + static_cast<int>(sizeof(uint32_t)) > slab_size)
        {
            return std::nullopt;
        }

        uint32_t dword_value = 0;
        const auto* frame_bytes = static_cast<const std::byte*>(frame_locals);
        std::memcpy(&dword_value, frame_bytes + offset, sizeof(uint32_t));
        return dword_value;
    }

    auto find_function_param_by_name(RC::Unreal::UFunction* function, std::wstring_view param_name)
        -> RC::Unreal::FProperty*
    {
        if (!function)
        {
            return nullptr;
        }

        RC::Unreal::FProperty* fuzzy_match = nullptr;
        const std::wstring suffix_prefix = std::wstring{param_name} + STR("_");
        for (RC::Unreal::FProperty* param = function->GetFirstProperty(); param;
             param = param->HasNext() ? param->GetNextFieldAsProperty() : nullptr)
        {
            if (!param || !param->HasAnyPropertyFlags(RC::Unreal::CPF_Parm))
            {
                continue;
            }

            const std::wstring param_full_name = param->GetName();
            if (param_full_name == param_name)
            {
                return param;
            }
            if (!fuzzy_match && param_full_name.starts_with(suffix_prefix))
            {
                fuzzy_match = param;
            }
        }

        return fuzzy_match;
    }

    auto read_unsigned_param_value(const void* value_ptr, int element_size) -> uint64_t
    {
        if (!value_ptr || element_size <= 0)
        {
            return 0;
        }

        const int clamped_size = std::min(element_size, static_cast<int>(sizeof(uint64_t)));
        uint64_t value = 0;
        std::memcpy(&value, value_ptr, static_cast<size_t>(clamped_size));
        return value;
    }

    auto retain_runtime_ap_message_params(RuntimeApMessageParamStorage& storage,
                                          std::vector<std::byte>&& params) -> void
    {
        if (params.empty())
        {
            return;
        }

        storage.retain_bytes(std::move(params), RuntimeApMessageParamStorage::max_entries);
    }

    auto
    find_first_function_param_by_class(RC::Unreal::UFunction* function,
                                       std::initializer_list<std::wstring_view> property_classes)
        -> RC::Unreal::FProperty*
    {
        if (!function)
        {
            return nullptr;
        }

        for (RC::Unreal::FProperty* param = function->GetFirstProperty(); param;
             param = param->HasNext() ? param->GetNextFieldAsProperty() : nullptr)
        {
            if (!param || !param->HasAnyPropertyFlags(RC::Unreal::CPF_Parm) ||
                param->HasAnyPropertyFlags(RC::Unreal::CPF_OutParm) ||
                param->HasAnyPropertyFlags(RC::Unreal::CPF_ReturnParm))
            {
                continue;
            }
            const std::wstring property_class = param->GetClass().GetName();
            if (std::any_of(property_classes.begin(), property_classes.end(),
                            [&](std::wstring_view candidate)
                            {
                                return property_class == candidate;
                            }))
            {
                return param;
            }
        }
        return nullptr;
    }

    auto try_read_function_object_param(RC::Unreal::UFunction* function_object, void* params,
                                        std::wstring_view param_name) -> RC::Unreal::UObject*
    {
        if (!function_object || !params)
        {
            return nullptr;
        }

        RC::Unreal::FProperty* property = find_function_param_by_name(function_object, param_name);
        if (!property || property->GetClass().GetName() != STR("ObjectProperty"))
        {
            return nullptr;
        }

        if (RC::Unreal::UObject** value =
                property->ContainerPtrToValuePtr<RC::Unreal::UObject*>(params))
        {
            return *value;
        }
        return nullptr;
    }

    auto try_read_reflected_object_property_value(RC::Unreal::UObject* object,
                                                  std::wstring_view property_name)
        -> RC::Unreal::UObject*
    {
        if (!object || !kh3ap::is_uobject_valid_fast(object))
        {
            return nullptr;
        }

        RC::Unreal::FProperty* property =
            object->GetPropertyByNameInChain(std::wstring{property_name}.c_str());
        if (!property || property->GetClass().GetName() != STR("ObjectProperty") ||
            property->GetElementSize() < static_cast<int>(sizeof(RC::Unreal::UObject*)))
        {
            return nullptr;
        }

        // Reuse the metadata just resolved. GetValuePtrByPropertyNameInChain
        // performs the same full hierarchy search a second time.
        const void* value_ptr = property->ContainerPtrToValuePtr<void>(object);
        if (!value_ptr)
        {
            return nullptr;
        }
        RC::Unreal::UObject* object_value = nullptr;
        std::memcpy(&object_value, value_ptr, sizeof(RC::Unreal::UObject*));
        if (!object_value || !kh3ap::is_uobject_valid_fast(object_value))
        {
            return nullptr;
        }
        return object_value;
    }

    auto join_wide_parts(const std::vector<std::wstring>& parts, std::wstring_view separator)
        -> std::wstring
    {
        std::wostringstream stream;
        for (size_t index = 0; index < parts.size(); ++index)
        {
            if (index > 0)
            {
                stream << separator;
            }
            stream << parts[index];
        }
        return stream.str();
    }

    auto try_read_object_string_property(RC::Unreal::UObject* object, const wchar_t* property_name)
        -> std::optional<std::wstring>
    {
        if (!object || !property_name)
        {
            return std::nullopt;
        }

        auto* value_ptr = object->GetValuePtrByPropertyName<RC::Unreal::FString>(property_name);
        if (!value_ptr)
        {
            return std::nullopt;
        }

        const wchar_t* chars = value_ptr->GetCharArray();
        if (!chars)
        {
            return std::nullopt;
        }

        return std::wstring{chars};
    }

    auto try_read_object_name_property(RC::Unreal::UObject* object, const wchar_t* property_name)
        -> std::optional<std::wstring>
    {
        if (!object || !property_name)
        {
            return std::nullopt;
        }

        auto* value_ptr = object->GetValuePtrByPropertyName<RC::Unreal::FName>(property_name);
        if (!value_ptr)
        {
            return std::nullopt;
        }

        return value_ptr->ToString();
    }

    auto try_read_object_bool_property(RC::Unreal::UObject* object, const wchar_t* property_name)
        -> std::optional<bool>
    {
        if (!object || !property_name)
        {
            return std::nullopt;
        }

        auto* value_ptr = object->GetValuePtrByPropertyName<bool>(property_name);
        if (!value_ptr)
        {
            return std::nullopt;
        }

        return *value_ptr;
    }

    auto try_read_object_property_object(RC::Unreal::UObject* object, const wchar_t* property_name)
        -> RC::Unreal::UObject*
    {
        if (!object || !property_name)
        {
            return nullptr;
        }

        auto* value_ptr = object->GetValuePtrByPropertyName<RC::Unreal::UObject*>(property_name);
        if (!value_ptr)
        {
            return nullptr;
        }

        return *value_ptr;
    }

    auto try_read_object_scalar_property_as_string(RC::Unreal::UObject* object,
                                                   const wchar_t* property_name)
        -> std::optional<std::wstring>
    {
        if (!object || !property_name)
        {
            return std::nullopt;
        }

        RC::Unreal::FProperty* property = object->GetPropertyByNameInChain(property_name);
        void* value_ptr = object->GetValuePtrByPropertyNameInChain(property_name);
        if (!property || !value_ptr)
        {
            return std::nullopt;
        }

        const std::wstring property_class = property->GetClass().GetName();
        if (property_class == STR("NameProperty"))
        {
            if (const auto value = try_read_object_name_property(object, property_name);
                value.has_value())
            {
                return value;
            }
        }
        if (property_class == STR("StrProperty"))
        {
            return try_read_object_string_property(object, property_name);
        }
        if (property_class == STR("BoolProperty"))
        {
            if (const auto value = try_read_object_bool_property(object, property_name);
                value.has_value())
            {
                return *value ? std::optional<std::wstring>{STR("true")}
                              : std::optional<std::wstring>{STR("false")};
            }
        }
        if (property_class == STR("ObjectProperty"))
        {
            if (RC::Unreal::UObject* value = try_read_object_property_object(object, property_name))
            {
                return value->GetFullName();
            }
            return std::optional<std::wstring>{STR("<none>")};
        }

        const int element_size = property->GetElementSize();
        if (element_size <= 0)
        {
            return std::nullopt;
        }

        const uint64_t unsigned_value = read_unsigned_param_value(value_ptr, element_size);
        if (property_class == STR("IntProperty") || property_class == STR("Int64Property"))
        {
            const auto signed_value = read_signed_param_value(value_ptr, element_size);
            return signed_value ? std::optional{std::to_wstring(*signed_value)} : std::nullopt;
        }
        if (property_class == STR("UInt32Property") || property_class == STR("UInt64Property") ||
            property_class == STR("ByteProperty"))
        {
            return std::to_wstring(unsigned_value);
        }

        return format_bytes_hex(value_ptr, static_cast<size_t>(std::clamp(element_size, 1, 16)));
    }

    auto try_get_function_default_object(RC::Unreal::UFunction* function_object)
        -> RC::Unreal::UObject*
    {
        if (!function_object)
        {
            return nullptr;
        }

        RC::Unreal::UObject* outer = function_object->GetOuterPrivate();
        RC::Unreal::UClass* owner_class = nullptr;
        while (outer)
        {
            if (RC::Unreal::UClass* const outer_class = RC::Unreal::Cast<RC::Unreal::UClass>(outer))
            {
                owner_class = outer_class;
                break;
            }
            outer = outer->GetOuterPrivate();
        }

        if (!owner_class)
        {
            return nullptr;
        }

        RC::Unreal::UObject** const default_object_ptr =
            owner_class->GetValuePtrByPropertyName<RC::Unreal::UObject*>(STR("ClassDefaultObject"));
        return (default_object_ptr && kh3ap::is_uobject_valid_fast(*default_object_ptr))
                   ? *default_object_ptr
                   : nullptr;
    }

    auto is_game_viewport_context(RC::Unreal::UObject* object) -> bool
    {
        if (!object)
        {
            return false;
        }
        return object->GetFullName().find(STR("GameViewportClient")) != std::wstring::npos;
    }

} // namespace kh3ap
