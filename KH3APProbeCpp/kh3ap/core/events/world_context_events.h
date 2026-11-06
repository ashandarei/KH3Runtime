#pragma once
#include <atomic>
#include <cstdint>

namespace kh3ap {
// Shared revision for the existing native map/controller/context producers.
// Independent consumers observe it without draining another feature's mailbox.
inline std::atomic<uint64_t> world_context_revision{};
}
