#pragma once

#include <Unreal/FWeakObjectPtr.hpp>
#include <chrono>
#include <string>

namespace kh3ap
{
    class CutsceneSkipView
    {
    public:
        void arm(RC::Unreal::UObject* source);
        void tick();

    private:
        void reset();
        bool pending{}, clearing{};
        RC::Unreal::FWeakObjectPtr source_actor, source_world;
        std::wstring source_name;
        std::chrono::steady_clock::time_point deadline{}, next_check{};
    };
}
