#include "kh02_music_slots.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/features/music/events/sead_bgm.h"
#include "kh3ap/features/music/music_object_registry.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;

    namespace
    {
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;
        constexpr uint8_t unknown_bgm_state = 255;
        constexpr uint8_t slot_state_prepared = 2;

        struct ReflectedCall : Kh02ReflectedCall
        {
            ReflectedCall(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "kh02_story", reflected_parameter_limit_bytes)
            {
            }
        };

        auto path(UObject* value) -> std::string
        {
            return is_uobject_valid_fast(value) ? to_utf8(value->GetPathName()) : std::string{};
        }
    } // namespace

    auto Kh02MusicSlots::slot(std::wstring_view name) -> UObject*
    {
        for (auto* manager : music_objects::snapshot().managers)
        {
            if (auto* slot = sead_bgm::controller_for_slot(manager, name))
            {
                return slot;
            }
        }
        throw std::runtime_error("kh02_music_slot_missing");
    }

    void Kh02MusicSlots::prepare_battle(std::wstring_view battle)
    {
        // KH3 fills these slots during world loading, before the source script
        // starts. Replace the RG fallback without starting battle music at rest.
        if (!sead_bgm::set_path(slot(L"Battle"), battle))
        {
            throw std::runtime_error("kh02_battle_music_prepare");
        }
        prepared_battle_track = battle;
    }

    void Kh02MusicSlots::retire_source(bool entering_phantom, Json& retired)
    {
        retired = Json::array();
        for (auto* manager : music_objects::snapshot().managers)
        {
            for (const auto& slot : sead_bgm::slot_controllers(manager))
            {
                if (!is_uobject_valid_fast(slot.controller) ||
                    (slot.name != L"Event" && slot.name != L"Boss" && slot.name != L"Battle" &&
                     (!entering_phantom || slot.name != L"Field")))
                {
                    continue;
                }
                const auto track = sead_bgm::current_track(slot.controller);
                if (!track.starts_with(L"/Game/Zound/BGM/Rebellis/"))
                {
                    continue;
                }
                // Source Matinee can start Event directly, outside PlayBGM's
                // adapter. Its slot survives travel and otherwise prevents
                // the destination's field music from resuming.
                if (!sead_bgm::stop(slot.controller))
                {
                    throw std::runtime_error("kh02_source_music_retire");
                }
                retired.push_back({{"slot", to_utf8(slot.name)}, {"track", to_utf8(track)}});
            }
        }
    }

    void Kh02MusicSlots::retire_opening(Json& retired)
    {
        for (auto* manager : music_objects::snapshot().managers)
        {
            for (const auto& slot : sead_bgm::slot_controllers(manager))
            {
                if (!is_uobject_valid_fast(slot.controller))
                {
                    continue;
                }
                const auto state = sead_bgm::slot_state(slot.controller);
                const bool pending =
                    state && (*state == slot_state_prepared || *state == sead_bgm::slot_state_playing ||
                              *state == sead_bgm::slot_state_suspended || *state == sead_bgm::slot_state_auto_resume);
                if (!pending)
                {
                    continue;
                }
                const auto track = sead_bgm::current_track(slot.controller);
                if (!sead_bgm::stop(slot.controller))
                {
                    throw std::runtime_error("kh02_opening_previous_music_stop");
                }
                retired.push_back({{"slot", to_utf8(slot.name)}, {"track", to_utf8(track)}, {"state_before", *state}});
            }
        }
    }

    auto Kh02MusicSlots::release_opening_event() -> Json
    {
        auto* event = slot(L"Event");
        const auto state = sead_bgm::slot_state(event);
        const auto track = sead_bgm::current_track(event);
        const bool pending =
            state && (*state == slot_state_prepared || *state == sead_bgm::slot_state_playing ||
                      *state == sead_bgm::slot_state_suspended || *state == sead_bgm::slot_state_auto_resume);
        if (!track.empty() && !track.starts_with(L"/Game/Zound/"))
        {
            throw std::runtime_error("kh02_opening_event_music_owner");
        }
        if (pending && !sead_bgm::stop(event))
        {
            throw std::runtime_error("kh02_opening_event_music_stop");
        }
        // The source fade can clear Event's sound before retiring its slot.
        // Stop that slot after the original opening scripts unload, allowing
        // their queued Field request to resume through SEAD's normal policy.
        return {{"track", to_utf8(track)},
                {"state_before", state.value_or(unknown_bgm_state)},
                {"stop_requested", pending}};
    }

    auto Kh02MusicSlots::field_slot() -> UObject*
    {
        if (is_uobject_valid_fast(bgm_slot.Get()))
        {
            return bgm_slot.Get();
        }
        for (auto* manager : music_objects::snapshot().managers)
        {
            if (auto* c = sead_bgm::controller_for_slot(manager, L"Field"))
            {
                bgm_slot = make_uobject_weak(c);
                return c;
            }
        }
        throw std::runtime_error("kh02_compat_bgm_slot");
    }

    auto Kh02MusicSlots::release_arrival_menu(uint64_t now) -> Json
    {
        auto* menu = slot(L"Menu");
        const auto state = sead_bgm::slot_state(menu);
        if (!state || *state > sead_bgm::slot_state_auto_resume)
        {
            throw std::runtime_error("kh02_arrival_menu_music_state");
        }
        const bool pending = *state == slot_state_prepared || *state == sead_bgm::slot_state_playing ||
                             *state == sead_bgm::slot_state_suspended || *state == sead_bgm::slot_state_auto_resume;
        if (pending && !sead_bgm::stop(menu))
        {
            throw std::runtime_error("kh02_arrival_menu_music_stop");
        }
        Json release = {{"state_before", *state}, {"stop_requested", pending}, {"at_ms", now}};
        return release;
    }

    void Kh02MusicSlots::set_volume(float value, float fade_seconds)
    {
        auto* controller = source_music_slot.Get();
        if (!is_uobject_valid_fast(controller))
        {
            controller = field_slot();
        }
        ReflectedCall volume(controller, L"SetVolume");
        volume.number(L"Volume", value);
        volume.number(L"FadeTime", fade_seconds);
        volume.run();
    }

    auto Kh02MusicSlots::is_suspended(UObject* sound) -> bool
    {
        const auto track_path = sound->GetPathName();
        bool suspended = false;
        for (auto* manager : music_objects::snapshot().managers)
        {
            for (const auto& slot : sead_bgm::slot_controllers(manager))
            {
                if (is_uobject_valid_fast(slot.controller) && sead_bgm::current_track(slot.controller) == track_path)
                {
                    const auto state = sead_bgm::slot_state(slot.controller);
                    suspended |= state && (*state == sead_bgm::slot_state_suspended ||
                                           *state == sead_bgm::slot_state_auto_resume);
                }
            }
        }
        return suspended;
    }

    void Kh02MusicSlots::play(UObject* sound, std::wstring_view slot_name, float seek_time, float fade_time)
    {
        if (slot_name == L"Field")
        {
            release_scene_field(sound->GetPathName());
            // Matinee's Event slot can retain this field cue after the scene
            // finishes. Retire that copy when its source graph selects Field,
            // allowing ordinary battle music to take priority again.
        }
        auto* controller = slot(slot_name);
        if (auto* previous = source_music_slot.Get(); is_uobject_valid_fast(previous) && previous != controller &&
                                                      previous != field_slot() &&
                                                      sead_bgm::current_track(previous) == source_music_track)
        {
            sead_bgm::stop(previous);
        }
        ReflectedCall set(controller, L"Set");
        set.object(L"Sound", sound);
        set.run();
        ReflectedCall fade(controller, L"SetPlayFadeInTime");
        fade.number(L"FadeTime", fade_time);
        fade.run();
        if (!sead_bgm::set_seek_time(controller, seek_time) || !sead_bgm::play(controller))
        {
            throw std::runtime_error("kh02_source_music_play");
        }
        source_music_slot = make_uobject_weak(controller);
        source_music_track = sound->GetPathName();
    }

    auto Kh02MusicSlots::release_scene_field(std::wstring_view field) -> bool
    {
        if (field.empty())
        {
            return false;
        }
        auto* event = slot(L"Event");
        if (sead_bgm::current_track(event) != field)
        {
            return false;
        }
        if (!sead_bgm::stop(event))
        {
            throw std::runtime_error("kh02_field_event_music_handoff");
        }
        return true;
    }

    auto Kh02MusicSlots::stop(std::wstring_view requested_track, float fade_time) -> Json
    {
        Json stopped = Json::array();
        for (auto* manager : music_objects::snapshot().managers)
        {
            for (const auto& slot : sead_bgm::slot_controllers(manager))
            {
                if (!is_uobject_valid_fast(slot.controller) ||
                    (slot.name != L"Event" && slot.name != L"Boss" && slot.name != L"Battle" && slot.name != L"Field"))
                {
                    continue;
                }
                auto track = sead_bgm::current_track(slot.controller);
                // A suspended native Battle slot can report no current
                // instance while retaining the track prepared by this host.
                // Fade it too, so stopping Boss cannot resume another copy.
                if (track.empty() && slot.name == L"Battle")
                {
                    track = prepared_battle_track;
                }
                if (track.empty() && source_music_slot.Get() == slot.controller)
                {
                    track = source_music_track;
                }
                if (!track.starts_with(L"/Game/Zound/BGM/Rebellis/") ||
                    (!requested_track.empty() && track != requested_track))
                {
                    continue;
                }
                const auto state = sead_bgm::slot_state(slot.controller).value_or(unknown_bgm_state);
                ReflectedCall fade(slot.controller, L"SetStopFadeOutTime");
                fade.number(L"FadeTime", fade_time);
                fade.run();
                if (!sead_bgm::stop(slot.controller))
                {
                    throw std::runtime_error("kh02_source_music_fade");
                }
                stopped.push_back({{"slot", to_utf8(slot.name)}, {"track", to_utf8(track)}, {"state_before", state}});
            }
        }
        return stopped;
    }

    auto Kh02MusicSlots::snapshot() const -> Json
    {
        Json slots = Json::array();
        for (auto* manager : music_objects::snapshot().managers)
        {
            for (const auto& slot : sead_bgm::slot_controllers(manager))
            {
                if (is_uobject_valid_fast(slot.controller))
                {
                    slots.push_back({{"name", to_utf8(slot.name)},
                                     {"state", sead_bgm::slot_state(slot.controller).value_or(unknown_bgm_state)},
                                     {"track", to_utf8(sead_bgm::current_track(slot.controller))}});
                }
            }
        }
        return slots;
    }

    void Kh02MusicSlots::prepare_settings(UObject* world, const std::optional<std::wstring>& field,
                                          std::wstring_view battle, Json& rows)
    {
        const auto name = world->GetPathName();
        for (auto* settings : music_objects::snapshot().settings)
        {
            if (!settings->GetPathName().starts_with(name + L":PersistentLevel."))
            {
                continue;
            }
            Json row{{"object", path(settings)},
                     {"field_before", to_utf8(sead_bgm::read_asset_path(settings, L"m_BGMField").value_or(L""))},
                     {"battle_before", to_utf8(sead_bgm::read_asset_path(settings, L"m_BGMBattle").value_or(L""))}};
            if (field && !sead_bgm::write_asset_path(settings, L"m_BGMField", *field))
            {
                throw std::runtime_error("kh02_field_music_reference");
            }
            if (!sead_bgm::write_asset_path(settings, L"m_BGMBattle", battle))
            {
                throw std::runtime_error("kh02_battle_music_reference");
            }
            row["field"] = to_utf8(sead_bgm::read_asset_path(settings, L"m_BGMField").value_or(L""));
            row["battle"] = to_utf8(sead_bgm::read_asset_path(settings, L"m_BGMBattle").value_or(L""));
            rows.push_back(std::move(row));
        }
        if (rows.size() != 1)
        {
            throw std::runtime_error("kh02_host_music_settings_identity");
        }
    }

    void Kh02MusicSlots::restore_field(UObject* world, std::wstring_view track)
    {
        bool restored = false;
        for (auto* settings : music_objects::snapshot().settings)
        {
            if (settings->GetPathName().starts_with(world->GetPathName() + L":PersistentLevel."))
            {
                if (!sead_bgm::write_asset_path(settings, L"m_BGMField", track))
                {
                    throw std::runtime_error("kh02_opening_field_music_restore");
                }
                restored = true;
            }
        }
        if (!restored)
        {
            throw std::runtime_error("kh02_opening_field_music_settings_missing");
        }
    }

    void Kh02MusicSlots::reset_source()
    {
        source_music_slot = {};
        source_music_track.clear();
    }

} // namespace kh3ap
