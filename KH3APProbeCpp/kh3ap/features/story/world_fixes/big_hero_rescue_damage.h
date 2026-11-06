#pragma once

#include <memory>

namespace kh3ap
{
    // Owns the existing gimmick damage entry point, including Kairi diagnostics.
    // Rescue-specific filtering runs only when a hit arrives; no actor polling.
    class BigHeroRescueDamage
    {
    public:
        BigHeroRescueDamage();
        ~BigHeroRescueDamage();
        void install();
        void uninstall();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
