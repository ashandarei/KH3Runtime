#pragma once

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::character_pose_handoff
{
    // Borrowed only within one synchronous game-thread handoff. The existing
    // animation-play hook uses this receiver to suppress parked-pose blends.
    inline thread_local RC::Unreal::UObject* animation_instance{};

    class Scope
    {
    public:
        explicit Scope(RC::Unreal::UObject* incoming)
            : previous_(animation_instance)
        {
            animation_instance = incoming;
        }
        ~Scope() { animation_instance = previous_; }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        RC::Unreal::UObject* previous_;
    };

    inline bool active_for(RC::Unreal::UObject* instance)
    {
        return instance && instance == animation_instance;
    }
}
