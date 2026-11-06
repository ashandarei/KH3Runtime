#pragma once
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Observe the source graph's existing latent fade continuation. Never
    // replace, dispatch or synthesize the source callback.
    class Kh02ArrivalNotifications
    {
      public:
        Kh02ArrivalNotifications();
        ~Kh02ArrivalNotifications();
        void arm(RC::Unreal::UObject* world);
        void cancel();
        auto pending_fade() const -> bool;
        auto take_completion() -> bool;
        auto snapshot() const -> nlohmann::json;
        auto failure() const -> const std::string&;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
