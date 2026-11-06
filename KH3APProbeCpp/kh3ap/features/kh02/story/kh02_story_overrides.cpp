#include "kh02_story_overrides.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/Hooks.hpp>
#include <stdexcept>
#include <utility>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr std::size_t signs_context_offset_bytes = 0;
        constexpr std::size_t signs_result_offset_bytes = sizeof(UObject*);
        constexpr std::size_t signs_parameter_minimum_bytes = sizeof(UObject*) + sizeof(uint8_t);
        constexpr std::size_t signs_parameter_maximum_bytes = 2 * sizeof(UObject*);
    } // namespace

    struct Kh02StoryNativeOverrides::Impl
    {
        inline static Impl* active{};
        inline static bool listener_registered{};
        FWeakObjectPtr target_activate_function, all_signs_function, matinee_play_function, information_function;
        Thunk original_target_activate{}, original_all_signs{}, original_matinee_play{}, original_information{};
        Thunk signs_replacement{};
        Replacements current_replacements;
        BeforeEncounter before_encounter;
        const FName play_event_name{L"Play", FNAME_Add};

        void install_signs(Thunk replacement)
        {
            auto* all_signs = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, L"/Script/TresGame.TresGameBlueprintLibrary:BP_TresHaveAllSignKeyItem");
            auto* signs_context = is_uobject_valid_fast(all_signs)
                                      ? find_function_param_by_name(all_signs, L"WorldContextObject")
                                      : nullptr;
            auto* signs_result =
                is_uobject_valid_fast(all_signs) ? find_function_param_by_name(all_signs, L"ReturnValue") : nullptr;
            if (!signs_context || signs_context->GetClass().GetName() != L"ObjectProperty" ||
                signs_context->GetOffset_Internal() != signs_context_offset_bytes ||
                signs_context->GetElementSize() != sizeof(UObject*) || !signs_result ||
                !signs_result->IsA<FBoolProperty>() ||
                signs_result->GetOffset_Internal() != signs_result_offset_bytes ||
                all_signs->GetParmsSize() < signs_parameter_minimum_bytes ||
                all_signs->GetParmsSize() > signs_parameter_maximum_bytes)
            {
                throw std::runtime_error("The zodiac inventory function has an unsupported signature.");
            }
            original_all_signs = all_signs->GetFuncPtr();
            all_signs_function = make_uobject_weak(all_signs);
            all_signs->SetFuncPtr(replacement);
            signs_replacement = replacement;
        }

        void install_remaining(Replacements replacements, BeforeEncounter callback)
        {
            // Retain replacement identities before the first mutation so a later
            // signature failure can still restore any functions already replaced.
            current_replacements = replacements;
            // Blueprint-to-native Play calls can bypass ProcessEvent. Wrap the
            // owning native thunk as well, retaining its original operand parsing.
            auto* play =
                UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, L"/Script/Engine.MatineeActor:Play");
            if (!is_uobject_valid_fast(play) || !(play->GetFunctionFlags() & FUNC_Native) || !play->GetFuncPtr())
            {
                throw std::runtime_error("kh02_matinee_play_function");
            }
            original_matinee_play = play->GetFuncPtr();
            matinee_play_function = make_uobject_weak(play);
            play->SetFuncPtr(replacements.play);
            auto* information = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, L"/Script/TresGame.TresGameBlueprintLibrary:TresUI_OpenInformation");
            if (!is_uobject_valid_fast(information) || !(information->GetFunctionFlags() & FUNC_Native) ||
                !information->GetFuncPtr())
            {
                throw std::runtime_error("kh02_information_function");
            }
            original_information = information->GetFuncPtr();
            information_function = make_uobject_weak(information);
            information->SetFuncPtr(replacements.information);
            auto* activate = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, L"/Script/TresGame.TresGimmickComponentBase:BP_ActivateGimmickComponent");
            if (!is_uobject_valid_fast(activate) || !(activate->GetFunctionFlags() & FUNC_Native) ||
                !activate->GetFuncPtr())
            {
                throw std::runtime_error("kh02_target_activate_function");
            }
            original_target_activate = activate->GetFuncPtr();
            target_activate_function = make_uobject_weak(activate);
            activate->SetFuncPtr(replacements.target_activate);
            before_encounter = std::move(callback);
            active = this;
            if (!listener_registered)
            {
                Hook::RegisterProcessEventPreCallback(
                    [](UObject* object, UFunction* function, void*)
                    {
                        if (auto* owner = active)
                        {
                            owner->route_encounter(object, function);
                        }
                    });
                listener_registered = true;
            }
        }

        void route_encounter(UObject* object, UFunction* function)
        {
            // Reject unrelated events before allocating a name string or touching
            // their receiver. Keep the exact check for case/number semantics.
            if (!is_uobject_valid_fast(function) || function->GetNamePrivate() != play_event_name ||
                function->GetName() != L"Play" || !is_uobject_valid_fast(object) ||
                !is_uobject_a_fast(object, UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr,
                                                                                     L"/Script/Engine.MatineeActor")))
            {
                return;
            }
            const auto path = object->GetPathName();
            const bool camera = path.starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_camera_ms040.") &&
                                (object->GetName() == L"ms040s" || object->GetName() == L"ms040e");
            const bool scene = path.starts_with(L"/Game/Zutscene/Localization/en/dw/cutscene_dw02_dw013.") &&
                               object->GetName() == L"dw013_1";
            if (!camera && !scene)
            {
                return;
            }

            before_encounter(object, scene);
        }

        void restore()
        {
            if (auto* function = static_cast<UFunction*>(information_function.Get());
                is_uobject_valid_fast(function) && function->GetFuncPtr() == current_replacements.information)
            {
                function->SetFuncPtr(original_information);
            }
            if (auto* function = static_cast<UFunction*>(all_signs_function.Get());
                is_uobject_valid_fast(function) && function->GetFuncPtr() == signs_replacement)
            {
                function->SetFuncPtr(original_all_signs);
            }
            if (auto* function = static_cast<UFunction*>(target_activate_function.Get());
                is_uobject_valid_fast(function) && function->GetFuncPtr() == current_replacements.target_activate)
            {
                function->SetFuncPtr(original_target_activate);
            }
            if (auto* fn = static_cast<UFunction*>(matinee_play_function.Get());
                is_uobject_valid_fast(fn) && fn->GetFuncPtr() == current_replacements.play)
            {
                fn->SetFuncPtr(original_matinee_play);
            }

            if (active == this)
            {
                active = nullptr;
            }
        }
    };

    Kh02StoryNativeOverrides::Kh02StoryNativeOverrides() : impl(std::make_unique<Impl>()) {}

    Kh02StoryNativeOverrides::~Kh02StoryNativeOverrides()
    {
        restore();
    }

    void Kh02StoryNativeOverrides::install_signs(Thunk replacement)
    {
        impl->install_signs(replacement);
    }

    void Kh02StoryNativeOverrides::install_remaining(Replacements replacements, BeforeEncounter callback)
    {
        impl->install_remaining(replacements, std::move(callback));
    }

    auto Kh02StoryNativeOverrides::original(Slot slot) const -> Thunk
    {
        switch (slot)
        {
        case Slot::signs:
            return impl->original_all_signs;
        case Slot::play:
            return impl->original_matinee_play;
        case Slot::information:
            return impl->original_information;
        case Slot::target_activate:
            return impl->original_target_activate;
        }
        return nullptr;
    }

    void Kh02StoryNativeOverrides::restore()
    {
        impl->restore();
    }
} // namespace kh3ap
