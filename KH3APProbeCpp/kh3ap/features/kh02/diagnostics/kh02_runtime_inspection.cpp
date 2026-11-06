#include "kh02_runtime_inspection.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <cstring>
#include <stdexcept>
#include <utility>
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;

    namespace
    {
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;

        struct ReflectedCall : Kh02ReflectedCall
        {
            ReflectedCall(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "kh02_story", reflected_parameter_limit_bytes)
            {
            }
        };

        constexpr int unreadable_flag_value = -99'999;
        constexpr int stream_inspection_limit = 128;

        auto flag(UObject* object, const wchar_t* name) -> bool
        {
            auto* property = is_uobject_valid_fast(object) ? object->GetPropertyByNameInChain(name) : nullptr;
            if (!property || !property->IsA<FBoolProperty>())
            {
                throw std::runtime_error("kh02_story_bool");
            }
            return static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(object);
        }

        auto path(UObject* object) -> std::string
        {
            return is_uobject_valid_fast(object) ? to_utf8(object->GetPathName()) : "";
        }

    } // namespace

    auto kh02_native_flags() -> Json
    {
        Json result = Json::object();
        auto* library = kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        for (const auto* key : {L"gameflow_DW", L"gameflow_DW_GEAR_GETCOUNT"})
        {
            ReflectedCall read(library, L"GetGameFlag");
            read.name(L"FlagName", key);
            read.run();
            result[to_utf8(key)] = read_function_int_param(read.function, read.parameters.data(), L"ReturnValue")
                                       .value_or(unreadable_flag_value);
        }
        return result;
    }

    auto kh02_stream_states(UObject* w, std::wstring_view prefix) -> Json
    {
        Json result = Json::array();
        auto* p = is_uobject_valid_fast(w) ? w->GetPropertyByNameInChain(L"StreamingLevels") : nullptr;

        struct Array
        {
            UObject** data;
            int32_t count, capacity;
        };

        if (!p || !p->IsA<FArrayProperty>() || p->GetElementSize() != sizeof(Array))
        {
            return result;
        }
        const auto* array = p->ContainerPtrToValuePtr<Array>(w);
        if (!array->data || array->count < 0 || array->count > stream_inspection_limit ||
            array->capacity < array->count)
        {
            return result;
        }
        for (int i = 0; i < array->count; ++i)
        {
            auto* s = array->data[i];
            if (!is_uobject_valid_fast(s) || s->GetOuterPrivate() != w)
            {
                continue;
            }
            ReflectedCall package(s, L"GetWorldAssetPackageFName");
            package.run();
            auto* name = package.param(L"ReturnValue");
            if (!name || name->GetClass().GetName() != L"NameProperty" || name->GetElementSize() != sizeof(FName))
            {
                throw std::runtime_error("kh02_ground_package_name");
            }
            const auto value = name->ContainerPtrToValuePtr<FName>(package.parameters.data())->ToString();
            if (!value.starts_with(prefix))
            {
                continue;
            }
            ReflectedCall loaded(s, L"IsLevelLoaded"), visible(s, L"IsLevelVisible"),
                pending(s, L"IsStreamingStatePending");
            loaded.run();
            visible.run();
            pending.run();
            result.push_back({{"package", to_utf8(value)},
                              {"stream", path(s)},
                              {"requested_visible", flag(s, L"bShouldBeVisible")},
                              {"loaded", loaded.boolean_result()},
                              {"visible", visible.boolean_result()},
                              {"pending", pending.boolean_result()}});
        }
        return result;
    }

    auto kh02_ground_streams(UObject* w) -> Json
    {
        return kh02_stream_states(w, L"/Game/Zaps/dw/umap/dw_02/dw_02_Ground_");
    }

} // namespace kh3ap
