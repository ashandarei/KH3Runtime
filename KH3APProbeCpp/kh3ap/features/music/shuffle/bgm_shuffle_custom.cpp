#include "kh3ap/features/music/shuffle/bgm_shuffle_service_impl.h"

namespace kh3ap
{
    auto RuntimeBgmShuffleService::Impl::update_now_playing() -> void
    {
        const SlotMappingScope slots{*this};
        now_playing.clear();
        // Suspended/held tracks never win over the audible slot. The Event
        // slot includes form themes; native and custom replacements share
        // this selection so a decoding file cannot claim the native title.
        static constexpr std::wstring_view priority[]{L"Event",L"Menu",L"link",L"Attraction",L"MiniGame",L"Boss",L"Mission",L"Battle",L"Field"};
        for(const auto name:priority)
        {
            auto* controller=slots.find(name);
            if(!controller || sead_bgm::slot_state(controller)!=sead_bgm::slot_state_playing) continue;
            for(size_t i=0;i<custom_slots.size();++i)
                if(name==custom_slots[i] && slot_muted[i] && !custom_player.file(i).empty())
                {
                    now_playing=custom_player.file(i).stem().wstring();
                    return;
                }
            const auto track=sead_bgm::current_track(controller);
            if(track.empty()) continue;
            now_playing=native_song_title(track);
            return;
        }
    }
    auto RuntimeBgmShuffleService::Impl::follow_volume_settings() -> void
    {
        if (!sead_bgm::still_live(game_instance, STR("TresGameInstance")))
        {
            const auto found = sead_bgm::find_live_objects(STR("TresGameInstance"));
            game_instance = found.empty() ? nullptr : found.front();
        }
        const auto settings = sead_bgm::read_volume_settings(game_instance);
        if (!settings || (settings->master == last_volume.master && settings->bgm == last_volume.bgm))
        {
            return;
        }
        last_volume = *settings;
        const float master = std::min(settings->master / volume_setting_max, 1.0f);
        const float bgm = std::min(settings->bgm / volume_setting_max, 1.0f);
        custom_gain = master * bgm;
        apply_custom_volume();
        Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Custom music volume | master={} bgm={} gain={}\n"),
                settings->master,
                settings->bgm,
                master * bgm);
    }

    auto RuntimeBgmShuffleService::Impl::duck_custom_music(bool menu_open) -> void
    {
        if (menu_open == custom_ducked)
        {
            return;
        }
        custom_ducked = menu_open;
        apply_custom_volume();
    }

    auto RuntimeBgmShuffleService::Impl::apply_custom_volume() -> void
    {
        custom_player.set_volume(custom_gain * (custom_ducked ? menu_duck_gain : 1.0f));
    }

    auto RuntimeBgmShuffleService::Impl::initialize_custom_music() -> void
    {
        if (custom_music_initialized) return;
        custom_music_initialized = true;
        custom_directory = custom_music_directory();
        visited_levels = read_custom_music_visited_levels(custom_directory);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        ++custom_library_loads;
#endif
        custom_library = load_custom_music_library(custom_directory);
        level_needs_apply = true;
        Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Custom music library loaded for this launch | tracks={} targets={} folder={}\n"),
                custom_library.tracks.size(), custom_library.targets.size(), custom_directory.wstring());
        for (const std::wstring& problem : custom_library.problems)
        {
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Custom music manifest | {}\n"), problem);
        }
        (void)write_custom_music_targets(custom_directory, visited_levels);
        if (!applied_level.empty()) note_visited_level(applied_level);
    }

    auto RuntimeBgmShuffleService::Impl::note_visited_level(const std::wstring& level) -> void
    {
        const std::wstring key = lower(level);
        if (key == garden_level ||
            std::ranges::any_of(visited_levels, [&key](const std::wstring& seen) { return lower(seen) == key; }))
        {
            return;
        }
        visited_levels.push_back(level);
        std::ranges::sort(visited_levels, [](const std::wstring& a, const std::wstring& b) { return lower(a) < lower(b); });
        (void)write_custom_music_targets(custom_directory, visited_levels);
    }

    auto RuntimeBgmShuffleService::Impl::track_target(const std::wstring& track) const -> std::wstring
    {
        return custom_library.override_target(track);
    }

    auto RuntimeBgmShuffleService::Impl::overridden(const std::wstring& track) const -> bool
    {
        return !track_target(track).empty();
    }

    auto RuntimeBgmShuffleService::Impl::level_target() const -> std::wstring
    {
        return custom_library.override_target({}, applied_level);
    }

    auto RuntimeBgmShuffleService::Impl::slot_target(std::wstring_view slot_name, const std::wstring& track) const -> std::wstring
    {
        const bool home = at_home(slot_name, track);
        if (lower(std::wstring{slot_name}) == STR("field"))
        {
            return custom_library.override_target(home ? std::wstring_view{track} : std::wstring_view{}, applied_level);
        }
        return home ? track_target(track) : std::wstring{};
    }

    auto RuntimeBgmShuffleService::Impl::level_overridden() const -> bool
    {
        return !level_target().empty();
    }

    auto RuntimeBgmShuffleService::Impl::custom_for_track(std::wstring_view slot_name, const std::wstring& track) const -> const CustomMusicTrack*
    {
        // The most specific override decides: a vanilla entry on the
        // level or track keeps it even under a file for its whole world.
        if (const std::wstring target = slot_target(slot_name, track); !target.empty())
        {
            return custom_library.for_target(target);
        }
        if (track.empty() || !at_home(slot_name, track))
        {
            return nullptr;
        }
        // Every track is also some other world's replacement: a Chaos
        // entry only belongs to an original of the current world, never
        // to a replacement that came from elsewhere.
        return world_code_of(track) == level_world ? drawn_custom_for(track) : nullptr;
    }

    auto RuntimeBgmShuffleService::Impl::drawn_custom_for(const std::wstring& original) const -> const CustomMusicTrack*
    {
        if (overridden(original))
        {
            return nullptr;
        }
        const auto entry = swaps.find(original);
        return entry != swaps.end() ? custom_library.for_table_entry(entry->second) : nullptr;
    }

    auto RuntimeBgmShuffleService::Impl::update_custom_slots() -> void
    {
        // Decoder completion publishes an event; no pending-task polling.
        const SlotMappingScope slots{*this};
        for (size_t index = 0; index < custom_slots.size(); ++index)
        {
            UObject* const controller = slots.find(custom_slots[index]);
            if (!controller)
            {
                release_custom_slot(index);
                continue;
            }
            if (muted_controllers[index].Get() != controller)
            {
                release_custom_slot(index);
                muted_controllers[index] = make_uobject_weak(controller);
                if (!muted_controllers[index].Get()) continue;
            }
            const auto state = sead_bgm::slot_state(controller);
            const std::wstring playing = sead_bgm::current_track(controller);
            const CustomMusicTrack* track = nullptr;
            if (form_theme_playing() && index == event_custom_channel && playing == form_track)
            {
                // A form theme is the keyblade's own music: its form's
                // file, or a file named after the theme track, replaces
                // it, not the level's file or the Chaos draw.
                track = form_custom_music();
                if (!track)
                {
                    track = custom_library.for_target(playing);
                }
            }
            else
            {
                track = custom_for_track(custom_slots[index], playing);
            }
            // A slot that starts playing again (a new Play, a new track)
            // is back at full volume: muted again, not trusted from before.
            const bool restarted = state == sead_bgm::slot_state_playing &&
                    (muted_state[index] != sead_bgm::slot_state_playing || muted_track[index] != playing);
            muted_state[index] = state.value_or(0);
            muted_track[index] = playing;
            if (track && state == sead_bgm::slot_state_playing)
            {
                const bool changed_file = custom_player.file(index) != track->file;
                if (custom_player.play(index, *track))
                {
                    if (!slot_muted[index] || restarted)
                    {
                        slot_muted[index] = sead_bgm::set_volume(controller, 0.0f, slot_mute_fade);
                    }
                    if (changed_file)
                    {
                        Output::send<LogLevel::Normal>(
                                STR("[KH3APProbeCpp] Custom music replaces | slot={} level={} sead_track={}\n"),
                                custom_slots[index], applied_level, playing);
                    }
                }
                else if (slot_muted[index])
                {
                    // Keep native music audible while decoding, or if the
                    // file cannot be played. Do not cancel a pending decode.
                    slot_muted[index] = !sead_bgm::set_volume(controller, 1.0f, slot_mute_fade);
                }
                continue;
            }
            const bool suspended =
                    state == sead_bgm::slot_state_suspended || state == sead_bgm::slot_state_auto_resume;
            if (track && suspended && custom_player.file(index) == track->file)
            {
                custom_player.hold(index);
                continue;
            }
            custom_player.stop(index);
            if (slot_muted[index])
            {
                slot_muted[index] = !sead_bgm::set_volume(controller, 1.0f, slot_mute_fade);
            }
        }
    }

    auto RuntimeBgmShuffleService::Impl::release_custom_slot(size_t index) -> void
    {
        custom_player.stop(index);
        UObject* const controller = muted_controllers[index].Get();
        if (slot_muted[index] && controller && is_uobject_valid_fast(controller))
        {
            (void)sead_bgm::set_volume(controller, 1.0f, slot_mute_fade);
        }
        slot_muted[index] = false;
        muted_controllers[index] = {};
        muted_state[index] = 0;
        muted_track[index].clear();
    }

    auto RuntimeBgmShuffleService::Impl::stop_custom_music() -> void
    {
        for (size_t index = 0; index < custom_slots.size(); ++index)
        {
            release_custom_slot(index);
        }
        custom_player.stop_all();
    }

}
