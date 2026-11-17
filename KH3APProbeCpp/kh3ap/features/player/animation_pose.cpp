#include "animation_pose.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/hooks/native_detour_lifetime.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include <atomic>
#include <memory>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap::animation_pose
{
    namespace
    {
        std::array<std::atomic<Callback>, static_cast<size_t>(Consumer::Count)> callbacks{};
        uint64_t original{};
        std::unique_ptr<PLH::x64Detour> hook;

        void evaluate(RC::Unreal::UObject* animation, Pose* pose, Curve* curve,
                      const Extraction* context, bool force_raw)
        {
            extract(animation, pose, curve, context, force_raw);
            for (auto& entry : callbacks)
            {
                if (auto callback = entry.load(std::memory_order_acquire))
                {
                    callback(animation, pose, curve, context, force_raw);
                }
            }
        }
    } // namespace

    void extract(RC::Unreal::UObject* animation, Pose* pose, Curve* curve,
                 const Extraction* context, bool force_raw)
    {
        reinterpret_cast<Callback>(original)(animation, pose, curve, context, force_raw);
    }

    bool install(Consumer consumer, Callback callback)
    {
        const auto index = static_cast<size_t>(consumer);
        if (index >= callbacks.size() || !callback)
        {
            return false;
        }
        if (!hook || !hook->isHooked() || !original)
        {
            if (!release_native_detour(hook) ||
                !character_native::verify_sites({character_native::Site::animation_sequence_pose}))
            {
                return false;
            }
            const auto address =
                reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) +
                character_native::rva(character_native::Site::animation_sequence_pose);
            hook = std::make_unique<PLH::x64Detour>(address, reinterpret_cast<uintptr_t>(&evaluate),
                                                    &original);
            if (!hook->hook() || !original)
            {
                release_native_detour(hook);
                return false;
            }
        }
        callbacks[index].store(callback, std::memory_order_release);
        return true;
    }
} // namespace kh3ap::animation_pose
