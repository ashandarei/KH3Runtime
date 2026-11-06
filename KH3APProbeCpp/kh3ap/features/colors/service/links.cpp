#include "kh3ap/features/colors/service/service_impl.h"
#include "kh3ap/features/colors/service/character.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/colors/color_material_ops.h"
#include "kh3ap/features/colors/color_weapons.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <optional>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr auto link_weapon_readiness_retry = std::chrono::milliseconds(100);
    }

    using namespace RC;
    using namespace RC::Unreal;
    using namespace color_service_detail;

    auto RuntimeColorService::Impl::observe_active_link(UObject* sora,
                                                        std::chrono::steady_clock::time_point now)
        -> void
    {
        if (!sora || !is_uobject_valid_fast(sora))
        {
            return;
        }
        UObject* const state_component = read_strong_object_property(sora, STR("MyStateComp"));
        if (!state_component || !is_uobject_valid_fast(state_component))
        {
            return;
        }
        UObject* const current_state =
            read_strong_object_property(state_component, STR("m_pCurrentState"));
        if (!current_state || !is_uobject_valid_fast(current_state) ||
            !current_state->GetClassPrivate()->GetName().starts_with(STR("TresPlayerState_DM")))
        {
            return;
        }
        maybe_queue(read_strong_object_property(current_state, STR("m_pObj")), now);
    }

    auto RuntimeColorService::Impl::repaint_links(std::chrono::steady_clock::time_point now) -> void
    {
        std::erase_if(link_repaints_,
                      [](const LinkRepaint& entry)
                      {
                          return !is_uobject_valid_fast(entry.pawn.Get());
                      });
        for (LinkRepaint& entry : link_repaints_)
        {
            if (now < entry.due || entry.step >= link_repaint_after.size())
            {
                continue;
            }
            RuntimeColorPendingPawn pending{};
            pending.pawn = entry.pawn;
            pending.character_key = entry.key;
            pending.queued_at = now - link_settle - anim_instance_grace;
            log_link_readback(entry.pawn.Get(), entry.key, STR("before_repaint"));
            (void)try_apply(pending, now);
            if (++entry.step < link_repaint_after.size())
            {
                entry.due = now + link_repaint_after[entry.step];
            }
        }
        std::erase_if(link_repaints_,
                      [](const LinkRepaint& entry)
                      {
                          return entry.step >= link_repaint_after.size();
                      });
    }

    auto
    RuntimeColorService::Impl::color_equipped_weapons(std::chrono::steady_clock::time_point now)
        -> void
    {
        if (!weapons_dirty_ || now < weapon_retry_at_)
        {
            return;
        }
        const RuntimeHotPathProfileScope profile{L"colors:weapon_update"};
        weapons_dirty_ =
            !runtime_color_apply_equipped_weapons(state, seed_identity(), harmony_mode());
        weapon_retry_at_ = weapons_dirty_ ? now + link_weapon_readiness_retry
                                          : std::chrono::steady_clock::time_point{};
    }
} // namespace kh3ap
