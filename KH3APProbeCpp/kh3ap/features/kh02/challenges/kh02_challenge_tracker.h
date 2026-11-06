#pragma once
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <cstdint>

namespace RC::Unreal
{
    class UObject;
    class UFunction;
} // namespace RC::Unreal

namespace kh3ap
{
    class Kh02ChallengeTracker
    {
      public:
        Kh02ChallengeTracker(std::map<int, int>& states, std::map<int, int>& counts);
        ~Kh02ChallengeTracker();
        // Game thread only. A world or native-save revision change discards all
        // pending hits/deaths; no raw UObject survives an event boundary.
        void bind(RC::Unreal::UObject* pawn, RC::Unreal::UObject* world, uint64_t revision);
        bool tick();
        void wardrobe(bool fully_equipped);
        bool team_finish(int id);
        bool event(int id, bool start, std::wstring_view caller);
        std::string snapshot() const;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
