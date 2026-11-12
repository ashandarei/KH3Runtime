#include "kh3ap/core/input/menu_face_button.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"

#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

namespace kh3ap
{
    auto runtime_menu_face_button(std::wstring_view key) -> RuntimeMenuFaceButton
    {
        using namespace RC::Unreal;
        if (!is_menu_face_button(key))
        {
            return RuntimeMenuFaceButton::none;
        }

        static FWeakObjectPtr owner_ref;
        static FWeakObjectPtr function_ref;
        auto* owner = owner_ref.Get();
        if (!is_uobject_valid_fast(owner))
        {
            owner = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, L"/Script/TresGame.Default__TresLSIGameActor");
            owner_ref = make_uobject_weak(owner);
        }
        auto* function = static_cast<UFunction*>(function_ref.Get());
        if (!is_uobject_valid_fast(function))
        {
            function = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, L"/Script/TresGame.TresLSIGameActor:NeedToExchangeActionButton");
            function_ref = make_uobject_weak(function);
        }
        // Static, no arguments, one native bool return. Do not guess a binding
        // if the function is unavailable or its reflected signature changes.
        if (!is_uobject_valid_fast(owner) || !is_uobject_valid_fast(function) ||
            function->GetParmsSize() != sizeof(bool))
        {
            return RuntimeMenuFaceButton::none;
        }
        bool exchange{};
        owner->ProcessEvent(function, &exchange);
        return menu_face_button(key, exchange);
    }
} // namespace kh3ap
