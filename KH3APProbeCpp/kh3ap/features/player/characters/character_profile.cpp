#include "kh3ap/features/player/characters/character_profile.h"
#define NOMINMAX
#include <Windows.h>
#include <atomic>
#include <cstring>
#include <DynamicOutput/DynamicOutput.hpp>

namespace kh3ap::character_native
{
    Build build()
    {
        static const Build result = []
        {
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE)
            {
                return Build::Unsupported;
            }
            const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            return pe->Signature == IMAGE_NT_SIGNATURE
                       ? identify(pe->FileHeader.TimeDateStamp, pe->OptionalHeader.SizeOfImage)
                       : Build::Unsupported;
        }();
        return result;
    }

    uintptr_t rva(Site site)
    {
        return rva(site, build());
    }

    bool verify_sites(std::initializer_list<Site> sites)
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
            if (result == 0)
            {
                const auto& entry = entries[index];
                const auto offset = rva(site, image);
                const auto bytes = image == Build::Steam ? entry.steam_bytes : entry.egs_bytes;
                result = offset && !bytes.empty() &&
                                 !std::memcmp(reinterpret_cast<const void*>(base + offset), bytes.data(), bytes.size())
                             ? 1 : -1;
                verified[index].store(result, std::memory_order_release);
                if (result < 0)
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Character native profile mismatch site={} storefront={}\n"),
                        index, image == Build::Steam ? L"steam" : L"egs");
                }
            }
            if (result < 0)
            {
                return false;
            }
        }
        return true;
    }
}
