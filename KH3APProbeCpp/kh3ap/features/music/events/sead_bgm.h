#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace RC::Unreal
{
    class UObject;
    class UFunction;
} // namespace RC::Unreal

// Game-thread helpers for KH3's SQEX SEAD BGM system. Field and battle music
// is referenced by legacy UE4.17 TAssetPtr properties (m_BGMField/m_BGMBattle
// on ATresWorldSettings and UTresWorldCodeLoadAsset) and played through nine
// named slots owned by the SQEXSEADBGMManager. Slot ownership and override rules
// are documented in docs/MUSIC_DEVELOPMENT.md.
namespace kh3ap::sead_bgm
{
    inline constexpr uint8_t slot_state_invalid = 0;
    inline constexpr uint8_t slot_state_auto_play = 2;

    inline constexpr auto maximum_volume_setting = 10;

    // Every live instance of a class, class default objects excluded. Scans the
    // whole object array, so callers must not do this per frame.
    auto find_live_objects(const wchar_t* class_name) -> std::vector<RC::Unreal::UObject*>;

    // True while a cached pointer still names a live object of that class.
    auto still_live(RC::Unreal::UObject* object, std::wstring_view class_name) -> bool;

    // Reads or rewrites a legacy AssetObjectProperty's object path. Writing
    // clears the cached weak pointer so the next resolve reads the new path.
    auto read_asset_path(RC::Unreal::UObject* object, const wchar_t* property_name)
        -> std::optional<std::wstring>;
    auto write_asset_path(RC::Unreal::UObject* object, const wchar_t* property_name,
                          std::wstring_view path) -> bool;

    // The live, reflected SetPath argument, before SEAD queues its async load.
    auto read_set_path_argument(RC::Unreal::UFunction* function, void* params)
        -> std::optional<std::wstring>;
    auto write_set_path_argument(RC::Unreal::UFunction* function, void* params,
                                 std::wstring_view path) -> bool;

    // The slot controller for a named slot ("Field", "Battle", ...), matched
    // through the manager's SlotSettings_ order.
    auto controller_for_slot(RC::Unreal::UObject* manager, std::wstring_view slot_name)
        -> RC::Unreal::UObject*;

    // Every slot's name and controller in one pass, null where a slot has
    // none.
    struct SlotController
    {
        std::wstring name{};
        RC::Unreal::UObject* controller{};
    };
    auto slot_controllers(RC::Unreal::UObject* manager) -> std::vector<SlotController>;

    // Path of the track a controller's slot currently holds, empty until the
    // asynchronous load has filled it.
    auto current_track(RC::Unreal::UObject* controller) -> std::wstring;

    // ESQEXSEAD_BGMState of the controller's slot, 3 being Playing.
    auto slot_state(RC::Unreal::UObject* controller) -> std::optional<uint8_t>;
    inline constexpr uint8_t slot_state_playing = 3;

    // USQEXSEADBGMSlotController::SetPath. SEAD accepts it on a Ready slot
    // and on a Playing one, where it switches tracks with a short fade.
    auto set_path(RC::Unreal::UObject* controller, std::wstring_view path) -> bool;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    // Emulate an external game request so diagnostic fixtures exercise hooks.
    auto test_request_path(RC::Unreal::UObject* controller, std::wstring_view path) -> bool;
#endif

    // USQEXSEADBGMSlotController::Play and ::Stop.
    auto play(RC::Unreal::UObject* controller) -> bool;
    auto stop(RC::Unreal::UObject* controller) -> bool;

    // USQEXSEADBGMSlotController::SetSection and ::GetSectionIndex: boss
    // tracks advance through sections as a fight changes phase.
    auto set_section(RC::Unreal::UObject* controller, int32_t index) -> bool;
    auto section_index(RC::Unreal::UObject* controller) -> std::optional<int32_t>;

    // USQEXSEADBGMSlotController::SetSeekTime, in seconds.
    auto set_seek_time(RC::Unreal::UObject* controller, float seconds) -> bool;

    // USQEXSEADBGMSlotController::SetVolume: the slot's own gain, 0 to 1,
    // reached over fade_time seconds.
    auto set_volume(RC::Unreal::UObject* controller, float volume, float fade_time) -> bool;

    inline constexpr uint8_t slot_state_suspended = 6;
    inline constexpr uint8_t slot_state_auto_resume = 7;
    inline constexpr uint8_t slot_state_finished = 4;
    inline constexpr uint8_t slot_state_stop = 5;

    // What a slot does when a slot of higher priority starts
    // (ESQEXSEAD_BGMStoreBehaviour): 0 Stop, 1 AutoPlay, 2 Suspend,
    // 3 AutoResume. AutoResume picks the track up where it was.
    inline constexpr uint8_t store_behaviour_auto_resume = 3;

    // The slot's FSQEXSEAD_BGMOptions defaults (USQEXSEADBGMSlot::
    // DefaultOptions_). The options in effect are native and unreadable.
    struct BgmOptions
    {
        uint8_t store_behaviour{};
        float stay_suspend_time{};
    };
    auto default_options(RC::Unreal::UObject* controller) -> std::optional<BgmOptions>;

    // USQEXSEADBGMSlotController::SetStoreBehaviour.
    auto set_store_behaviour(RC::Unreal::UObject* controller, uint8_t behaviour) -> bool;

    // The player's master and music volume settings as stored in
    // UTresGameSettings (SoundMasterVolume, SoundBgmVolume), raw.
    struct VolumeSettings
    {
        uint8_t master{};
        uint8_t bgm{};
    };
    auto read_volume_settings(RC::Unreal::UObject* game_instance) -> std::optional<VolumeSettings>;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto test_volume_settings(RC::Unreal::UObject* game_instance, VolumeSettings volume) -> bool;
    auto test_request_stop(RC::Unreal::UObject* controller) -> bool;
#endif

    // UGameplayStatics::GetCurrentLevelName through any actor of the world.
    auto current_level_name(RC::Unreal::UObject* world_context) -> std::optional<std::wstring>;
} // namespace kh3ap::sead_bgm
