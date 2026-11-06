#pragma once
#include "kh02_challenge_policy.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace kh3ap
{
    // WeakReference is copied, never resolved here. Native ownership/world gates
    // stay at the observation site; matching is deterministic and Unreal-free.
    template <class WeakReference> class Kh02CombatEventJournal
    {
      public:
        using Clock = std::chrono::steady_clock;
        static constexpr std::size_t actor_observation_limit = 8'192;
        static constexpr std::size_t damage_history_limit = 512;
        static constexpr std::size_t death_history_limit = 256;
        static constexpr auto damage_before_death_window = std::chrono::seconds(2);
        static constexpr auto damage_after_death_window = std::chrono::milliseconds(150);
        static constexpr auto damage_history_retention = std::chrono::seconds(2);

        enum class Observation
        {
            ignored,
            accepted,
            overflow
        };

        struct Damage
        {
            WeakReference enemy;
            uint64_t identity;
            kh02_tracking::Hit hit;
            Clock::time_point at;
        };

        struct Death
        {
            WeakReference enemy;
            uint64_t identity;
            std::wstring type;
            Clock::time_point at;
        };

        auto saw_defeat(uint64_t identity) const -> bool
        {
            return seen.contains(identity);
        }

        auto defeat(WeakReference enemy, uint64_t identity, std::wstring type, Clock::time_point at) -> Observation
        {
            if (seen.size() >= actor_observation_limit || !seen.insert(identity).second)
            {
                return Observation::ignored;
            }
            // Seen identities are authoritative even if this frame's bounded
            // death queue is full: the enclosing damage call still observes it.
            if (death_events.size() >= death_history_limit)
            {
                return Observation::overflow;
            }
            death_events.push_back({enemy, identity, std::move(type), at});
            return Observation::accepted;
        }

        auto damage(WeakReference enemy, uint64_t identity, const kh02_tracking::Hit& hit, float applied_damage,
                    Clock::time_point at) -> Observation
        {
            std::erase_if(damage_events, [identity](const Damage& previous) { return previous.identity == identity; });
            if (!(applied_damage > 0 || hit.lethal))
            {
                return Observation::ignored;
            }
            if (damage_events.size() >= damage_history_limit)
            {
                return Observation::overflow;
            }
            damage_events.push_back({enemy, identity, hit, at});
            return Observation::accepted;
        }

        auto match(const Death& death) const -> kh02_tracking::Hit
        {
            for (const auto& candidate : damage_events)
            {
                if (candidate.identity == death.identity && candidate.at >= death.at - damage_before_death_window &&
                    candidate.at <= death.at + damage_after_death_window)
                {
                    return candidate.hit;
                }
            }
            return {};
        }

        auto deaths() const -> const std::vector<Death>&
        {
            return death_events;
        }

        void retire_frame(Clock::time_point now)
        {
            death_events.clear();
            std::erase_if(damage_events, [now](const Damage& hit) { return now - hit.at > damage_history_retention; });
        }

        void clear()
        {
            damage_events.clear();
            death_events.clear();
            seen.clear();
        }

      private:
        std::vector<Damage> damage_events;
        std::vector<Death> death_events;
        std::set<uint64_t> seen;
    };
} // namespace kh3ap
