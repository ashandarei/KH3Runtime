#pragma once
#include <memory>
#include <functional>

namespace kh3ap {
// Mutes only this KH3 process's render sessions. Volume levels and the game's
// saved audio settings are never changed. COM work runs outside the game thread.
class RuntimeMoMAudio {
public:
    explicit RuntimeMoMAudio(std::function<void(const wchar_t*)> report = {});
    ~RuntimeMoMAudio();
    void set_muted(bool muted);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
