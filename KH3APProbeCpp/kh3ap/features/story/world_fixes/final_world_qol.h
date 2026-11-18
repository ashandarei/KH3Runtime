#pragma once
#include <chrono>

namespace kh3ap
{
    class RuntimeState;
    void tick_final_world_qol(std::chrono::steady_clock::time_point now, RuntimeState& runtime);
}
