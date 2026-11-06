#include "kh02_intro_presentation.h"
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
#include "kh3ap/core/input/runtime_player_control.h"
#include <chrono>
#include <cmath>

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

        constexpr float completed_fade_tolerance = .001f;
        constexpr int intro_fade_layer = 0;
        constexpr float intro_natural_end_seconds = 14.f;
        constexpr float intro_fade_cancel_seconds = .3f;
        constexpr auto view_cleanup_timeout = std::chrono::seconds(5);

    } // namespace

    void Kh02IntroPresentation::set_hud_hidden(bool hide)
    {
        if (hidden == hide)
        {
            return;
        }
        auto* library = kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        ReflectedCall status(library, hide ? L"TresUI_HideHudStatus" : L"TresUI_ShowHudStatus");
        status.run();
        ReflectedCall minimap(library, L"TresUI_SetEnableNaviMap");
        minimap.boolean(L"bEnable", !hide);
        minimap.run();
        if (hide)
        {
            ReflectedCall information(library, L"TresUI_CloseInformation");
            information.run();
        }
        hidden = hide;
    }

    auto Kh02IntroPresentation::fade(UObject* pawn) -> Json
    {
        auto* library = kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        // DW009's original fade track and its skipped BPTresStartFadeIn
        // branch own layer zero. Inspect that layer only.
        ReflectedCall amount(library, L"BPTresGetFadeAmount");
        amount.object(L"WorldContextObject", pawn);
        amount.integer(L"InLayer", intro_fade_layer);
        amount.run();
        auto* p = amount.param(L"ReturnValue");
        if (!p || p->GetClass().GetName() != L"FloatProperty" || p->GetElementSize() != sizeof(float))
        {
            throw std::runtime_error("kh02_intro_fade_amount_layout");
        }
        const float value = *p->ContainerPtrToValuePtr<float>(amount.parameters.data());
        if (!std::isfinite(value) || value < 0 || value > 1.0f + completed_fade_tolerance)
        {
            throw std::runtime_error("kh02_intro_fade_amount_range");
        }
        ReflectedCall changing(library, L"BPTresIsFadeChange");
        changing.object(L"WorldContextObject", pawn);
        changing.integer(L"InLayer", intro_fade_layer);
        changing.run();
        return {{"layer", intro_fade_layer}, {"amount", value}, {"changing", changing.boolean_result()}};
    }

    auto Kh02IntroPresentation::finish(UObject* pawn, bool source_finished, float maximum_position) -> bool
    {
        if (!cleanup_requested)
        {
            // Restore the view only after the original scene has retired its
            // script, including its own Skip path. Never finish a live scene.
            if (!source_finished)
            {
                throw std::runtime_error("kh02_intro_cleanup_requires_source_finish");
            }
            cleanup = {{"before", fade(pawn)},
                       {"requested", false},
                       {"verified", false},
                       {"natural_end", maximum_position >= intro_natural_end_seconds},
                       {"source_script_retired", true}};
            if (hidden)
            {
                // DW009 normally chains into another cinematic. Its Skip
                // exit can retire the script while retaining cinematic
                // control. Balance the host's entry only after retirement.
                ReflectedCall end(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                                  L"TresEndCinematicMode");
                end.object(L"WorldContextObject", pawn);
                end.run();
                cleanup["cinematic_end_requested"] = true;
            }
            cleanup_since = std::chrono::steady_clock::now();
            cleanup_requested = true;
            if (cleanup["before"]["amount"].get<float>() > completed_fade_tolerance ||
                cleanup["before"]["changing"].get<bool>())
            {
                ReflectedCall cancel(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                                     L"BPTresCancelFade");
                cancel.object(L"WorldContextObject", pawn);
                cancel.integer(L"InLayer", intro_fade_layer);
                cancel.number(L"InFadeTime", intro_fade_cancel_seconds);
                cancel.boolean(L"InApplyGameSpeed", false);
                cancel.run();
                cleanup["requested"] = true;
            }
        }
        cleanup["after"] = fade(pawn);
        const auto& after = cleanup["after"];
        const bool control_ready = runtime_sora_control_ready(pawn);
        cleanup["control_ready"] = control_ready;
        const bool ready =
            after["amount"].get<float>() <= completed_fade_tolerance && !after["changing"].get<bool>() && control_ready;
        cleanup["verified"] = ready;
        if (!ready && std::chrono::steady_clock::now() - cleanup_since > view_cleanup_timeout)
        {
            throw std::runtime_error("kh02_intro_fade_cleanup_timeout");
        }
        return ready;
    }

    auto Kh02IntroPresentation::hud_hidden() const -> bool
    {
        return hidden;
    }

    void Kh02IntroPresentation::reset_cleanup()
    {
        cleanup_requested = false;
        cleanup = Json::object();
    }

    auto Kh02IntroPresentation::cleanup_snapshot() const -> const Json&
    {
        return cleanup;
    }

} // namespace kh3ap
