#pragma once

#include <atomic>

namespace kh3ap
{
    // The existing state-write hook emits the accepted transition on its owning
    // thread. Observers must not change states or retain unprotected UObjects.
    using CharacterStateObserver = void (*)(void* component, void* state) noexcept;
    inline std::atomic<CharacterStateObserver> character_state_observer{};
} // namespace kh3ap
