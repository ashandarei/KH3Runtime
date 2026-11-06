#include "kh02_freeze_observer.h"
#include "kh02_challenge_actor_state.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr int frozen_actor_limit = 512;
        constexpr int ice_element = 2;
    } // namespace

    void Kh02FreezeObserver::observe(UObject* enemy, uint64_t identity, int element, bool was_frozen,
                                     std::map<int, int>& states, std::map<int, int>& counts, bool& pending_change)
    {
        if (states[kh02_challenge_id::kill_streak_blizaga] == kh02_challenge_state::complete)
        {
            return;
        }
        if (was_frozen && element != ice_element)
        {
            const int best = freeze_cohorts.shatter(identity);
            if (best > counts[kh02_challenge_id::kill_streak_blizaga])
            {
                counts[kh02_challenge_id::kill_streak_blizaga] = best;
                states[kh02_challenge_id::kill_streak_blizaga] = best == kh02_tracking::freeze_cohort_minimum_enemies
                                                                     ? kh02_challenge_state::complete
                                                                     : kh02_challenge_state::in_progress;
                pending_change = true;
            }
            frozen_actors.erase(identity);
        }
        auto* state = kh02_challenge_current_state(enemy);
        const bool frozen = is_uobject_valid_fast(state) &&
                            state->GetClassPrivate()->GetName().find(L"BadStatFreeze") != std::wstring::npos;
        if (element != ice_element || was_frozen)
        {
            return;
        }
        if (!frozen)
        {
            // Damage schedules the bad-status transition; it need not be the
            // current state until a later update. Observe only recently hit
            // candidates, with weak identities and a bounded deadline.
            if (pending_freezes.size() < frozen_actor_limit)
            {
                pending_freezes[identity] = {make_uobject_weak(enemy),
                                             std::chrono::steady_clock::now() + std::chrono::milliseconds(500)};
            }
            return;
        }
        record_freeze(enemy, identity);
    }

    void Kh02FreezeObserver::record_freeze(UObject* enemy, uint64_t identity)
    {
        if (frozen_actors.size() >= frozen_actor_limit)
        {
            return;
        }
        frozen_actors[identity] = make_uobject_weak(enemy);
        // Examine the cohort only on an actual freeze event. Different casts
        // can contribute enemies that are frozen at the same time.
        std::erase_if(frozen_actors,
                      [&](const auto& entry)
                      {
                          auto* current = kh02_challenge_current_state(entry.second.Get());
                          return !is_uobject_valid_fast(current) ||
                                 current->GetClassPrivate()->GetName().find(L"BadStatFreeze") == std::wstring::npos;
                      });
        std::set<uint64_t> active;
        for (const auto& [id, actor] : frozen_actors)
        {
            active.insert(id);
        }
        freeze_cohorts.observe(active);
    }

    void Kh02FreezeObserver::tick()
    {
        const auto now = std::chrono::steady_clock::now();
        std::erase_if(pending_freezes,
                      [&](const auto& entry)
                      {
                          auto* enemy = entry.second.actor.Get();
                          if (!is_uobject_valid_fast(enemy) || now > entry.second.expires)
                          {
                              return true;
                          }
                          auto* state = kh02_challenge_current_state(enemy);
                          if (is_uobject_valid_fast(state) &&
                              state->GetClassPrivate()->GetName().find(L"BadStatFreeze") != std::wstring::npos)
                          {
                              record_freeze(enemy, entry.first);
                              return true;
                          }
                          return false;
                      });
    }

} // namespace kh3ap
