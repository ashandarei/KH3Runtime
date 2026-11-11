// Diagnostics-only, bounded experiment: bind the reference used by the native
// GetCenterActor getter without loading the native arena or changing its geometry.
#include "kh3ap/features/enemies/enemy_clear/service_impl.h"
#include "kh3ap/features/enemies/enemy_clear/support.h"
#include "kh3ap/features/enemies/bosses/xemnas_center.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/AActor.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/Transform.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace enemy_clear_detail;

    void RuntimeEnemyClearService::Impl::restore_xemnas_anchor()
    {
        auto* actor = anchor_actor.Get();
        auto* entity = anchor_entity.Get();
        if (auto** slot = data_xemnas_center_slot(entity); slot && *slot == actor)
        {
            // Do not overwrite a newer owning-game change.
            *slot = anchor_original.Get();
        }
        if (is_uobject_valid_fast(actor))
        {
            Call destroy(actor, L"K2_DestroyActor");
            destroy.invoke();
        }
        anchor_actor = nullptr;
        anchor_entity = nullptr;
        anchor_boss = nullptr;
        anchor_pawn = nullptr;
        anchor_world = nullptr;
        anchor_original = nullptr;
        anchor_expires = 0;
        anchor_snapshot = "{\"active\":false,\"restored\":true}";
    }

    void RuntimeEnemyClearService::Impl::tick_xemnas_anchor()
    {
        if (!anchor_expires)
            return;
        auto* pawn = config.playable_sora();
        if (unix_ms() >= anchor_expires || !is_uobject_valid_fast(anchor_boss.Get()) ||
            !is_uobject_valid_fast(anchor_actor.Get()) || !is_uobject_valid_fast(pawn) ||
            pawn != anchor_pawn.Get() || actor_world(pawn) != anchor_world.Get())
            restore_xemnas_anchor();
    }

    void RuntimeEnemyClearService::Impl::accept_xemnas_anchor(UObject* pawn, const std::string& text)
    {
        const auto seconds = JsonFieldReader::int64_field(text, "seconds").value_or(-1);
        if (seconds < 0 || seconds > 600)
            throw std::runtime_error("xemnas_anchor_seconds_out_of_range");
        if (!seconds)
        {
            restore_xemnas_anchor();
            finish(L"completed", L"xemnas_anchor_readback");
            return;
        }
        if (anchor_expires)
            throw std::runtime_error("restore_previous_xemnas_anchor_first");
        const auto selected = JsonFieldReader::string_field(text, "actor").value_or(L"");
        enumerate(pawn);
        std::erase_if(targets, [&](const Target& target) { return target.name != selected; });
        if (selected.empty() || targets.size() != 1 || targets.front().hp_before <= 0 ||
            targets.front().class_name != data_xemnas_class)
            throw std::runtime_error("xemnas_anchor_requires_exact_live_data_xemnas");
        auto* boss = targets.front().actor.Get();
        auto* entity = targets.front().entity.Get();
        auto** slot = data_xemnas_center_slot(entity);
        const auto original = data_xemnas_center_actor(boss);
        if (!slot || !original)
            throw std::runtime_error("xemnas_anchor_user_data_layout");
        if (*slot || *original)
            throw std::runtime_error("xemnas_anchor_requires_missing_native_reference");
        const auto center = data_xemnas_center_location(boss);
        if (!center)
            throw std::runtime_error("xemnas_anchor_center_layout");
        auto* klass = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/Engine.TargetPoint");
        if (!is_uobject_valid_fast(klass))
            throw std::runtime_error("xemnas_anchor_class_missing");
        anchor_entity = make_uobject_weak(entity);
        anchor_boss = make_uobject_weak(boss);
        anchor_pawn = make_uobject_weak(pawn);
        anchor_world = make_uobject_weak(actor_world(pawn));
        if (!anchor_entity.Get() || !anchor_boss.Get() || !anchor_pawn.Get() || !anchor_world.Get())
            throw std::runtime_error("xemnas_anchor_weak_context_missing");
        const FTransform transform{FQuat{0, 0, 0, 1}, FVector{(*center)[0], (*center)[1], (*center)[2]}, FVector{1, 1, 1}};
        auto* actor = UGameplayStatics::BeginDeferredActorSpawnFromClass(
            pawn, klass, transform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn, nullptr);
        if (!is_uobject_valid_fast(actor))
            throw std::runtime_error("xemnas_anchor_spawn_failed");
        try
        {
            UGameplayStatics::FinishSpawningActor(actor, transform);
            anchor_actor = make_uobject_weak(actor);
            if (anchor_actor.Get() != actor || actor_world(actor) != anchor_world.Get())
                throw std::runtime_error("xemnas_anchor_spawn_identity_failed");
            *slot = actor;
            anchor_expires = unix_ms() + seconds * 1000;
            if (data_xemnas_center_actor(boss) != actor)
                throw std::runtime_error("xemnas_anchor_native_reference_not_observed");
            const auto actual = position(actor);
            for (size_t i = 0; i < center->size(); ++i)
                if (!std::isfinite(actual[i]) || std::abs(actual[i] - (*center)[i]) > 1)
                    throw std::runtime_error("xemnas_anchor_position_not_observed");
            std::ostringstream out;
            out << "{\"active\":true,\"native_reference_verified\":true,\"boss\":\""
                << json_escape(selected) << "\",\"actor\":\"" << json_escape(actor->GetPathName())
                << "\",\"expires_unix_ms\":" << anchor_expires << ",\"position\":";
            json_position(out, actual);
            out << ",\"shield_points\":0}";
            anchor_snapshot = out.str();
        }
        catch (...)
        {
            if (*slot == actor)
                *slot = nullptr;
            Call destroy(actor, L"K2_DestroyActor");
            destroy.invoke();
            anchor_expires = 0;
            anchor_actor = nullptr;
            throw;
        }
        finish(L"completed", L"xemnas_anchor_readback");
    }
}
