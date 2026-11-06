#pragma once
#include <memory>
#include <string>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02Hud
    {
      public:
        Kh02Hud();
        ~Kh02Hud();
        bool prepare(RC::Unreal::UObject* context);
        void reset_world();
        void tick(RC::Unreal::UObject* pawn, bool active);
        void counter(int value, int maximum, bool visible);
        std::string snapshot() const;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
