#include "kh3ap/features/player/player_action_commands.h"
#include "kh3ap/features/enemies/enemy_clear/service_impl.h"
#include "kh3ap/features/enemies/enemy_clear/support.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/features/treasure/chest_persistence_service.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/core/objects/actor_discovery.h"

#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <algorithm>
#include <stdexcept>
#include <cmath>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace enemy_clear_detail;

    UObject* RuntimeEnemyClearService::Impl::resolve_chest(UObject* pawn,const std::wstring& requested_actor,const std::wstring& requested_treasure)
    {
        if (requested_actor.empty() || requested_treasure.empty()) throw std::runtime_error("missing_chest_identity");
        auto* klass=UObjectGlobals::StaticFindObject<UClass*>(nullptr,nullptr,L"/Script/TresGame.TresTreasureBox");
        UObject* chest=nullptr;
        for (auto* actor:actors_of_class(pawn,klass))
            if(is_uobject_valid_fast(actor) && actor->GetPathName()==requested_actor)
            { if(chest) throw std::runtime_error("ambiguous_chest"); chest=actor; }
        if(!chest || property_name(chest,L"m_TreasureID")!=requested_treasure)
            throw std::runtime_error("chest_identity_changed");
        return chest;
    }

    void RuntimeEnemyClearService::Impl::break_container(UObject* pawn,const std::wstring& requested_actor,const std::wstring& requested_treasure)
    {
        auto* chest=resolve_chest(pawn,requested_actor,requested_treasure);
        if(std::any_of(chest_gates.begin(),chest_gates.end(),[&](auto& e){return e.second.gate_enabled&&e.second.gate_chest.Get()==chest&&!keyblade_inventory(pawn,e.second.keyblade);}))
            throw std::runtime_error("chest_requires_keyblade_use_check_interaction");
        if(property_bool(chest,L"bHidden",false)||!property_bool(chest,L"m_bIsActive",false)||
            boolean(chest,L"GetActorEnableCollision"))throw std::runtime_error("chest_not_waiting_for_container");
        auto* klass=UObjectGlobals::StaticFindObject<UClass*>(nullptr,nullptr,
            L"/Game/Blueprints/Gimmick/g_TresDiveGimBase.g_TresDiveGimBase_C");
        UObject* container=nullptr;
        for(auto* actor:actors_of_class(pawn,klass))
            if(is_uobject_valid_fast(actor)&&try_read_reflected_object_property_value(actor,L"TreasureBox")==chest)
            { if(container)throw std::runtime_error("ambiguous_chest_container");container=actor; }
        if(!container||boolean(container,L"BP_IsDestroyedGimmickForPlan"))throw std::runtime_error("unbroken_dive_container_required");
        const auto pos=position(container);double distance=0;
        for(size_t i=0;i<3;++i)distance+=std::pow(pos[i]-position_before[i],2.0);
        if(distance>container_interaction_radius*container_interaction_radius)throw std::runtime_error("container_out_of_range");
        container_ref=container;container_chest_ref=chest;container_name=container->GetPathName();
        interaction_actor=requested_actor;interaction_treasure=requested_treasure;
        Call destroy(container,L"RequestDestroyGimmickForPlan");
        if(!write_bool_function_param(destroy.field(L"PlayAnimation"),destroy.params.data(),true))
            throw std::runtime_error("unsupported_container_break_layout");
        report(L"accepted",L"container_break_checkpoint");break_dispatched=true;
        // The owning Blueprint handles collision, saved state and reveal.
        // This is the authorized container break, not AActor::Destroy or
        // a fabricated chest/item/check callback.
        destroy.invoke(false);
    }

    void RuntimeEnemyClearService::Impl::open_chest(UObject* pawn, const std::wstring& requested_actor, const std::wstring& requested_treasure)
    {
        auto* chest=resolve_chest(pawn,requested_actor,requested_treasure);
        if(std::any_of(chest_gates.begin(),chest_gates.end(),[&](auto& e){return e.second.gate_enabled&&e.second.gate_chest.Get()==chest&&!keyblade_inventory(pawn,e.second.keyblade);}))
            throw std::runtime_error("chest_requires_keyblade_use_check_interaction");
        if(property_bool(chest,L"bHidden",false) || !property_bool(chest,L"m_bIsActive",true) ||
                !boolean(chest,L"GetActorEnableCollision")) throw std::runtime_error("chest_inactive");
        const auto pos=position(chest); double distance=0;
        for(size_t i=0;i<3;++i) distance+=std::pow(pos[i]-position_before[i],2.0);
        if(distance>chest_interaction_radius*chest_interaction_radius) throw std::runtime_error("chest_out_of_interaction_range");
        auto* reactor=try_read_reflected_object_property_value(chest,L"MyReactor");
        auto* controller=try_read_reflected_object_property_value(pawn,L"Controller");
        if(!is_uobject_valid_fast(reactor) || !is_uobject_valid_fast(controller)) throw std::runtime_error("missing_chest_reactor");
        if(!boolean(reactor,L"BP_IsGimmickComponentActive"))throw std::runtime_error("chest_reactor_inactive");
        Call set(controller,L"BP_NotifySetActionCommand1");
        if(!write_integral_function_param(set.field(L"inCommand"),set.params.data(),player_action_command_value(PlayerActionCommand::OPEN)) ||
                !write_float_function_param(set.field(L"inTimer"),set.params.data(),1.0) ||
                !write_object_function_param(set.function,set.params.data(),L"inActor",chest) ||
                !write_object_function_param(set.function,set.params.data(),L"InComponent",reactor))
            throw std::runtime_error("unsupported_chest_action_layout");
        interaction_actor=requested_actor; interaction_treasure=requested_treasure;
        report(L"accepted",L"chest_interaction_checkpoint");
        set.invoke();
        press_action(controller,L"interact");
        interaction_dispatched=true;
    }

    void RuntimeEnemyClearService::Impl::accept_mom_barrier(UObject* pawn, const std::string& text)
    {mom_snapshot=mom_barrier.command(pawn,text);finish(L"completed",L"mom_barrier_snapshot");return;}

    void RuntimeEnemyClearService::Impl::accept_repair_chest_record(UObject* pawn, const std::string& text)
    {
        auto* chest=resolve_chest(pawn,JsonFieldReader::string_field(text,"actor").value_or(L""),
            JsonFieldReader::string_field(text,"treasure_id").value_or(L""));
        Call tag(chest,L"ActorHasTag");
        if(!write_name_or_string_function_param(tag.field(L"Tag"),tag.params.data(),L"AP_ChestOpenedPresentation"))
            throw std::runtime_error("chest_record_tag_layout");
        tag.invoke();
        if(!static_cast<FBoolProperty*>(tag.field(L"ReturnValue"))->GetPropertyValueInContainer(tag.params.data()))
            throw std::runtime_error("chest_record_requires_persisted_open_presentation");
        if(is_uobject_valid_fast(try_read_reflected_object_property_value(chest,L"m_pOpenner")))
            throw std::runtime_error("chest_record_opening_in_progress");
        const bool before=reconcile_chest_record(chest,true);
        inspect_scene(pawn);
        finish(L"completed",before?L"chest_record_already_set":L"chest_record_repaired");return;
    }

    void RuntimeEnemyClearService::Impl::accept_open_chest(UObject* pawn, const std::string& text)
    {
        open_chest(pawn,JsonFieldReader::string_field(text,"actor").value_or(L""),
                   JsonFieldReader::string_field(text,"treasure_id").value_or(L""));
        waiting=true; return;
    }

    void RuntimeEnemyClearService::Impl::accept_break_emblem_cover(UObject* pawn, const std::string& text)
    {
        // Native ID 18 is under this shipped breakable display. Use
        // its owning destruction/reveal event, never its photo flag.
        if(world_name.find(L"/Game/Levels/ts/ts_02/")==std::wstring::npos)
            throw std::runtime_error("emblem_cover_requires_galaxy_toys");
        auto* klass=find_loaded_runtime_asset_class(L"/Game/Blueprints/Gimmick/ts/g_ts_floortable/g_ts_floortable_01.g_ts_floortable_01_C");
        if(!is_uobject_valid_fast(klass))throw std::runtime_error("emblem_cover_class_unavailable");
        UObject* cover=nullptr;
        for(auto* actor:find_live_actors_of_class(pawn,klass,16))
            if(actor->GetName()==L"g_ts_floortable_3")
            {if(cover)throw std::runtime_error("ambiguous_emblem_cover");cover=actor;}
        if(!is_uobject_valid_fast(cover))throw std::runtime_error("emblem_cover_unavailable");
        const auto pos=position(cover);double distance=0;
        for(size_t i=0;i<3;++i)distance+=std::pow(pos[i]-position_before[i],2.0);
        if(distance>emblem_cover_interaction_radius*emblem_cover_interaction_radius)throw std::runtime_error("emblem_cover_out_of_range");
        container_ref=cover;container_name=cover->GetPathName();
        container_destroyed=boolean(cover,L"BP_IsDestroyedGimmickForPlan");
        if(container_destroyed){finish(L"completed",L"emblem_cover_already_destroyed");return;}
        Call destroy(cover,L"RequestDestroyGimmickForPlan");
        if(!write_bool_function_param(destroy.field(L"PlayAnimation"),destroy.params.data(),true))
            throw std::runtime_error("emblem_cover_break_layout");
        report(L"accepted",L"emblem_cover_break_checkpoint");break_dispatched=true;
        destroy.invoke(false);waiting=true;return;
    }

    void RuntimeEnemyClearService::Impl::accept_break_container(UObject* pawn, const std::string& text)
    {
        break_container(pawn,JsonFieldReader::string_field(text,"actor").value_or(L""),
            JsonFieldReader::string_field(text,"treasure_id").value_or(L""));
        waiting=true;return;
    }

}
