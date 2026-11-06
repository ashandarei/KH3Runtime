#include <array>
#include <memory>
#include "kh3ap/features/story/progression/battlegate_activation_service.h"

#include "kh3ap/features/story/progression/battlegate_activation.h"
#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>

#include <utility>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        constexpr auto max_battlegate_frame_depth = 4;
        constexpr auto hook_path = L"/Script/TresGame.TresGameBlueprintLibrary:IsGameCleared";
    } // namespace

    struct RuntimeBattlegateActivationService::Impl
    {
        RuntimeBattlegateActivationConfig config;
        std::pair<int, int> hook_ids{};
        bool registered{};
        std::array<bool, battlegate_activation_rules.size()> logged{};

        explicit Impl(RuntimeBattlegateActivationConfig value) : config(std::move(value))
        {
        }
        ~Impl()
        {
            if (registered)
            {
                try
                {
                    UObjectGlobals::UnregisterHook(hook_path, hook_ids);
                }
                catch (...)
                {
                }
            }
        }

        auto handle_post(UnrealScriptFunctionCallableContext& context) -> void
        {
            if (!context.RESULT_DECL)
            {
                return;
            }
            auto* getter = UObjectGlobals::StaticFindObject<UFunction*>(UFunction::StaticClass(),
                                                                        nullptr, hook_path);
            if (!is_uobject_valid_fast(getter))
            {
                return;
            }
            auto* return_property = getter->GetReturnProperty();
            if (!return_property || !return_property->IsA<FBoolProperty>())
            {
                return;
            }
            auto* boolean = static_cast<FBoolProperty*>(return_property);
            // Preserve real postgame behavior, including revisits to earlier
            // story states. Only a false Battlegate result needs correction.
            if (boolean->GetPropertyValue(context.RESULT_DECL))
            {
                return;
            }

            auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&context.TheStack);
            for (size_t depth = 0; depth < max_battlegate_frame_depth && frame; ++depth)
            {
                if (is_uobject_valid_fast(frame->Node) && is_uobject_valid_fast(frame->Object) &&
                    frame->Code)
                {
                    const auto caller = frame->Node->GetFullName();
                    for (size_t index = 0; index < battlegate_activation_rules.size(); ++index)
                    {
                        const auto& rule = battlegate_activation_rules[index];
                        if (!rule.matches(caller, frame->Object->GetFullName()))
                        {
                            continue;
                        }
                        auto& script = frame->Node->GetScript();
                        const auto begin = reinterpret_cast<uintptr_t>(script.GetData());
                        const auto code = reinterpret_cast<uintptr_t>(frame->Code);
                        if (!begin || script.Num() <= 0 || code < begin ||
                            code - begin >= static_cast<size_t>(script.Num()))
                        {
                            return;
                        }
                        bool matched{};
                        for (auto* property : frame->Node->ForEachProperty())
                        {
                            if (!property || !property->IsA<FBoolProperty>() ||
                                !battlegate_result_local(rule, property->GetName()))
                            {
                                continue;
                            }
                            matched = is_battlegate_result_branch(
                                {script.GetData(), static_cast<size_t>(script.Num())}, code - begin,
                                reinterpret_cast<uintptr_t>(getter),
                                reinterpret_cast<uintptr_t>(property));
                            if (matched)
                            {
                                break;
                            }
                        }
                        if (!matched)
                        {
                            return;
                        }
                        int32_t story{};
                        if (!try_call_get_game_flag(rule.story_flag, story, config.runtime_state,
                                                    config.get_game_flag_path,
                                                    config.blueprint_library_default_object_path,
                                                    false) ||
                            story < rule.completed_value)
                        {
                            return;
                        }
                        boolean->SetPropertyValue(context.RESULT_DECL, true);
                        if (!logged[index])
                        {
                            logged[index] = true;
                            Output::send<LogLevel::Warning>(
                                STR("[KH3APProbeCpp] BattlegateActivation flag={} story={} "
                                    "completed={} "
                                    "result=true scope=portal_branch "
                                    "saved_game_cleared_unchanged=true\n"),
                                rule.story_flag, story, rule.completed_value);
                        }
                        return;
                    }
                }
                if (frame->PreviousFrame == frame)
                {
                    break;
                }
                frame = frame->PreviousFrame;
            }
        }

        auto install_once() -> void
        {
            if (registered)
            {
                return;
            }
            auto* getter = UObjectGlobals::StaticFindObject<UFunction*>(UFunction::StaticClass(),
                                                                        nullptr, hook_path);
            if (!is_uobject_valid_fast(getter) || !(getter->GetFunctionFlags() & FUNC_Native) ||
                !getter->GetReturnProperty() || !getter->GetReturnProperty()->IsA<FBoolProperty>())
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] BattlegateActivation rejected getter layout\n"));
                return;
            }
            try
            {
                hook_ids = UObjectGlobals::RegisterHook(
                    hook_path,
                    [](UnrealScriptFunctionCallableContext&, void*)
                    {
                    },
                    [this](UnrealScriptFunctionCallableContext& context, void*)
                    {
                        try
                        {
                            handle_post(context);
                        }
                        catch (...)
                        {
                        }
                    },
                    nullptr);
                registered = true;
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] BattlegateActivation installed "
                        "mode=world_story_completion scope=portal_branch no_flag_write=true "
                        "no_poll=true\n"));
            }
            catch (...)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] BattlegateActivation hook registration failed\n"));
            }
        }
    };

    RuntimeBattlegateActivationService::RuntimeBattlegateActivationService(
        RuntimeBattlegateActivationConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }
    RuntimeBattlegateActivationService::~RuntimeBattlegateActivationService() = default;
    auto RuntimeBattlegateActivationService::install_once() -> void
    {
        impl_->install_once();
    }
} // namespace kh3ap
