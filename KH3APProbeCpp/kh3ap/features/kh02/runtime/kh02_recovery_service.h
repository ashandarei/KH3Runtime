#pragma once
#include "kh3ap/features/kh02/transport/kh02_asset_companion.h"
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02Story;

    struct Kh02RecoveryConfig
    {
        std::function<bool()> can_notify;
        std::function<void(const std::string&)> report;
    };

    // Safe return/one-shot notification owner. No normal story polling.
    class Kh02RecoveryService
    {
      public:
        explicit Kh02RecoveryService(Kh02RecoveryConfig config);
        ~Kh02RecoveryService();
        auto active() const -> bool;
        auto needs_tick() const -> bool;
        void begin(Kh02Story& story, const std::string& reason, kh02control::Identity provider);
        void request_return();
        void tick(RC::Unreal::UObject* pawn);
        auto snapshot() const -> nlohmann::json;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
