#pragma once
#include <memory>
#include <string_view>

namespace RC::Unreal { class UObject; }
namespace kh3ap {
    // A separate native item-banner panel; never attaches a cockpit or Sora.
    class RuntimeSpaceNotifications {
    public:
        RuntimeSpaceNotifications();
        ~RuntimeSpaceNotifications();
        void enqueue(std::wstring_view token);
        void tick(RC::Unreal::UObject* safe_controller, bool in_space, bool overview);
        unsigned long long shown() const;
        unsigned long long pending() const;
    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
