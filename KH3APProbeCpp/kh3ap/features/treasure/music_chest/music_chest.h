#pragma once

#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FMulticastDelegateProperty.hpp>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kh3ap::music_chest
{
    using Vec = std::array<float, 3>;
    using Clock = std::chrono::steady_clock;

    struct Call
    {
        RC::Unreal::UObject* obj;
        RC::Unreal::UFunction* fn;
        std::vector<std::byte> data;

        Call(RC::Unreal::UObject* object, const wchar_t* name);
        ~Call();
        RC::Unreal::FProperty* field(const wchar_t* name);
        void run();
        void boolean(const wchar_t* name, bool value);
    };

    int64_t unix_ms();
    Vec position(RC::Unreal::UObject* object);
    RC::Unreal::UObject* world(RC::Unreal::UObject* actor);
    RC::Unreal::UObject* owning_world(RC::Unreal::UObject* actor);
    RC::Unreal::UObject* loaded(const wchar_t* path);
    uint8_t reactor_command(RC::Unreal::UObject* reactor, const uint8_t* value = nullptr);
    RC::Unreal::FMulticastScriptDelegate* chest_delegate(RC::Unreal::UObject* reactor);
    bool active_chest(RC::Unreal::UObject* chest);

    inline constexpr auto notice_path = L"/Game/Mods/KH3AP/MusicChestNotice.MusicChestNotice_C";
    inline constexpr auto paint_path =
        L"/Game/Mods/KH3APDiagnostics/MoMBarrier/M_NativeMusicChest.M_NativeMusicChest";
    inline constexpr auto spawned_class_path =
        L"/Game/Mods/KH3APDiagnostics/MoMBarrier/A_MoMNativeChest.A_MoMNativeChest_C";
    inline constexpr auto spawned_mesh_path =
        L"/Game/Maps/ra/gimmick/g_ra_TreasureBox_l/mdl/g_ra_TreasureBox_l.g_ra_TreasureBox_l";
    inline constexpr auto spawned_diffuse_path =
        L"/Game/Maps/ra/gimmick/g_ra_TreasureBox_l/mdl/T_ra_TreasureBox_l_D.T_ra_TreasureBox_l_D";
    inline constexpr auto spawned_animation_path =
        L"/Game/Character/gimmick/g_dw_TreasureBox_l/anm/rt_dw/body/002.002";
    inline constexpr auto music_gate_notice_interval = std::chrono::seconds(1);
    inline constexpr auto music_chest_asset_retry_interval = std::chrono::seconds(2);
    inline constexpr auto music_gate_interaction_radius = 600;
} // namespace kh3ap::music_chest
