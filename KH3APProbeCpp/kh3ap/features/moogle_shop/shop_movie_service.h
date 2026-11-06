#pragma once
#include <chrono>
#include <functional>
#include <utility>

namespace RC::Unreal { class UObject; }

namespace kh3ap
{
    // Preloads the movie asynchronously as soon as Sora is available. Stops all
    // polling after patching; the asset is rooted for the process lifetime.
    class RuntimeShopMovieService
    {
    public:
        explicit RuntimeShopMovieService(std::function<RC::Unreal::UObject*()> context)
            : context_(std::move(context)) {}
        auto tick() -> void;
    private:
        std::function<RC::Unreal::UObject*()> context_;
        bool complete_{};
        bool requested_{};
        std::chrono::steady_clock::time_point next_poll_{}, started_{};
    };
}
