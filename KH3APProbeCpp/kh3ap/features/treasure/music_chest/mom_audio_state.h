#pragma once
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace kh3ap {
// Kept on the audio worker. Each session is captured only once per handoff;
// repeated discovery must never replace its original state with our own mute.
class RuntimeMoMAudioState {
public:
    struct Session {
        std::function<std::optional<bool>()> read_mute;
        std::function<bool(bool)> write_mute;
    };

    bool mute(const std::wstring& id, Session session) {
        auto it = saved_.find(id);
        if (it == saved_.end()) {
            const auto previous = session.read_mute();
            if (!previous) return false;
            it = saved_.emplace(id, Saved{std::move(session), *previous}).first;
        }
        // Keep the snapshot even if a write/readback fails, so restoration can
        // still undo a write that reached the device before it disconnected.
        auto& saved = it->second;
        return saved.session.write_mute(true);
    }

    bool restore() {
        for (auto it = saved_.begin(); it != saved_.end();) {
            if (it->second.session.write_mute(it->second.previous))
                it = saved_.erase(it);
            else ++it;
        }
        return saved_.empty();
    }
    bool empty() const { return saved_.empty(); }

private:
    struct Saved { Session session; bool previous; };
    std::map<std::wstring, Saved> saved_;
};
}
