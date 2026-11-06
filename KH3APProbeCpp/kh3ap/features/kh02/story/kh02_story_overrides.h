#pragma once
#include <functional>
#include <memory>

namespace RC::Unreal
{
    class UObject;
    struct FFrame;
} // namespace RC::Unreal

namespace kh3ap
{
    // A source-native lease. Alias phase sequencing stays in the story composition root.
    class Kh02StoryNativeOverrides
    {
      public:
        using Thunk = void (*)(RC::Unreal::UObject*, RC::Unreal::FFrame&, void*);
        enum class Slot
        {
            signs,
            play,
            information,
            target_activate
        };

        struct Replacements
        {
            Thunk play{}, information{}, target_activate{};
        };

        using BeforeEncounter = std::function<void(RC::Unreal::UObject*, bool)>;
        Kh02StoryNativeOverrides();
        ~Kh02StoryNativeOverrides();
        void install_signs(Thunk replacement);
        void install_remaining(Replacements replacements, BeforeEncounter callback);
        auto original(Slot slot) const -> Thunk;
        void restore();

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
