#pragma once
#include <memory>
#include <cstdint>
#include <optional>
#include <string_view>
#include <iosfwd>
namespace RC::Unreal { class UObject; }
namespace kh3ap {
// Uses the same cooked Blueprint policy as Garden portals. The native menu
// continues to own input, cancellation, selection, and travel.
class RuntimeSpaceLanding {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    RuntimeSpaceLanding();
    ~RuntimeSpaceLanding();
    // True only when deferred asset preparation completes. The caller queues
    // one presentation refresh; idle frames never evaluate world progress.
    bool tick(RC::Unreal::UObject* context, bool in_space);
    bool ready() const;
    std::optional<bool> progressed(std::wstring_view code);
    bool build_list(RC::Unreal::UObject* menu, int32_t world_code);
    void write_status(std::ostream& out);
};
}
