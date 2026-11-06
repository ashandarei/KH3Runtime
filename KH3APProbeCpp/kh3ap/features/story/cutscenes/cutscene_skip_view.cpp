#include <chrono>
#include "kh3ap/features/story/cutscenes/cutscene_skip_view.h"
#include "kh3ap/features/story/cutscenes/cutscene_skip_view_policy.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <stdexcept>
#include <cstring>

namespace kh3ap
{
    using namespace RC::Unreal;
    namespace
    {
        constexpr auto max_cutscene_view_parameter_bytes = 128;

        constexpr auto cutscene_hidden_opacity_threshold = 0.01f;
        constexpr auto cutscene_skip_fade_seconds = 0.3f;
        constexpr auto library_path = L"/Script/TresGame.Default__TresGameBlueprintLibrary";
        constexpr auto inspection_interval = std::chrono::milliseconds(100);
        constexpr auto handoff_timeout = std::chrono::seconds(30);
        constexpr int foreground_layer = 0;

        struct Call : Kh02ReflectedCall
        {
            Call(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "cutscene_skip_view",
                                    max_cutscene_view_parameter_bytes)
            {
            }
        };
    } // namespace

    void CutsceneSkipView::reset()
    {
        pending = false;
        clearing = false;
        source_actor = {};
        source_world = {};
        source_name.clear();
    }

    void CutsceneSkipView::arm(UObject* source)
    {
        if (!is_uobject_valid_fast(source) ||
            !cutscene_skip_view_scene(source->GetClassPrivate()->GetPathName()))
        {
            return;
        }
        auto* world = kh02_actor_world(source);
        if (!is_uobject_valid_fast(world) || world != kh02_current_world())
        {
            return;
        }
        reset();
        source_actor = make_uobject_weak(source);
        source_world = make_uobject_weak(world);
        source_name = source->GetPathName();
        deadline = std::chrono::steady_clock::now() + handoff_timeout;
        next_check = {};
        pending = true;
    }

    void CutsceneSkipView::tick()
    {
        // Only a real Skip marker arms this bounded transaction. Idle frames
        // perform no world, pawn, UI or fade discovery.
        if (!pending)
        {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now < next_check)
        {
            return;
        }
        next_check = now + inspection_interval;
        auto* world = source_world.Get();
        if (!is_uobject_valid_fast(world) || world != kh02_current_world() || now > deadline)
        {
            reset();
            return;
        }
        if (is_uobject_valid_fast(source_actor.Get()))
        {
            return;
        }
        try
        {
            auto* library = kh02_require_loaded_object(library_path);
            Call local(library, L"GetLocalTresPlayerPawnBase");
            local.object(L"WorldContextObject", world);
            local.run();
            auto* pawn = local.result();
            if (!is_uobject_valid_fast(pawn) || kh02_actor_world(pawn) != world ||
                !runtime_sora_control_ready(pawn))
            {
                return;
            }
            Call loading(library, L"TresUI_IsClosedLoadingScreen");
            loading.run();
            Call cinematic(library, L"TresIsCinematicMode");
            cinematic.object(L"WorldContextObject", pawn);
            cinematic.run();
            Call paused(kh02_require_loaded_object(L"/Script/Engine.Default__GameplayStatics"),
                        L"IsGamePaused");
            paused.object(L"WorldContextObject", pawn);
            paused.run();
            if (!cutscene_skip_view_ready({true, true, true, loading.boolean_result(),
                                           paused.boolean_result(), cinematic.boolean_result()}))
            {
                return;
            }
            Call amount(library, L"BPTresGetFadeAmount");
            amount.object(L"WorldContextObject", pawn);
            amount.integer(L"InLayer", foreground_layer);
            amount.run();
            auto* value = amount.param(L"ReturnValue");
            if (!value || value->GetClass().GetName() != L"FloatProperty" ||
                value->GetElementSize() != sizeof(float))
            {
                throw std::runtime_error("fade_amount_layout");
            }
            const float opacity = *value->ContainerPtrToValuePtr<float>(amount.parameters.data());
            Call changing(library, L"BPTresIsFadeChange");
            changing.object(L"WorldContextObject", pawn);
            changing.integer(L"InLayer", foreground_layer);
            changing.run();
            const bool animating = changing.boolean_result();
            if (std::isfinite(opacity) && opacity <= cutscene_hidden_opacity_threshold &&
                !animating)
            {
                if (clearing)
                {
                    RC::Output::send<RC::LogLevel::Normal>(
                        STR("[KH3APProbeCpp] CutsceneSkipView verified source={}\n"), source_name);
                }
                reset();
                return;
            }
            if (!clearing && cutscene_skip_view_stuck(opacity, animating))
            {
                Call cancel(library, L"BPTresCancelFade");
                cancel.object(L"WorldContextObject", pawn);
                cancel.integer(L"InLayer", foreground_layer);
                cancel.number(L"InFadeTime", cutscene_skip_fade_seconds);
                cancel.boolean(L"InApplyGameSpeed", false);
                cancel.run();
                clearing = true;
                RC::Output::send<RC::LogLevel::Normal>(
                    STR("[KH3APProbeCpp] CutsceneSkipView releasing retired-source fade "
                        "source={}\n"),
                    source_name);
            }
        }
        catch (const std::exception& error)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] CutsceneSkipView failed source={} reason={}\n"), source_name,
                std::wstring(error.what(), error.what() + std::strlen(error.what())));
            reset();
        }
    }
} // namespace kh3ap
