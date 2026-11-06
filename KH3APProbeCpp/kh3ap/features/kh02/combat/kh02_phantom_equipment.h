#pragma once
#include <memory>

namespace kh3ap
{
    // Adapts Phantom Aqua's imported legacy weapon set at native equipment initialization.
    class Kh02PhantomEquipment
    {
      public:
        Kh02PhantomEquipment();
        ~Kh02PhantomEquipment();
        Kh02PhantomEquipment(const Kh02PhantomEquipment&) = delete;
        auto operator=(const Kh02PhantomEquipment&) -> Kh02PhantomEquipment& = delete;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
