#include "kh3ap/features/music/shuffle/bgm_shuffle_service_impl.h"
#include <Unreal/FFrame.hpp>
#include "kh3ap/features/music/shuffle/music_request_bytecode.h"

namespace kh3ap::bgm_shuffle_detail
{
    namespace
    {
        constexpr auto music_object_readiness_retry = std::chrono::milliseconds(100);
    }

    auto read_character_state(UObject* pawn) -> std::optional<uint8_t>
    {
        UFunction* const function = pawn->GetFunctionByNameInChain(STR("GetStateID"));
        FProperty* const result =
                function && is_uobject_valid_fast(function) ? find_function_param_by_name(function, STR("ReturnValue")) : nullptr;
        if (!result || result->GetElementSize() != 1 || function->GetParmsSize() <= 0)
        {
            return std::nullopt;
        }
        std::vector<std::byte> params(static_cast<size_t>(function->GetParmsSize()), std::byte{0});
        {
            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            pawn->ProcessEvent(function, params.data());
        }
        return *result->ContainerPtrToValuePtr<uint8_t>(params.data());
    }

    auto held_weapon_class(UObject* pawn) -> std::wstring
    {
        UObject* const equipment = read_strong_object_property(pawn, STR("MyEquipment"));
        if (!equipment)
        {
            return {};
        }
        for (UObject* const weapon : read_object_array_property(equipment, STR("m_WeaponEquips")))
        {
            if (weapon && is_uobject_valid_fast(weapon))
            {
                return weapon->GetClassPrivate()->GetName();
            }
        }
        return {};
    }

    auto lower(std::wstring text) -> std::wstring
    {
        for (wchar_t& ch : text)
        {
            ch = static_cast<wchar_t>(std::towlower(ch));
        }
        return text;
    }

    auto world_code_of(std::wstring_view track) -> std::wstring_view
    {
        for (const BgmShuffleWorld& world : bgm_shuffle_worlds())
        {
            for (const auto* list : {&world.fields, &world.battles})
            {
                for (const std::wstring_view candidate : *list)
                {
                    if (candidate == track)
                    {
                        return world.code;
                    }
                }
            }
        }
        return {};
    }

    auto persistent_level_package(std::wstring_view level) -> std::wstring
    {
        return lower(STR("/") + std::wstring{level} + STR(".") + std::wstring{level} + STR(":"));
    }

    auto load_asset_name_for_level(std::wstring_view level) -> std::wstring
    {
        std::wstring code{level.substr(0, level.find(STR('_')))};
        for (wchar_t& ch : code)
        {
            ch = static_cast<wchar_t>(std::towupper(ch));
        }
        return STR("TresWorldLoad") + code;
    }
}

namespace kh3ap
{
    auto RuntimeBgmShuffleService::now_playing_title() const -> const std::wstring&
    {
        return impl_->now_playing;
    }
    auto RuntimeBgmShuffleService::Impl::tick() -> void
    {
        ++perf.frames;
        report_perf();
        tick_steps();
    }

    auto RuntimeBgmShuffleService::Impl::tick_steps() -> void
    {
        if (!events_initialized)
        {
            events_initialized = true;
            music_objects::initialize();
            (void)install_sead_music_events();
            initialize_custom_music();
        }
        // Only a currently running fade has frame work. No engine reads.
        custom_player.update();
        uint32_t updates = take_music_updates();
        uint32_t native_events = take_sead_music_events();
        // A form's weapon can revert while the pawn remains in Idle. Treat
        // the owning replacement operation like a state-change notification.
        if (native_events & sead_music_event_bit(SeadMusicEvent::EquipmentRebuilt))
        {
            updates |= static_cast<uint32_t>(MusicUpdate::Form);
            native_events &= ~sead_music_event_bit(SeadMusicEvent::EquipmentRebuilt);
        }
        if (!follow_form_events) updates &= ~static_cast<uint32_t>(MusicUpdate::Form);
        const auto now = Clock::now();
        if (object_ready_at && now >= *object_ready_at)
        {
            object_ready_at.reset();
            updates |= static_cast<uint32_t>(MusicUpdate::Objects);
        }
        const bool form_due = form_deadline && now >= *form_deadline;
        if (!updates && !native_events && !form_due) return;
        form_deadline.reset();
        updates |= std::exchange(deferred_updates, 0);
        native_events |= std::exchange(deferred_native_events, 0);
        const bool gameplay = config.gameplay_ready && config.gameplay_ready();
        duck_custom_music(!gameplay || (config.menu_open && config.menu_open()));
        follow_volume_settings();
        if (!gameplay)
        {
            now_playing.clear();
            deferred_updates |= updates;
            deferred_native_events |= native_events;
            if (gameplay_active)
            {
                (void)stop_form_theme();
                form_tracker.reset();
                form_requested_track.reset();
                form_observation_dirty = true;
                form_deadline.reset();
                stop_custom_music();
                applied_level.clear();
                level_needs_apply = true;
            }
            gameplay_active = false;
            return;
        }
        gameplay_active = true;
        if ((native_events & sead_music_event_bit(SeadMusicEvent::Reset)) && lower(applied_level) == garden_level)
        {
            // GoA's BeginPlay can reset a controller that was already queued
            // during loading. Its native Reset notification invalidates that
            // route once; unrelated object-ready events still leave it alone.
            garden_field_controller = {};
            garden_field_target.clear();
            level_needs_apply = true;
        }
        follow_form_events = form_themes_on() || any_form_custom_music();
        form_observation_dirty = form_due || (updates & (
            static_cast<uint32_t>(MusicUpdate::Startup) | static_cast<uint32_t>(MusicUpdate::Settings) |
            static_cast<uint32_t>(MusicUpdate::Context) | static_cast<uint32_t>(MusicUpdate::Menu) |
            static_cast<uint32_t>(MusicUpdate::Form)));
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        ++music_updates;
        if (native_events) { ++native_event_updates; last_native_events = native_events; }
#endif
        if (updates & static_cast<uint32_t>(MusicUpdate::Objects)) level_needs_apply = true;
        const SlotMappingScope slots{*this};
        // Refresh after all rewrites/custom starts, before releasing this
        // event's slot mapping. Early returns in Vanilla mode count too.
        struct RefreshTitle { Impl& owner; ~RefreshTitle() { owner.update_now_playing(); } } refresh_title{*this};
        {
            const PerfScope scope{perf, PerfStep::Table};
            refresh_table();
        }
        {
            const PerfScope scope{perf, PerfStep::LogChanges};
            log_music_changes();
        }
        {
            const PerfScope scope{perf, PerfStep::LevelChange};
            apply_on_level_change(STR("level_change"));
        }
        if (!active()) return;
        if (!hook_installed)
        {
            install_hook();
        }
        {
            const PerfScope scope{perf, PerfStep::FormTheme};
            update_form_theme(now);
            report_slots(now);
        }
        {
            const PerfScope scope{perf, PerfStep::SwapOriginals};
            swap_playing_originals(now);
        }
        {
            const PerfScope scope{perf, PerfStep::SwapGroups};
            swap_group_tracks(now);
        }
        {
            const PerfScope scope{perf, PerfStep::CustomSlots};
            follow_volume_settings();
            update_custom_slots();
        }
    }

    auto RuntimeBgmShuffleService::Impl::at_home(std::wstring_view slot_name, const std::wstring& track) const -> bool
    {
        const std::wstring key{slot_name};
        if (const auto found = slot_written.find(key); found != slot_written.end() && found->second == track)
        {
            return false;
        }
        if (const auto found = group_written.find(key);
            found != group_written.end() && found->second.controller.Get() &&
            found->second.replacement != found->second.original && found->second.replacement == track)
        {
            return false;
        }
        const std::wstring_view world = world_code_of(track);
        return world.empty() || level_world.empty() || world == level_world;
    }

    auto RuntimeBgmShuffleService::Impl::active() const -> bool
    {
        return lower(applied_level) == garden_level || !swaps.empty() || !written.empty() ||
               !custom_library.tracks.empty() || custom_player.active() || form_themes_on() ||
               form_on_event ||
               std::ranges::any_of(group_written,
                                   [](const auto& entry)
                                   {
                                       return !entry.second.original.empty();
                                   }) ||
               std::ranges::any_of(slot_muted,
                                   [](bool muted)
                                   {
                                       return muted;
                                   }) ||
               std::ranges::any_of(slot_written,
                                   [](const auto& entry)
                                   {
                                       return !entry.second.empty();
                                   });
    }

    auto RuntimeBgmShuffleService::Impl::refresh_table() -> void
    {
        const int32_t mode = config.mode ? config.mode() : 0;
        std::wstring identity = config.seed_identity ? config.seed_identity() : std::wstring{};
        const bool chaos = static_cast<BgmShuffleMode>(mode) == BgmShuffleMode::Chaos;
        const bool include_custom = chaos && (!config.include_custom_music || config.include_custom_music());
        std::wstring pools = include_custom ? custom_library.signature : std::wstring{};
        if (table_built && mode == table_mode && identity == table_identity && pools == table_pools &&
            include_custom == table_include_custom_music)
        {
            return;
        }
        table_built = true;
        table_mode = mode;
        table_include_custom_music = include_custom;
        table_identity = std::move(identity);
        table_pools = std::move(pools);
        swaps = build_bgm_swaps(
                static_cast<BgmShuffleMode>(mode),
                table_identity,
                include_custom ? custom_library.pools() : BgmCustomPools{});
        world_replacements.clear();
        for (const auto& [original, replacement] : swaps)
        {
            if (!bgm_shuffle_group_of(original))
            {
                world_replacements.insert(replacement);
            }
        }
        // A new table applies to the current level too.
        level_needs_apply = true;
        playing_retry_at.clear();
        group_retry_at.clear();
        Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Music shuffle table built | mode={} swaps={}\n"),
                mode,
                swaps.size());
    }

    auto RuntimeBgmShuffleService::Impl::replacement_for(const std::wstring& original) const -> const std::wstring*
    {
        if (overridden(original))
        {
            return nullptr;
        }
        const auto found = swaps.find(original);
        return found != swaps.end() && !is_custom_bgm_track(found->second) ? &found->second : nullptr;
    }

    auto RuntimeBgmShuffleService::Impl::install_hook() -> void
    {
        hook_installed = true;
        play_function = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr,
                nullptr,
                std::wstring{play_function_path}.c_str());
        if (!play_function || !is_uobject_valid_fast(play_function))
        {
            play_function = nullptr;
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Music shuffle Play hook target missing; native event reconciliation remains available\n"));
            return;
        }
        if (auto* function = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/SQEXSEAD.SQEXSEADBGMSlotController:SetPath"));
            is_uobject_valid_fast(function))
        {
            set_path_function = make_uobject_weak(function);
            if (set_path_function.Get())
                set_path_hook_ids = UObjectGlobals::RegisterHook(function,
                    [this](UnrealScriptFunctionCallableContext& context, void*) {
                        // Our own SetPath calls already carry their final target.
                        // Mapping them again would shuffle replacements twice.
                        if (RuntimeGrantInternalCallDepth::is_active() || !active())
                        {
                            return;
                        }
                        auto* function = static_cast<UFunction*>(set_path_function.Get());
                        // Only a function-owned parameter frame is writable.
                        // Never interpret a Blueprint caller's locals as SetPath arguments.
                        if (get_frame_node(context.TheStack) != function)
                        {
                            auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&context.TheStack);
                            if (is_uobject_valid_fast(frame->Node) && frame->Code)
                            {
                                auto& script = frame->Node->GetScript();
                                const auto begin = reinterpret_cast<uintptr_t>(script.GetData());
                                const auto code = reinterpret_cast<uintptr_t>(frame->Code);
                                if (begin && code >= begin && code - begin < static_cast<size_t>(script.Num()))
                                    if (const auto requested = music_request_literal(
                                        {script.GetData() + (code - begin), static_cast<size_t>(script.Num()) - (code - begin)}))
                                        if (const auto replacement = intercept_requested_track(context.Context, *requested))
                                            pending_music_requests.push_back(
                                                {&context.TheStack, make_uobject_weak(context.Context), *replacement});
                            }
                            return;
                        }
                        void* params = get_frame_locals(context.TheStack);
                        const auto requested = sead_bgm::read_set_path_argument(function, params);
                        if (!requested) return;
                        if (const auto replacement = intercept_requested_track(context.Context, *requested))
                            (void)sead_bgm::write_set_path_argument(function, params, *replacement);
                    }, [this](UnrealScriptFunctionCallableContext& context, void*) {
                        if (RuntimeGrantInternalCallDepth::is_active() || pending_music_requests.empty()) return;
                        if (pending_music_requests.back().frame != &context.TheStack) return;
                        auto request = std::move(pending_music_requests.back());
                        pending_music_requests.pop_back();
                        // Blueprint arguments are expressions in the caller's
                        // frame, not writable SetPath locals. Replace the queued
                        // path immediately after SetPath, before its following Play,
                        // without modifying the caller or its bytecode.
                        if (auto* controller = request.controller.Get())
                        {
                            const bool queued = sead_bgm::set_path(controller, request.replacement);
                            Output::send<LogLevel::Normal>(
                                STR("[KH3APProbeCpp] Music shuffle redirected Blueprint request | target={} queued={}\n"),
                                request.replacement, queued);
                        }
                    }, nullptr);
        }
        // Level Blueprints call Play on the Field slot once its track is
        // assigned: the earliest moment to redirect it when this fires
        // before the queued event is drained.
        play_hook_ids = UObjectGlobals::RegisterHook(
                play_function,
                [this](UnrealScriptFunctionCallableContext& context, void*) {
                    const PerfScope scope{perf, PerfStep::PlayHook};
                    if (active())
                    {
                        if (!RuntimeGrantInternalCallDepth::is_active()) note_slot_restart(context.Context);
                        apply_on_level_change(STR("blueprint_play"));
                    }
                },
                [](UnrealScriptFunctionCallableContext&, void*) {},
                nullptr);
    }

    auto RuntimeBgmShuffleService::Impl::note_slot_restart(UObject* controller) -> void
    {
        // A new playback request is not a failed asynchronous SetPath attempt.
        // In particular a same-map Retry must not inherit its old cooldown.
        playing_retry_at.erase(controller);
        group_retry_at.erase(controller);
        request_music_update(MusicUpdate::Context);
    }

    auto RuntimeBgmShuffleService::Impl::intercept_requested_track(
            UObject* controller, const std::wstring& requested) -> std::optional<std::wstring>
    {
        if (!is_uobject_valid_fast(controller))
        {
            return std::nullopt;
        }
        const SlotMappingScope slots{*this};
        const auto found = std::ranges::find_if(slots.entries(), [controller](const auto& slot) {
            return slot.controller() == controller;
        });
        if (found == slots.entries().end() || std::ranges::find(group_slots, found->name) == group_slots.end())
            return std::nullopt; // Event/cutscenes keep their timing and form ownership.
        note_slot_restart(controller);
        const bool world_slot = std::ranges::find(swapped_slots, found->name) != swapped_slots.end();
        if ((found->name == swapped_slots[0] && level_overridden()) ||
            (!bgm_shuffle_group_of(requested) && (world_code_of(requested).empty() ||
             (world_slot && world_code_of(requested) != level_world)))) return std::nullopt;
        const auto* replacement = replacement_for(requested);
        if (!replacement || *replacement == requested) return std::nullopt;
        const auto weak = make_uobject_weak(controller);
        if (!weak.Get()) return std::nullopt;
        // Remember provenance before the original becomes audible, including
        // while the new slot is Ready and its Sound_ has not resolved yet.
        remember_slot_rewrite(found->name, controller, requested, *replacement, sead_bgm::current_track(controller));
        if (world_slot && !bgm_shuffle_group_of(requested)) slot_written[found->name] = *replacement;
        Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Music shuffle intercepted request | slot={} {} -> {}\n"),
                found->name, requested, *replacement);
        return *replacement;
    }

    auto RuntimeBgmShuffleService::Impl::remember_slot_rewrite(
            std::wstring_view slot, UObject* controller, const std::wstring& original,
            const std::wstring& replacement, const std::wstring& previous) -> void
    {
        const auto weak = make_uobject_weak(controller);
        if (!weak.Get()) return;
        auto& ours = group_written[std::wstring{slot}];
        ours.controller = weak;
        ours.original = original;
        ours.replacement = replacement;
        ours.pending_from = previous;
        ours.queued_until = Clock::now() + playing_retry_interval;
        ours.finished_retry_at = {};
        ours.finished_restarts = 0;
    }

    auto RuntimeBgmShuffleService::Impl::refresh_world_context() -> bool
    {
        const auto objects = music_objects::snapshot();
        auto* pawn = config.sora_pawn ? config.sora_pawn() : nullptr;
        const auto level = sead_bgm::current_level_name(pawn);
        UObject* selected = nullptr;
        if (level && !level->empty())
        {
            const auto own_package = persistent_level_package(*level);
            for (auto* settings : objects.settings)
                if (lower(settings->GetPathName()).find(own_package) != std::wstring::npos)
                { selected = settings; break; }
        }
        // Before a pawn exists, retain the original startup-context behavior.
        else if (!objects.settings.empty()) selected = objects.settings.front();
        auto* previous = world_context.Get();
        world_context = make_uobject_weak(selected);
        if (objects.unfinished) object_ready_at = Clock::now() + music_object_readiness_retry;
        else object_ready_at.reset();
        return previous != world_context.Get();
    }

    auto RuntimeBgmShuffleService::Impl::report_waiting(std::wstring_view reason) -> void
    {
        if (waiting_reason == reason)
        {
            return;
        }
        waiting_reason = reason;
        Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Music shuffle waiting | reason={} world_settings={}\n"),
                reason,
                music_objects::snapshot().settings.size());
    }

    auto RuntimeBgmShuffleService::Impl::bgm_manager() -> UObject*
    {
        if (!manager.Get())
        {
            const auto objects = music_objects::snapshot();
            manager = make_uobject_weak(objects.managers.empty() ? nullptr : objects.managers.front());
        }
        return manager.Get();
    }

    auto RuntimeBgmShuffleService::Impl::original_value(UObject* object, const wchar_t* property) -> std::wstring
    {
        const std::wstring key = object->GetPathName() + STR("|") + property;
        const std::wstring current = sead_bgm::read_asset_path(object, property).value_or(STR(""));
        const auto written_entry = written.find(key);
        if (written_entry != written.end() && written_entry->second == current)
        {
            return originals[key];
        }
        written.erase(key);
        originals[key] = current;
        return current;
    }

    auto RuntimeBgmShuffleService::Impl::rewrite(UObject* object, const wchar_t* property, const std::wstring& original) -> bool
    {
        const std::wstring key = object->GetPathName() + STR("|") + property;
        const std::wstring* const replacement = replacement_for(original);
        if (!replacement && !written.contains(key))
        {
            return false;
        }
        const std::wstring& target = replacement ? *replacement : original;
        if (sead_bgm::read_asset_path(object, property).value_or(STR("")) != target &&
            !sead_bgm::write_asset_path(object, property, target))
        {
            return false;
        }
        if (replacement)
        {
            written[key] = *replacement;
        }
        else
        {
            written.erase(key);
        }
        return true;
    }

    auto RuntimeBgmShuffleService::Impl::swap_slot(std::wstring_view slot_name, const std::wstring& original) -> std::wstring_view
    {
        // An overridden level keeps its field music out of the shuffle.
        const bool level_kept = slot_name == swapped_slots[0] && level_overridden();
        const bool garden_field = slot_name == swapped_slots[0] && lower(applied_level) == garden_level;
        const std::wstring* const replacement = level_kept ? nullptr : replacement_for(original);
        std::wstring& ours = slot_written[std::wstring{slot_name}];
        if (!replacement && ours.empty() && !level_kept && !garden_field)
        {
            return STR("unmapped");
        }
        const SlotMappingScope slots{*this};
        UObject* const controller = slots.find(slot_name);
        if (!controller)
        {
            return STR("no_controller");
        }
        const std::wstring current = sead_bgm::current_track(controller);
        if (garden_field)
        {
            // The Blueprint's fire-and-forget sound is always suppressed.
            // SetPath can load cold assets asynchronously; only this slot
            // plays the chosen native track, with the usual custom-file fallback.
            const std::wstring target = replacement ? *replacement : original;
            if (target.empty()) return STR("no_garden_track");
            if (garden_field_controller.Get() == controller && garden_field_target == target)
                return STR("garden_queued");
            const auto state = sead_bgm::slot_state(controller);
            if (state == sead_bgm::slot_state_suspended || state == sead_bgm::slot_state_auto_resume)
            {
                // SetPath itself clears SEAD's held sound. Keep the current
                // route until its native resume/stop event wakes reconciliation.
                level_needs_apply = true;
                return STR("garden_held");
            }
            const bool start = garden_field_controller.Get() != controller || garden_field_target.empty();
            // An existing playing route only needs its queued path replaced.
            if (!sead_bgm::set_path(controller, target) || (start && !sead_bgm::play(controller)))
            {
                level_needs_apply = true;
                return STR("garden_dispatch_failed");
            }
            garden_field_controller = make_uobject_weak(controller);
            garden_field_target = target;
            ours = replacement ? target : std::wstring{};
            remember_slot_rewrite(slot_name, controller, original, target, current);
            return STR("garden_field");
        }
        if (!replacement)
        {
            // Only a track this service put there goes back to the
            // level's original; anything else is the game's choice. An
            // overridden level gets its original back from a world load
            // asset rewritten for its neighbours too. Suspended/AutoPlay
            // slots read empty after SetPath: replace our queued path too,
            // or the last shuffle resumes after switching back to Vanilla.
            const bool restore = !original.empty() && current != original &&
                    (current == ours || (current.empty() && !ours.empty()) ||
                     (level_kept && !current.empty()));
            if (!restore)
            {
                ours.clear();
                return STR("unmapped");
            }
            if (!sead_bgm::set_path(controller, original))
            {
                level_needs_apply = true;
                return STR("set_path_failed");
            }
            ours.clear();
            return STR("restored");
        }
        if (current == *replacement)
        {
            ours = *replacement;
            remember_slot_rewrite(slot_name, controller, original, *replacement, current);
            return STR("already_replaced");
        }
        if (!sead_bgm::set_path(controller, *replacement))
        {
            return STR("set_path_failed");
        }
        ours = *replacement;
        remember_slot_rewrite(slot_name, controller, original, *replacement, current);
        return STR("ok");
    }

    auto RuntimeBgmShuffleService::Impl::apply_on_level_change(std::wstring_view reason) -> void
    {
        const bool context_replaced = refresh_world_context();
        const auto level = sead_bgm::current_level_name(world_context.Get());
        if (!level || level->empty())
        {
            report_waiting(!world_context.Get() ? STR("no_world_settings") : STR("no_level_name"));
            return;
        }
        const bool level_changed = *level != applied_level || context_replaced;
        if (!level_changed && !level_needs_apply)
        {
            return;
        }
        const auto objects = music_objects::snapshot();
        applied_level = *level;
        level_needs_apply = false;
        waiting_reason.clear();
        // group_written is kept: a fight can run across levels (the final
        // fight goes bt_02 to bt_04 on one Boss slot), and forgetting
        // the replacement written there made it swap it again. A slot
        // that really reloads goes idle first, which clears its entry.
        // Rebuilding a table or reloading custom overrides is not a map
        // transition: keep ownership of active form themes and held music.
        if (level_changed)
        {
            (void)stop_form_theme();
            form_tracker.reset();
            form_requested_track.reset();
            form_observation_dirty = true;
            form_deadline.reset();
            stop_custom_music();
            note_visited_level(*level);
            garden_field_controller = {};
            garden_field_target.clear();
        }

        std::array<std::wstring, 2> level_originals{};
        std::wstring originals_source{STR("none")};
        size_t rewrites = 0;
        // Objects no longer live were unloaded; they come back from disk
        // with their originals, so their bookkeeping is dropped.
        std::unordered_set<std::wstring> live_keys{};
        const auto track = [&live_keys](UObject* object, const wchar_t* property) {
            live_keys.insert(object->GetPathName() + STR("|") + property);
        };
        // Only the persistent level's own WorldSettings speak for the
        // level (GoA_01 sets its track there). Streamed sublevels carry
        // their own WorldSettings, some of them filled, and a departing
        // level's can outlive the transition.
        const std::wstring own_package = persistent_level_package(*level);
        for (UObject* const settings : objects.settings)
        {
            const bool own = lower(settings->GetPathName()).find(own_package) != std::wstring::npos;
            for (size_t index = 0; index < bgm_properties.size(); ++index)
            {
                track(settings, bgm_properties[index]);
                const std::wstring original = original_value(settings, bgm_properties[index]);
                if (own && level_originals[index].empty() && !original.empty())
                {
                    level_originals[index] = original;
                    originals_source = settings->GetPathName();
                }
                rewrites += rewrite(settings, bgm_properties[index], original) ? 1 : 0;
            }
        }
        const std::wstring level_asset = load_asset_name_for_level(*level);
        for (UObject* const asset : objects.load_assets)
        {
            const bool own = asset->GetName() == level_asset;
            for (size_t index = 0; index < bgm_properties.size(); ++index)
            {
                track(asset, bgm_properties[index]);
                const std::wstring original = original_value(asset, bgm_properties[index]);
                if (own && level_originals[index].empty())
                {
                    level_originals[index] = original;
                    originals_source = asset->GetName();
                }
                rewrites += rewrite(asset, bgm_properties[index], original) ? 1 : 0;
            }
        }
        std::erase_if(written, [&live_keys](const auto& entry) { return !live_keys.contains(entry.first); });
        std::erase_if(originals, [&live_keys](const auto& entry) { return !live_keys.contains(entry.first); });

        if (lower(*level) == garden_level && level_originals[0].empty())
            level_originals[0] = runtime_goa_music_path;
        level_world = world_code_of(level_originals[0]);
        if (level_world.empty())
        {
            level_world = world_code_of(level_originals[1]);
        }
        const std::wstring_view field = swap_slot(swapped_slots[0], level_originals[0]);
        const std::wstring_view battle = swap_slot(swapped_slots[1], level_originals[1]);
        Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Music shuffle applied | level={} via={} source={} rewrites={} field={} ({}) battle={} ({})\n"),
                *level,
                reason,
                originals_source,
                rewrites,
                field,
                level_originals[0],
                battle,
                level_originals[1]);
    }

    auto RuntimeBgmShuffleService::Impl::swap_playing_originals(Clock::time_point now) -> void
    {
        // A form theme is often the current world's own track.
        if (level_world.empty() || form_theme_playing())
        {
            return;
        }
        const SlotMappingScope slots{*this};
        for (const std::wstring_view slot_name : swapped_slots)
        {
            UObject* const controller = slots.find(slot_name);
            if (!controller || sead_bgm::slot_state(controller) != sead_bgm::slot_state_playing)
            {
                const auto state = controller ? sead_bgm::slot_state(controller) : std::nullopt;
                if (state == sead_bgm::slot_state_stop || state == sead_bgm::slot_state_finished || state == 0)
                    playing_retry_at.erase(controller);
                continue;
            }
            const std::wstring track = sead_bgm::current_track(controller);
            const std::wstring* const replacement = replacement_for(track);
            if (!replacement || world_code_of(track) != level_world ||
                (slot_name == swapped_slots[0] && level_overridden()))
            {
                continue;
            }
            auto& retry_at = playing_retry_at[controller];
            if (now < retry_at)
            {
                continue;
            }
            retry_at = now + playing_retry_interval;
            const bool switched = sead_bgm::set_path(controller, *replacement);
            if (switched)
            {
                slot_written[std::wstring{slot_name}] = *replacement;
                remember_slot_rewrite(slot_name, controller, track, *replacement, track);
            }
            Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Music shuffle switched playing {} | {} -> {} ok={}\n"),
                    slot_name,
                    track,
                    *replacement,
                    switched);
        }
    }

    auto RuntimeBgmShuffleService::Impl::swap_group_tracks(Clock::time_point now) -> void
    {
        const SlotMappingScope slots{*this};
        for (const std::wstring_view slot_name : group_slots)
        {
            UObject* const controller = slots.find(slot_name);
            const auto state = controller ? sead_bgm::slot_state(controller) : std::nullopt;
            const std::wstring key{slot_name};
            GroupRewrite& ours = group_written[key];
            if (ours.controller.Get() != controller)
            {
                ours = {};
            }
            // Chaos can replace a looping encounter/field theme with a short
            // native cue. Finished belongs to that replacement, not the game's
            // original request. Preserve provenance and restart the currently
            // selected target; Stop/Invalid still release ownership below.
            if (state == sead_bgm::slot_state_finished && !ours.original.empty() &&
                ours.replacement != ours.original)
            {
                auto& retry_at = ours.finished_retry_at;
                if (now < retry_at) continue;
                const auto* replacement = replacement_for(ours.original);
                const std::wstring target = replacement ? *replacement : ours.original;
                const bool restarted = sead_bgm::set_path(controller, target) && sead_bgm::play(controller);
                if (restarted)
                {
                    ++ours.finished_restarts;
                    ours.pending_from = sead_bgm::current_track(controller);
                    ours.replacement = target;
                    ours.queued_until = now + playing_retry_interval;
                    // Successful short cues can end again within five seconds.
                    // Only failed dispatches need the failure backoff.
                    retry_at = {};
                }
                else retry_at = now + playing_retry_interval;
                Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Music shuffle restarted finished replacement | slot={} target={} ok={}\n"),
                    slot_name, target, restarted);
                continue;
            }
            if (!state || std::ranges::find(slot_idle_states, *state) != slot_idle_states.end())
            {
                if (state == 1 && now < ours.queued_until) continue;
                group_retry_at.erase(controller);
                ours = {};
                continue;
            }
            if (*state != sead_bgm::slot_state_playing)
            {
                continue;
            }
            const std::wstring track = sead_bgm::current_track(controller);
            // SetPath loads asynchronously, so the original (or an empty
            // track) can remain visible until our replacement is ready.
            const bool owned = !ours.original.empty() &&
                    (track == ours.replacement || track == ours.original || track == ours.pending_from || track.empty());
            if (!owned)
            {
                ours = {};
            }
            const bool world_slot = std::ranges::find(swapped_slots, slot_name) != swapped_slots.end();
            const bool shuffled = bgm_shuffle_group_of(track) || (!world_slot && !world_code_of(track).empty());
            if ((!form_track.empty() && track == form_track) || (!owned &&
                (track.empty() || !shuffled ||
                 (world_slot && world_replacements.contains(track)))))
            {
                continue;
            }
            const std::wstring original = owned ? ours.original : track;
            const std::wstring* const replacement = replacement_for(original);
            if (!replacement && !owned)
            {
                continue;
            }
            const std::wstring target = replacement ? *replacement : original;
            if (owned && target == track && target == ours.replacement)
            {
                ours.queued_until = {};
                ours.pending_from.clear();
                if (!replacement)
                {
                    ours = {};
                }
                continue;
            }
            if (owned && target == ours.replacement && now < ours.queued_until) continue;
            auto& retry_at = group_retry_at[controller];
            if (now < retry_at)
            {
                continue;
            }
            retry_at = now + playing_retry_interval;
            const auto weak = make_uobject_weak(controller);
            const bool switched = weak.Get() && sead_bgm::set_path(controller, target);
            if (switched)
            {
                ours.controller = weak;
                ours.original = original;
                ours.replacement = target;
                ours.pending_from = track;
            }
            Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Music shuffle switched playing {} | {} -> {} ok={}\n"),
                    slot_name,
                    track,
                    target,
                    switched);
        }
    }

    RuntimeBgmShuffleService::RuntimeBgmShuffleService(RuntimeBgmShuffleConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeBgmShuffleService::~RuntimeBgmShuffleService() = default;

    auto RuntimeBgmShuffleService::tick() -> void
    {
        impl_->tick();
    }
}
