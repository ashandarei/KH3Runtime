#pragma once
#include <cstdint>
#include <map>
#include <string>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02StoryFlags;
    class Kh02DressingRoom;
    class Kh02ChallengeTracker;

    // Owns the live progress binding; world travel and entry execution stay in the story coordinator.
    class Kh02ProgressSession
    {
      public:
        struct Entry
        {
            bool backtrack{};
            bool resume{};
        };

        Kh02ProgressSession(Kh02StoryFlags& flags, Kh02DressingRoom& wardrobe, Kh02ChallengeTracker& tracker,
                            std::map<int, int>& challenges, std::map<int, int>& counts, std::map<int, bool>& events);
        void publish();
        auto bind(RC::Unreal::UObject* world, int checkpoint, bool backtrack) -> Entry;
        void invalidate();
        auto matches(const std::wstring& seed, int64_t player_slot, uint64_t revision) const -> bool;

        auto revision() const -> uint64_t
        {
            return binding_revision;
        }

        auto restored() const -> bool
        {
            return was_restored;
        }

        auto error() const -> const std::string&
        {
            return publish_error;
        }

      private:
        Kh02StoryFlags& source_flags;
        Kh02DressingRoom& dressing_room;
        Kh02ChallengeTracker& challenge_tracker;
        std::map<int, int>& challenges;
        std::map<int, int>& counts;
        std::map<int, bool>& events;
        uint64_t binding_revision{UINT64_MAX};
        std::wstring binding_seed;
        int64_t binding_player_slot{};
        bool was_restored{};
        std::string publish_error;
    };
} // namespace kh3ap
