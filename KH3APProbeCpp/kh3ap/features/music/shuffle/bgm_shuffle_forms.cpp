#include "kh3ap/features/music/shuffle/bgm_shuffle_service_impl.h"

namespace kh3ap
{
    auto RuntimeBgmShuffleService::Impl::form_kept_vanilla() const -> bool
    {
        if (anti_form)
        {
            return custom_library.keeps_vanilla(anti_form_custom_target);
        }
        const std::wstring_view target =
                form_tracker.keyblade() ? form_custom_target(*form_tracker.keyblade()) : std::wstring_view{};
        return !target.empty() && custom_library.keeps_vanilla(target);
    }

    auto RuntimeBgmShuffleService::Impl::override_playing() -> PlayingOverride
    {
        const SlotMappingScope slots{*this};
        for (const auto& slot : slots.entries())
        {
            auto* controller = slot.controller();
            if (!controller || slot.name == form_theme_slot ||
                sead_bgm::slot_state(controller) != sead_bgm::slot_state_playing)
            {
                continue;
            }
            const std::wstring track = sead_bgm::current_track(controller);
            if (std::wstring target = slot_target(slot.name, track); custom_library.for_target(target))
            {
                return {slot.name, std::move(target)};
            }
        }
        return {};
    }

    auto RuntimeBgmShuffleService::Impl::form_custom_music() const -> const CustomMusicTrack*
    {
        if (anti_form)
        {
            return custom_library.for_target(anti_form_custom_target);
        }
        const std::wstring_view target =
                form_tracker.keyblade() ? form_custom_target(*form_tracker.keyblade()) : std::wstring_view{};
        return target.empty() ? nullptr : custom_library.for_target(target);
    }

    auto RuntimeBgmShuffleService::Impl::any_form_custom_music() const -> bool
    {
        return std::ranges::any_of(form_custom_targets(), [this](const FormCustomTarget& target) {
            return custom_library.for_target(target.target) != nullptr;
        });
    }

    auto RuntimeBgmShuffleService::Impl::form_theme_playing() const -> bool
    {
        return !form_track.empty();
    }

    auto RuntimeBgmShuffleService::Impl::form_themes_on() const -> bool
    {
        return config.form_themes_enabled && config.form_themes_enabled();
    }

    auto RuntimeBgmShuffleService::Impl::update_form_theme(Clock::time_point now) -> void
    {
        const bool was_active = form_theme_playing();
        if (form_observation_dirty)
        {
            form_observation_dirty = false;
            UObject* const pawn = config.sora_pawn ? config.sora_pawn() : nullptr;
            if ((form_themes_on() || any_form_custom_music()) && pawn && is_uobject_valid_fast(pawn))
            {
                form_requested_track = form_tracker.update({
                    .state = read_character_state(pawn),
                    .keyblade = read_equipped_keyblade_name(pawn),
                    .weapon = held_weapon_class(pawn),
                    .now = now,
                });
            }
            else
            {
                form_tracker.reset();
                form_requested_track.reset();
            }
        }
        form_deadline = form_tracker.next_deadline();
        std::optional<std::wstring> track = form_requested_track;
        anti_form = form_tracker.anti_form();
        // With Form Themes off, only a form with a custom file plays; a
        // form with a vanilla entry plays nothing.
        const CustomMusicTrack* const form_custom = track ? form_custom_music() : nullptr;
        if (track && ((!form_themes_on() && !form_custom) || form_kept_vanilla()))
        {
            track.reset();
        }
        // Custom files outrank forms: no theme over music the player
        // chose, and one already playing ends.
        if (track || was_active)
        {
            if (const PlayingOverride held = override_playing(); !held.track.empty())
            {
                if (held.track != override_seen)
                {
                    override_seen = held.track;
                    Output::send<LogLevel::Normal>(
                            STR("[KH3APProbeCpp] Form theme held back | override={} slot={}\n"),
                            held.track,
                            held.slot);
                }
                track.reset();
            }
        }
        const SlotMappingScope slots{*this};
        UObject* const event = slots.find(form_theme_slot);
        if (track)
        {
            if (*track == form_track && form_on_event && form_controller.Get() == event)
            {
                // A scripted sequence may take Event mid-form: the theme
                // waits, and comes back once the game has stopped Event.
                const auto event_state = event ? sead_bgm::slot_state(event) : std::nullopt;
                // The end of a cutscene suspends Event, taking the theme
                // for the cutscene's music, and nothing resumes it: the
                // slots under it stay suspended too (silence through the
                // last phase of the final fight, 2026-09-29). Back in
                // play, Play resumes it.
                const bool suspended = event_state == sead_bgm::slot_state_suspended &&
                        sead_bgm::current_track(event) == form_track;
                if (!suspended)
                {
                    theme_suspended_at.reset();
                }
                else if (!theme_suspended_at)
                {
                    theme_suspended_at = now;
                }
                else if (now - *theme_suspended_at >= theme_resume_delay &&
                         !(config.menu_open && config.menu_open()))
                {
                    theme_suspended_at.reset();
                    Output::send<LogLevel::Normal>(
                            STR("[KH3APProbeCpp] Form theme resumed after a suspend | ok={}\n"),
                            sead_bgm::play(event));
                }
                if (theme_suspended_at && !(config.menu_open && config.menu_open()))
                {
                    const auto due = *theme_suspended_at + theme_resume_delay;
                    if (due > now && (!form_deadline || due < *form_deadline)) form_deadline = due;
                }
                const bool freed = !event_state || *event_state == sead_bgm::slot_state_stop ||
                        *event_state == sead_bgm::slot_state_finished || *event_state == 0;
                if (!freed)
                {
                    return;
                }
                form_on_event = false;
            }
            // A native slot event retries when the game frees Event.
            const bool retry = *track == form_track;
            const auto state = event ? sead_bgm::slot_state(event) : std::nullopt;
            const std::wstring current = event ? sead_bgm::current_track(event) : std::wstring{};
            // The game's own music (a cutscene, a mission's end) holds
            // Event, even while its track loads and reads empty; only a
            // theme this service started there may be switched.
            const bool ours = form_on_event && form_controller.Get() == event &&
                    (current.empty() || current == form_track);
            const bool busy = state == sead_bgm::slot_state_playing && !ours;
            const auto event_weak = make_uobject_weak(event);
            if (!event_weak.Get()) return;
            form_track = *track;
            form_on_event = false;
            if (event && !busy && state != sead_bgm::slot_state_playing)
            {
                hold_playing_slots();
            }
            // The theme still carries the form on Event, so the music it
            // interrupts resumes after; a custom file plays over it,
            // once decoding/playback succeeds in update_custom_slots.
            if (event && !busy && sead_bgm::set_path(event, *track))
            {
                if (!anti_form && form_tracker.keyblade())
                {
                    if (const float start = form_theme_start_seconds(*form_tracker.keyblade()); start > 0.0f)
                    {
                        (void)sead_bgm::set_seek_time(event, start);
                    }
                }
                form_on_event = state == sead_bgm::slot_state_playing || sead_bgm::play(event);
                if (form_on_event)
                {
                    form_controller = event_weak;
                    // Play resets native volume. SetPath on an already
                    // playing slot does not, so retain its mute ownership
                    // until update_custom_slots can restore or replace it.
                    if (state != sead_bgm::slot_state_playing)
                    {
                        slot_muted[event_custom_channel] = false;
                    }
                }
            }
            if (retry && !form_on_event)
            {
                return;
            }
            Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Form theme started | keyblade={} anti_form={} track={} event_state={} busy={} playing={}\n"),
                    form_tracker.keyblade().value_or(STR("?")),
                    anti_form,
                    *track,
                    state ? static_cast<int>(*state) : -1,
                    busy,
                    form_on_event);
            slot_report = {now + slot_report_delay, STR("form_started")};
            return;
        }
        if (!was_active)
        {
            return;
        }
        const std::wstring current = event ? sead_bgm::current_track(event) : std::wstring{};
        // Still ours (or still loading it): stop it, and SEAD resumes
        // what it suspended.
        const bool stopped = stop_form_theme();
        Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Form theme ended | reason={} event_track={} stopped={}\n"),
                form_tracker.end_reason().empty() ? std::wstring_view{STR("disabled")} : form_tracker.end_reason(),
                current,
                stopped);
        slot_report = {now + slot_report_delay, STR("form_ended")};
    }

    auto RuntimeBgmShuffleService::Impl::stop_form_theme() -> bool
    {
        UObject* const event = form_controller.Get();
        const std::wstring current = event ? sead_bgm::current_track(event) : std::wstring{};
        const bool ours = form_on_event && event && (current == form_track || current.empty());
        const bool stopped = ours && sead_bgm::stop(event);
        form_track.clear();
        form_on_event = false;
        form_controller = {};
        theme_suspended_at.reset();
        release_held_slots();
        return stopped;
    }

    auto RuntimeBgmShuffleService::Impl::hold_playing_slots() -> void
    {
        const SlotMappingScope slots{*this};
        for (const auto& slot : slots.entries())
        {
            auto* controller = slot.controller();
            if (!controller || slot.name == form_theme_slot ||
                sead_bgm::slot_state(controller) != sead_bgm::slot_state_playing)
            {
                continue;
            }
            const auto defaults = sead_bgm::default_options(controller);
            if (std::ranges::any_of(held_slots, [controller](const HeldSlot& held) {
                return held.controller.Get() == controller;
            }))
            {
                continue;
            }
            const auto weak = make_uobject_weak(controller);
            const bool set = defaults && weak.Get() &&
                    sead_bgm::set_store_behaviour(controller, sead_bgm::store_behaviour_auto_resume);
            if (set)
            {
                HeldSlot held{};
                held.controller = weak;
                held.behaviour = defaults->store_behaviour;
                held_slots.push_back(held);
            }
            Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Form theme holds slot | slot={} default_store={} stay_suspend={} set={}\n"),
                    slot.name,
                    defaults ? static_cast<int>(defaults->store_behaviour) : -1,
                    defaults ? defaults->stay_suspend_time : -1.0f,
                    set);
        }
    }

    auto RuntimeBgmShuffleService::Impl::release_held_slots() -> void
    {
        for (const HeldSlot& held : held_slots)
        {
            if (UObject* const controller = held.controller.Get(); controller && is_uobject_valid_fast(controller))
            {
                (void)sead_bgm::set_store_behaviour(controller, held.behaviour);
            }
        }
        held_slots.clear();
    }
}
