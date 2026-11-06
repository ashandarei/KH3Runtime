#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Owns native registrations and trampolines. Challenge attribution remains in the tracker.
    class Kh02ChallengeHooks
    {
      public:
        using DamageFunction = float (*)(RC::Unreal::UObject*, float, const void*, RC::Unreal::UObject*,
                                         RC::Unreal::UObject*);

        struct Callbacks
        {
            void* context;
            float (*damage)(void*, RC::Unreal::UObject*, float, const void*, RC::Unreal::UObject*, RC::Unreal::UObject*,
                            DamageFunction);
            void (*defeated)(void*, RC::Unreal::UObject*, int32_t) noexcept;
            void (*state_changed)(void*, void*, void*) noexcept;
            bool (*observes_shotlock)(void*, RC::Unreal::UObject*);
            void (*shotlock_result)(void*, int);
        };

        explicit Kh02ChallengeHooks(Callbacks callbacks);
        ~Kh02ChallengeHooks();
        Kh02ChallengeHooks(const Kh02ChallengeHooks&) = delete;
        auto operator=(const Kh02ChallengeHooks&) -> Kh02ChallengeHooks& = delete;
        void install(std::string& error);
        void stop();
        auto damage_installed() const -> bool;
        auto shotlock_installed() const -> bool;
        static auto contains_image_address(uintptr_t image_base, uintptr_t address) -> bool;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
