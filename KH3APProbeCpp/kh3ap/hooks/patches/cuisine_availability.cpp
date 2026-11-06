#include "kh3ap/core/platform/game_build.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/hooks/patches/cuisine_availability.h"
#include "kh3ap/core/platform/steam_parity_profile.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/NameTypes.hpp>
#include <UnrealDef.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstring>

namespace kh3ap
{
    struct RuntimeCuisineAvailability::Impl
    {
        // The shipping EGS ABI passes the eight-byte FName in RDX. This
        // owner is native game data, not a UObject; never cache its address.
        using Query = bool(*)(void*, uint64_t);
        inline static Impl* active{};
        uint64_t original{}, cuisine{}, restaurant{}, camera{};
        std::unique_ptr<PLH::x64Detour> hook;

        static bool query(void* owner, uint64_t name)
        {
            auto& self = *active;
            if (owner && (name == self.cuisine || name == self.restaurant || name == self.camera)) return true;
            return reinterpret_cast<Query>(self.original)(owner, name);
        }

        static uint64_t name_key(const wchar_t* text)
        {
            const RC::Unreal::FName name{text, RC::Unreal::FNAME_Add};
            return static_cast<uint32_t>(name.GetComparisonIndex()) |
                   (static_cast<uint64_t>(name.GetNumber()) << 32);
        }

        void install()
        {
            if (hook) return;
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            if (!base) return;
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
            const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            // Native TresUnlockFlagData query: resolves the requested row and
            // compares GameFlagName against GameFlagLabel. Cuisine/Bistro
            // require TT_END; the camera requires TT_TRM_EVENT03. Override only
            // these unlock queries; normal camera safety checks still apply.
            const auto* profile=steam_parity_native_profile(pe->FileHeader.TimeDateStamp,pe->OptionalHeader.SizeOfImage);
            if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
                !profile || std::memcmp(reinterpret_cast<void*>(base+profile->cuisine_query.rva),
                    profile->cuisine_query.bytes.data(),profile->cuisine_query.bytes.size()))
            {
                RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Cuisine availability rejected: unsupported image or signature\n"));
                return;
            }
            cuisine = name_key(L"cuisine");
            restaurant = name_key(L"restaurant");
            camera = name_key(L"spcamera");
            active = this;
            hook = std::make_unique<PLH::x64Detour>(base + profile->cuisine_query.rva, reinterpret_cast<uintptr_t>(&query), &original);
            const bool installed = hook->hook() && original;
            if (!installed) uninstall();
            RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Cuisine availability installed={} cuisine=always restaurant=always spcamera=always storefront={}\n"), installed,profile->image_size==kh3ap::native_build::steam.image_size?STR("steam"):STR("egs"));
        }

        void uninstall()
        {
            if (hook && hook->isHooked()) hook->unHook();
            hook.reset();
            if (active == this) active = nullptr;
        }
    };

    RuntimeCuisineAvailability::RuntimeCuisineAvailability() : impl_(std::make_unique<Impl>()) {}
    RuntimeCuisineAvailability::~RuntimeCuisineAvailability() { impl_->uninstall(); }
    void RuntimeCuisineAvailability::install() { impl_->install(); }
    void RuntimeCuisineAvailability::uninstall() { impl_->uninstall(); }
}
