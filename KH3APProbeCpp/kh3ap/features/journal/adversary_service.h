#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
namespace RC::Unreal { class UObject; }
namespace kh3ap {
// EGS native journal counters, grouped by main/sub IDs. No actor discovery or
// journal UI is required. Unsupported instruction/layout guards fail closed.
auto read_adversary_defeats() -> std::optional<std::set<std::wstring>>;
auto read_adversary_revision() -> uint64_t;
// Content adapter, invoked synchronously on the counter's thread.
// Observer must be noexcept and do no filesystem work or gameplay grants.
using AdversaryDefeatObserver=void (*)(RC::Unreal::UObject*,int32_t) noexcept;
void set_adversary_defeat_observer(AdversaryDefeatObserver observer) noexcept;
class RuntimeAdversaryService {
public:
    RuntimeAdversaryService(std::function<std::optional<std::filesystem::path>()> bridge,
                           std::function<RC::Unreal::UObject*()> sora);
    ~RuntimeAdversaryService();
    RuntimeAdversaryService(const RuntimeAdversaryService&) = delete;
    RuntimeAdversaryService& operator=(const RuntimeAdversaryService&) = delete;
    void tick(std::chrono::steady_clock::time_point now);
private:
    std::function<std::optional<std::filesystem::path>()> bridge_;
    std::function<RC::Unreal::UObject*()> sora_;
    struct CounterHook;
    std::unique_ptr<CounterHook> hook_;
    uint64_t observed_generation_{};
    bool was_playable_{};
    bool needs_sync_{true};
    std::wstring seed_;
    int64_t slot_{},epoch_{-1};
    std::optional<std::set<std::wstring>> reported_;
};
}
