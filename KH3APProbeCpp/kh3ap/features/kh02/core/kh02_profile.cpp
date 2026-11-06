#include "kh02_profile.h"
#define NOMINMAX
#include <Windows.h>
#include <atomic>
#include <cstring>

namespace kh3ap::kh02_native
{
    auto verify_sites(std::initializer_list<Site> sites) -> bool
    {
        static std::array<std::atomic<int>, static_cast<size_t>(Site::count)> verified{};
        const auto image = build();
        if (image == Build::Unsupported)
        {
            return false;
        }
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        for (const auto site : sites)
        {
            const auto index = static_cast<size_t>(site);
            auto result = verified[index].load(std::memory_order_acquire);
            if (!result)
            {
                const auto& entry = entries[index];
                const auto offset = rva(site, image);
                const auto bytes = image == Build::Steam ? entry.steam_bytes : entry.egs_bytes;
                const auto size = image_size(image);
                result = offset && offset < size && !bytes.empty() && bytes.size() <= size - offset &&
                                 !std::memcmp(reinterpret_cast<const void*>(base + offset), bytes.data(), bytes.size())
                             ? 1
                             : -1;
                verified[index].store(result, std::memory_order_release);
            }
            if (result < 0)
            {
                return false;
            }
        }
        return true;
    }
} // namespace kh3ap::kh02_native
