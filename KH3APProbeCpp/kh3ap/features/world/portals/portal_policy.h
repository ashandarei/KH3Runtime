#pragma once

#include <array>
#include <string_view>

namespace kh3ap::portals
{
    inline constexpr auto dark_world_portal_id = 8;

    struct Kh02Access
    {
        bool enabled{}, unlocked{};
        bool operator==(const Kh02Access&) const = default;
    };

    constexpr auto kh02_access(bool ready, bool enabled, bool oblivion) -> Kh02Access
    {
        return {ready && enabled, ready && enabled && oblivion};
    }

    inline constexpr float centered_face_x = 0.5f;

    struct Definition
    {
        int id;
        std::wstring_view code;
        std::wstring_view first;
        std::wstring_view second;
        std::wstring_view portrait_a;
        std::wstring_view portrait_b;
        // Horizontal face centers in the native portrait canvas, independent of
        // transparent padding or an off-center pose. Each paired face is aligned
        // separately before the material alternates between them.
        float face_a_x{centered_face_x};
        float face_b_x{centered_face_x};
    };
    inline constexpr std::array definitions{
        Definition{2, L"di", L"Party Member: Kairi", L"", L"MenuFaceExKairi", L"", .63f},
        Definition{3, L"kg", L"Party Member: Ventus", L"", L"MenuFaceExVentus", L"", .63f},
        Definition{4, L"he", L"Party Member: Hercules", L"", L"MenuFaceHeHercules", L"", .65f},
        Definition{5, L"ca", L"Party Member: Captain Jack Sparrow", L"", L"MenuFaceCaJackSparrow",
                   L"", .62f},
        Definition{6, L"fz", L"Party Member: Marshmallow", L"", L"MenuFaceFzMarshmallow", L"",
                   .62f},
        Definition{7, L"ra", L"Party Member: Rapunzel", L"", L"MenuFaceRaRapunzel", L"", .66f},
        Definition{8, L"dw", L"Party Member: Aqua", L"", L"MenuFaceExAqua", L"", .66f},
        Definition{9, L"rg", L"Party Member: Riku", L"", L"MenuFaceEx3Riku", L"", .69f},
        Definition{10, L"bx", L"Party Member: Baymax", L"", L"MenuFaceBxBaymax", L"", .59f},
        Definition{11, L"mi", L"Party Member: Sulley", L"", L"MenuFaceMiSulley", L"", .61f},
        Definition{12, L"ts", L"Party Member: Woody", L"", L"MenuFaceTsWoody", L"", .61f},
        Definition{13, L"tt", L"Party Member: Roxas", L"", L"MenuFaceExRoxas", L"", .63f},
    };
    constexpr const Definition* by_id(int id)
    {
        for (const auto& entry : definitions)
        {
            if (entry.id == id)
            {
                return &entry;
            }
        }
        return nullptr;
    }
    template <class Has> bool allowed(const Definition& entry, Has has)
    {
        return has(entry.first) || (!entry.second.empty() && has(entry.second));
    }
    template <class Has> bool allowed(std::wstring_view code, Has has)
    {
        // Unreality keeps its original Blueprint and never uses party gating.
        if (code == L"ss")
        {
            return true;
        }
        for (const auto& entry : definitions)
        {
            if (entry.code == code)
            {
                return allowed(entry, has);
            }
        }
        return false;
    }
} // namespace kh3ap::portals
