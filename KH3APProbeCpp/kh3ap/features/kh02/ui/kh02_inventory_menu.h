#pragma once
#include <memory>

namespace kh3ap
{
    // Extends the native camp roster for the currently possessed alternate hero.
    // Native equipment, stock accounting and menu teardown remain the owners.
    class Kh02InventoryMenu
    {
      public:
        Kh02InventoryMenu();
        ~Kh02InventoryMenu();
        void install();
        static void install_shared();

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
