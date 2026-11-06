#include "kh02_collapse_presentation.h"
#include "kh02_cutscene_reflection.h"
#include "kh02_matinee_binding.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include <cmath>
#include <algorithm>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02cutscene_detail;
    using Json = nlohmann::json;

    namespace
    {
        constexpr float native_scene_completion_tolerance_seconds = 0.02f;
        constexpr float completed_fade_tolerance = .001f;
        constexpr float collapse_fade_cancel_seconds = .3f;
        constexpr int collapse_fade_layer = 0;
        constexpr auto view_cleanup_timeout = std::chrono::seconds(5);

        auto source_fade(UObject* pawn) -> Json
        {
            auto* library = kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary");
            ReflectedCall amount(library, L"BPTresGetFadeAmount");
            amount.object(L"WorldContextObject", pawn);
            amount.integer(L"InLayer", collapse_fade_layer);
            amount.run();
            auto* result = amount.param(L"ReturnValue");
            if (!result || result->GetClass().GetName() != L"FloatProperty" ||
                result->GetElementSize() != sizeof(float))
            {
                throw std::runtime_error("kh02_collapse_fade_amount_layout");
            }
            const float value = *result->ContainerPtrToValuePtr<float>(amount.parameters.data());
            if (!std::isfinite(value) || value < 0.f || value > 1.f + completed_fade_tolerance)
            {
                throw std::runtime_error("kh02_collapse_fade_amount_range");
            }
            ReflectedCall changing(library, L"BPTresIsFadeChange");
            changing.object(L"WorldContextObject", pawn);
            changing.integer(L"InLayer", collapse_fade_layer);
            changing.run();
            return {{"layer", collapse_fade_layer}, {"amount", value}, {"changing", changing.boolean_result()}};
        }
    } // namespace

    void Kh02CollapsePresentation::finish_view(UObject* pawn, UObject* world)
    {
        if (!view_cleanup_pending || !collapse_observed || !collapse_handoff_completed ||
            is_uobject_valid_fast(collapse_script.Get()))
        {
            return;
        }
        if (!is_uobject_valid_fast(world) || collapse_world.Get() != world || !is_uobject_valid_fast(pawn) ||
            kh02_actor_world(pawn) != world)
        {
            return;
        }
        // Stop alone is too early: native Skip still owns its fade and source
        // continuations. Wait for script retirement and the actual playable
        // handoff before inspecting or releasing DW012's foreground fade.
        if (!runtime_sora_control_ready(pawn))
        {
            return;
        }
        auto* library = kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        ReflectedCall closed(library, L"TresUI_IsClosedLoadingScreen");
        closed.run();
        ReflectedCall paused(kh02_require_loaded_object(L"/Script/Engine.Default__GameplayStatics"), L"IsGamePaused");
        paused.object(L"WorldContextObject", pawn);
        paused.run();
        if (!closed.boolean_result() || paused.boolean_result())
        {
            return;
        }
        const auto fade = source_fade(pawn);
        if (!view_cleanup_started)
        {
            view_cleanup_started = true;
            view_cleanup_since = std::chrono::steady_clock::now();
            view_cleanup_proof = {{"before", fade},
                                  {"source_script_retired", true},
                                  {"control_ready", true},
                                  {"requested", false},
                                  {"verified", false}};
            if (fade["amount"].get<float>() > completed_fade_tolerance || fade["changing"].get<bool>())
            {
                ReflectedCall cancel(library, L"BPTresCancelFade");
                cancel.object(L"WorldContextObject", pawn);
                cancel.integer(L"InLayer", collapse_fade_layer);
                cancel.number(L"InFadeTime", collapse_fade_cancel_seconds);
                cancel.boolean(L"InApplyGameSpeed", false);
                cancel.run();
                view_cleanup_proof["requested"] = true;
            }
        }
        view_cleanup_proof["after"] = fade;
        if (fade["amount"].get<float>() <= completed_fade_tolerance && !fade["changing"].get<bool>())
        {
            view_cleanup_proof["verified"] = true;
            view_cleanup_pending = false;
        }
        else if (std::chrono::steady_clock::now() - view_cleanup_since > view_cleanup_timeout)
        {
            throw std::runtime_error("kh02_collapse_fade_cleanup_timeout");
        }
    }

    void Kh02CollapsePresentation::tick(UObject* pawn, UObject* world, UObject* event_aqua,
                                        std::string& presentation_error)
    {
        if (auto* collapse = collapse_matinee.Get(); is_uobject_valid_fast(collapse))
        {
            const bool playing = flag(collapse, L"bIsPlaying");
            collapse_observed |= playing;
            collapse_maximum = std::max(collapse_maximum, real(collapse, L"InterpPosition"));
            if (playing && !collapse_binding_checked)
            {
                auto* target = kh02_runtime_matinee_group_actor(collapse, L"TresInterpGroup_0");
                collapse_binding_verified = is_uobject_valid_fast(target) && target == event_aqua;
                collapse_binding_checked = true;
                collapse_binding_proof = {{"configured_actor", path(event_aqua)},
                                          {"runtime_actor", path(target)},
                                          {"verified", collapse_binding_verified}};
            }
            if (playing && collapse_binding_verified && is_uobject_valid_fast(pawn) &&
                kh02_actor_world(pawn) == kh02_actor_world(collapse) && pawn != event_aqua && !flag(pawn, L"bHidden"))
            {
                if (!is_uobject_valid_fast(collapse_hidden_hero.Get()))
                {
                    collapse_hidden_hero = make_uobject_weak(pawn);
                    collapse_hero_was_hidden = false;
                    auto* movement = try_read_reflected_object_property_value(pawn, L"MyMovement");
                    if (!is_uobject_valid_fast(movement))
                    {
                        throw std::runtime_error("kh02_collapse_movement_missing");
                    }
                    ReflectedCall enabled(movement, L"IsComponentTickEnabled");
                    enabled.run();
                    collapse_movement_was_enabled = enabled.boolean_result();
                    collapse_movement = make_uobject_weak(movement);
                    ReflectedCall stop(movement, L"StopMovementImmediately");
                    stop.run();
                    ReflectedCall hold(movement, L"SetComponentTickEnabled");
                    hold.boolean(L"bEnabled", false);
                    hold.run();
                    collapse_handoff_proof["movement_held"] = true;
                }
                ReflectedCall hide(pawn, L"SetActorHiddenInGame");
                hide.boolean(L"bNewHidden", true);
                hide.run();
            }
        }
        const bool stopped =
            !is_uobject_valid_fast(collapse_matinee.Get()) || !flag(collapse_matinee.Get(), L"bIsPlaying");
        if (stopped && collapse_observed && !collapse_handoff_completed)
        {
            collapse_handoff_completed = true;
            try
            {
                auto* scene = collapse_matinee.Get();
                auto* data = is_uobject_valid_fast(scene)
                                 ? try_read_reflected_object_property_value(scene, L"MatineeData")
                                 : nullptr;
                const float length = is_uobject_valid_fast(data) ? real(data, L"InterpLength") : 0.f;
                const bool natural_end =
                    length > 0.f && collapse_maximum >= length - native_scene_completion_tolerance_seconds;
                collapse_handoff_proof["natural_end"] = natural_end;
                collapse_handoff_proof["length"] = length;
                if (natural_end)
                {
                    auto* event_pawn = event_aqua;
                    if (!collapse_binding_verified || !is_uobject_valid_fast(pawn) ||
                        !is_uobject_valid_fast(event_pawn) || !is_uobject_valid_fast(world) ||
                        world != kh02_actor_world(pawn) || world != kh02_actor_world(event_pawn) ||
                        world->GetPathName() != L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown" ||
                        collapse_hidden_hero.Get() != pawn)
                    {
                        throw std::runtime_error("kh02_collapse_finish_context");
                    }
                    // Both pawns use the same foot-origin TresRootComponent.
                    // Transfer the real final event transform before retiring
                    // its presentation or releasing the playable movement.
                    ReflectedCall location(event_pawn, L"K2_GetActorLocation");
                    location.run();
                    const auto position = location.result_vector();
                    ReflectedCall rotation(event_pawn, L"K2_GetActorRotation");
                    rotation.run();
                    const auto facing = rotation.result_vector();
                    for (size_t axis = 0; axis < 3; ++axis)
                    {
                        if (!std::isfinite(position[axis]) || !std::isfinite(facing[axis]))
                        {
                            throw std::runtime_error("kh02_collapse_finish_transform");
                        }
                    }
                    ReflectedCall move(pawn, L"K2_SetActorLocationAndRotation");
                    move.vector(L"NewLocation", position[0], position[1], position[2]);
                    move.vector(L"NewRotation", facing[0], facing[1], facing[2]);
                    move.boolean(L"bSweep", false);
                    move.boolean(L"bTeleport", true);
                    move.run();
                    if (!move.boolean_result())
                    {
                        throw std::runtime_error("kh02_collapse_finish_move");
                    }
                    collapse_handoff_proof["hero"] = path(pawn);
                    collapse_handoff_proof["event_actor"] = path(event_pawn);
                    collapse_handoff_proof["position"] = position;
                    collapse_handoff_proof["rotation"] = facing;
                    collapse_handoff_proof["transferred"] = true;
                }
            }
            catch (const std::exception& error)
            {
                presentation_error = error.what();
                collapse_handoff_proof["error"] = error.what();
            }
        }
        if (stopped && is_uobject_valid_fast(collapse_movement.Get()))
        {
            ReflectedCall stop(collapse_movement.Get(), L"StopMovementImmediately");
            stop.run();
            ReflectedCall restore(collapse_movement.Get(), L"SetComponentTickEnabled");
            restore.boolean(L"bEnabled", collapse_movement_was_enabled);
            restore.run();
            collapse_movement = {};
            collapse_handoff_proof["movement_released"] = true;
        }
        if (stopped && is_uobject_valid_fast(collapse_hidden_hero.Get()))
        {
            ReflectedCall restore(collapse_hidden_hero.Get(), L"SetActorHiddenInGame");
            restore.boolean(L"bNewHidden", collapse_hero_was_hidden);
            restore.run();
            collapse_hidden_hero = {};
        }
        finish_view(pawn, world);
    }

    void Kh02CollapsePresentation::bind(UObject* matinee, UObject* script)
    {
        collapse_binding_checked = false;
        collapse_binding_verified = false;
        collapse_binding_proof = Json::object();
        collapse_matinee = make_uobject_weak(matinee);
        collapse_script = make_uobject_weak(script);
        collapse_world = make_uobject_weak(kh02_actor_world(matinee));
        view_cleanup_pending = true;
        view_cleanup_started = false;
        view_cleanup_proof = Json::object();
    }

    void Kh02CollapsePresentation::restore() noexcept
    {
        restore_resource(
            [&]
            {
                if (auto* movement = collapse_movement.Get(); is_uobject_valid_fast(movement))
                {
                    ReflectedCall stop(movement, L"StopMovementImmediately");
                    stop.run();
                    ReflectedCall release(movement, L"SetComponentTickEnabled");
                    release.boolean(L"bEnabled", collapse_movement_was_enabled);
                    release.run();
                }
            });
        restore_resource(
            [&]
            {
                if (auto* hero = collapse_hidden_hero.Get(); is_uobject_valid_fast(hero))
                {
                    ReflectedCall release(hero, L"SetActorHiddenInGame");
                    release.boolean(L"bNewHidden", collapse_hero_was_hidden);
                    release.run();
                }
            });
    }

    void Kh02CollapsePresentation::reset()
    {
        collapse_world = {};
        view_cleanup_pending = false;
        view_cleanup_started = false;
        view_cleanup_proof = Json::object();
        collapse_matinee = {};
        collapse_script = {};
        collapse_hidden_hero = {};
        collapse_movement = {};
        collapse_observed = false;
        collapse_maximum = 0.f;
        collapse_handoff_completed = false;
        collapse_binding_checked = false;
        collapse_binding_verified = false;
        collapse_binding_proof = Json::object();
        collapse_handoff_proof = Json::object();
    }

    bool Kh02CollapsePresentation::needs_frame() const
    {
        return is_uobject_valid_fast(collapse_movement.Get()) || is_uobject_valid_fast(collapse_hidden_hero.Get()) ||
               (view_cleanup_pending && view_cleanup_started);
    }

    Json Kh02CollapsePresentation::snapshot() const
    {
        Json result = {{"actor", path(collapse_matinee.Get())}, {"script", path(collapse_script.Get())},
                       {"observed", collapse_observed},         {"maximum", collapse_maximum},
                       {"binding", collapse_binding_proof},     {"handoff", collapse_handoff_proof},
                       {"view_cleanup", view_cleanup_proof}};
        if (auto* matinee = collapse_matinee.Get(); is_uobject_valid_fast(matinee))
        {
            result.update({{"playing", flag(matinee, L"bIsPlaying")}, {"position", real(matinee, L"InterpPosition")}});
        }
        return result;
    }
} // namespace kh3ap
