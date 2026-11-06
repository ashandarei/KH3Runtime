#include "kh02_arrival_presentation.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/world/loading_runtime.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <cstring>
#include <stdexcept>
#include <tuple>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;
        constexpr int arrival_cover_fade_layer = 1; // PRE_FG, above the source FG fade.
        constexpr int loading_background_black = 0;
        constexpr float arrival_reveal_seconds = 0.3f;

        struct ReflectedCall : Kh02ReflectedCall
        {
            ReflectedCall(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "kh02_map_session", reflected_parameter_limit_bytes)
            {
            }
        };

        auto no_hp_damage_override(UObject* pawn) -> bool
        {
            auto* property = is_uobject_valid_fast(pawn) ? pawn->GetPropertyByNameInChain(L"m_bNoHpDamage") : nullptr;
            if (!property || !property->IsA<FBoolProperty>())
            {
                throw std::runtime_error("kh02_arrival_damage_condition_layout");
            }
            return static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(pawn);
        }
    } // namespace

    void Kh02ArrivalPresentation::bind_context(UObject* context)
    {
        cover_context = make_uobject_weak(context);
    }

    void Kh02ArrivalPresentation::cover(UObject* context, float amount)
    {
        // The stock manual loading movie already covers the destination. PRE_FG
        // is composited over that movie too, so keeping our black cover there
        // would hide its artwork and Continue prompt.
        if (amount > 0)
        {
            auto* instance = try_read_reflected_object_property_value(kh02_current_viewport(), L"GameInstance");
            auto* manager = try_read_reflected_object_property_value(instance, L"m_UIManager");
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            if (loading_runtime::supported_image(base))
            {
                const auto screen = loading_runtime::loading_screen_snapshot(base, manager);
                if (screen.known && screen.open && !screen.closed && screen.mode == 1)
                {
                    amount = 0;
                }
            }
        }
        ReflectedCall fade(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                           L"BPTresSetFadeColorAndAmount");
        fade.object(L"WorldContextObject", context);
        fade.integer(L"InLayer", arrival_cover_fade_layer);
        fade.number(L"InAmount", amount);
        auto* color = fade.param(L"InFadeColor");
        const float black[]{0, 0, 0, 1};
        if (!color || color->GetElementSize() != sizeof(black) || !color->IsA<FStructProperty>())
        {
            throw std::runtime_error("kh02_arrival_color_layout");
        }
        std::memcpy(color->ContainerPtrToValuePtr<void>(fade.parameters.data()), black, sizeof(black));
        fade.run();
    }

    void Kh02ArrivalPresentation::release_input()
    {
        release_damage_protection();
        if (auto* controller_object = controller.Get(); is_uobject_valid_fast(controller_object))
        {
            claims.release(
                [&](Kh02ArrivalInput input)
                {
                    const bool movement = input == Kh02ArrivalInput::Movement;
                    ReflectedCall release(controller_object, movement ? L"SetIgnoreMoveInput" : L"SetIgnoreLookInput");
                    release.boolean(movement ? L"bNewMoveInput" : L"bNewLookInput", false);
                    release.run();
                });
        }
        controller = {};
        claims = {};
    }

    void Kh02ArrivalPresentation::reveal(UObject* context)
    {
        ReflectedCall fade(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                           L"BPTresCancelFade");
        fade.object(L"WorldContextObject", context);
        fade.integer(L"InLayer", arrival_cover_fade_layer);
        fade.number(L"InFadeTime", arrival_reveal_seconds);
        fade.boolean(L"InApplyGameSpeed", false);
        fade.run();
    }

    void Kh02ArrivalPresentation::reveal_source(UObject* context)
    {
        ReflectedCall fade(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                           L"BPTresCancelFade");
        fade.object(L"WorldContextObject", context);
        fade.integer(L"InLayer", 0);
        fade.number(L"InFadeTime", arrival_reveal_seconds);
        fade.boolean(L"InApplyGameSpeed", false);
        fade.run();
    }

    void Kh02ArrivalPresentation::hold_input(UObject* pawn)
    {
        auto* controller_object = try_read_reflected_object_property_value(pawn, L"Controller");
        if (is_uobject_valid_fast(controller_object) &&
            try_read_reflected_object_property_value(controller_object, L"Pawn") == pawn)
        {
            if (controller.Get() != controller_object)
            {
                release_input();
                controller = make_uobject_weak(controller_object);
            }
            for (auto [method, getter, parameter, claim] :
                 {std::tuple{L"SetIgnoreMoveInput", L"IsMoveInputIgnored", L"bNewMoveInput", &claims.movement},
                  std::tuple{L"SetIgnoreLookInput", L"IsLookInputIgnored", L"bNewLookInput", &claims.look}})
            {
                ReflectedCall ignored(controller_object, getter);
                ignored.run();
                if (!*claim || !ignored.boolean_result())
                {
                    ReflectedCall hold(controller_object, method);
                    hold.boolean(parameter, true);
                    hold.run();
                    *claim = true;
                }
            }
            if (protected_pawn.Get() != pawn)
            {
                release_damage_protection();
                original_no_hp_damage = no_hp_damage_override(pawn);
                protected_pawn = make_uobject_weak(pawn);
                protected_world = make_uobject_weak(kh02_actor_world(pawn));
                // The general IsConditionNoHpDamage query also includes
                // cinematic/state protection. Own only the setter's reflected
                // override, leaving those independent protections untouched.
                damage_claimed = protected_pawn.Get() == pawn && !original_no_hp_damage;
                damage_restoration_verified = !damage_claimed;
            }
            if (damage_claimed)
            {
                ReflectedCall protect(pawn, L"SetConditionNoHpDamage");
                protect.boolean(L"InNoHpDamage", true);
                protect.run();
            }
        }
    }

    void Kh02ArrivalPresentation::release_damage_protection() noexcept
    {
        auto* pawn = protected_pawn.Get();
        auto* world = protected_world.Get();
        const bool claimed = damage_claimed;
        protected_pawn = {};
        protected_world = {};
        damage_claimed = false;
        if (!claimed)
        {
            return;
        }
        try
        {
            // Travel can retain the outgoing UObject after its gameplay state
            // has retired. Do not call pawn condition methods across worlds.
            if (!is_uobject_valid_fast(pawn) || !is_uobject_valid_fast(world) || world != kh02_current_world() ||
                kh02_actor_world(pawn) != world)
            {
                damage_restoration_verified = true;
                return;
            }
            ReflectedCall restore(pawn, L"SetConditionNoHpDamage");
            restore.boolean(L"InNoHpDamage", original_no_hp_damage);
            restore.run();
            damage_restoration_verified = no_hp_damage_override(pawn) == original_no_hp_damage;
        }
        catch (...)
        {
            // Cleanup also runs from map/session destructors. Never unwind
            // those paths or prevent the independent input claims releasing.
            damage_restoration_verified = false;
        }
    }

    auto Kh02ArrivalPresentation::changing(UObject* context) -> bool
    {
        ReflectedCall changing(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                               L"BPTresIsFadeChange");
        changing.object(L"WorldContextObject", context);
        changing.integer(L"InLayer", arrival_cover_fade_layer);
        changing.run();
        return changing.boolean_result();
    }

    void Kh02ArrivalPresentation::reset_bindings()
    {
        release_damage_protection();
        cover_context = {};
        controller = {};
        claims = {};
    }

    auto Kh02ArrivalPresentation::loading_closed() -> bool
    {
        ReflectedCall query(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                            L"TresUI_IsClosedLoadingScreen");
        query.run();
        return query.boolean_result();
    }

    void Kh02ArrivalPresentation::open_loading()
    {
        ReflectedCall open(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                           L"TresUI_OpenLoadingScreen");
        open.integer(L"BgColor", loading_background_black);
        open.run();
    }

    void Kh02ArrivalPresentation::close_loading()
    {
        ReflectedCall close(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                            L"TresUI_CloseLoadingScreen");
        close.run();
    }
} // namespace kh3ap
