#pragma once

#include <Unreal/Property/FMulticastDelegateProperty.hpp>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace RC::Unreal
{
    class AActor;
    class FProperty;
    class UClass;
    class UFunction;
    class UObject;
} // namespace RC::Unreal

namespace kh3ap::enemy_clear_detail
{
    using namespace RC::Unreal;
    constexpr auto photo_hold_timeout = std::chrono::seconds(120);
    constexpr auto combo_press_interval = std::chrono::milliseconds(50);
    constexpr auto opening_enemy_discovery_delay = std::chrono::milliseconds(100);
    constexpr auto enemy_clear_button_hold_duration = std::chrono::milliseconds(100);
    constexpr auto battlegate_readiness_interval = std::chrono::milliseconds(100);
    constexpr auto enemy_clear_update_interval = std::chrono::milliseconds(100);
    constexpr auto battlegate_notice_interval = std::chrono::seconds(1);
    constexpr auto damage_pulse_observation_delay = std::chrono::seconds(1);
    constexpr auto max_enemy_command_parameter_bytes = 4096;
    constexpr auto enemy_clear_request_history_limit = 128;
    constexpr auto max_enemy_command_world_coordinate = 10000000;
    constexpr auto max_enemy_inspection_values = 128;
    constexpr auto maximum_camera_fov_degrees = 180;
    constexpr auto max_inspected_scene_objects = 512;
    constexpr auto container_interaction_radius = 600.0;
    constexpr auto chest_interaction_radius = 350.0;
    constexpr auto minimum_pooh_button_hold_ms = 50;
    constexpr auto maximum_pooh_button_hold_ms = 3000;
    constexpr auto minimum_stick_hold_ms = 50;
    constexpr auto maximum_stick_hold_ms = 2000;
    constexpr auto maximum_shotlock_button_hold_ms = 10000;
    constexpr auto maximum_center_duration_seconds = 600;
    constexpr auto max_enemy_request_coordinate = 10000000;
    constexpr auto max_attack_combo_count = 32;
    constexpr auto minimum_attack_combo_interval_ms = 120;
    constexpr auto maximum_attack_combo_interval_ms = 500;
    constexpr auto emblem_cover_interaction_radius = 2500.0;
    using Clock = std::chrono::steady_clock;
    constexpr auto result_name = L"runtime_enemy_clear_result.json";
    constexpr auto events_name = L"runtime_enemy_clear_events.jsonl";
    constexpr size_t max_targets = 512;
    constexpr auto test_material_path =
        L"/Game/Maps/dw/gimmick/g_dw_TreasureBox_l/mdl/MI_dw_TreasureBox_l.MI_dw_TreasureBox_l";
    constexpr auto chest_notice_class_path =
        L"/Game/Mods/KH3APDiagnostics/ChestNotice.ChestNotice_C";

    int64_t unix_ms();
    std::string utf8(const wchar_t* name);
    UClass* chest_notice_class();
    uint8_t reactor_command(UObject* reactor, const uint8_t* value = nullptr);
    FMulticastScriptDelegate* chest_delegate(UObject* reactor);
    UObject* test_material();
    UObject* validated_test_material();
    std::array<float, 4> chest_hsb(UObject* material, const std::array<float, 4>* color = nullptr);
    UObject* chest_diffuse(UObject* material);
    bool keyblade_inventory(UObject* pawn, const std::wstring& token, bool grant = false,
                            bool* dispatched = nullptr);
    UObject* material_zero(UObject* mesh);
    void set_material_zero(UObject* mesh, UObject* material);
    int64_t integer(UObject* object, const wchar_t* name);
    bool boolean(UObject* object, const wchar_t* name);
    std::array<float, 3> position(UObject* object);
    std::array<float, 3> rotation(UObject* object);
    int32_t property_int(UObject* object, const wchar_t* name);
    std::wstring property_name(UObject* object, const wchar_t* name);
    bool property_bool(UObject* object, const wchar_t* name, bool fallback);
    UObject* actor_world(UObject* actor);
    void json_position(std::ostream& out, const std::array<float, 3>& p);
    std::vector<AActor*> actors_of_class(UObject* pawn, UClass* klass);

    struct Call
    {
        UObject* object;
        UFunction* function;
        std::vector<std::byte> params;
        Call(UObject* target, const wchar_t* name);
        FProperty* field(const wchar_t* name);
        void invoke(bool suppress_grant_observers = true);
    };
} // namespace kh3ap::enemy_clear_detail
