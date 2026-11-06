#include "keyblade_ascension_sync.h"
#include "keyblade_options_helpers.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/bridge/utilities.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <vector>

namespace kh3ap::keyblade_options
{
    using namespace RC;
    using namespace RC::Unreal;
    namespace
    {
        auto try_call_actor_bool(UObject* actor, std::wstring_view function_path) -> bool
        {
            if (!actor)
            {
                return false;
            }

            UFunction* function_object = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, std::wstring{function_path}.c_str());
            if (!function_object)
            {
                const size_t separator = function_path.rfind(L':');
                if (separator != std::wstring_view::npos)
                {
                    const std::wstring function_name{function_path.substr(separator + 1)};
                    function_object = actor->GetFunctionByNameInChain(function_name.c_str());
                    if (!function_object && function_name == STR("IsAscensionMode"))
                    {
                        function_object = actor->GetFunctionByNameInChain(STR("IsInAscensionMode"));
                    }
                }
            }
            if (!function_object)
            {
                return false;
            }

            std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
            std::fill(params.begin(), params.end(), std::byte{0});
            {
                const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
                actor->ProcessEvent(function_object, params.empty() ? nullptr : params.data());
            }

            FProperty* const return_property =
                find_function_param_by_name(function_object, STR("ReturnValue"));
            void* const return_value =
                return_property ? return_property->ContainerPtrToValuePtr<void>(params.data())
                                : nullptr;
            return return_property && return_value &&
                   read_unsigned_param_value(return_value,
                                             std::min(return_property->GetElementSize(), 1)) != 0;
        }

        auto refresh_ascension_actor_cache(RuntimeKeybladeStyleState& state, UObject* sora,
                                           const RuntimeKeybladeStyleSanityDeps& deps) -> bool
        {
            if (!sora || !state.ascension_actor_scan_requested)
            {
                return false;
            }

            state.ascension_actor_scan_requested = false;
            const bool scan_after_cinematic_exit = state.ascension_scan_after_cinematic_exit;
            state.ascension_scan_after_cinematic_exit = false;
            state.ascension_actor = {};
            state.ascension_checks_suspended = false;

            UClass* const enemy_class = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, std::wstring{deps.tres_enemy_pawn_e_ex_301_class_path}.c_str());
            if (!enemy_class)
            {
                return false;
            }

            std::vector<AActor*> actors;
            UGameplayStatics::GetAllActorsOfClass(sora, enemy_class, actors);
            bool armored_found = false;
            for (AActor* const actor : actors)
            {
                if (!actor || !kh3ap::is_uobject_valid_fast(actor))
                {
                    continue;
                }
                const UClass* const actor_class = actor->GetClassPrivate();
                if (!actor_class)
                {
                    continue;
                }
                const std::wstring class_name = actor_class->GetName();
                if (class_name.find(STR("e_ex301_Avatar")) != std::wstring::npos)
                {
                    armored_found = true;
                }
                else if (!state.ascension_actor.Get() &&
                         class_name.find(STR("e_ex301_Pawn")) != std::wstring::npos)
                {
                    state.ascension_actor = make_uobject_weak(actor);
                }
            }

            // On the way into the fight, an armored actor means there is nothing
            // useful to poll yet.  After the cinematic exits, prefer the normal
            // pawn even if the level still contains dormant avatar actors.
            if (armored_found && !scan_after_cinematic_exit)
            {
                state.ascension_actor = {};
                state.ascension_checks_suspended = true;
            }
            else if (scan_after_cinematic_exit && !state.ascension_actor.Get())
            {
                state.ascension_checks_suspended = true;
            }
            return true;
        }

        auto is_ascension_mode_active(RuntimeKeybladeStyleState& state,
                                      const RuntimeKeybladeStyleSanityDeps& deps) -> bool
        {
            if (state.ascension_checks_suspended || !state.ascension_actor.Get() ||
                !kh3ap::is_uobject_valid_fast(state.ascension_actor.Get()))
            {
                return false;
            }
            return try_call_actor_bool(state.ascension_actor.Get(),
                                       deps.tres_enemy_pawn_e_ex_301_is_ascension_mode_path);
        }

    } // namespace

    auto prepare_keyblade_style_context(RuntimeKeybladeStyleState& state, UObject* sora,
                                        UObject* controller, bool sora_trusted,
                                        const RuntimeKeybladeStyleSanityDeps& deps) -> bool
    {
        const bool in_bt_04 = deps.runtime_context_summary.find(STR("/Game/Levels/bt/bt_04/")) !=
                              std::wstring_view::npos;

        if (!in_bt_04)
        {
            if (state.ascension_mode)
            {
                state.force_apply = true;
            }
            state.ascension_mode = false;
            state.special_anti_form_started = false;
            state.ascension_actor = {};
            state.ascension_actor_scan_requested = false;
            state.ascension_scan_after_cinematic_exit = false;
            state.ascension_check_due = {};
            state.ascension_checks_suspended = false;
        }

        // The existing validated pawn cache is the active-Sora gate. Movement is
        // intentionally irrelevant: the loop remains active while Sora is idle.
        if (!sora || !sora_trusted || !kh3ap::is_uobject_valid_fast(sora))
        {
            state.last_sora = {};
            state.last_weapon.clear();
            state.ascension_actor = {};
            state.special_anti_form_started = false;
            state.ascension_actor_scan_requested = false;
            state.ascension_scan_after_cinematic_exit = false;
            state.ascension_check_due = {};
            state.ascension_checks_suspended = false;
            state.force_apply = true;
            return false;
        }

        // A new pawn can be cached before its equipment and reflected functions
        // are ready. Let one existing pass complete before reading its style
        // ability entry.
        if (state.last_sora.Get() != sora)
        {
            state.last_sora = make_uobject_weak(sora);
            state.last_weapon.clear();
            state.force_apply = true;
            if (deps.debug_logging)
            {
                Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Keyblade style sanity waiting for stable pawn\n"));
            }
            return false;
        }

        const bool ascension_check_due =
            in_bt_04 && state.ascension_actor_scan_requested &&
            state.ascension_check_due != std::chrono::steady_clock::time_point{} &&
            deps.now >= state.ascension_check_due;
        if (ascension_check_due)
        {
            const bool refreshed = refresh_ascension_actor_cache(state, sora, deps);
            state.ascension_check_due = {};
            const bool ascension_mode = refreshed && !state.ascension_checks_suspended
                                            ? is_ascension_mode_active(state, deps)
                                            : false;
            if (state.ascension_mode != ascension_mode)
            {
                state.ascension_mode = ascension_mode;
                state.force_apply = true;
            }
        }

        if (!controller || !kh3ap::is_uobject_valid_fast(controller))
        {
            state.last_sora = {};
            state.last_weapon.clear();
            state.force_apply = true;
            return false;
        }

        return true;
    }
} // namespace kh3ap::keyblade_options

namespace kh3ap
{
    namespace
    {
        constexpr auto keyblade_options_retry_delay = std::chrono::seconds(1);
    }
    auto schedule_runtime_keyblade_ascension_check(RuntimeState& runtime_state,
                                                   bool entering_cinematic) -> void
    {
        auto& state = runtime_state.keyblade_style_state();
        if (entering_cinematic)
        {
            state.ascension_actor_scan_requested = false;
            state.ascension_scan_after_cinematic_exit = false;
            state.ascension_check_due = {};
            return;
        }

        state.ascension_actor_scan_requested = true;
        state.ascension_scan_after_cinematic_exit = true;
        state.ascension_check_due = std::chrono::steady_clock::now() + keyblade_options_retry_delay;
    }
} // namespace kh3ap
