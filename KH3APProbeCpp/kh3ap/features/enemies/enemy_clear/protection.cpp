#include "kh3ap/features/enemies/enemy_clear/service_impl.h"
#include "kh3ap/features/enemies/enemy_clear/support.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/features/player/state/sora_pawn_catalog.h"

#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <algorithm>
#include <stdexcept>
#include <cstring>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace enemy_clear_detail;

    void RuntimeEnemyClearService::prepare_protection_handoff(UObject* outgoing)
    {
        auto* self = Impl::instance;
        if (!self || self->opening_protection_expires || self->protection_expires <= unix_ms() ||
            self->protected_sora.Get() != outgoing || self->controlled_combat_pawn() != outgoing)
        {
            return;
        }
        self->protection_swap_source = make_uobject_weak(outgoing);
        self->protection_swap_incoming = {};
        self->protection_swap_until = std::min(self->protection_expires, unix_ms() + int64_t{10000});
    }

    void RuntimeEnemyClearService::handoff_protection(const FWeakObjectPtr& outgoing, UObject* incoming)
    {
        auto* self = Impl::instance;
        if (!self || self->opening_protection_expires || self->protection_expires <= unix_ms() ||
            !self->protection_swap_until ||
            std::memcmp(&self->protection_swap_source, &outgoing, sizeof(outgoing)) != 0 ||
            !is_uobject_valid_fast(incoming) ||
            actor_world(incoming) != self->protected_world.Get())
        {
            return;
        }
        self->protection_swap_incoming = make_uobject_weak(incoming);
        if (self->controlled_combat_pawn() != incoming)
        {
            return;
        }
        const auto expires = self->protection_expires;
        const bool no_response = self->protection_no_damage_response;
        self->remove_protection();
        self->original_no_damage = boolean(incoming, L"IsConditionNoHpDamage");
        self->original_hp_floor = boolean(incoming, L"IsConditionHpLimitOne");
        self->original_no_damage_response = boolean(incoming, L"IsConditionNoDamageResponse");
        self->protected_sora = make_uobject_weak(incoming);
        self->protected_world = make_uobject_weak(actor_world(incoming));
        self->protection_expires = expires;
        self->protection_no_damage_response = no_response;
        self->set_condition(incoming, L"SetConditionNoHpDamage", L"InNoHpDamage", true);
        self->set_condition(incoming, L"SetConditionHpLimitOne", L"InHpLimitOne", true);
        if (no_response)
        {
            self->set_condition(incoming, L"SetConditionNoDamageResponse", L"InNoDamageResponse", true);
        }
        if (!boolean(incoming, L"IsConditionNoHpDamage") || !boolean(incoming, L"IsConditionHpLimitOne") ||
            (no_response && !boolean(incoming, L"IsConditionNoDamageResponse")))
        {
            throw std::runtime_error("protection_handoff_not_observed");
        }
        self->protection_reason = L"explicit_character_swap_lease_handoff";
    }

    void RuntimeEnemyClearService::Impl::set_condition(UObject* pawn, const wchar_t* function, const wchar_t* param, bool value)
    {
        Call c(pawn, function);
        if (!write_bool_function_param(c.field(param), c.params.data(), value))
            throw std::runtime_error("protection_condition_layout");
        c.invoke();
    }

    void RuntimeEnemyClearService::Impl::remove_protection()
    {
        auto* pawn = protected_sora.Get();
        if (is_uobject_valid_fast(pawn))
        {
            set_condition(pawn, L"SetConditionNoHpDamage", L"InNoHpDamage", original_no_damage);
            set_condition(pawn, L"SetConditionHpLimitOne", L"InHpLimitOne", original_hp_floor);
            if (protection_no_damage_response)
                set_condition(pawn, L"SetConditionNoDamageResponse", L"InNoDamageResponse", original_no_damage_response);
            if (boolean(pawn,L"IsConditionNoHpDamage") != original_no_damage ||
                boolean(pawn,L"IsConditionHpLimitOne") != original_hp_floor ||
                (protection_no_damage_response && boolean(pawn,L"IsConditionNoDamageResponse") != original_no_damage_response))
                throw std::runtime_error("protection_restore_not_observed");
        }
        protected_sora=nullptr; protected_world=nullptr; protection_expires=0; protection_no_damage_response=false;
        protection_swap_source = {};
        protection_swap_incoming = {};
        protection_swap_until = 0;
    }

    void RuntimeEnemyClearService::Impl::tick_protection()
    {
        if(opening_protection_expires) {
            tick_opening_protection();
            return;
        }
        if (!protection_expires) return;
        if (protection_swap_until)
        {
            if (unix_ms() >= protection_swap_until || !protected_world.Get() ||
                actor_world(controlled_combat_pawn()) != protected_world.Get())
            {
                // A temporarily absent possession is allowed only while this
                // explicit handoff is unfinished and its original world lives.
                auto* current = controlled_combat_pawn();
                if (unix_ms() >= protection_swap_until || !protected_world.Get() ||
                    (current && actor_world(current) != protected_world.Get()))
                {
                    remove_protection();
                    protection_reason = L"swap_handoff_expired_or_world_changed";
                    return;
                }
            }
            if (auto* incoming = protection_swap_incoming.Get(); incoming && controlled_combat_pawn() == incoming)
            {
                RuntimeEnemyClearService::handoff_protection(protection_swap_source, incoming);
            }
            return;
        }
        auto* pawn=protected_sora.Get();
        if (unix_ms() >= protection_expires || !is_uobject_valid_fast(pawn) ||
            controlled_combat_pawn()!=pawn || actor_world(pawn)!=protected_world.Get())
        { remove_protection(); protection_reason=L"expired_or_context_changed"; }
    }

    void RuntimeEnemyClearService::Impl::tick_opening_protection()
    {
        if(unix_ms()>=opening_protection_expires) {
            remove_protection();opening_protection_expires=0;protection_reason=L"opening_protection_expired";return;
        }
        auto* engine=opening_engine.Get();
        if(!is_uobject_valid_fast(engine)) {
            engine=UObjectGlobals::StaticFindObject<UObject*>(nullptr,nullptr,L"/Engine/Transient.TresGameEngine_0");
            opening_engine=engine;
        }
        auto* viewport=try_read_reflected_object_property_value(engine,L"GameViewport");
        auto* world=try_read_reflected_object_property_value(viewport,L"World");
        if(!is_uobject_valid_fast(world))return;
        const bool opening=world->GetPathName()==L"/Game/Levels/ew_DLC/ew_04/ew_04.ew_04";
        if(!opening) {
            if(opening_protection_seen) {
                remove_protection();opening_protection_expires=0;
                protection_reason=L"opening_finished_original_conditions_restored";
            }
            return;
        }
        opening_protection_seen=true;
        if(opening_world.Get()!=world) {opening_world=world;opening_controller=nullptr;}
        auto* controller=opening_controller.Get();
        if(!is_uobject_valid_fast(controller)) {
            if(Clock::now()<opening_discovery_at)return;
            opening_discovery_at=Clock::now()+opening_enemy_discovery_delay;
            std::vector<UObject*> controllers;
            UObjectGlobals::FindAllOf(L"TresPlayerControllerSora",controllers);
            for(auto* candidate:controllers) if(is_uobject_valid_fast(candidate) && actor_world(candidate)==world) {
                controller=candidate;opening_controller=candidate;break;
            }
        }
        auto* pawn=try_read_reflected_object_property_value(controller,L"Pawn");
        if(!is_uobject_valid_fast(pawn) || actor_world(pawn)!=world ||
            try_read_reflected_object_property_value(pawn,L"Controller")!=controller ||
            !is_uobject_valid_fast(pawn->GetClassPrivate()) ||
            !runtime_sora_pawn_catalog_matches_any(pawn->GetPathName(),pawn->GetClassPrivate()->GetName()))return;
        if(protected_sora.Get()!=pawn) {
            remove_protection();
            original_no_damage=boolean(pawn,L"IsConditionNoHpDamage");
            original_hp_floor=boolean(pawn,L"IsConditionHpLimitOne");
            protected_sora=make_uobject_weak(pawn);protected_world=make_uobject_weak(world);
        }
        protection_expires=opening_protection_expires;
        if(!boolean(pawn,L"IsConditionNoHpDamage"))set_condition(pawn,L"SetConditionNoHpDamage",L"InNoHpDamage",true);
        if(!boolean(pawn,L"IsConditionHpLimitOne"))set_condition(pawn,L"SetConditionHpLimitOne",L"InHpLimitOne",true);
        protection_reason=L"new_game_opening_native_protection";
    }

    void RuntimeEnemyClearService::Impl::accept_protection(UObject* pawn, const std::string& text)
    {
        const auto seconds=JsonFieldReader::int64_field(text,"seconds").value_or(-1);
        if (seconds<0 || seconds>maximum_center_duration_seconds) throw std::runtime_error("protection_seconds_out_of_range");
        report(L"accepted",L"protection_checkpoint");
        if (!seconds) { remove_protection(); protection_reason=L"restored"; }
        else
        {
            if (!protection_expires)
            {
                original_no_damage=boolean(pawn,L"IsConditionNoHpDamage");
                original_hp_floor=boolean(pawn,L"IsConditionHpLimitOne");
                protected_sora=make_uobject_weak(pawn); protected_world=make_uobject_weak(actor_world(pawn));
            }
            protection_expires=unix_ms()+seconds*1000;
            set_condition(pawn,L"SetConditionNoHpDamage",L"InNoHpDamage",true);
            set_condition(pawn,L"SetConditionHpLimitOne",L"InHpLimitOne",true);
            const bool no_response = JsonFieldReader::bool_field(text,"no_damage_response").value_or(false);
            if (no_response && !protection_no_damage_response)
            {
                original_no_damage_response=boolean(pawn,L"IsConditionNoDamageResponse");
                protection_no_damage_response=true;
            }
            if (protection_no_damage_response)
            {
                set_condition(pawn,L"SetConditionNoDamageResponse",L"InNoDamageResponse",no_response ? true : original_no_damage_response);
                if (boolean(pawn,L"IsConditionNoDamageResponse") != (no_response ? true : original_no_damage_response))
                    throw std::runtime_error("protection_no_damage_response_not_observed");
                protection_no_damage_response=no_response;
            }
            if (!boolean(pawn,L"IsConditionNoHpDamage") || !boolean(pawn,L"IsConditionHpLimitOne"))
                throw std::runtime_error("protection_not_observed");
            protection_reason=no_response ? L"native_no_hp_damage_hp_floor_and_no_damage_response" : L"native_no_hp_damage_and_hp_floor";
        }
        finish(L"completed",L"protection_readback"); return;
    }

}
