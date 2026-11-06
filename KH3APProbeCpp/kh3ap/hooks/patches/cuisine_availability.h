#pragma once

#include <memory>

namespace kh3ap
{
    // Keep Cuisine, Visit the Bistro and the camera independent of TT gameflow.
    class RuntimeCuisineAvailability
    {
    public:
        RuntimeCuisineAvailability();
        ~RuntimeCuisineAvailability();
        void install();
        void uninstall();
    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
