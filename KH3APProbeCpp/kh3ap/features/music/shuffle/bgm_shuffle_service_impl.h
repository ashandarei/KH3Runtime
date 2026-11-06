#include "kh3ap/features/music/events/music_update_events.h"
#pragma once

// Private to the music service: its state and the steps it runs, split
// across bgm_shuffle_service.cpp (level changes and the shuffle),
// bgm_shuffle_custom.cpp (custom music), bgm_shuffle_forms.cpp
// (form themes) and bgm_shuffle_debug.cpp (logs and timings).

#include "kh3ap/features/music/shuffle/bgm_shuffle_service.h"

#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/features/music/shuffle/bgm_shuffle_table.h"
#include "kh3ap/features/music/audio/custom_music_library.h"
#include "kh3ap/features/music/audio/custom_music_player.h"
#include "kh3ap/features/music/now_playing/now_playing_title.h"
#include "kh3ap/features/colors/color_material_ops.h"
#include "kh3ap/features/music/shuffle/form_theme.h"
#include "kh3ap/features/player/keyblades/keyblade_options_helpers.h"
#include "kh3ap/features/music/shuffle/goa_music_patch.h"
#include "kh3ap/features/music/events/sead_bgm.h"
#include "kh3ap/features/music/music_object_registry.h"
#include "kh3ap/features/music/events/sead_music_events.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <optional>
#include <cwctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kh3ap::bgm_shuffle_detail
{
    using namespace RC;
    using namespace RC::Unreal;

    using Clock = std::chrono::steady_clock;

    inline constexpr std::wstring_view play_function_path = STR("/Script/SQEXSEAD.SQEXSEADBGMSlotController:Play");

    // Slots a custom track can replace, each on the player channel of
    // its index: all nine, Field first for its level targets. Event
    // carries cutscenes and form themes.
    inline constexpr std::array<std::wstring_view, 9> custom_slots{
            STR("Field"), STR("Battle"), STR("Mission"), STR("Boss"), STR("MiniGame"),
            STR("Attraction"), STR("link"), STR("Menu"), STR("Event")};
    inline constexpr size_t event_custom_channel = 8;
    inline constexpr std::wstring_view garden_level = STR("goa_01");
    // Custom tracks' gain while a menu is open (by ear: the game's own
    // ducking is not read).
    inline constexpr float menu_duck_gain = 0.5f;
    // How long a slot takes to fade out under a custom track, and back.
    inline constexpr float slot_mute_fade = 0.5f;
    // Top of the game's volume sliders, as stored in UTresGameSettings
    // (confirmed in game: 0 to 10).
    inline constexpr float volume_setting_max = 10.0f;
    // How often the service's own cost is checked (diagnostics).
    inline constexpr auto perf_report_interval = std::chrono::seconds(5);
    // How long a form theme the game suspended waits, in play, before it
    // is resumed.
    inline constexpr auto theme_resume_delay = std::chrono::milliseconds(1500);
    // How long after a form theme starts or ends the slots are logged.
    inline constexpr auto slot_report_delay = std::chrono::seconds(1);
    // A playing slot that keeps its original after a SetPath is retried
    // at this pace instead of every level check.
    inline constexpr auto playing_retry_interval = std::chrono::seconds(5);

    inline constexpr std::array<std::wstring_view, 2> swapped_slots{STR("Field"), STR("Battle")};
    // Form themes play on the cutscene slot, which suspends every other
    // one: SEAD resumes the interrupted track where it was when the
    // theme stops, as it resumes Field after a battle.
    inline constexpr std::wstring_view form_theme_slot = STR("Event");
    // Where group tracks (attractions, Links, minigames, data battles,
    // Battlegates) may start: every slot but Event, whose cutscene music
    // stays in time with the picture.
    inline constexpr std::array<std::wstring_view, 8> group_slots{
            STR("Attraction"), STR("link"), STR("MiniGame"), STR("Menu"),
            STR("Mission"), STR("Boss"), STR("Battle"), STR("Field")};
    // ESQEXSEAD_BGMState values after which the game sets the slot's next
    // track itself: Invalid, Ready, Finished, Stop.
    inline constexpr std::array<uint8_t, 4> slot_idle_states{0, 1, 4, 5};

    inline constexpr std::array<const wchar_t*, 2> bgm_properties{STR("m_BGMField"), STR("m_BGMBattle")};

    auto read_character_state(UObject* pawn) -> std::optional<uint8_t>;

    // Class of the first weapon Sora holds (MyEquipment -> m_WeaponEquips).
    auto held_weapon_class(UObject* pawn) -> std::wstring;

    auto lower(std::wstring text) -> std::wstring;

    // The shuffle world a track belongs to, or empty when none does.
    auto world_code_of(std::wstring_view track) -> std::wstring_view;

    // "he_02" -> "/he_02.he_02:", the path fragment of objects in the
    // level's persistent level, lowercased.
    auto persistent_level_package(std::wstring_view level) -> std::wstring;

    // "ts_01" -> "TresWorldLoadTS", the world load asset of a level.
    auto load_asset_name_for_level(std::wstring_view level) -> std::wstring;
}

namespace kh3ap
{
    using namespace bgm_shuffle_detail;

    struct RuntimeBgmShuffleService::Impl
    {
        explicit Impl(RuntimeBgmShuffleConfig shuffle_config)
            : config(std::move(shuffle_config))
        {
        }

        ~Impl()
        {
            uninstall_sead_music_events();
            if (hook_installed && play_function && is_uobject_valid_fast(play_function))
            {
                UObjectGlobals::UnregisterHook(play_function, play_hook_ids);
            }
            if (auto* function = set_path_function.Get())
                UObjectGlobals::UnregisterHook(static_cast<UFunction*>(function), set_path_hook_ids);
        }

        // Diagnostics for a slowdown reported in play: how long each step of
        // the service takes, logged every few seconds when a frame's share
        // gets noticeable.
        enum class PerfStep : size_t
        {
            Frame, Table, LogChanges, LevelChange, FormTheme, SwapOriginals, SwapGroups, CustomSlots, PlayHook, Count
        };
        struct PerfStats
        {
            std::array<Clock::duration, static_cast<size_t>(PerfStep::Count)> total{};
            std::array<Clock::duration, static_cast<size_t>(PerfStep::Count)> worst{};
            std::array<size_t, static_cast<size_t>(PerfStep::Count)> calls{};
            size_t frames{};
            Clock::time_point since{Clock::now()};
        };

        class PerfScope
        {
        public:
            PerfScope(PerfStats& stats, PerfStep step) : stats_(stats), step_(static_cast<size_t>(step)) {}
            ~PerfScope()
            {
                const auto spent = Clock::now() - start_;
                stats_.total[step_] += spent;
                stats_.worst[step_] = (std::max)(stats_.worst[step_], spent);
                ++stats_.calls[step_];
            }
            PerfScope(const PerfScope&) = delete;
            auto operator=(const PerfScope&) -> PerfScope& = delete;

        private:
            PerfStats& stats_;
            size_t step_;
            Clock::time_point start_{Clock::now()};
        };

        auto report_perf() -> void;

        auto tick() -> void;

        auto tick_steps() -> void;
        auto update_now_playing() -> void;
        std::wstring now_playing{};

        // Custom tracks bypass the game's mixer, so they follow its master
        // and music sliders here.
        auto follow_volume_settings() -> void;

        // The game lowers its music while a menu is open; custom tracks
        // follow: the pause menu through the blocking UI state, the others
        // because they block gameplay.
        auto duck_custom_music(bool menu_open) -> void;

        auto apply_custom_volume() -> void;

        // Read the configured folder and manifest once on the first runtime
        // tick, before gameplay. File/folder edits apply on the next launch.
        auto initialize_custom_music() -> void;

        // Adds a level to TARGETS.txt the first time the player enters it.
        auto note_visited_level(const std::wstring& level) -> void;

        // A track plays where the game put it, not as a replacement this
        // service set there: overrides (custom files, vanilla entries) only
        // apply at home.
        auto at_home(std::wstring_view slot_name, const std::wstring& track) const -> bool;

        // The target that overrides a track, if any.
        auto track_target(const std::wstring& track) const -> std::wstring;

        // The player's override for a track (a file or a vanilla entry): the
        // shuffle leaves it alone.
        auto overridden(const std::wstring& track) const -> bool;

        // The target that overrides the current level's field music: the
        // level ("he_03"), else its world's exploration music
        // ("he_exploration"), else the whole world ("he", or by name
        // "Olympus"); empty when none does.
        auto level_target() const -> std::wstring;

        // Resolve field level/track/world targets together. Borrowed shuffle
        // tracks do not acquire the overrides of their source world.
        auto slot_target(std::wstring_view slot_name, const std::wstring& track) const -> std::wstring;

        // The current level's field music is overridden.
        auto level_overridden() const -> bool;

        // What replaces a track that plays at home: a custom track named
        // after it wins, then the table's own entry when Chaos drew a custom
        // track.
        auto custom_for_track(std::wstring_view slot_name, const std::wstring& track) const -> const CustomMusicTrack*;

        // The custom track Chaos drew for an original, if any.
        auto drawn_custom_for(const std::wstring& original) const -> const CustomMusicTrack*;

        // The form Sora is in has a vanilla entry: no theme for it.
        auto form_kept_vanilla() const -> bool;

        // A custom file heard now where the game plays the music it replaces
        // outranks form themes; empty when none. A vanilla entry only keeps
        // the music out of the shuffle: forms still play over it (user
        // order: custom files, then forms, then the shuffle).
        struct PlayingOverride
        {
            std::wstring slot{};
            std::wstring track{};
        };

        auto override_playing() -> PlayingOverride;

        // A custom file for the form Sora is in, Anti Form's first.
        auto form_custom_music() const -> const CustomMusicTrack*;

        // Some custom file is bound to a form: forms are followed even with
        // Form Themes off.
        auto any_form_custom_music() const -> bool;

        // Each tick, in every mode: a slot playing a track with a custom
        // file is faded out and the file plays instead. A slot holds its
        // custom track while suspended, as SEAD holds its own.
        auto update_custom_slots() -> void;
        auto release_custom_slot(size_t index) -> void;
        auto stop_custom_music() -> void;

        // Every change of a slot's state or track, on all nine slots and in
        // every mode, cutscenes included: which music plays where.
        auto log_music_changes() -> void;

        // A keyblade or Anti Form theme is on the slots.
        auto form_theme_playing() const -> bool;

        auto form_themes_on() const -> bool;

        // While a themed form runs, its theme plays on the Event slot. A
        // cutscene already on Event keeps it, and one that takes it during
        // the form is left alone when the form ends.
        auto update_form_theme(Clock::time_point now) -> void;
        auto stop_form_theme() -> bool;

        // A slot interrupted by the form theme must resume, not stop (a Boss
        // slot stops by default): every playing slot is set to AutoResume
        // until the form ends.
        auto hold_playing_slots() -> void;

        // Back to each held slot's default store behaviour.
        auto release_held_slots() -> void;

        // Diagnostics: every slot's state and track, a moment after a form
        // theme starts or ends.
        auto report_slots(Clock::time_point now) -> void;

        // Shuffling, or still holding rewrites to undo after going back to
        // Vanilla.
        auto active() const -> bool;

        auto refresh_table() -> void;

        // The SEAD track replacing an original. A custom track drawn by Chaos
        // is played by the custom player instead, over the original, and an
        // overridden original is not shuffled.
        auto replacement_for(const std::wstring& original) const -> const std::wstring*;

        auto install_hook() -> void;
        auto intercept_requested_track(UObject* controller, const std::wstring& requested) -> std::optional<std::wstring>;
        auto note_slot_restart(UObject* controller) -> void;
        auto remember_slot_rewrite(std::wstring_view slot, UObject* controller,
                                   const std::wstring& original, const std::wstring& replacement,
                                   const std::wstring& previous) -> void;

        // Keeps a live TresWorldSettings as the world context for the level
        // name, preferring the persistent level's own, which outlives the
        // streamed sublevels. Returns true when the cached one was replaced.
        // The level's WorldSettings and every live world load asset, found in
        // one scan of the object array; kept for the level change that
        // follows, then dropped.
        auto refresh_world_context() -> bool;

        auto report_waiting(std::wstring_view reason) -> void;

        auto bgm_manager() -> UObject*;

        struct LiveSlot
        {
            std::wstring name;
            FWeakObjectPtr weak_controller;
            auto controller() const -> UObject*
            {
                auto* object = weak_controller.Get();
                return is_uobject_valid_fast(object) ? object : nullptr;
            }
        };

        // Nested operations (including synchronous Play/SetPath hooks) share
        // this update's mapping. Outside an update, an operation gets a fresh
        // mapping. State/track values remain live reads after each mutation.
        class SlotMappingScope
        {
        public:
            explicit SlotMappingScope(Impl& owner) : owner_(owner), previous_(owner.slot_mapping_)
            {
                if (!previous_)
                {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                    ++owner.slot_mapping_reads;
#endif
                    auto slots = sead_bgm::slot_controllers(owner.bgm_manager());
                    slots_.reserve(slots.size());
                    for (auto& slot : slots)
                        slots_.push_back({std::move(slot.name), make_uobject_weak(slot.controller)});
                }
                owner_.slot_mapping_ = this;
            }
            ~SlotMappingScope() { owner_.slot_mapping_ = previous_; }
            SlotMappingScope(const SlotMappingScope&) = delete;
            auto operator=(const SlotMappingScope&) -> SlotMappingScope& = delete;
            auto entries() const -> const std::vector<LiveSlot>&
            { return previous_ ? previous_->entries() : slots_; }
            auto find(std::wstring_view name) const -> UObject*
            {
                for (const auto& slot : entries())
                    if (slot.name.size() == name.size() &&
                        std::equal(slot.name.begin(), slot.name.end(), name.begin(), [](wchar_t a, wchar_t b) {
                            return std::towlower(a) == std::towlower(b);
                        })) return slot.controller();
                return nullptr;
            }
        private:
            Impl& owner_;
            const SlotMappingScope* previous_;
            std::vector<LiveSlot> slots_;
        };
        const SlotMappingScope* slot_mapping_{}; // Only points into the current call stack.
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        uint64_t music_updates{}, slot_mapping_reads{}, custom_library_loads{}, native_event_updates{};
        uint32_t last_native_events{};
#endif

        // The value an object held before the shuffle touched it. A value
        // equal to what the shuffle wrote is ours; anything else means the
        // object was reloaded, and is recorded as the new original.
        auto original_value(UObject* object, const wchar_t* property) -> std::wstring;

        // Writes the replacement of the original, or puts the original back
        // when the table no longer maps a value this service wrote.
        auto rewrite(UObject* object, const wchar_t* property, const std::wstring& original) -> bool;

        auto swap_slot(std::wstring_view slot_name, const std::wstring& original) -> std::wstring_view;

        // Once per level load. The level's originals come from its own
        // WorldSettings when set (the Garden of Assemblage, some areas),
        // otherwise from its world's load asset. Every live WorldSettings and
        // load asset is rewritten, so later areas of the same world load the
        // replacements natively.
        auto apply_on_level_change(std::wstring_view reason) -> void;

        // Safety net for a slot that started an original the level check did
        // not redirect, such as an area whose track the game assigned after
        // the check ran. SEAD switches a playing slot with a short fade.
        // Only the current world's own tracks count as originals: every
        // track is also some other world's replacement, and a replacement
        // always comes from another world, so it is never switched again.
        auto swap_playing_originals(Clock::time_point now) -> void;

        // Group tracks, and world tracks on the other slots (a scripted
        // fight can play a world's battle theme on Mission): once a slot
        // plays one, it switches to the replacement. Every such track is
        // also another's replacement, so the one this service set stays
        // until the slot goes idle and the game sets the next track. On
        // Field and Battle, which the level change handles, a world track's
        // replacement (Chaos draws group tracks there) and a form theme are
        // left alone. Every track a slot starts is logged.
        auto swap_group_tracks(Clock::time_point now) -> void;

        RuntimeBgmShuffleConfig config;
        PerfStats perf{};
        std::unordered_map<std::wstring, std::wstring> swaps{};
        bool table_built{};
        int32_t table_mode{};
        bool table_include_custom_music{};
        std::wstring table_identity{};
        std::wstring table_pools{};
        bool events_initialized{};
        bool follow_form_events{};
        uint32_t deferred_native_events{}, deferred_updates{};
        std::optional<Clock::time_point> form_deadline{};
        bool gameplay_active{};

        bool hook_installed{};
        UFunction* play_function{};
        std::pair<int, int> play_hook_ids{};
        FWeakObjectPtr set_path_function{};
        std::pair<int, int> set_path_hook_ids{};
        struct PendingMusicRequest
        {
            void* frame{};
            FWeakObjectPtr controller{};
            std::wstring replacement{};
        };
        std::vector<PendingMusicRequest> pending_music_requests{};

        FWeakObjectPtr manager{};
        FWeakObjectPtr world_context{};
        std::wstring applied_level{};
        bool level_needs_apply{true};
        // Readiness retries exist only for observed objects still in PostLoad.
        std::optional<Clock::time_point> object_ready_at{};
        // The shuffle world of the applied level; empty outside the shuffle.
        std::wstring_view level_world{};
        std::wstring waiting_reason{};
        // GoA's standalone Blueprint sound is suppressed before BeginPlay.
        // Its Field slot owns every mode, including Vanilla. Remember an
        // asynchronous SetPath so unrelated object events do not queue it again.
        FWeakObjectPtr garden_field_controller{};
        std::wstring garden_field_target{};
        CustomMusicLibrary custom_library{};
        FormThemeTracker form_tracker{};
        bool form_observation_dirty{true};
        std::optional<std::wstring> form_requested_track{};
        // Slot name -> the track it played before the form theme.
        // The form theme was started on the Event slot.
        bool form_on_event{};
        FWeakObjectPtr form_controller{};
        // The override last logged holding a form theme back.
        std::wstring override_seen{};
        // Slots set to AutoResume for the form, with their default store
        // behaviour when it could be read.
        struct HeldSlot
        {
            FWeakObjectPtr controller{};
            uint8_t behaviour{};
        };
        std::vector<HeldSlot> held_slots{};
        std::optional<std::pair<Clock::time_point, std::wstring_view>> slot_report{};
        std::wstring form_track{};
        // Since when the game has held the form theme suspended.
        std::optional<Clock::time_point> theme_suspended_at{};
        bool anti_form{};
        std::vector<std::wstring> visited_levels{};
        std::filesystem::path custom_directory{};
        bool custom_music_initialized{};
        // Slots this service faded out under a custom track.
        std::array<bool, custom_slots.size()> slot_muted{};
        std::array<FWeakObjectPtr, custom_slots.size()> muted_controllers{};
        // Each slot's state and track at the last check, to see it restart.
        std::array<uint8_t, custom_slots.size()> muted_state{};
        std::array<std::wstring, custom_slots.size()> muted_track{};
        RuntimeCustomMusicPlayer custom_player{};
        UObject* game_instance{};
        // Impossible values, so the first read always applies.
        sead_bgm::VolumeSettings last_volume{0xFF, 0xFF};
        // Custom tracks' gain from the game's master and music sliders.
        float custom_gain{1.0f};
        bool custom_ducked{};
        std::unordered_map<std::wstring, std::wstring> originals{};
        std::unordered_map<std::wstring, std::wstring> written{};
        // Slot name -> the track this service last set on it; empty once the
        // game or a restore has taken the slot back.
        std::unordered_map<std::wstring, std::wstring> slot_written{};
        // Preserve each group's original for mode changes, including while
        // the slot is suspended under a form theme. Weak ownership prevents
        // a new controller from inheriting a departing level's rewrite.
        struct GroupRewrite
        {
            FWeakObjectPtr controller{};
            std::wstring original{};
            std::wstring replacement{};
            std::wstring pending_from{};
            Clock::time_point queued_until{};
            Clock::time_point finished_retry_at{};
            size_t finished_restarts{};
        };
        std::unordered_map<std::wstring, GroupRewrite> group_written{};
        std::unordered_map<UObject*, Clock::time_point> group_retry_at{};
        // Slot name -> the last track logged playing there.
        struct LoggedSlot
        {
            std::optional<uint8_t> state{};
            std::wstring track{};
        };
        // Slot name -> what log_music_changes last reported.
        std::unordered_map<std::wstring, LoggedSlot> slot_logged{};
        // What the table puts in place of world tracks.
        std::unordered_set<std::wstring> world_replacements{};
        std::unordered_map<UObject*, Clock::time_point> playing_retry_at{};
    };
}
