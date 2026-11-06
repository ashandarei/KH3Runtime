#pragma once
#include <chrono>
#include <cstdint>
namespace RC::Unreal { class UObject; }
namespace kh3ap
{
    void tick_runtime_tutorial_suppression(std::chrono::steady_clock::time_point now);
    uint64_t runtime_suppressed_help_count();
    uint64_t runtime_suppressed_help_completion_count();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    bool test_runtime_space_dialogue_suppression(RC::Unreal::UObject* ui);
#endif
}
