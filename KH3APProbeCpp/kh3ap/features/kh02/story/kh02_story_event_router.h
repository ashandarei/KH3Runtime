#pragma once
#include <functional>
#include <memory>
#include <string>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02StoryTargets;
    class Kh02MapSession;
    struct Kh02MirrorGameplaySession;

    class Kh02StoryEventRouter
    {
      public:
        struct Callbacks
        {
            std::function<void(RC::Unreal::UObject*)> chest_opened;
            std::function<void(RC::Unreal::UObject*)> host_begin_play;
            std::function<void(RC::Unreal::UObject*)> projectile_begin_play;
            std::function<void(RC::Unreal::UObject*)> chest_mapset_finished;
        };

        Kh02StoryEventRouter(Kh02MirrorGameplaySession& mirror, Kh02StoryTargets& targets, Kh02MapSession& maps,
                             std::string& compatibility_error, Callbacks callbacks);
        ~Kh02StoryEventRouter();
        void install();

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
