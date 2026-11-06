#pragma once

#include <cstdint>

namespace PLH
{
    inline uint64_t destination{}, continuation{};
    inline bool fail_hook{}, fail_unhook{};
    inline int destroyed{};

    class x64Detour
    {
    public:
        enum Scheme
        {
            VALLOC2
        };
        x64Detour(uint64_t, uint64_t target, uint64_t* original) : original_(original)
        {
            destination = target;
        }
        ~x64Detour()
        {
            ++destroyed;
        }
        auto setIsFollowCallOnFnAddress(bool) -> void
        {
        }
        auto setDetourScheme(Scheme) -> void
        {
        }
        auto hook() -> bool
        {
            if (fail_hook)
            {
                return false;
            }
            *original_ = continuation;
            return true;
        }
        auto unHook() -> bool
        {
            return !fail_unhook;
        }

    private:
        uint64_t* original_;
    };
} // namespace PLH
