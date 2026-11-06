#include "kh02_menu_movie.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <stdexcept>

namespace kh3ap::kh02menu
{
    using namespace RC::Unreal;

    UObject* invoke_movie(UObject* object, const wchar_t* method, const std::vector<MovieArgument>& arguments,
                          double* number)
    {
        auto* function = is_uobject_valid_fast(object) ? object->GetFunctionByNameInChain(L"Invoke") : nullptr;
        if (!is_uobject_valid_fast(function) || function->GetParmsSize() > 256 || arguments.size() > 512)
        {
            throw std::runtime_error("kh02_challenge_invoke_layout");
        }
        std::vector<std::byte> parameters(function->GetParmsSize());
        auto* arguments_property = find_function_param_by_name(function, L"Params");
        if (!arguments_property || !arguments_property->IsA<FArrayProperty>() ||
            arguments_property->GetElementSize() != 16)
        {
            throw std::runtime_error("kh02_challenge_args_layout");
        }
        auto* argument_property = static_cast<FArrayProperty*>(arguments_property)->GetInner();
        if (!argument_property || argument_property->GetClass().GetName() != L"StructProperty" ||
            argument_property->GetElementSize() < 24 || argument_property->GetElementSize() > 128)
        {
            throw std::runtime_error("kh02_challenge_value_layout");
        }
        auto* method_property = find_function_param_by_name(function, L"FunctionName");
        if (!write_name_or_string_function_param(method_property, parameters.data(), method))
        {
            throw std::runtime_error("kh02_challenge_method");
        }
        std::vector<std::byte> values(arguments.size() * argument_property->GetElementSize());

        struct Cleanup
        {
            FProperty* argument_property;
            std::vector<std::byte>& values;
            size_t count;

            ~Cleanup()
            {
                for (size_t i = 0; i < count; ++i)
                {
                    destroy_property_value_dynamic(argument_property,
                                                   values.data() + i * argument_property->GetElementSize());
                }
            }
        } cleanup{argument_property, values, arguments.size()};

        for (size_t i = 0; i < arguments.size(); ++i)
        {
            auto* value = values.data() + i * argument_property->GetElementSize();
            bool written = false;
            if (auto* text_argument = std::get_if<std::wstring>(&arguments[i]))
            {
                written = write_as_value_string(argument_property, value, *text_argument);
            }
            else if (auto* integer_argument = std::get_if<int>(&arguments[i]))
            {
                written = write_as_value_int(argument_property, value, *integer_argument);
            }
            else
            {
                written = write_as_value_bool(argument_property, value, std::get<bool>(arguments[i]));
            }
            if (!written)
            {
                throw std::runtime_error("kh02_challenge_value");
            }
        }

        struct Array
        {
            void* data;
            int32_t count, capacity;
        };

        *arguments_property->ContainerPtrToValuePtr<Array>(parameters.data()) = {
            values.data(), static_cast<int32_t>(arguments.size()), static_cast<int32_t>(arguments.size())};
        object->ProcessEvent(function, parameters.data());
        UObject* returned = nullptr;
        if (auto* result = find_function_param_by_name(function, L"ReturnValue"))
        {
            auto* value = result->ContainerPtrToValuePtr<void>(parameters.data());
            returned = try_get_as_value_object(value, result);
            if (number)
            {
                *number = try_get_as_value_number(value, result).value_or(-1);
            }
            destroy_property_value_dynamic(result, value);
        }
        // Args borrows our vector; never destroy the containing parameter array.
        if (method_property)
        {
            destroy_property_value_dynamic(method_property,
                                           method_property->ContainerPtrToValuePtr<void>(parameters.data()));
        }
        return returned;
    }

    UObject* find_movie_member(UObject* object, const wchar_t* path)
    {
        std::wstring remaining(path);
        size_t position = 0;
        while (object && position < remaining.size())
        {
            const auto end = remaining.find(L'.', position);
            const auto key = remaining.substr(position, end == std::wstring::npos ? end : end - position);
            object = try_gfx_get_member_value_object(gfx, object, key);
            if (end == std::wstring::npos)
            {
                break;
            }
            position = end + 1;
        }
        return object;
    }

    UObject* open_movie_asset(UObject* movie_asset, int priority)
    {
        if (!is_uobject_valid_fast(movie_asset))
        {
            throw std::runtime_error("kh02_menu_movie_asset");
        }
        auto* movie_class_default = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, L"/Script/ScaleformUI.Default__GFxMoviePlayer");
        if (!is_uobject_valid_fast(movie_class_default))
        {
            throw std::runtime_error("kh02_challenge_movie_class");
        }
        FStaticConstructObjectParameters construct(movie_class_default->GetClassPrivate(),
                                                   movie_asset->GetOuterPrivate());
        construct.SetFlags = RF_Transient;
        auto* movie_settings = UObjectGlobals::StaticConstructObject(construct);
        if (!is_uobject_valid_fast(movie_settings) ||
            !write_integral_function_param(movie_settings->GetPropertyByNameInChain(L"Priority"), movie_settings,
                                           priority))
        {
            throw std::runtime_error("kh02_challenge_movie_priority");
        }
        auto* function = movie_class_default->GetFunctionByNameInChain(L"OpenMovie");
        if (!is_uobject_valid_fast(function) || function->GetParmsSize() > 256)
        {
            throw std::runtime_error("kh02_challenge_open_function");
        }
        std::vector<std::byte> parameters(function->GetParmsSize());
        if (!write_object_function_param(function, parameters.data(), L"SwfMovie", movie_asset) ||
            !write_object_function_param(function, parameters.data(), L"MoviePlayer", movie_settings) ||
            !write_bool_function_param(find_function_param_by_name(function, L"DisplayWithHudOff"), parameters.data(),
                                       true))
        {
            throw std::runtime_error("kh02_challenge_open_params");
        }
        movie_class_default->ProcessEvent(function, parameters.data());
        auto* player = try_read_function_object_param(function, parameters.data(), L"ReturnValue");
        if (!is_uobject_valid_fast(player))
        {
            throw std::runtime_error("kh02_challenge_open_movie");
        }
        auto* root = try_read_reflected_object_property_value(player, L"Root");
        if (!is_uobject_valid_fast(root))
        {
            throw std::runtime_error("kh02_challenge_root");
        }
        try_gfx_set_member_value_bool(gfx, root, L"visible", false);
        for (auto* key : {L"bAllowInput", L"bAllowFocus", L"bCaptureKeyboardInput", L"bCaptureMouseInput",
                          L"bCaptureGamepadInput", L"bCaptureTouchInput", L"bCloseOnLevelChange"})
        {
            write_bool_function_param(player->GetPropertyByNameInChain(key), player, false);
        }
        for (auto* method : {L"SetMovieCanReceiveInput", L"SetMovieCanReceiveFocus"})
        {
            auto* input_function = player->GetFunctionByNameInChain(method);
            if (is_uobject_valid_fast(input_function) && input_function->GetParmsSize() == sizeof(uint8_t))
            {
                uint8_t disabled{};
                player->ProcessEvent(input_function, &disabled);
            }
        }
        return player;
    }
} // namespace kh3ap::kh02menu
