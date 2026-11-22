#pragma once

#include <memory>

namespace kh3ap
{
    // Keep Cuisine, Visit the Bistro, the camera and the Photo Missions menu independent of TT gameflow.
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
