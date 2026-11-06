#pragma once
#include <string>
namespace RC::Unreal
{
    class UObject;
}
namespace kh3ap
{
    class RuntimeState;
    // Diagnostics-only, game-thread reads of functions used by event handlers.
    auto benchmark_event_reads(RuntimeState&, RC::Unreal::UObject*) -> std::string;
} // namespace kh3ap
