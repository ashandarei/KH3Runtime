#include "kh02_reflected_call.h"
#include <Unreal/Property/FArrayProperty.hpp>
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/named_function_cache.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        auto resolve_call_function(UObject* target, const wchar_t* method) -> UFunction*
        {
            // Repeated character handoffs use the same small set of classes
            // and methods. Avoid walking their full function chains each time.
            static thread_local NamedFunctionCache<128> functions;
            return functions.resolve(target, method);
        }
    }

    auto kh02_require_loaded_object(const wchar_t* path) -> UObject*
    {
        // Native script classes and CDOs are used repeatedly during movie and
        // cutscene presentation. Keep weak identities, never a retained world.
        // Other asset paths still resolve normally on each synchronous request.
        static thread_local std::map<std::wstring, FWeakObjectPtr, std::less<>> native_objects;
        const std::wstring_view object_path(path);
        const bool native_object = object_path.starts_with(L"/Script/");
        if (native_object)
        {
            const auto found = native_objects.find(object_path);
            if (found != native_objects.end())
            {
                if (auto* object = found->second.Get(); is_uobject_valid_fast(object))
                {
                    return object;
                }
                native_objects.erase(found);
            }
        }
        auto* object = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, path);
        if (!is_uobject_valid_fast(object))
        {
            throw std::runtime_error("kh02_object_missing:" + to_utf8(path));
        }
        if (native_object)
        {
            constexpr size_t maximum_native_objects = 64;
            if (native_objects.size() == maximum_native_objects)
            {
                native_objects.erase(native_objects.begin());
            }
            native_objects.emplace(object_path, make_uobject_weak(object));
        }
        return object;
    }

    auto kh02_native_material_slot(UObject* mesh, int index) -> UObject**
    {
        auto* p = mesh->GetPropertyByNameInChain(L"m_OverrideMaterials");
        if (!p || !p->IsA<FArrayProperty>() || p->GetElementSize() != 16)
        {
            throw std::runtime_error("kh02_story_material_cache");
        }
        auto* inner = static_cast<FArrayProperty*>(p)->GetInner();
        if (!inner || inner->GetClass().GetName() != L"ObjectProperty" || inner->GetElementSize() != sizeof(UObject*))
        {
            throw std::runtime_error("kh02_story_material_cache_type");
        }

        struct Array
        {
            UObject** data;
            int32_t count, capacity;
        };

        auto* a = p->ContainerPtrToValuePtr<Array>(mesh);
        // The native parameter cache is initialized lazily. If absent, the game
        // will build it from the renderer's already-adapted material instances.
        if (a->count == 0 && a->capacity >= 0)
        {
            return nullptr;
        }
        if (a->count != 11 || a->capacity < a->count || !a->data || index < 0 || index >= a->count)
        {
            throw std::runtime_error("kh02_story_material_cache_bounds");
        }
        return a->data + index;
    }

    Kh02ReflectedCall::Kh02ReflectedCall(UObject* target, const wchar_t* method, const char* context,
                                         size_t max_parameter_bytes)
        : owner(target), function(resolve_call_function(target, method)),
          context_(context)
    {
        if (!is_uobject_valid_fast(function) || function->GetParmsSize() > max_parameter_bytes)
        {
            fail("function", method);
        }
        parameters.resize(function->GetParmsSize());
    }

    Kh02ReflectedCall::Kh02ReflectedCall(const wchar_t* path, const wchar_t* method, const char* context,
                                         size_t max_parameter_bytes)
        : Kh02ReflectedCall(kh02_require_loaded_object(path), method, context, max_parameter_bytes)
    {
    }

    void Kh02ReflectedCall::fail(const char* operation, const wchar_t* parameter) const
    {
        throw std::runtime_error(context_ + ":" + operation + ":" + to_utf8(parameter));
    }

    auto Kh02ReflectedCall::param(const wchar_t* name) -> FProperty*
    {
        return find_function_param_by_name(function, name);
    }

    void Kh02ReflectedCall::object(const wchar_t* name, UObject* value)
    {
        auto* property = param(name);
        if (property && property->GetClass().GetName() == L"ClassProperty" &&
            property->GetElementSize() == sizeof(UObject*))
        {
            if (!is_uobject_a_fast(value, UClass::StaticClass()))
            {
                fail("class_parameter", name);
            }
            *property->ContainerPtrToValuePtr<UObject*>(parameters.data()) = value;
            return;
        }
        if (!write_object_function_param(function, parameters.data(), name, value))
        {
            fail("object_parameter", name);
        }
    }

    void Kh02ReflectedCall::boolean(const wchar_t* name, bool value)
    {
        if (!write_bool_function_param(param(name), parameters.data(), value))
        {
            fail("bool_parameter", name);
        }
    }

    void Kh02ReflectedCall::integer(const wchar_t* name, int value)
    {
        if (!write_integral_function_param(param(name), parameters.data(), value))
        {
            fail("int_parameter", name);
        }
    }

    void Kh02ReflectedCall::name(const wchar_t* name, const wchar_t* value)
    {
        auto* property = param(name);
        if (!property || property->GetClass().GetName() != L"NameProperty" ||
            property->GetElementSize() != sizeof(FName))
        {
            fail("name_parameter", name);
        }
        *property->ContainerPtrToValuePtr<FName>(parameters.data()) = FName(value, FNAME_Add);
    }

    void Kh02ReflectedCall::number(const wchar_t* name, float value)
    {
        auto* property = param(name);
        if (!property || property->GetClass().GetName() != L"FloatProperty" || property->GetElementSize() != 4)
        {
            fail("float_parameter", name);
        }
        *property->ContainerPtrToValuePtr<float>(parameters.data()) = value;
    }

    void Kh02ReflectedCall::string(const wchar_t* name, const std::wstring& value)
    {
        struct StringView
        {
            const wchar_t* data;
            int32_t count, capacity;
        };

        auto* property = param(name);
        if (!property || property->GetClass().GetName() != L"StrProperty" ||
            property->GetElementSize() != sizeof(StringView))
        {
            fail("string_parameter", name);
        }
        auto& owned = strings_[name];
        owned = value;
        const StringView view{owned.c_str(), int32_t(owned.size() + 1), int32_t(owned.size() + 1)};
        std::memcpy(property->ContainerPtrToValuePtr<void>(parameters.data()), &view, sizeof(view));
    }

    void Kh02ReflectedCall::numbers(const wchar_t* name, std::initializer_list<float> values)
    {
        auto* property = param(name);
        if (!property || property->GetClass().GetName() != L"StructProperty" ||
            property->GetElementSize() != values.size() * sizeof(float))
        {
            fail("struct_parameter", name);
        }
        std::memcpy(property->ContainerPtrToValuePtr<void>(parameters.data()), values.begin(),
                    values.size() * sizeof(float));
    }

    void Kh02ReflectedCall::vector(const wchar_t* name, float x, float y, float z)
    {
        numbers(name, {x, y, z});
    }

    void Kh02ReflectedCall::vector2(const wchar_t* name, float x, float y)
    {
        numbers(name, {x, y});
    }

    void Kh02ReflectedCall::spawn_transform(float x, float y, float z)
    {
        auto* property = param(L"SpawnTransform");
        // Shipping KH3 uses this float Transform layout even when the UE4SS
        // host's public math wrappers use a different representation.
        const float values[12]{0, 0, 0, 1, x, y, z, 0, 1, 1, 1, 0};
        if (!property || property->GetClass().GetName() != L"StructProperty" ||
            property->GetElementSize() != sizeof(values) || property->GetOffset_Internal() != 16)
        {
            fail("transform_parameter", L"SpawnTransform");
        }
        std::memcpy(property->ContainerPtrToValuePtr<void>(parameters.data()), values, sizeof(values));
    }

    void Kh02ReflectedCall::run()
    {
        owner->ProcessEvent(function, parameters.empty() ? nullptr : parameters.data());
    }

    auto Kh02ReflectedCall::result() -> UObject*
    {
        return try_read_function_object_param(function, parameters.data(), L"ReturnValue");
    }

    auto Kh02ReflectedCall::boolean_result(const wchar_t* name) -> bool
    {
        auto* property = param(name);
        if (!property || property->GetClass().GetName() != L"BoolProperty")
        {
            fail("bool_result", name);
        }
        return static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(parameters.data());
    }

    auto Kh02ReflectedCall::result_vector() -> std::array<float, 3>
    {
        auto* property = param(L"ReturnValue");
        std::array<float, 3> result{};
        if (!property || property->GetClass().GetName() != L"StructProperty" ||
            property->GetElementSize() != sizeof(result))
        {
            fail("vector_result");
        }
        std::memcpy(result.data(), property->ContainerPtrToValuePtr<void>(parameters.data()), sizeof(result));
        return result;
    }
} // namespace kh3ap
