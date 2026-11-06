#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace RC::Unreal { class UObject; }

namespace kh3ap
{
    inline constexpr auto station_map = L"/Game/Maps/StationOfAwakening/umap/StationOfAwakening";
    inline constexpr auto intro_goa_map = L"/Game/Maps/GardenOfAssemblage/umap/GoA_01";

    inline constexpr auto opening_darkside_map = L"/Game/Levels/ew_DLC/ew_04/ew_04";

    // Independent startup possession check. Normal AP grants also accept Station.
    auto runtime_station_sora() -> RC::Unreal::UObject*;

    struct RuntimeNewGameConfig
    {
        std::function<std::optional<std::filesystem::path>()> bridge_dir;
        std::function<RC::Unreal::UObject*()> playable_sora;
        std::function<int(std::wstring_view)> party_unlock_state;
    };

    class RuntimeNewGameService
    {
    public:
        explicit RuntimeNewGameService(RuntimeNewGameConfig config);
        ~RuntimeNewGameService();
        void tick(bool unreal_initialized);
    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
