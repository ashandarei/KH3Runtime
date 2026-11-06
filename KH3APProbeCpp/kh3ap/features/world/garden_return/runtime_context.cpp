#include "runtime_context.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/Property/FBoolProperty.hpp>

namespace kh3ap::garden_return_detail
{
    using namespace RC::Unreal;

    UObject* object_field(UObject* object, const wchar_t* name)
    {
        if (!is_uobject_valid_fast(object))
        {
            return nullptr;
        }
        auto* result = try_read_reflected_object_property_value(object, name);
        return is_uobject_valid_fast(result) ? result : nullptr;
    }

    UObject* library()
    {
        const RuntimeHotPathProfileScope profile{L"garden_return:library_lookup"};
        auto* result = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        return is_uobject_valid_fast(result) ? result : nullptr;
    }

    bool loading_closed()
    {
        auto* owner = library();
        auto* function = owner ? owner->GetFunctionByNameInChain(L"TresUI_IsClosedLoadingScreen") : nullptr;
        if (!is_uobject_valid_fast(function) || function->GetParmsSize() != 1)
        {
            return false;
        }
        auto* result = find_function_param_by_name(function, L"ReturnValue");
        if (!result || !result->IsA<FBoolProperty>())
        {
            return false;
        }
        uint8_t params{};
        owner->ProcessEvent(function, &params);
        return static_cast<FBoolProperty*>(result)->GetPropertyValueInContainer(&params);
    }

    UObject* player()
    {
        return object_field(object_field(object_field(object_field(viewport(), L"GameInstance"), L"m_UIManager"),
                                         L"m_pPlayerController"),
                            L"Pawn");
    }

    int player_id(UObject* pawn)
    {
        auto* property = is_uobject_valid_fast(pawn) ? pawn->GetPropertyByNameInChain(L"m_PlayerUniqueID") : nullptr;
        if (!property || property->GetElementSize() != 1)
        {
            return -1;
        }
        auto* controller = object_field(pawn, L"Controller");
        if (!controller || object_field(controller, L"Pawn") != pawn)
        {
            return -1;
        }
        return *property->ContainerPtrToValuePtr<uint8_t>(pawn);
    }

    bool save_open()
    {
        auto* save =
            object_field(object_field(object_field(viewport(), L"GameInstance"), L"m_UIManager"), L"m_SaveLoad");
        return save && field<uint8_t>(save, 0xa1);
    }

    UObject* viewport()
    {
        const RuntimeHotPathProfileScope profile{L"garden_return:viewport_lookup"};
        return object_field(
            UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, L"/Engine/Transient.TresGameEngine_0"),
            L"GameViewport");
    }

    std::wstring world()
    {
        auto* value = object_field(viewport(), L"World");
        return value ? value->GetFullName() : L"";
    }
} // namespace kh3ap::garden_return_detail
