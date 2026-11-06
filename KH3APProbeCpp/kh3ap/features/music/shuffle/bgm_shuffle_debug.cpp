#include "kh3ap/features/music/shuffle/bgm_shuffle_service_impl.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/bridge/utilities.h"
#include <sstream>
#endif

namespace kh3ap
{
    namespace
    {
        constexpr auto slow_game_frame_rate_threshold = 40;
        constexpr auto music_time_share_denominator = 200;
        constexpr auto slow_music_step_threshold = std::chrono::milliseconds(4);
    }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto RuntimeBgmShuffleService::test_snapshot() -> std::string
    {
        std::ostringstream out;
        const auto events = sead_music_event_snapshot();
        const auto objects = music_objects::snapshot();
        out << std::boolalpha << "{\"native_events\":{\"installed\":" << events.installed
            << ",\"game_thread\":" << events.game_thread << ",\"other_thread_calls\":" << events.other_thread_calls
            << ",\"suppressed_calls\":" << events.suppressed_calls << ",\"updates\":" << impl_->native_event_updates
            << ",\"last_mask\":" << impl_->last_native_events << ",\"calls\":{";
        for (size_t i = 0; i < events.calls.size(); ++i)
        {
            if (i) out << ',';
            out << '"' << sead_music_event_names[i] << "\":" << events.calls[i];
        }
        out << "}},\"level\":\"" << json_escape(impl_->applied_level)
            << "\",\"mode\":" << impl_->table_mode << ",\"gameplay\":" << impl_->gameplay_active
            << ",\"form_owned\":" << impl_->form_on_event << ",\"form_track\":\"" << json_escape(impl_->form_track)
            << "\",\"held_slots\":" << impl_->held_slots.size()
            << ",\"custom_directory\":\"" << json_escape(impl_->custom_directory.wstring())
            << "\",\"custom_tracks\":" << impl_->custom_library.tracks.size()
            << ",\"custom_signature\":\"" << json_escape(impl_->custom_library.signature) << '"'
            << ",\"custom_gain\":" << impl_->custom_gain << ",\"custom_ducked\":" << impl_->custom_ducked
            << ",\"test_stats\":{\"music_updates\":" << impl_->music_updates
            << ",\"slot_mapping_reads\":" << impl_->slot_mapping_reads
            << ",\"music_object_bootstrap_scans\":" << objects.bootstrap_scans
            << ",\"music_object_generation\":" << objects.generation
            << ",\"music_world_settings\":" << objects.settings.size()
            << ",\"music_load_assets\":" << objects.load_assets.size()
            << ",\"music_objects_unfinished\":" << objects.unfinished
            << ",\"custom_library_loads\":" << impl_->custom_library_loads << '}'
            << ",\"custom_pool_enabled\":" << impl_->table_include_custom_music
            << ",\"custom_pool_tracks\":" << (impl_->table_include_custom_music ? impl_->custom_library.tracks.size() : 0)
            << ",\"custom_draws\":[";
        std::vector<std::pair<std::wstring, std::wstring>> custom_draws;
        for (const auto& [original, replacement] : impl_->swaps)
            if (const auto* track = impl_->custom_library.for_table_entry(replacement))
                custom_draws.emplace_back(original, track->file.wstring());
        std::ranges::sort(custom_draws);
        bool draw_comma = false;
        for (const auto& [original, file] : custom_draws)
        {
            if (draw_comma) out << ',';
            draw_comma = true;
            out << "{\"original\":\"" << json_escape(original) << "\",\"file\":\"" << json_escape(file) << "\"}";
        }
        out << "],\"audio\":" << impl_->custom_player.test_snapshot() << ",\"slots\":[";
        bool comma = false;
        for (const auto& slot : sead_bgm::slot_controllers(impl_->bgm_manager()))
        {
            if (comma) out << ',';
            comma = true;
            const auto state = sead_bgm::slot_state(slot.controller);
            out << "{\"name\":\"" << json_escape(slot.name) << "\",\"controller\":\""
                << json_escape(slot.controller ? slot.controller->GetFullName() : L"")
                << "\",\"state\":" << (state ? static_cast<int>(*state) : -1)
                << ",\"track\":\"" << json_escape(sead_bgm::current_track(slot.controller)) << '"';
            for (size_t index = 0; index < custom_slots.size(); ++index)
                if (lower(slot.name) == lower(std::wstring{custom_slots[index]}))
                    out << ",\"custom_channel\":" << index << ",\"native_muted\":" << impl_->slot_muted[index];
            if (const auto found = impl_->group_written.find(slot.name); found != impl_->group_written.end())
                out << ",\"group_original\":\"" << json_escape(found->second.original) << '"'
                    << ",\"finished_restarts\":" << found->second.finished_restarts;
            out << '}';
        }
        out << "]}";
        return out.str();
    }
#endif
    auto RuntimeBgmShuffleService::Impl::report_perf() -> void
    {
        // Diagnostic builds only: player builds keep their logs quiet.
        if constexpr (KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS == 0)
        {
            perf = PerfStats{};
            return;
        }
        const auto now = Clock::now();
        if (now - perf.since < perf_report_interval)
        {
            return;
        }
        static constexpr std::array<std::wstring_view, static_cast<size_t>(PerfStep::Count)> names{
                STR("frame"), STR("table"), STR("log_changes"), STR("level_change"), STR("form_theme"),
                STR("swap_originals"), STR("swap_groups"), STR("custom_slots"), STR("play_hook")};
        Clock::duration spent{};
        Clock::duration worst{};
        for (size_t index = 0; index < names.size(); ++index)
        {
            spent += perf.total[index];
            worst = (std::max)(worst, perf.worst[index]);
        }
        const auto window = now - perf.since;
        // More than 0.5 % of the time, one step over 4 ms, or the game
        // below 40 frames a second (frames counts the game's ticks), to
        // tell a slowdown from outside the service.
        const bool slow_game = perf.frames * std::chrono::seconds(1) < window * slow_game_frame_rate_threshold;
        if (spent * music_time_share_denominator > window || worst > slow_music_step_threshold || slow_game)
        {
            std::wstring steps{};
            for (size_t index = 0; index < names.size(); ++index)
            {
                if (perf.calls[index] == 0)
                {
                    continue;
                }
                steps += std::format(
                        STR(" {}={}x/{:.2f}ms/max{:.2f}ms"),
                        names[index],
                        perf.calls[index],
                        std::chrono::duration<double, std::milli>(perf.total[index]).count(),
                        std::chrono::duration<double, std::milli>(perf.worst[index]).count());
            }
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Music perf | window={:.1f}s frames={} busy={:.2f}ms{}\n"),
                    std::chrono::duration<double>(window).count(),
                    perf.frames,
                    std::chrono::duration<double, std::milli>(spent).count(),
                    steps);
        }
        perf = PerfStats{};
    }

    auto RuntimeBgmShuffleService::Impl::log_music_changes() -> void
    {
        // ESQEXSEAD_BGMState, in enum order.
        static constexpr std::array<std::wstring_view, 8> state_names{
                STR("Invalid"), STR("Ready"), STR("AutoPlay"), STR("Playing"),
                STR("Finished"), STR("Stop"), STR("Suspended"), STR("AutoResume")};
        const SlotMappingScope slots{*this};
        for (const auto& slot : slots.entries())
        {
            auto* controller = slot.controller();
            if (!controller)
            {
                continue;
            }
            const auto state = sead_bgm::slot_state(controller);
            const std::wstring track = sead_bgm::current_track(controller);
            LoggedSlot& logged = slot_logged[slot.name];
            // A track reads empty while it loads: that alone is no change.
            const bool track_changed = !track.empty() && track != logged.track;
            if (state == logged.state && !track_changed)
            {
                continue;
            }
            logged.state = state;
            if (!track.empty())
            {
                logged.track = track;
            }
            Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Music change | slot={} state={} track={} level={}\n"),
                    slot.name,
                    state && *state < state_names.size() ? state_names[*state]
                                                          : std::wstring_view{STR("?")},
                    logged.track,
                    applied_level);
        }
    }

    auto RuntimeBgmShuffleService::Impl::report_slots(Clock::time_point now) -> void
    {
        if (!slot_report || now < slot_report->first)
        {
            return;
        }
        std::wstring states{};
        const SlotMappingScope slots{*this};
        for (const auto& slot : slots.entries())
        {
            auto* controller = slot.controller();
            const auto state = controller ? sead_bgm::slot_state(controller) : std::nullopt;
            const std::wstring track = controller ? sead_bgm::current_track(controller) : std::wstring{};
            states += slot.name + STR("=") + (state ? std::to_wstring(*state) : std::wstring{STR("?")});
            if (!track.empty())
            {
                states += STR("(") + track.substr(track.rfind(STR('.')) + 1) + STR(")");
            }
            states += STR(" ");
        }
        Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Music slots after {} | {}\n"),
                slot_report->second,
                states);
        slot_report.reset();
    }
}
