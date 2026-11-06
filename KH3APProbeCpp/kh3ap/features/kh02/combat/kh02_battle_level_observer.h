#pragma once
#include <memory>

namespace kh3ap
{
    class Kh02BattleLevelObserver
    {
      public:
        struct Callbacks
        {
            void* context;
            void (*apply)(void*, int);
            void (*failure)(void*, const char*);
        };

        explicit Kh02BattleLevelObserver(Callbacks callbacks);
        ~Kh02BattleLevelObserver();
        void set_active(bool active);
        void mark_ready();
        auto ready() const -> bool;
        void stop();

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
