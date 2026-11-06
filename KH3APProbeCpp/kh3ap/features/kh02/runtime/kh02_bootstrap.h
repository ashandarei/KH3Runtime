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
    class Kh02Bootstrap
    {
      public:
        explicit Kh02Bootstrap(std::function<bool(RC::Unreal::UObject*)> prepare);
        ~Kh02Bootstrap();
        // Install before Unreal initialization is published to the game loop.
        void install();
        void tick();
        auto failed() const -> bool;
        void require_restart(const std::string& reason);
        auto error() const -> const std::string&;
        auto inspect() const -> std::string;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
