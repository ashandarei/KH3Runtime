#pragma once
#include <functional>
#include <memory>
#include <ostream>
#include <string_view>
#include <cstdint>

namespace kh3ap {
// Runs after KH3 assigns a battle actor to an existing Scaleform guide slot.
// No actor discovery, filesystem polling, or new UI assets are needed.
class RuntimeSpaceBattleMarkers {
public:
    using Completed = std::function<bool(std::wstring_view)>;
    using Revision = std::function<uint64_t()>;
    RuntimeSpaceBattleMarkers(Completed, Revision);
    ~RuntimeSpaceBattleMarkers();
    bool install();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    void inspect(std::ostream&) const;
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
