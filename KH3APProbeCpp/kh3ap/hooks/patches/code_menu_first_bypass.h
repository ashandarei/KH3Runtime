#pragma once

#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>

namespace kh3ap
{
    // Local runtime experiment: leave the asset and the native menu itself intact.
    // Unknown Blueprint argument encodings fall through without advancing Code.
    class RuntimeCodeMenuFirstBypass
    {
        using UObject = RC::Unreal::UObject;
        using UFunction = RC::Unreal::UFunction;
        using FFrame = RC::Unreal::FFrame;
        inline static RC::Unreal::UnrealScriptFunction original_{};
        RC::Unreal::FWeakObjectPtr function_{};

        static void intercept(UObject* context, FFrame& stack, void* result)
        {
            auto* frame = reinterpret_cast<RC::Unreal::FFrame_50_AndBelow*>(&stack);
            bool cheat{}, challenge{};
            const wchar_t* route = L"ProcessEvent";
            if (frame->Code)
            {
                // Reviewed call uses EX_True, EX_False, EX_EndFunctionParms.
                // Accept either boolean literal, but never guess variable widths.
                auto* code = frame->Code;
                if ((code[0] != 0x27 && code[0] != 0x28) ||
                    (code[1] != 0x27 && code[1] != 0x28) || code[2] != 0x16)
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[KH3APProbeCpp] CodeMenuFirst bypass passthrough: unsupported Blueprint arguments\n"));
                    original_(context, stack, result);
                    return;
                }
                cheat = code[0] == 0x27;
                challenge = code[1] == 0x27;
                frame->Code += 3;
                route = L"Blueprint literals";
            }
            else if (frame->Locals)
            {
                // Install validates two native bools at byte offsets 0 and 1.
                cheat = frame->Locals[0] != 0;
                challenge = frame->Locals[1] != 0;
            }
            else
            {
                original_(context, stack, result);
                return;
            }
            std::wstring caller;
            if (!try_get_uobject_full_name_fast(frame->Node, caller)) caller = L"<unknown>";
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] CodeMenuFirst bypass intercepted bCheat={} bChallenge={} route={} caller={} original_called=false\n"),
                cheat, challenge, route, caller);
        }

    public:
        void install()
        {
            using namespace RC::Unreal;
            if (function_.Get()) return;
            auto* function = UObjectGlobals::StaticFindObject<UFunction*>(
                UFunction::StaticClass(), nullptr,
                STR("/Script/TresGame.TresGameBlueprintLibrary:TresUI_SetCodeMenuFirst"));
            if (!is_uobject_valid_fast(function)) return;
            auto* cheat = find_function_param_by_name(function, STR("bCheat"));
            auto* challenge = find_function_param_by_name(function, STR("bChallenge"));
            if (!function->HasAnyFunctionFlags(FUNC_Native) ||
                function->GetNumParms() != 2 || function->GetParmsSize() != 2 ||
                function->GetReturnProperty() || !cheat || !challenge ||
                !cheat->IsA<FBoolProperty>() || !challenge->IsA<FBoolProperty>() ||
                cheat->GetOffset_Internal() != 0 || challenge->GetOffset_Internal() != 1)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] CodeMenuFirst bypass rejected: signature mismatch\n"));
                return;
            }
            original_ = function->GetFuncPtr();
            if (!original_ || original_ == &intercept) return;
            function_ = function;
            function->SetFuncPtr(&intercept);
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] CodeMenuFirst bypass installed: runtime experiment enabled\n"));
        }

        void uninstall()
        {
            auto* function = static_cast<UFunction*>(function_.Get());
            if (is_uobject_valid_fast(function) && function->GetFuncPtr() == &intercept)
                function->SetFuncPtr(original_);
            function_ = {};
        }
    };
}
