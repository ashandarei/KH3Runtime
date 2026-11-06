#include "kh02_shooting_star.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#define NOMINMAX
#include <Windows.h>
#include <stdexcept>
#include <utility>

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;

    namespace
    {
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;
        constexpr int actor_scan_limit = 16'384;
        constexpr uint64_t hero_possession_settle_ms = 500;
        constexpr std::size_t native_vector_bytes = 3 * sizeof(float);
        constexpr std::size_t native_name_bytes = sizeof(FName);
        static_assert(native_name_bytes == sizeof(uint64_t));

        struct ReflectedCall : Kh02ReflectedCall
        {
            ReflectedCall(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "kh02_story", reflected_parameter_limit_bytes)
            {
            }
        };

        auto flag(UObject* object, const wchar_t* name) -> bool
        {
            auto* property = is_uobject_valid_fast(object) ? object->GetPropertyByNameInChain(name) : nullptr;
            if (!property || !property->IsA<FBoolProperty>())
            {
                throw std::runtime_error("kh02_story_bool");
            }
            return static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(object);
        }

        auto path(UObject* object) -> std::string
        {
            return is_uobject_valid_fast(object) ? to_utf8(object->GetPathName()) : "";
        }
    } // namespace

    void Kh02ShootingStar::reconcile(UObject* pawn, bool challenge_complete)
    {
        if (hero.Get() != pawn)
        {
            hero = make_uobject_weak(pawn);
            hero_arrived_at = GetTickCount64();
            hero_checked = false;
            handoff = Json::object();
        }
        if (hero_checked || GetTickCount64() - hero_arrived_at < hero_possession_settle_ms || challenge_complete)
        {
            return;
        }
        auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
        if (!is_uobject_valid_fast(controller) || try_read_reflected_object_property_value(controller, L"Pawn") != pawn)
        {
            return;
        }
        UObject* trigger = nullptr;
        UObject* star = nullptr;
        for (auto* actor : find_live_actors_of_class(pawn, AActor::StaticClass(), actor_scan_limit))
        {
            if (!actor->GetPathName().starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_challenge."))
            {
                continue;
            }
            if (actor->GetName() == L"lv_TriggerBox_C_1")
            {
                trigger = actor;
            }
            if (actor->GetName() == L"g_dw_ShootingStar_C_39")
            {
                star = actor;
            }
        }
        if (!trigger || !star)
        {
            throw std::runtime_error("kh02_star_handoff_actors");
        }
        ReflectedCall overlap(trigger, L"IsOverlappingActor");
        overlap.object(L"Other", pawn);
        overlap.run();
        handoff = {{"hero", path(pawn)}, {"overlapping", overlap.boolean_result()}, {"restarted", false}};
        if (overlap.boolean_result())
        {
            // Replacement pawns can enter before possession, then the old
            // pawn's EndOverlap stops the gimmick. Reconcile the real overlap
            // once possession settles, using the trigger's original binding.
            auto* property = trigger->GetPropertyByNameInChain(L"BeginOverlap_StartGimmicks");

            struct TriggerBindings
            {
                UObject** data;
                int32_t count;
                int32_t capacity;
            };

            if (!property || !property->IsA<FArrayProperty>() || property->GetElementSize() != sizeof(TriggerBindings))
            {
                throw std::runtime_error("kh02_star_handoff_binding");
            }
            const auto* array = property->ContainerPtrToValuePtr<TriggerBindings>(trigger);
            if (!array->data || array->count != 1 || array->capacity < 1 || array->data[0] != star)
            {
                throw std::runtime_error("kh02_star_handoff_binding_actor");
            }
            ReflectedCall started(star, L"IsStartedGimmick");
            started.run();
            if (!*started.param(L"IsStarted")->ContainerPtrToValuePtr<bool>(started.parameters.data()) &&
                !flag(star, L"temp_IsComplate"))
            {
                auto* controller_id = trigger->GetPropertyByNameInChain(L"ControllerID");
                if (!controller_id || controller_id->GetClass().GetName() != L"NameProperty" ||
                    controller_id->GetElementSize() != native_name_bytes)
                {
                    throw std::runtime_error("kh02_star_handoff_controller");
                }
                ReflectedCall start(star, L"StartGimmick");
                start.boolean(L"ActivateGimmick", true);
                start.boolean(L"Reset", true);
                start.boolean(L"PlayAnimation", true);
                start.name(L"ControllerId", controller_id->ContainerPtrToValuePtr<FName>(trigger)->ToString().c_str());
                start.run();
                handoff["restarted"] = true;
            }
        }
        hero_checked = true;
    }

    void Kh02ShootingStar::tick(UObject* pawn, bool challenge_complete)
    {
        try
        {
            reconcile(pawn, challenge_complete);
        }
        catch (const std::exception& error)
        {
            handoff["error"] = error.what();
            hero_checked = true;
        }
    }

    void Kh02ShootingStar::clear_world()
    {
        hero = {};
        hero_arrived_at = 0;
        hero_checked = false;
    }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto Kh02ShootingStar::snapshot(UObject* pawn, int challenge_state) const -> Json
    {
        Json result{{"challenge", challenge_state}, {"actors", Json::array()}};
        result["hero_handoff"] = handoff;
        for (auto* actor : find_live_actors_of_class(pawn, AActor::StaticClass(), actor_scan_limit))
        {
            const auto name = actor->GetPathName();
            if (!name.starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_challenge.") &&
                !name.starts_with(L"/Game/Zevels/dw/dw_02/umap/dw_02_camera_ms201."))
            {
                continue;
            }
            const auto short_name = actor->GetName();
            if (short_name != L"g_dw_ShootingStar_C_39" && short_name != L"TargetPoint_ShootingStar" &&
                !short_name.starts_with(L"TresFieldVoiceVolume") &&
                actor->GetClassPrivate()->GetName() != L"dw_02_camera_ms201_C")
            {
                continue;
            }
            Json row{{"actor", path(actor)}};
            ReflectedCall location(actor, L"K2_GetActorLocation");
            location.run();
            const auto* location_values =
                location.param(L"ReturnValue")->ContainerPtrToValuePtr<float>(location.parameters.data());
            row["location"] = {location_values[0], location_values[1], location_values[2]};
            if (short_name.starts_with(L"TresFieldVoiceVolume"))
            {
                ReflectedCall overlap(actor, L"IsOverlappingActor");
                overlap.object(L"Other", pawn);
                overlap.run();
                row["hero_overlapping"] = overlap.boolean_result();
                row["look_conditions"] = Json::array();
                auto* blueprints = actor->GetPropertyByNameInChain(L"m_BluePrints");

                struct Conditions
                {
                    UObject** data;
                    int32_t count;
                    int32_t capacity;
                };

                if (blueprints && blueprints->IsA<FArrayProperty>() &&
                    blueprints->GetElementSize() == sizeof(Conditions))
                {
                    const auto* conditions = blueprints->ContainerPtrToValuePtr<Conditions>(actor);
                    constexpr int maximum_look_conditions = 8;
                    if (conditions->count >= 0 && conditions->count <= maximum_look_conditions &&
                        conditions->capacity >= conditions->count && (!conditions->count || conditions->data))
                    {
                        for (int index = 0; index < conditions->count; ++index)
                        {
                            auto* condition = conditions->data[index];
                            if (!is_uobject_valid_fast(condition))
                            {
                                continue;
                            }
                            Json state{{"object", path(condition)}};
                            ReflectedCall basic(condition, L"IsBasicCondtion");
                            basic.run();
                            state["basic_condition"] = basic.boolean_result();
                            auto* target = try_read_reflected_object_property_value(condition, L"Target");
                            auto* angle = condition->GetPropertyByNameInChain(L"Angle");
                            auto* in_rate = condition->GetPropertyByNameInChain(L"InRate");
                            constexpr std::size_t vector2_bytes = 2 * sizeof(float);
                            if (is_uobject_valid_fast(target) && angle &&
                                angle->GetClass().GetName() == L"FloatProperty" &&
                                angle->GetElementSize() == sizeof(float) && in_rate &&
                                in_rate->GetClass().GetName() == L"StructProperty" &&
                                in_rate->GetElementSize() == vector2_bytes)
                            {
                                const auto* rates = in_rate->ContainerPtrToValuePtr<float>(condition);
                                ReflectedCall look(condition, L"IsLook2D");
                                look.object(L"Target", target);
                                look.number(L"Angle", *angle->ContainerPtrToValuePtr<float>(condition));
                                look.vector2(L"InRate", rates[0], rates[1]);
                                look.run();
                                state["looking_at_target"] = look.boolean_result(L"ret");
                            }
                            row["look_conditions"].push_back(std::move(state));
                        }
                    }
                }
                ReflectedCall bounds(actor, L"GetActorBounds");
                bounds.boolean(L"bOnlyCollidingComponents", false);
                bounds.run();
                for (const auto* key : {L"Origin", L"BoxExtent"})
                {
                    auto* bounds_property = bounds.param(key);
                    if (bounds_property && bounds_property->GetElementSize() == native_vector_bytes)
                    {
                        const auto* bounds_values =
                            bounds_property->ContainerPtrToValuePtr<float>(bounds.parameters.data());
                        row[to_utf8(key)] = {bounds_values[0], bounds_values[1], bounds_values[2]};
                    }
                }
            }
            if (short_name == L"g_dw_ShootingStar_C_39")
            {
                row["initialized"] = flag(actor, L"temp_IsInitialize");
                row["observed"] = flag(actor, L"temp_IsComplate");
                row["watch_window"] = flag(actor, L"temp_OK");
                ReflectedCall started(actor, L"IsStartedGimmick");
                started.run();
                row["started"] = *started.param(L"IsStarted")->ContainerPtrToValuePtr<bool>(started.parameters.data());
            }
            if (actor->GetClassPrivate()->GetName() == L"dw_02_camera_ms201_C")
            {
                row["camera_active"] = flag(actor, L"On");
            }
            result["actors"].push_back(std::move(row));
        }
        return result;
    }
#endif
} // namespace kh3ap
