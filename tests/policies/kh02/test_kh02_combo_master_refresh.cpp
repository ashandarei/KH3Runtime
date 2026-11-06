#include "kh3ap/features/kh02/combat/kh02_combo_master.h"
#include <cassert>
#include <chrono>
#include <string_view>
#include <vector>

namespace
{
    std::vector<std::string_view> calls;
    bool settled = true;
} // namespace

namespace kh3ap
{
    // Substitute engine-facing work while exercising the production scheduler.
    auto Kh02ComboMaster::update(RC::Unreal::UObject*) -> bool
    {
        calls.push_back("update");
        return settled;
    }
} // namespace kh3ap

int main()
{
    using namespace std::chrono;
    kh3ap::Kh02ComboMaster service;
    const auto start = steady_clock::time_point{};
    bool ready = true;
    const auto guard = [&]()
    {
        calls.push_back("ready");
        return ready;
    };
    const auto pawn = []() -> RC::Unreal::UObject*
    {
        calls.push_back("pawn");
        return nullptr;
    };
    const std::vector<std::string_view> complete{"ready", "pawn", "update"};

    service.tick(0, start, guard, pawn);
    assert(calls == complete);
    calls.clear();
    for (int elapsed = 1; elapsed <= 30000; ++elapsed)
    {
        service.tick(0, start + milliseconds(elapsed), guard, pawn);
    }
    assert(calls.empty());

    // A map/possession event wakes the settled context immediately.
    service.tick(1, start + seconds(31), guard, pawn);
    assert(calls == complete);
    calls.clear();

    // Loading/save guards retry only while a refresh remains unfinished.
    ready = false;
    service.tick(2, start + seconds(32), guard, pawn);
    assert(calls == std::vector<std::string_view>{"ready"});
    calls.clear();
    ready = true;
    service.tick(2, start + seconds(32) + milliseconds(249), guard, pawn);
    assert(calls.empty());
    service.tick(2, start + seconds(32) + milliseconds(250), guard, pawn);
    assert(calls == complete);
    calls.clear();

    // A failed native grant/removal remains pending until it actually settles.
    settled = false;
    service.tick(3, start + seconds(33), guard, pawn);
    assert(calls == complete);
    calls.clear();
    service.tick(3, start + seconds(33) + milliseconds(249), guard, pawn);
    assert(calls.empty());
    settled = true;
    service.tick(3, start + seconds(33) + milliseconds(250), guard, pawn);
    assert(calls == complete);
    calls.clear();
    service.tick(3, start + seconds(60), guard, pawn);
    assert(calls.empty());

    // New context events bypass an unfinished older context's retry deadline.
    ready = false;
    service.tick(4, start + seconds(61), guard, pawn);
    calls.clear();
    ready = true;
    service.tick(5, start + seconds(61) + milliseconds(1), guard, pawn);
    assert(calls == complete);
}
