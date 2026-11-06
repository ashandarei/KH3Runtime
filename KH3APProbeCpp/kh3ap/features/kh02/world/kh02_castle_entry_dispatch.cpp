#include "kh02_castle_entry_dispatch.h"
#include "kh3ap/features/kh02/story/kh02_loading_policy.h"
#include <Unreal/UFunction.hpp>
#include <Unreal/TArray.hpp>

#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <stdexcept>

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

    } // namespace

    void kh02_dispatch_castle_entry(UObject* script, bool first_castle_intro, bool entry_retry, bool entry_backtrack,
                                    bool arrival_owned, Json& loading_proof)
    {
        // Revisits use the saved playable checkpoint dispatcher. First entry
        // prepares the world here; DW009, its flashback and DW011 own the
        // cinematic progression and the eventual field-music handoff.
        const auto* entry_event = entry_retry ? L"dw_02_Persistent_REvt_dw_02LevelStart_DevilTower_Retry"
                                              : L"dw_02_Persistent_REvt_dw_02LevelStart";
        ReflectedCall init(script, entry_event);
        {
            // First entry and revisits use the same playable dispatcher.
            // Its texture-loading cushion is redundant with our actual
            // stream/fade readiness gate. Resume this one Delay next tick
            // and restore its bytecode before any other invocation runs.
            // Cinematic dispatchers and boss Retry keep their own timing.
            Kh02CastleDelayOverride restore;

            if (!first_castle_intro && !entry_retry && arrival_owned)
            {
                auto* fn = script->GetFunctionByNameInChain(L"ExecuteUbergraph_dw_02_gameflow");
                auto* kismet = kh02_require_loaded_object(L"/Script/Engine.Default__KismetSystemLibrary");
                auto* delay = is_uobject_valid_fast(kismet) ? kismet->GetFunctionByNameInChain(L"Delay") : nullptr;
                if (is_uobject_valid_fast(fn) && is_uobject_valid_fast(delay))
                {
                    auto& code = fn->GetScript();
                    if (restore.apply({code.GetData(), static_cast<size_t>(code.Num())},
                                      reinterpret_cast<uintptr_t>(kismet), reinterpret_cast<uintptr_t>(delay)))
                    {
                        loading_proof["revisit_texture_wait"] = {
                            {"fixed_seconds", kh02_castle_loading::texture_wait_seconds},
                            {"mode", "arrival_readiness_gate"},
                            {"continuation", kh02_castle_loading::ready_continuation},
                            {"entry", entry_backtrack ? "revisit" : "first"}};
                    }
                }
            }
            if (first_castle_intro)
            {
                ReflectedCall challenges(
                    kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"), L"RemoteEvent");
                challenges.object(L"WorldContextObject", script);
                challenges.name(L"EventName", L"dw_02_gameflow_REvt_Challenge_Open_LevelStart");
                challenges.run();
                loading_proof["source_entry_event"] = "opening_preparation";
            }
            else
            {
                init.run();
                loading_proof["source_entry_event"] = to_utf8(entry_event);
            }
        }
    }

} // namespace kh3ap
