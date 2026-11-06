#pragma once
#include <Unreal/FWeakObjectPtr.hpp>

namespace kh3ap
{
    // Borrows KH3's real loading part only during portal preparation.
    class Kh02PortalLoading
    {
      public:
        ~Kh02PortalLoading();
        void open(RC::Unreal::UObject* pawn);
        void prepare_travel();
        void handoff();
        void close();
        auto active() const -> bool;

      private:
        RC::Unreal::FWeakObjectPtr native_screen, native_manager;
        bool travel_prepared{};
    };
} // namespace kh3ap
