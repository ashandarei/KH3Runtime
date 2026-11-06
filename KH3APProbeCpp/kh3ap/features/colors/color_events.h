#pragma once
#include <atomic>
#include <cstdint>
namespace kh3ap {
inline std::atomic<uint64_t> color_equipment_generation{};
inline void notify_color_equipment_changed() noexcept {
    color_equipment_generation.fetch_add(1,std::memory_order_release);
}
}
