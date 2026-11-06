#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <set>
namespace kh3ap {
// The native writer supplies POD names only. Multiple writes before the next
// game-thread dispatch coalesce; unrelated writes never wake the consumer.
class VBonusFlagEvents {
    const bool observe_false_writes;
    mutable std::mutex mutex;
    std::set<uint64_t> watched, queued;
    std::atomic<bool> notified{false};
    uint64_t count{};
public:
    explicit VBonusFlagEvents(bool observe_false = false) : observe_false_writes(observe_false) {}
    void configure(std::set<uint64_t> names) {
        std::lock_guard lock(mutex); watched=std::move(names);queued.clear();notified=false;
    }
    void write(uint64_t name,bool value) {
        // Completion consumers keep their positive-only behavior. State
        // enforcement consumers may also need to observe a native reset.
        if(!value&&!observe_false_writes)return;
        std::lock_guard lock(mutex);
        if(!(watched.find(name)!=watched.end()))return;
        queued.insert(name);++count;notified.store(true,std::memory_order_release);
    }
    bool pending() const { return notified.load(std::memory_order_acquire); }
    std::set<uint64_t> take() {
        std::lock_guard lock(mutex);std::set<uint64_t> result;result.swap(queued);notified=false;return result;
    }
    uint64_t events() const { std::lock_guard lock(mutex);return count; }
};
}
