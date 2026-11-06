#include "kh02_music_slots.h"
#include "kh02_story_music.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
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
        constexpr std::size_t event_history_limit = 64;
        constexpr std::size_t music_request_history_limit = 128;

        auto path(UObject* value) -> std::string
        {
            return is_uobject_valid_fast(value) ? to_utf8(value->GetPathName()) : std::string{};
        }
    } // namespace

    struct Kh02StoryMusic::Impl
    {
        explicit Impl(ResolveActorWorld resolver) : actor_world(resolver) {}

        ResolveActorWorld actor_world;
        FWeakObjectPtr music_world;
        Kh02MusicSlots slots;
        std::wstring field_track;
        Json music_proof = Json::object(), music_requests = Json::array();

        void prepare_music_slots(std::wstring_view battle = L"/Game/Zound/BGM/Rebellis/BGM_yami_battle.BGM_yami_battle")
        {
            slots.prepare_battle(battle);
            music_proof["battle_prepared"] = true;
        }

        void retire_source_music(bool entering_phantom)
        {
            slots.retire_source(entering_phantom, music_proof["retired_slots"]);
        }

        void retire_opening_music()
        {
            if (!music_proof.contains("opening_retired_slots"))
            {
                music_proof["opening_retired_slots"] = Json::array();
            }
            slots.retire_opening(music_proof["opening_retired_slots"]);
        }

        void release_opening_event_music()
        {
            if (!music_proof.contains("opening_event_released"))
            {
                music_proof["opening_event_released"] = slots.release_opening_event();
            }
        }

        void configure_music(UObject* w, int area, bool first_castle_intro)
        {
            if (music_world.Get() == w)
            {
                return;
            }
            const auto name = w->GetPathName();
            if (!area)
            {
                return;
            }
            const wchar_t* field = area == 2 || area == 9   ? L"BGM_yami_field"
                                   : area >= 3 && area <= 7 ? L"BGM_yami_field_sw"
                                   : area == 8              ? L"BGM_yami_field_sb"
                                                            : nullptr;
            music_proof = {{"world", to_utf8(name)}, {"settings", Json::array()}};
            retire_source_music(area == 7);
            if (area == 2 && first_castle_intro)
            {
                // First entry starts with silence. Retire the previous world's
                // pending/suspended slots too, before original DW009 selects music.
                retire_opening_music();
            }
            slots.reset_source();
            const auto battle = area == 7 ? L"/Game/Zound/BGM/Rebellis/BGM_genei_aqua.BGM_genei_aqua"
                                          : L"/Game/Zound/BGM/Rebellis/BGM_yami_battle.BGM_yami_battle";
            std::optional<std::wstring> field_path;
            if (field)
            {
                field_path = area == 2 && first_castle_intro
                                 ? std::wstring{}
                                 : std::wstring(L"/Game/Zound/BGM/Rebellis/") + field + L"." + field;
            }
            slots.prepare_settings(w, field_path, battle, music_proof["settings"]);
            field_track = field_path.value_or(std::wstring{});
            prepare_music_slots(battle);
            music_world = make_uobject_weak(w);
        }

        void play_source_music(UObject* script, UObject* sound, float seek_time, float fade_time,
                               bool opening_field_handoff, const std::function<void()>& on_handoff)
        {
            if (!is_uobject_valid_fast(sound) || !sound->GetPathName().starts_with(L"/Game/Zound/BGM/"))
            {
                throw std::runtime_error("kh02_source_music_asset");
            }
            auto* w = actor_world(script);
            if (!is_uobject_valid_fast(w) || w != music_world.Get())
            {
                if (music_requests.size() < music_request_history_limit)
                {
                    music_requests.push_back({{"rejected", true},
                                              {"caller", path(script)},
                                              {"caller_world", path(w)},
                                              {"music_world", path(music_world.Get())},
                                              {"track", path(sound)}});
                }
                return;
            }
            const auto name = sound->GetName();
            if (opening_field_handoff && name == L"BGM_yami_field")
            {
                // The original DW011 completion selects the field cue after
                // resetting the camera. It has no new loading-screen callback.
                // The validated authored cue also marks that handoff when an
                // optional music slot cannot be restored on this machine.
                on_handoff();
                slots.restore_field(w, sound->GetPathName());
                music_proof["opening_field_restored"] = true;
            }
            const wchar_t* slot = name == L"BGM_yami_battle" ? L"Battle"
                                  : name == L"BGM_yami_boss" || name == L"BGM_genei_aqua" || name == L"BGM_devilwave1"
                                      ? L"Boss"
                                      : L"Field";
            slots.play(sound, slot, seek_time, fade_time);
            if (music_requests.size() < event_history_limit)
            {
                music_requests.push_back(
                    {{"track", to_utf8(slots.source_track())}, {"slot", to_utf8(slot)}, {"caller", path(script)}});
            }
        }

        void stop_source_music(UObject* script, UObject* sound, float fade_time)
        {
            if (actor_world(script) != music_world.Get())
            {
                return;
            }
            const auto requested_track = is_uobject_valid_fast(sound) ? sound->GetPathName() : std::wstring{};
            auto stopped = slots.stop(requested_track, fade_time);
            if (music_requests.size() < music_request_history_limit)
            {
                music_requests.push_back({{"operation", "fade_out"},
                                          {"caller", path(script)},
                                          {"requested_track", to_utf8(requested_track)},
                                          {"fade_seconds", fade_time},
                                          {"stopped_slots", std::move(stopped)}});
            }
        }

        auto music_snapshot() -> Json
        {
            Json out = music_proof;
            out["requests"] = music_requests;
            out["slots"] = slots.snapshot();
            return out;
        }

        auto release_arrival_menu(uint64_t now) -> Json
        {
            auto release = slots.release_arrival_menu(now);
            music_proof["arrival_menu_release"] = release;
            return release;
        }

        void set_volume(float value, float fade_seconds)
        {
            slots.set_volume(value, fade_seconds);
        }

        auto is_suspended(UObject* sound) -> bool
        {
            return slots.is_suspended(sound);
        }
    };

    Kh02StoryMusic::Kh02StoryMusic(ResolveActorWorld resolver) : impl(std::make_unique<Impl>(resolver)) {}

    Kh02StoryMusic::~Kh02StoryMusic() = default;

    void Kh02StoryMusic::configure(UObject* world, int area, bool first_intro)
    {
        impl->configure_music(world, area, first_intro);
    }

    void Kh02StoryMusic::prepare_battle()
    {
        impl->prepare_music_slots();
    }

    void Kh02StoryMusic::retire_opening()
    {
        impl->retire_opening_music();
    }

    void Kh02StoryMusic::release_opening_event()
    {
        impl->release_opening_event_music();
    }

    void Kh02StoryMusic::release_scene_field()
    {
        impl->music_proof["scene_field_event_released"] = impl->slots.release_scene_field(impl->field_track);
    }

    auto Kh02StoryMusic::release_arrival_menu(uint64_t now) -> Json
    {
        return impl->release_arrival_menu(now);
    }

    void Kh02StoryMusic::play_source(UObject* caller, UObject* sound, float seek, float fade, bool handoff,
                                     const std::function<void()>& callback)
    {
        impl->play_source_music(caller, sound, seek, fade, handoff, callback);
    }

    void Kh02StoryMusic::stop_source(UObject* caller, UObject* sound, float fade)
    {
        impl->stop_source_music(caller, sound, fade);
    }

    void Kh02StoryMusic::set_volume(float value, float fade)
    {
        impl->set_volume(value, fade);
    }

    auto Kh02StoryMusic::is_suspended(UObject* sound) -> bool
    {
        return impl->is_suspended(sound);
    }

    auto Kh02StoryMusic::snapshot() -> Json
    {
        return impl->music_snapshot();
    }
} // namespace kh3ap
