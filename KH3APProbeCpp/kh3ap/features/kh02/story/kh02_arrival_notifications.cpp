#include "kh02_arrival_notifications.h"
#include "kh3ap/features/kh02/core/kh02_profile.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/NameTypes.hpp>
#define NOMINMAX
#include <Windows.h>
#include <vector>
#include <cstring>
#include <utility>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    struct Kh02ArrivalNotifications::Impl
    {
        inline static Impl* active{};
        inline static bool post_registered{};
        inline static uint64_t original_start{}, original_cancel{};
        std::unique_ptr<PLH::x64Detour> start_hook, cancel_hook;

        struct Continuation
        {
            FWeakObjectPtr object;
            FWeakObjectPtr function;
            int32_t linkage{};
        };

        std::vector<Continuation> continuations;
        FWeakObjectPtr world;
        bool completed{};
        uint64_t captured{}, delivered{};
        std::string error;

        void capture(UObject* context, const std::byte* latent, uint32_t layer)
        {
            auto* owner_world = world.Get();
            if (!is_uobject_valid_fast(owner_world) || !is_uobject_valid_fast(context) || layer != 0 || !latent ||
                (context != owner_world && kh02_actor_world(context) != owner_world))
            {
                return;
            }
            int32_t linkage{};
            FName execution;
            UObject* target{};
            std::memcpy(&linkage, latent, sizeof(linkage));
            std::memcpy(&execution, latent + 8, sizeof(execution));
            std::memcpy(&target, latent + 16, sizeof(target));
            if (!is_uobject_valid_fast(target) || kh02_actor_world(target) != owner_world)
            {
                return;
            }
            auto* callback = target->GetFunctionByNameInChain(execution);
            if (!is_uobject_valid_fast(callback) || callback->GetParmsSize() != sizeof(int32_t))
            {
                throw std::runtime_error("kh02_arrival_fade_continuation_layout");
            }
            if (continuations.size() >= 64)
            {
                throw std::runtime_error("kh02_arrival_fade_continuation_limit");
            }
            continuations.push_back({make_uobject_weak(target), make_uobject_weak(callback), linkage});
            ++captured;
        }

        static void observe(UObject* context, const std::byte* latent, uint32_t layer)
        {
            if (auto* owner = active)
            {
                try
                {
                    owner->capture(context, latent, layer);
                }
                catch (const std::exception& failure)
                {
                    owner->error = failure.what();
                }
            }
        }

        // Verified native implementations called by the reflected exec thunks:
        // EGS uses 0x10e5ce0 / 0x10e6030; Steam uses 0xe81c50 / 0xe81a80.
        // Steam's exec wrapper 0xe7dca0 receives color before layer and
        // reorders them before calling the native start implementation.
        // Hook that implementation, not the wrapper. The native ABI
        // passes the layer before color, and the 24-byte latent info by address.
        // Both native callees consume the full R8D layer. The reflected enum
        // is a byte, but using uint8_t here restores only R8B after observe()
        // and leaves its clobbered upper bits as an invalid native fade layer.
        static void start(UObject* context, const std::byte* latent, uint32_t layer, const void* color, float seconds,
                          bool game_speed, bool game_pause)
        {
            observe(context, latent, layer);
            using Start = decltype(&start);
            reinterpret_cast<Start>(original_start)(context, latent, layer, color, seconds, game_speed, game_pause);
        }

        static void cancel_fade(UObject* context, const std::byte* latent, uint32_t layer, float seconds,
                                bool game_speed, bool game_pause)
        {
            observe(context, latent, layer);
            using Cancel = decltype(&cancel_fade);
            reinterpret_cast<Cancel>(original_cancel)(context, latent, layer, seconds, game_speed, game_pause);
        }

        void install()
        {
            active = this;
            if (start_hook)
            {
                return;
            }
            auto* image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
            using kh02_native::Site;
            if (!kh02_native::verify_sites({Site::arrival_fade_start, Site::arrival_fade_cancel}))
            {
                throw std::runtime_error("kh02_arrival_fade_notification_profile");
            }
            start_hook = std::make_unique<PLH::x64Detour>(
                reinterpret_cast<uint64_t>(image + kh02_native::rva(Site::arrival_fade_start)),
                reinterpret_cast<uint64_t>(&start), &original_start);
            cancel_hook = std::make_unique<PLH::x64Detour>(
                reinterpret_cast<uint64_t>(image + kh02_native::rva(Site::arrival_fade_cancel)),
                reinterpret_cast<uint64_t>(&cancel_fade), &original_cancel);
            if (!start_hook->hook() || !cancel_hook->hook())
            {
                throw std::runtime_error("kh02_arrival_fade_notification_hook");
            }
            if (!post_registered)
            {
                Hook::RegisterProcessEventPostCallback(
                    [](UObject* object, UFunction* function, void* params)
                    {
                        auto* owner = active;
                        if (!owner || !params || owner->continuations.empty())
                        {
                            return;
                        }
                        for (auto item = owner->continuations.begin(); item != owner->continuations.end();)
                        {
                            if (item->object.Get() == object && item->function.Get() == function &&
                                item->linkage == *static_cast<int32_t*>(params))
                            {
                                owner->completed = true;
                                ++owner->delivered;
                                item = owner->continuations.erase(item);
                            }
                            else
                            {
                                ++item;
                            }
                        }
                    });
                post_registered = true;
            }
        }

        ~Impl()
        {
            if (active == this)
            {
                active = nullptr;
            }
            if (start_hook)
            {
                start_hook->unHook();
            }
            if (cancel_hook)
            {
                cancel_hook->unHook();
            }
        }
    };

    Kh02ArrivalNotifications::Kh02ArrivalNotifications() : impl(std::make_unique<Impl>()) {}

    Kh02ArrivalNotifications::~Kh02ArrivalNotifications() = default;

    void Kh02ArrivalNotifications::arm(UObject* world)
    {
        cancel();
        impl->install();
        impl->world = make_uobject_weak(world);
    }

    void Kh02ArrivalNotifications::cancel()
    {
        impl->world = {};
        impl->continuations.clear();
        impl->completed = false;
        impl->captured = 0;
        impl->delivered = 0;
        impl->error.clear();
    }

    auto Kh02ArrivalNotifications::pending_fade() const -> bool
    {
        return !impl->continuations.empty();
    }

    auto Kh02ArrivalNotifications::take_completion() -> bool
    {
        return std::exchange(impl->completed, false);
    }

    auto Kh02ArrivalNotifications::snapshot() const -> nlohmann::json
    {
        return {{"captured", impl->captured}, {"delivered", impl->delivered}, {"error", impl->error}};
    }

    auto Kh02ArrivalNotifications::failure() const -> const std::string&
    {
        return impl->error;
    }
} // namespace kh3ap
