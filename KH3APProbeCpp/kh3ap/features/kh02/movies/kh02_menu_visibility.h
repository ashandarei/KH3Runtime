#pragma once
#include <Unreal/FWeakObjectPtr.hpp>

namespace kh3ap
{
    // Native camp keeps pause/input ownership while a private movie covers it.
    class Kh02NativeMenuVisibility
    {
      public:
        Kh02NativeMenuVisibility() = default;
        ~Kh02NativeMenuVisibility();
        Kh02NativeMenuVisibility(const Kh02NativeMenuVisibility&) = delete;
        auto operator=(const Kh02NativeMenuVisibility&) -> Kh02NativeMenuVisibility& = delete;
        void suspend(RC::Unreal::UObject* camp);
        void restore();

      private:
        struct Display
        {
            RC::Unreal::FWeakObjectPtr root;
            double alpha{1};
        };

        static void suspend(Display& display, RC::Unreal::UObject* root);
        static void restore(Display& display);
        Display menu, cursor;
    };
} // namespace kh3ap
