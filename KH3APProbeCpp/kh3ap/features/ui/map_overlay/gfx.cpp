#include "kh3ap/core/gfx/as_value_type.h"
#include "kh3ap/features/ui/map_overlay/gfx_bindings.h"
#include "kh3ap/features/ui/map_overlay/support.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include <Unreal/UObject.hpp>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace map_overlay_detail;
    namespace map_overlay_detail
    {
        thread_local GfxBinding gfx_get_binding, gfx_set_binding, gfx_invoke_binding;
        auto borrow_string(FProperty* property, void* buffer, const wchar_t* text) -> bool
        {
            if (!property || property->GetClass().GetName() != L"StrProperty" ||
                property->GetElementSize() != sizeof(BorrowedString))
            {
                return false;
            }
            const int32_t count = static_cast<int32_t>(std::wcslen(text) + 1);
            const BorrowedString str{text, count, count};
            std::memcpy(property->ContainerPtrToValuePtr<void>(buffer), &str, sizeof(str));
            return true;
        }

        auto gfx_number(const GfxProbeFunctionPaths& paths, UObject* object, const wchar_t* name,
                        bool boolean) -> std::optional<double>
        {
            if (!is_uobject_valid_fast(object))
            {
                return {};
            }
            auto* fn = gfx_get_binding.resolve(object, paths.get_member_value_path, false);
            if (!fn)
            {
                return {};
            }
            alignas(std::max_align_t) std::array<std::byte, max_gfx_parameter_bytes> params{};
            if (!gfx_get_binding.borrow_name(params.data(), name))
            {
                return {};
            }
            auto* result = gfx_get_binding.result;
            if (!result)
            {
                return {};
            }
            object->ProcessEvent(fn, params.data());
            if (boolean)
            {
                auto* value = result->ContainerPtrToValuePtr<void>(params.data());
                return gfx_get_binding.return_value.read_bool(value);
            }
            return gfx_get_binding.return_value.number(
                result->ContainerPtrToValuePtr<void>(params.data()));
        }

        auto gfx_object(const GfxProbeFunctionPaths& paths, UObject* object, const wchar_t* name)
            -> UObject*
        {
            if (!is_uobject_valid_fast(object))
            {
                return nullptr;
            }
            auto* fn = gfx_get_binding.resolve(object, paths.get_member_value_path, false);
            if (!fn)
            {
                return nullptr;
            }
            alignas(std::max_align_t) std::array<std::byte, max_gfx_parameter_bytes> params{};
            if (!gfx_get_binding.borrow_name(params.data(), name))
            {
                return nullptr;
            }
            auto* result = gfx_get_binding.result;
            if (!result)
            {
                return nullptr;
            }
            object->ProcessEvent(fn, params.data());
            auto* value = gfx_get_binding.return_value.object(
                result->ContainerPtrToValuePtr<void>(params.data()));
            return is_uobject_valid_fast(value) ? value : nullptr;
        }

        auto gfx_set(const GfxProbeFunctionPaths& paths, UObject* object, const wchar_t* name,
                     double value, bool boolean) -> bool
        {
            if (!is_uobject_valid_fast(object))
            {
                return false;
            }
            auto* fn = gfx_set_binding.resolve(object, paths.set_member_value_path, false, true);
            if (!fn)
            {
                return false;
            }
            alignas(std::max_align_t) std::array<std::byte, max_gfx_parameter_bytes> params{};
            if (!gfx_set_binding.borrow_name(params.data(), name))
            {
                return false;
            }
            auto* input = gfx_set_binding.value;
            if (!input)
            {
                return false;
            }
            auto* target = input->ContainerPtrToValuePtr<void>(params.data());
            if (!(boolean ? gfx_set_binding.input_value.write_bool(target, value != 0)
                          : gfx_set_binding.input_value.write_number(target, value)))
            {
                return false;
            }
            object->ProcessEvent(fn, params.data());
            return true;
        }

        auto gfx_numbers(const GfxProbeFunctionPaths& paths, UObject* object, const wchar_t* method,
                         std::initializer_list<double> values, std::optional<double>* number)
            -> UObject*
        {
            if (!is_uobject_valid_fast(object))
            {
                return nullptr;
            }
            auto* fn = gfx_invoke_binding.resolve(object, paths.invoke_path, true);
            if (!fn)
            {
                return nullptr;
            }
            alignas(std::max_align_t) std::array<std::byte, max_gfx_parameter_bytes> params{};
            if (!gfx_invoke_binding.borrow_name(params.data(), method))
            {
                return nullptr;
            }
            auto* array = gfx_invoke_binding.value;
            if (!array || array->GetClass().GetName() != L"ArrayProperty")
            {
                return nullptr;
            }
            auto* inner = const_cast<FProperty*>(static_cast<FArrayProperty*>(array)->GetInner());
            if (!inner || inner->GetElementSize() <= 0)
            {
                return nullptr;
            }
            const auto argument_bytes =
                static_cast<size_t>(inner->GetElementSize()) * values.size();
            if (argument_bytes > max_gfx_argument_bytes)
            {
                return nullptr;
            }
            alignas(std::max_align_t) std::array<std::byte, max_gfx_argument_bytes> arguments{};
            size_t i = 0;
            for (double value : values)
            {
                if (!gfx_invoke_binding.input_value.write_number(
                        arguments.data() + i++ * inner->GetElementSize(), value))
                {
                    return nullptr;
                }
            }
            struct Array
            {
                void* data;
                int32_t count, capacity;
            };
            const Array args{arguments.data(), static_cast<int32_t>(values.size()),
                             static_cast<int32_t>(values.size())};
            std::memcpy(array->ContainerPtrToValuePtr<void>(params.data()), &args, sizeof(args));
            object->ProcessEvent(fn, params.data());
            auto* result = gfx_invoke_binding.result;
            if (number && result)
            {
                *number = gfx_invoke_binding.return_value.number(
                    result->ContainerPtrToValuePtr<void>(params.data()));
            }
            return result ? gfx_invoke_binding.return_value.object(
                                result->ContainerPtrToValuePtr<void>(params.data()))
                          : nullptr;
        }

        auto gfx_object_argument(const GfxProbeFunctionPaths& paths, UObject* object,
                                 const wchar_t* method, UObject* argument) -> UObject*
        {
            if (!is_uobject_valid_fast(object) || !is_uobject_valid_fast(argument))
            {
                return nullptr;
            }
            auto* fn = gfx_invoke_binding.resolve(object, paths.invoke_path, true);
            if (!fn)
            {
                return nullptr;
            }
            alignas(std::max_align_t) std::array<std::byte, max_gfx_parameter_bytes> params{};
            if (!gfx_invoke_binding.borrow_name(params.data(), method))
            {
                return nullptr;
            }
            auto* array = gfx_invoke_binding.value;
            if (!array || array->GetClass().GetName() != L"ArrayProperty")
            {
                return nullptr;
            }
            auto* inner = const_cast<FProperty*>(static_cast<FArrayProperty*>(array)->GetInner());
            if (!inner || inner->GetElementSize() <= 0 ||
                inner->GetElementSize() > max_gfx_parameter_bytes)
            {
                return nullptr;
            }
            alignas(std::max_align_t) std::array<std::byte, max_gfx_parameter_bytes> value{};
            if (!gfx_invoke_binding.input_value.write_object(value.data(), argument))
            {
                return nullptr;
            }
            struct Array
            {
                void* data;
                int32_t count, capacity;
            };
            const Array args{value.data(), 1, 1};
            std::memcpy(array->ContainerPtrToValuePtr<void>(params.data()), &args, sizeof(args));
            object->ProcessEvent(fn, params.data());
            auto* result = gfx_invoke_binding.result;
            return result ? gfx_invoke_binding.return_value.object(
                                result->ContainerPtrToValuePtr<void>(params.data()))
                          : nullptr;
        }

        auto gfx_invoke(const GfxProbeFunctionPaths& paths, UObject* object, const wchar_t* method,
                        const wchar_t* value) -> bool
        {
            if (!is_uobject_valid_fast(object))
            {
                return false;
            }
            auto* fn = gfx_invoke_binding.resolve(object, paths.invoke_path, true);
            if (!fn)
            {
                return false;
            }
            std::vector<std::byte> params(fn->GetParmsSize());
            if (!borrow_string(gfx_invoke_binding.name, params.data(), method))
            {
                return false;
            }
            auto* array = gfx_invoke_binding.value;
            if (!array || array->GetClass().GetName() != L"ArrayProperty")
            {
                return false;
            }
            auto* inner = const_cast<FProperty*>(static_cast<FArrayProperty*>(array)->GetInner());
            std::vector<std::byte> argument;
            if (value)
            {
                if (!inner || inner->GetClass().GetName() != L"StructProperty")
                {
                    return false;
                }
                auto* structure = static_cast<FStructProperty*>(inner)->GetStruct();
                auto* type = find_struct_field_by_name(structure, L"ASType");
                auto* string = find_struct_field_by_name(structure, L"ASString");
                argument.resize(inner->GetElementSize());
                if (!type || type->GetElementSize() != 1 ||
                    !borrow_string(string, argument.data(), value))
                {
                    return false;
                }
                *type->ContainerPtrToValuePtr<uint8_t>(argument.data()) =
                    as_value_type_id(ASValueType::StringW);
                struct Array
                {
                    void* data;
                    int32_t count, capacity;
                };
                const Array args{argument.data(), 1, 1};
                std::memcpy(array->ContainerPtrToValuePtr<void>(params.data()), &args,
                            sizeof(args));
            }
            object->ProcessEvent(fn, params.data());
            return true;
        }
    } // namespace map_overlay_detail
} // namespace kh3ap
