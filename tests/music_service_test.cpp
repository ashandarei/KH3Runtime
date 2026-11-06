#include "music_service_under_test.h"
#include "kh3ap/features/music/shuffle/music_request_bytecode.h"

#include <cassert>
#include <fstream>
#include <iostream>

namespace kh3ap
{
    // XAudio2 boundary: false means decoding is pending or playback failed.
    struct RuntimeCustomMusicPlayer::Impl
    {
        std::array<std::filesystem::path, channel_count> files{}, pending{};
    };
    RuntimeCustomMusicPlayer::RuntimeCustomMusicPlayer() : impl_(std::make_unique<Impl>()) {}
    RuntimeCustomMusicPlayer::~RuntimeCustomMusicPlayer() = default;
    auto RuntimeCustomMusicPlayer::play(size_t index, const CustomMusicTrack& track) -> bool
    {
        const auto found = fake_runtime::playable_files.find(track.file.wstring());
        if (found != fake_runtime::playable_files.end() && !found->second)
        {
            impl_->files[index].clear();
            impl_->pending[index] = track.file;
            return false;
        }
        impl_->pending[index].clear();
        impl_->files[index] = track.file;
        return true;
    }
    auto RuntimeCustomMusicPlayer::hold(size_t) -> void {}
    auto RuntimeCustomMusicPlayer::stop(size_t index) -> void
    {
        impl_->files[index].clear();
        impl_->pending[index].clear();
    }
    auto RuntimeCustomMusicPlayer::stop_all() -> void { for (size_t index = 0; index < channel_count; ++index) stop(index); }
    auto RuntimeCustomMusicPlayer::file(size_t index) const -> const std::filesystem::path& { return impl_->files[index]; }
    auto RuntimeCustomMusicPlayer::active() const -> bool
    {
        for (size_t index = 0; index < channel_count; ++index)
            if (!impl_->files[index].empty() || !impl_->pending[index].empty()) return true;
        return false;
    }
    auto RuntimeCustomMusicPlayer::set_volume(float) -> void {}
    auto RuntimeCustomMusicPlayer::update() -> void {}
    auto custom_music_directory() -> std::filesystem::path
    { ++fake_runtime::music_directory_reads; return fake_runtime::music_folder; }

    auto TestBgmShuffleService::Impl::install_hook() -> void { hook_installed = true; }
    auto TestBgmShuffleService::Impl::report_perf() -> void
    {
    }
}
namespace kh3ap::bgm_shuffle_detail
{
    auto read_character_state(UObject* pawn) -> std::optional<uint8_t> { return pawn->character_state; }
    auto held_weapon_class(UObject* pawn) -> std::wstring { return pawn->weapon; }
}

using namespace kh3ap;

struct Fixture
{
    bool gameplay{true}, forms{}, menu{}, include_custom{true};
    int mode{};
    UObject sora{}, field{}, boss{}, event{};
    std::unique_ptr<TestBgmShuffleService::Impl> service;

    Fixture()
    {
        music_update_events.store(static_cast<uint32_t>(MusicUpdate::Startup));
        fake_runtime::manager = {};
        fake_runtime::settings = {};
        fake_runtime::settings.path = L"/Game/Maps/he_01.he_01:PersistentLevel.WorldSettings";
        fake_runtime::settings.assets[L"m_BGMField"] = bgm_shuffle_worlds()[0].fields[0];
        fake_runtime::settings.assets[L"m_BGMBattle"] = bgm_shuffle_worlds()[0].battles[0];
        fake_runtime::world_settings = {&fake_runtime::settings};
        fake_runtime::load_assets.clear();
        fake_runtime::playable_files.clear();
        field.track = bgm_shuffle_worlds()[0].fields[0];
        field.serial = boss.serial = event.serial = 0;
        boss.track = bgm_shuffle_groups().back().tracks[0];
        event.state = sead_bgm::slot_state_stop;
        fake_runtime::slots = {{L"Field", &field}, {L"Boss", &boss}, {L"Event", &event}};
        service = std::make_unique<TestBgmShuffleService::Impl>(
            RuntimeBgmShuffleConfig{[this]
                                    {
                                        return mode;
                                    },
                                    []
                                    {
                                        return L"regression_seed";
                                    },
                                    [this]
                                    {
                                        return gameplay;
                                    },
                                    [this]
                                    {
                                        return forms;
                                    },
                                    [this]
                                    {
                                        return &sora;
                                    },
                                    [this]
                                    {
                                        return menu;
                                    },
                                    [this]
                                    {
                                        return include_custom;
                                    }});
        service->custom_music_initialized = true; // Most scenarios inject their library directly.
        service->world_context = &fake_runtime::settings;
        service->applied_level = L"he_01";
        service->level_world = L"HE";
        service->level_needs_apply = false;
    }

    void add(std::wstring target, std::wstring file)
    {
        service->custom_library.tracks[file].file = file;
        service->custom_library.targets[std::move(target)] = file;
    }
};

static void test_override_priority()
{
    Fixture f;
    auto& s = *f.service;
    f.add(L"he", L"world.ogg");
    s.custom_library.vanilla_targets.insert(L"bgm_he_field");
    s.update_custom_slots();
    assert(s.custom_player.file(0).empty() && f.field.volume == 1.0f);
    assert(s.override_playing().track.empty());
    s.custom_library.vanilla_targets.clear();
    f.add(L"bgm_he_field", L"specific.ogg");
    s.update_custom_slots();
    assert(s.custom_player.file(0) == L"specific.ogg");
    s.custom_library.vanilla_targets.insert(L"he_01");
    s.update_custom_slots();
    assert(s.custom_player.file(0).empty() && f.field.volume == 1.0f);
    assert(s.override_playing().track.empty());
    s.custom_library.vanilla_targets.clear();
    f.add(L"he_01", L"level.ogg");
    s.update_custom_slots();
    assert(s.custom_player.file(0) == L"level.ogg");

    // An override for a borrowed shuffle track must not affect its recipient.
    s.custom_library = {};
    f.add(L"bgm_ts_field", L"toy_box.ogg");
    f.field.track = bgm_shuffle_worlds()[2].fields[0];
    s.slot_written[L"Field"] = f.field.track;
    s.update_custom_slots();
    assert(s.custom_player.file(0).empty());
}

static void test_failed_or_loading_audio()
{
    Fixture f;
    auto& s = *f.service;
    f.add(L"bgm_he_field", L"pending.ogg");
    fake_runtime::playable_files[L"pending.ogg"] = false;
    s.update_custom_slots();
    s.update_custom_slots();
    assert(s.custom_player.file(0).empty() && f.field.volume == 1.0f);
    assert(s.custom_player.active()); // A pending decode must not be cancelled.
    fake_runtime::playable_files[L"pending.ogg"] = true;
    s.update_custom_slots();
    assert(s.custom_player.file(0) == L"pending.ogg" && f.field.volume == 0.0f);
    f.add(L"bgm_he_field", L"broken.ogg");
    fake_runtime::playable_files[L"broken.ogg"] = false;
    s.update_custom_slots();
    assert(s.custom_player.file(0).empty() && f.field.volume == 1.0f);

    // Custom form files must also leave the native theme audible on failure.
    s.stop_custom_music();
    s.custom_library = {};
    f.forms = true;
    f.add(L"form_herosorigin", L"broken.ogg");
    s.form_observation_dirty = true; s.update_form_theme(Clock::now());
    assert(s.form_custom_music());
    s.update_custom_slots();
    assert(f.event.volume == 1.0f);

    fake_runtime::playable_files[L"broken.ogg"] = true;
    s.update_custom_slots();
    assert(f.event.volume == 0.0f);
    // Switching directly to another form uses SetPath without a new Play.
    // Its native theme must recover the volume owned by the previous file.
    f.sora.keyblade = L"WEP_KEYBLADE03";
    f.sora.weapon = L"w_so091_C";
    s.form_observation_dirty = true; s.update_form_theme(Clock::now() + std::chrono::seconds(1));
    s.update_custom_slots();
    assert(s.form_on_event && !s.form_custom_music());
    assert(f.event.volume == 1.0f && s.custom_player.file(event_custom_channel).empty());
}

static void test_channel_cleanup()
{
    Fixture f;
    auto& s = *f.service;
    f.add(L"bgm_he_field", L"field.ogg");
    s.update_custom_slots();
    fake_runtime::slots[0].controller = nullptr;
    s.update_custom_slots();
    assert(s.custom_player.file(0).empty() && f.field.volume == 1.0f);

    fake_runtime::slots[0].controller = &f.field;
    s.update_custom_slots();
    ++f.field.serial;
    f.field.volume = 0.35f;
    s.release_custom_slot(0);
    assert(f.field.volume == 0.35f); // Do not restore volume on a reused object.

    request_music_update(MusicUpdate::Context);
    s.tick_steps();
    assert(!s.custom_player.file(0).empty());
    f.menu = true;
    request_music_update(MusicUpdate::Context);
    s.tick_steps();
    assert(!s.custom_player.file(0).empty()); // Ordinary pause menus only duck.
    f.gameplay = false;
    request_music_update(MusicUpdate::Context);
    s.tick_steps();
    assert(!s.custom_player.active() && f.field.volume == 1.0f);
    f.gameplay = true;
    f.menu = false;
    request_music_update(MusicUpdate::Context);
    s.tick_steps();
    assert(!s.custom_player.file(0).empty());
}

static void test_garden_removal()
{
    Fixture f;
    auto& s = *f.service;
    s.applied_level = L"goa_01";
    s.level_world = L"GOA";
    f.field.track = runtime_goa_music_path;
    f.field.state = sead_bgm::slot_state_playing;
    f.add(L"goa_01", L"garden.ogg");
    s.update_custom_slots();
    assert(s.custom_player.file(0) == L"garden.ogg");
    assert(f.field.volume == 0.0f);
    for (size_t i = 1; i < RuntimeCustomMusicPlayer::channel_count; ++i)
        assert(s.custom_player.file(i).empty()); // No independent Garden player.
    s.custom_library = {};
    assert(s.active()); // Cleanup must run after removal of the last file.
    s.update_custom_slots();
    assert(!s.custom_player.active() && f.field.volume == 1.0f);
    assert(s.active()); // Vanilla still has a managed Garden Field slot.
    f.add(L"bgm_field_rg03", L"garden.ogg");
    s.custom_library.vanilla_targets.insert(L"goa_01");
    s.update_custom_slots();
    assert(!s.custom_player.active() && f.field.volume == 1.0f);
}

static void test_garden_cold_entry_and_mode_changes()
{
    Fixture f;
    auto& s = *f.service;
    f.mode = 1;
    f.sora.level = fake_runtime::settings.level = L"GoA_01";
    fake_runtime::settings.path = L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01:PersistentLevel.WorldSettings";
    fake_runtime::settings.assets[L"m_BGMField"] = runtime_goa_music_path;
    fake_runtime::settings.assets[L"m_BGMBattle"] = runtime_goa_music_path;
    f.field.track.clear();
    f.field.state = 1;
    f.field.defer_path = true; // The replacement is not loaded on the first visit.
    s.refresh_table();
    const std::wstring target = *s.replacement_for(std::wstring{runtime_goa_music_path});
    s.apply_on_level_change(L"cold_garden");
    assert(f.field.pending_track == target && f.field.path_writes == 1);
    assert(f.field.state == sead_bgm::slot_state_playing);
    assert(!s.custom_player.active());
    request_music_update(MusicUpdate::Objects);
    s.tick_steps();
    assert(f.field.path_writes == 1); // Pending load is not queued on each object event.

    f.field.track = target;
    f.field.pending_track.clear();
    f.field.defer_path = false;
    f.field.state = 1;
    f.field.track.clear(); // A late BeginPlay Reset after the initial route.
    test_music_mailbox.post(SeadMusicEvent::Reset);
    s.tick_steps();
    assert(f.field.track == target && f.field.path_writes == 2);
    assert(f.field.state == sead_bgm::slot_state_playing);
    f.field.state = sead_bgm::slot_state_suspended; // A Link/Boss owns priority.
    f.mode = 0;
    s.refresh_table();
    s.apply_on_level_change(L"garden_vanilla");
    assert(f.field.track == target && f.field.path_writes == 2 && s.level_needs_apply);
    assert(f.field.state == sead_bgm::slot_state_suspended);
    f.field.state = sead_bgm::slot_state_playing;
    test_music_mailbox.post(SeadMusicEvent::AutoResume);
    s.tick_steps();
    assert(f.field.track == runtime_goa_music_path && f.field.path_writes == 3);
    f.field.state = sead_bgm::slot_state_suspended;
    f.mode = 1;
    s.refresh_table();
    s.apply_on_level_change(L"garden_shuffle");
    assert(f.field.track == runtime_goa_music_path && f.field.path_writes == 3);
    assert(f.field.state == sead_bgm::slot_state_suspended);
    f.field.state = sead_bgm::slot_state_playing;
    test_music_mailbox.post(SeadMusicEvent::AutoResume);
    s.tick_steps();
    assert(f.field.track == target && f.field.path_writes == 4);

    // Reloading the same map can replace its native controllers.
    UObject replacement_field;
    fake_runtime::slots[0].controller = &replacement_field;
    s.level_needs_apply = true;
    s.apply_on_level_change(L"garden_controller_replaced");
    assert(replacement_field.path_writes == 1 && replacement_field.track == target);
}

static void test_garden_vanilla_start_and_custom_failure()
{
    Fixture f;
    auto& s = *f.service;
    f.sora.level = fake_runtime::settings.level = L"GoA_01";
    fake_runtime::settings.path = L"/Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01:PersistentLevel.WorldSettings";
    fake_runtime::settings.assets[L"m_BGMField"] = runtime_goa_music_path;
    fake_runtime::settings.assets[L"m_BGMBattle"] = runtime_goa_music_path;
    f.field.track.clear();
    f.field.state = 1;
    s.tick_steps(); // No shuffle or custom files: Vanilla must not remain silent.
    assert(s.active() && f.field.track == runtime_goa_music_path);
    assert(f.field.state == sead_bgm::slot_state_playing && f.field.path_writes == 1);
    f.add(L"goa_01", L"garden.ogg");
    fake_runtime::playable_files[L"garden.ogg"] = false;
    s.update_custom_slots();
    assert(f.field.volume == 1.0f && s.custom_player.file(0).empty());
    fake_runtime::playable_files[L"garden.ogg"] = true;
    s.update_custom_slots();
    assert(f.field.volume == 0.0f && s.custom_player.file(0) == L"garden.ogg");
}

static void test_group_restoration()
{
    Fixture f;
    auto& s = *f.service;
    const auto now = Clock::now();
    const auto original = f.boss.track;
    f.mode = 1;
    s.refresh_table();
    s.swap_group_tracks(now);
    assert(f.boss.track != original);
    const auto first = f.boss.track;
    s.swap_group_tracks(now + std::chrono::seconds(6));
    assert(f.boss.track == first); // Never shuffle our replacement again.
    f.mode = 0;
    s.refresh_table();
    f.boss.state = sead_bgm::slot_state_auto_resume;
    s.swap_group_tracks(now + std::chrono::seconds(7));
    assert(s.active()); // Preserve original while the form suspends Boss.
    f.boss.state = sead_bgm::slot_state_playing;
    s.swap_group_tracks(now + std::chrono::seconds(8));
    assert(f.boss.track == original);
    s.swap_group_tracks(now + std::chrono::seconds(9));
    assert(s.group_written[L"Boss"].original.empty());

    // A changed replacement loads asynchronously; keep the original through
    // the old replacement, loading gap, and acknowledgement of the new path.
    f.mode = 1;
    s.refresh_table();
    s.swap_group_tracks(now + std::chrono::seconds(10));
    f.boss.defer_path = true;
    f.mode = 3;
    s.refresh_table();
    s.swap_group_tracks(now + std::chrono::seconds(11));
    const auto target = s.swaps.at(original);
    s.swap_group_tracks(now + std::chrono::seconds(12));
    assert(s.group_written[L"Boss"].original == original);
    f.boss.track.clear();
    s.swap_group_tracks(now + std::chrono::seconds(13));
    assert(s.group_written[L"Boss"].original == original);
    f.boss.track = target;
    s.swap_group_tracks(now + std::chrono::seconds(14));
    assert(s.group_written[L"Boss"].original == original && f.boss.track == target);
    f.mode = 0;
    s.refresh_table();
    s.swap_group_tracks(now + std::chrono::seconds(15));
    assert(f.boss.pending_track == original && s.active());
    f.boss.track = f.boss.pending_track;
    s.swap_group_tracks(now + std::chrono::seconds(16));
    assert(s.group_written[L"Boss"].original.empty());
}

static void test_request_interception_and_restart()
{
    Fixture f;
    auto& s = *f.service;
    f.mode = 1;
    s.refresh_table();
    const auto original = f.boss.track;
    const auto target = s.swaps.at(original);
    const auto now = Clock::now();
    s.group_retry_at[&f.boss] = now + std::chrono::seconds(5);
    const auto requested = s.intercept_requested_track(&f.boss, original);
    assert(requested && *requested == target);
    assert(!s.group_retry_at.contains(&f.boss));
    // A pre-play rewrite must survive SEAD's Ready/loading phase, and
    // retain the original so switching back to Vanilla can restore it.
    f.boss.state = 1;
    f.boss.track.clear();
    s.swap_group_tracks(now);
    assert(s.group_written[L"Boss"].original == original);
    f.boss.state = sead_bgm::slot_state_playing;
    f.boss.track = target;
    s.swap_group_tracks(now + std::chrono::milliseconds(250));
    assert(f.boss.track == target && s.group_written[L"Boss"].original == original);
    f.mode = 0;
    s.refresh_table();
    s.swap_group_tracks(now + std::chrono::milliseconds(500));
    assert(f.boss.track == original);
    // Event, explicit vanilla/custom overrides, and an unowned controller
    // must never be rewritten by the early path.
    f.mode = 1;
    s.refresh_table();
    assert(!s.intercept_requested_track(&f.event, original));
    // A track-level explicit override uses the same selector as playback.
    f.add(lower(original.substr(original.rfind(L'.') + 1)), L"x.wav");
    assert(!s.intercept_requested_track(&f.boss, original));
    s.custom_library = {};
    UObject unrelated;
    assert(!s.intercept_requested_track(&unrelated, original));
    s.playing_retry_at[&f.field] = now + std::chrono::seconds(5);
    (void)take_music_updates();
    s.note_slot_restart(&f.field);
    assert(!s.playing_retry_at.contains(&f.field) && take_music_updates() != 0);
    // Native Stop/Play paths can bypass reflected hooks entirely.
    s.group_retry_at[&f.boss] = now + std::chrono::seconds(5);
    f.boss.state = sead_bgm::slot_state_stop;
    s.swap_group_tracks(now);
    assert(!s.group_retry_at.contains(&f.boss));
    s.playing_retry_at[&f.field] = now + std::chrono::seconds(5);
    f.field.state = sead_bgm::slot_state_stop;
    s.swap_playing_originals(now);
    assert(!s.playing_retry_at.contains(&f.field));
}

static void test_finished_replacement_recovery()
{
    Fixture f;
    auto& s = *f.service;
    const auto now = Clock::now();
    const auto original = f.boss.track;
    const std::wstring short_cue = L"/Game/Sound/BGM/BGM_cook_jojo_first.BGM_cook_jojo_first";
    s.swaps[original] = short_cue;
    s.swap_group_tracks(now);
    assert(f.boss.track == short_cue);
    // Finishing before the usual five-second retry delay must replay the cue.
    f.boss.state = sead_bgm::slot_state_finished;
    f.boss.track.clear();
    s.swap_group_tracks(now + std::chrono::seconds(1));
    assert(f.boss.state == sead_bgm::slot_state_playing && f.boss.track == short_cue);
    assert(s.group_written[L"Boss"].original == original);
    f.boss.state = sead_bgm::slot_state_finished;
    s.swap_group_tracks(now + std::chrono::milliseconds(1500));
    assert(f.boss.state == sead_bgm::slot_state_playing && s.group_written[L"Boss"].finished_restarts == 2);
    // Switching modes while Finished must recover the original, not forget it.
    s.swaps.clear();
    f.boss.state = sead_bgm::slot_state_finished;
    f.boss.track.clear();
    s.swap_group_tracks(now + std::chrono::seconds(2));
    assert(f.boss.state == sead_bgm::slot_state_playing && f.boss.track == original);
    s.swap_group_tracks(now + std::chrono::seconds(3));
    assert(s.group_written[L"Boss"].original.empty());
    // Explicit game stops release ownership and must never restart music.
    s.swaps[original] = short_cue;
    s.swap_group_tracks(now + std::chrono::seconds(8));
    f.boss.state = sead_bgm::slot_state_stop;
    f.boss.track.clear();
    s.swap_group_tracks(now + std::chrono::seconds(9));
    assert(f.boss.state == sead_bgm::slot_state_stop && f.boss.track.empty());
    // Field/Battle rewrites need the same lifetime as Mission/Boss rewrites.
    const auto field_original = f.field.track;
    s.swaps[field_original] = short_cue;
    s.swap_slot(L"Field", field_original);
    f.field.state = sead_bgm::slot_state_finished;
    f.field.track.clear();
    s.swap_group_tracks(now + std::chrono::seconds(10));
    assert(f.field.state == sead_bgm::slot_state_playing && f.field.track == short_cue);
    s.swaps.clear();
    f.field.state = sead_bgm::slot_state_finished;
    s.swap_group_tracks(now + std::chrono::seconds(11));
    assert(f.field.state == sead_bgm::slot_state_playing && f.field.track == field_original);
}

static void test_suspended_field_restore()
{
    Fixture f;
    auto& s = *f.service;
    const auto original = f.field.track;
    f.mode = 3;
    s.refresh_table();
    f.field.defer_path = true;
    s.swap_slot(L"Field", original);
    assert(f.field.pending_track != original);
    f.field.track.clear();
    f.field.state = 2; // AutoPlay waits for the higher-priority Boss slot.
    f.mode = 0;
    s.refresh_table();
    f.field.reject_path = true;
    s.level_needs_apply = false;
    assert(s.swap_slot(L"Field", original) == L"set_path_failed");
    assert(!s.slot_written[L"Field"].empty() && s.level_needs_apply);
    f.field.reject_path = false;
    assert(s.swap_slot(L"Field", original) == L"restored");
    assert(f.field.pending_track == original);
}

static void test_form_ownership()
{
    Fixture f;
    auto& s = *f.service;
    f.forms = true;
    const auto now = Clock::now();
    s.form_observation_dirty = true; s.update_form_theme(now);
    assert(s.form_on_event && f.event.state == sead_bgm::slot_state_playing);
    assert(!s.held_slots.empty());
    f.mode = 1;
    s.refresh_table();
    s.apply_on_level_change(L"test_mode_change");
    assert(s.form_on_event && f.event.stops == 0 && !s.held_slots.empty());
    s.form_observation_dirty = true; s.update_form_theme(now + std::chrono::seconds(1));
    f.sora.character_state = char_state::attack;
    f.sora.weapon = L"w_so020_C";
    s.form_observation_dirty = true; s.update_form_theme(now + std::chrono::seconds(2));
    assert(!s.form_on_event && f.event.stops == 1 && s.held_slots.empty());
    assert(f.boss.behaviour == f.boss.default_behaviour);

    f.sora.character_state = char_state::style_change;
    f.sora.weapon = L"w_so021_C";
    s.form_observation_dirty = true; s.update_form_theme(now + std::chrono::seconds(3));
    assert(s.form_on_event);
    ++f.event.serial;
    ++f.boss.serial;
    f.event.track = L"new_cutscene";
    f.boss.behaviour = 123;
    s.stop_form_theme();
    assert(f.event.stops == 1 && f.event.track == L"new_cutscene" && f.boss.behaviour == 123);
}

static void test_library_is_loaded_once_per_launch()
{
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
        ("kh3ap_music_startup_" + std::to_string(Clock::now().time_since_epoch().count()));
    assert(fs::create_directory(root));
    const auto first = root / "first", second = root / "second";
    for (const auto& folder : {first, second})
    {
        fs::create_directories(folder);
        std::ofstream{folder / "he_01.wav"} << "same";
    }
    {
        Fixture f;
        auto& s = *f.service;
        f.gameplay = false; // Startup loads even before playable Sora exists.
        fake_runtime::music_folder = first;
        fake_runtime::music_directory_reads = 0;
        s.custom_music_initialized = false;
        s.tick_steps();
        assert(fake_runtime::music_directory_reads == 1);
        assert(s.custom_directory == first && s.custom_library.tracks.size() == 1);
        assert(fs::is_regular_file(first / "TARGETS.txt"));
        const auto signature = s.custom_library.signature;

        // Added/removed files, manifest edits, folder selection and gameplay
        // transitions must not trigger any subsequent library discovery.
        fs::remove(first / "he_01.wav");
        std::ofstream{first / "new.ogg"} << "new";
        std::ofstream{first / "custom_music.json"} << R"({"tracks":[{"replaces":["he_01"],"vanilla":true}]})";
        fake_runtime::music_folder = second;
        for (int i = 0; i < 20; ++i)
        {
            f.gameplay = i % 2 == 0;
            request_music_update(MusicUpdate::Context);
            s.tick_steps();
            assert(s.custom_library.signature == signature);
            assert(s.custom_directory == first && s.custom_library.tracks.size() == 1);
            assert(s.custom_library.for_target(L"he_01")->file == first / "he_01.wav");
        }
        assert(fake_runtime::music_directory_reads == 1);
    }
    {
        Fixture next_launch;
        auto& s = *next_launch.service;
        s.custom_music_initialized = false;
        s.initialize_custom_music();
        assert(s.custom_directory == second);
        s.update_custom_slots();
        assert(s.custom_player.file(0) == second / "he_01.wav");
    }
    {
        Fixture next_launch;
        fake_runtime::music_folder = first;
        auto& s = *next_launch.service;
        s.custom_music_initialized = false;
        s.initialize_custom_music();
        assert(s.custom_library.tracks.contains(L"new.ogg"));
        assert(!s.custom_library.tracks.contains(L"he_01.wav"));
        assert(s.custom_library.keeps_vanilla(L"he_01"));
    }
    {
        Fixture missing_at_startup;
        fake_runtime::music_folder = root / "missing";
        auto& s = *missing_at_startup.service;
        s.custom_music_initialized = false;
        s.initialize_custom_music();
        assert(s.custom_library.tracks.empty());
        fs::create_directories(fake_runtime::music_folder);
        std::ofstream{fake_runtime::music_folder / "he_01.wav"} << "late";
        s.initialize_custom_music();
        assert(s.custom_library.tracks.empty()); // No implicit retry/poll after a missing folder.
    }
    fake_runtime::music_folder.clear();
    assert(fs::equivalent(root.parent_path(), fs::temp_directory_path()));
    fs::remove_all(root);
}

static void test_chaos_custom_toggle()
{
    Fixture f;
    auto& s = *f.service;
    // A large pool guarantees that some custom entries are drawn without
    // making the regression depend on any one seed's assigned world.
    for (int i = 0; i < 100; ++i)
    {
        const auto name = L"custom_" + std::to_wstring(i) + L".wav";
        s.custom_library.tracks[name].file = name;
    }
    s.custom_library.signature = L"no_manifest_folder";
    const auto has_custom = [&]() {
        return std::ranges::any_of(s.swaps, [](const auto& pair) { return is_custom_bgm_track(pair.second); });
    };
    f.mode = 3;
    s.refresh_table();
    assert(s.table_include_custom_music && has_custom());
    const auto enabled_swaps = s.swaps;
    const auto drawn = std::ranges::find_if(s.swaps, [](const auto& pair) { return is_custom_bgm_track(pair.second); });
    f.field.track = drawn->first;
    s.level_world = world_code_of(drawn->first);
    s.applied_level = lower(std::wstring{s.level_world}) + L"_01";
    s.update_custom_slots();
    assert(!s.custom_player.file(0).empty() && f.field.volume == 0.0f);
    f.include_custom = false;
    s.level_needs_apply = false;
    s.refresh_table();
    assert(!s.table_include_custom_music && !has_custom() && s.level_needs_apply);
    s.update_custom_slots();
    assert(s.custom_player.file(0).empty() && f.field.volume == 1.0f);
    // Explicit replacements remain audible even with the Chaos pool off.
    f.add(s.applied_level, L"explicit.ogg");
    s.update_custom_slots();
    assert(s.custom_player.file(0) == L"explicit.ogg");
    s.custom_library.tracks.erase(L"explicit.ogg");
    s.custom_library.targets.clear();
    s.update_custom_slots();
    assert(s.custom_player.file(0).empty() && f.field.volume == 1.0f);
    f.include_custom = true;
    s.refresh_table();
    assert(s.swaps == enabled_swaps);
    for (const int mode : {0, 1, 2})
    {
        f.mode = mode;
        s.refresh_table();
        assert(!s.table_include_custom_music && !has_custom());
    }
}

static void test_update_mapping_reads()
{
    Fixture f;
    f.mode = 1;
    request_music_update(MusicUpdate::Context);
    f.service->tick_steps();
    fake_runtime::slot_mapping_reads = 0;
    request_music_update(MusicUpdate::Context);
    f.service->tick_steps();
    std::cout << "Steady music update mapping reads: " << fake_runtime::slot_mapping_reads << '\n';
    assert(fake_runtime::slot_mapping_reads == 1);
    assert(f.service->slot_mapping_ == nullptr);

    // Quiet frames do no mapping work, regardless of elapsed time.
    fake_runtime::slot_mapping_reads = 0;
    (void)take_music_updates();
    f.service->tick_steps();
    assert(fake_runtime::slot_mapping_reads == 0);

    // A replacement controller is picked up on the next actual update.
    UObject replacement;
    replacement.track = bgm_shuffle_worlds()[0].fields[0];
    fake_runtime::slots[0].controller = &replacement;
    f.add(L"he_01", L"replacement.ogg");
    request_music_update(MusicUpdate::Context);
    f.service->tick_steps();
    assert(fake_runtime::slot_mapping_reads == 1);
    assert(replacement.volume == 0.0f && f.service->muted_controllers[0].Get() == &replacement);
    assert(f.service->slot_logged[L"Field"].track == bgm_shuffle_worlds()[0].fields[0]);

    // Empty/missing mappings are shared too, then discarded for the next update.
    fake_runtime::slots.clear();
    fake_runtime::slot_mapping_reads = 0;
    request_music_update(MusicUpdate::Context);
    f.service->tick_steps();
    assert(fake_runtime::slot_mapping_reads == 1 && f.service->slot_mapping_ == nullptr);
    assert(f.service->custom_player.file(0).empty() && replacement.volume == 1.0f);
}

static void test_slot_mapping_lifetime_and_live_state()
{
    Fixture f;
    auto& s = *f.service;
    fake_runtime::slot_mapping_reads = 0;
    {
        const TestBgmShuffleService::Impl::SlotMappingScope update{s};
        assert(update.find(L"FIELD") == &f.field && update.find(L"missing") == nullptr);
        f.add(L"he_01", L"field.ogg");
        s.update_custom_slots();
        assert(f.field.volume == 0.0f);
        f.field.state = sead_bgm::slot_state_stop;
        s.update_custom_slots();
        assert(s.custom_player.file(0).empty() && f.field.volume == 1.0f);
        f.field.state = sead_bgm::slot_state_playing;
        s.update_custom_slots();
        assert(s.custom_player.file(0) == L"field.ogg");

        // Track changes are also visible without rebuilding the mapping.
        s.custom_library.targets.clear();
        f.add(L"new_track", L"changed.ogg");
        assert(sead_bgm::set_path(&f.field, L"new_track"));
        s.update_custom_slots();
        assert(s.custom_player.file(0) == L"changed.ogg");

        // A Play/SetPath hook re-entering the service reuses the mapping.
        const auto original = f.boss.track;
        s.swaps[original] = L"replacement_track";
        assert(s.intercept_requested_track(&f.boss, original) == L"replacement_track");
        assert(fake_runtime::slot_mapping_reads == 1);
        assert(s.slot_mapping_ == &update);

        // Serial reuse during the same call must not touch the new object.
        ++f.field.serial;
        f.field.volume = 0.4f;
        assert(update.find(L"Field") == nullptr);
        s.update_custom_slots();
        assert(f.field.volume == 0.4f && s.custom_player.file(0).empty());
        f.boss.alive = false;
        assert(update.find(L"Boss") == nullptr);
    }
    assert(s.slot_mapping_ == nullptr);
    s.update_custom_slots();
    assert(fake_runtime::slot_mapping_reads == 2 && f.field.volume == 0.0f);
    // Exception unwinding also drops the scoped pointer.
    try {
        const TestBgmShuffleService::Impl::SlotMappingScope update{s};
        throw 1;
    } catch (int) {}
    assert(s.slot_mapping_ == nullptr);
}

static void test_native_completion_wakes_update()
{
    Fixture f;
    auto& s = *f.service;
    s.tick_steps();
    (void)take_music_updates();
    fake_runtime::slot_mapping_reads = 0;
    s.tick_steps();
    assert(fake_runtime::slot_mapping_reads == 0);
    test_music_mailbox.post(SeadMusicEvent::Loaded);
    test_music_mailbox.post(SeadMusicEvent::Play);
    s.tick_steps();
    assert(fake_runtime::slot_mapping_reads == 1); // coalesced into one event update
    (void)take_music_updates();
    s.tick_steps();
    assert(fake_runtime::slot_mapping_reads == 1); // consumed only once
    f.gameplay = false;
    test_music_mailbox.post(SeadMusicEvent::Finished);
    s.tick_steps();
    assert(fake_runtime::slot_mapping_reads == 1);
    f.gameplay = true;
    request_music_update(MusicUpdate::Context);
    s.tick_steps();
    assert(fake_runtime::slot_mapping_reads == 2); // retained across readiness gate
}

static void test_event_only_idle_and_gate()
{
    Fixture f;
    auto& s = *f.service;
    f.mode = 1;
    s.tick_steps();
    fake_runtime::slot_mapping_reads = 0;
    // Even an expired diagnostic log deadline cannot cause idle discovery.
    s.slot_report = {Clock::now() - std::chrono::hours(1), L"idle"};
    for (int i = 0; i < 10000; ++i) s.tick_steps();
    assert(fake_runtime::slot_mapping_reads == 0);
    for (auto reason : {MusicUpdate::Settings, MusicUpdate::Context, MusicUpdate::Menu, MusicUpdate::Decoded, MusicUpdate::Volume})
    {
        request_music_update(reason); request_music_update(reason);
        s.tick_steps();
        assert(fake_runtime::slot_mapping_reads == 1);
        fake_runtime::slot_mapping_reads = 0;
        s.tick_steps();
        assert(fake_runtime::slot_mapping_reads == 0);
    }
    f.gameplay = false;
    request_music_update(MusicUpdate::Context);
    s.tick_steps();
    test_music_mailbox.post(SeadMusicEvent::Loaded);
    s.tick_steps();
    for (int i = 0; i < 100; ++i) s.tick_steps();
    assert(fake_runtime::slot_mapping_reads == 0);
    f.gameplay = true;
    // Readiness alone is not polled. Its owning event releases deferred work.
    s.tick_steps();
    assert(fake_runtime::slot_mapping_reads == 0);
    request_music_update(MusicUpdate::Context);
    s.tick_steps();
    assert(fake_runtime::slot_mapping_reads == 1);
}

static void test_form_events_drive_theme_and_end()
{
    Fixture f;
    auto& s = *f.service;
    f.forms = true;
    request_music_update(MusicUpdate::Settings);
    s.tick_steps();
    assert(s.form_on_event && !s.form_track.empty());
    fake_runtime::slot_mapping_reads = 0;
    s.tick_steps();
    assert(fake_runtime::slot_mapping_reads == 0);
    f.sora.character_state = char_state::attack;
    request_music_update(MusicUpdate::Form);
    s.tick_steps();
    assert(!s.form_on_event && s.form_track.empty() && s.held_slots.empty());
    assert(fake_runtime::slot_mapping_reads == 1);
}

static void test_weapon_replacement_without_state_change()
{
    Fixture f;
    auto& s = *f.service;
    f.forms = true;
    f.sora.character_state = 1; // Idle throughout; only the equipment changes.
    s.tick_steps();
    assert(s.form_on_event && !s.form_deadline);
    f.sora.weapon = L"w_so020_C";
    fake_runtime::slot_mapping_reads = 0;
    test_music_mailbox.post(SeadMusicEvent::EquipmentRebuilt);
    s.tick_steps();
    assert(s.form_on_event && s.form_deadline); // arm the existing base-weapon grace
    assert(fake_runtime::slot_mapping_reads == 1);
    s.tick_steps();
    assert(fake_runtime::slot_mapping_reads == 1);

    f.forms = false;
    request_music_update(MusicUpdate::Settings);
    s.tick_steps();
    assert(!s.form_on_event && !s.form_deadline);
    fake_runtime::slot_mapping_reads = 0;
    test_music_mailbox.post(SeadMusicEvent::EquipmentRebuilt);
    s.tick_steps();
    assert(fake_runtime::slot_mapping_reads == 0); // no music work with forms disabled
}

static void test_level_objects_and_live_context()
{
    Fixture f;
    auto& s=*f.service;
    f.mode=1;
    s.refresh_table();
    const std::wstring original{bgm_shuffle_worlds()[0].fields[0]};
    s.apply_on_level_change(L"test_first");
    const auto replacement=s.replacement_for(original);
    assert(replacement && fake_runtime::settings.assets[L"m_BGMField"]==*replacement);

    // A late streamed settings object must be rewritten on its lifecycle wake,
    // even though the persistent map and existing context have not changed.
    UObject streamed;
    streamed.path=L"/Game/Maps/he_01_sub.he_01_sub:PersistentLevel.WorldSettings";
    streamed.assets[L"m_BGMField"]=original;
    fake_runtime::world_settings.push_back(&streamed);
    request_music_update(MusicUpdate::Objects);
    s.tick_steps();
    assert(streamed.assets[L"m_BGMField"]==*s.replacement_for(original));

    // A departing world's settings may remain live. Select by current Sora's
    // map rather than accepting that old, still-valid cached context.
    UObject next;
    next.level=L"he_02";next.path=L"/Game/Maps/he_02.he_02:PersistentLevel.WorldSettings";
    next.assets[L"m_BGMField"]=original;
    fake_runtime::world_settings.push_back(&next);
    f.sora.level=L"he_02";
    s.apply_on_level_change(L"test_transition");
    assert(s.world_context.Get()==&next && s.applied_level==L"he_02");

    f.mode=0;s.refresh_table();s.apply_on_level_change(L"test_vanilla");
    assert(next.assets[L"m_BGMField"]==original && streamed.assets[L"m_BGMField"]==original);
    assert(fake_runtime::settings.assets[L"m_BGMField"]==original);
}

static void test_now_playing_tracks_audible_music()
{
    Fixture f;
    auto& s=*f.service;
    f.boss.state=sead_bgm::slot_state_stop;
    s.tick_steps(); // Vanilla also publishes its actual track.
    assert(s.now_playing==L"Mount Olympus");
    // Idle frames preserve the title with no new slot mapping read.
    fake_runtime::slot_mapping_reads=0;
    s.tick_steps();
    assert(s.now_playing==L"Mount Olympus" && fake_runtime::slot_mapping_reads==0);

    f.field.track=L"/Game/Sound/BGM/BGM_ra_field_2.BGM_ra_field_2";
    s.update_now_playing();
    assert(s.now_playing==L"Happy Hair Day -Into the Forest Deep-");
    f.event.track=L"/Game/Sound/_DLC/BGM/BGM_battle_yozora.BGM_battle_yozora";
    f.event.state=sead_bgm::slot_state_playing;
    s.update_now_playing();
    assert(s.now_playing==L"Nachtfl\u00fcgel");

    f.event.state=sead_bgm::slot_state_suspended;
    s.custom_player.play(0,CustomMusicTrack{std::filesystem::path{L"C:/Music/My Very Long Custom Song Name.ogg"}});
    s.slot_muted[0]=true;
    s.update_now_playing();
    assert(s.now_playing==L"My Very Long Custom Song Name");
    s.custom_player.stop(0); // Decode failure/no voice keeps native title.
    s.update_now_playing();
    assert(s.now_playing==L"Happy Hair Day -Into the Forest Deep-");
    f.field.state=sead_bgm::slot_state_suspended;
    s.update_now_playing();
    assert(s.now_playing.empty());

    f.field.state=sead_bgm::slot_state_playing;
    f.gameplay=false;
    request_music_update(MusicUpdate::Context);
    s.tick_steps();
    assert(s.now_playing.empty());
    assert(native_song_title(L"/Game/Sound/BGM/xx/BGM_Unlisted_Cue_2.BGM_Unlisted_Cue_2")==L"Unlisted Cue 2");
}

int main()
{
    test_now_playing_tracks_audible_music();
    test_garden_cold_entry_and_mode_changes();
    test_garden_vanilla_start_and_custom_failure();
    test_level_objects_and_live_context();
    test_event_only_idle_and_gate();
    test_form_events_drive_theme_and_end();
    test_weapon_replacement_without_state_change();
    const std::string literal = "/Game/Sound/BGM/BGM_bx_battle.BGM_bx_battle";
    std::vector<uint8_t> code{0x67, 0x1f};
    code.insert(code.end(), literal.begin(), literal.end());
    code.insert(code.end(), {0, 0x16, 0x07});
    assert(music_request_literal(code) == std::wstring(literal.begin(), literal.end()));
    assert(!music_request_literal(std::span{code}.first(code.size() - 2))); // no end-of-parameters token
    code[0] = 0x00; // A variable expression cannot be read as a soft-object literal.
    assert(!music_request_literal(code));
    code[0] = 0x67;
    code[1] = 0x34; // Unknown encoding is left to the engine.
    assert(!music_request_literal(code));
    test_override_priority();
    test_failed_or_loading_audio();
    test_channel_cleanup();
    test_garden_removal();
    test_group_restoration();
    test_request_interception_and_restart();
    test_finished_replacement_recovery();
    test_suspended_field_restore();
    test_form_ownership();
    test_library_is_loaded_once_per_launch();
    test_chaos_custom_toggle();
    test_update_mapping_reads();
    test_slot_mapping_lifetime_and_live_state();
    test_native_completion_wakes_update();
    std::cout << "Music service regression scenarios passed\n";
}
