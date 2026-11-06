#pragma once
#include <memory>
#include <string>
#include <functional>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{

    // Owns the configured GoA portal and asynchronous provider readiness.
    class Kh02Portal
    {
      public:
        explicit Kh02Portal(std::function<bool(RC::Unreal::UObject*)> before_travel = {},
                            std::function<bool()> assets_notified = {});
        ~Kh02Portal();
        void cancel();
        void tick(RC::Unreal::UObject* pawn, bool playable);
        auto failed_preparation() const -> bool;
        auto work_pending() const -> bool;
        auto inspect() const -> std::string;
        void interact(RC::Unreal::UObject* pawn);
        void press(RC::Unreal::UObject* pawn);

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
