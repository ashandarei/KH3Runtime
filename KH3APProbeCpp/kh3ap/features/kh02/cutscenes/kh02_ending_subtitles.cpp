#include "kh02_ending_subtitles.h"
#include "kh02_cutscene_reflection.h"
#include "kh02_cutscene_session_state.h"
#include "kh02/story_movie_metadata.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/features/kh02/movies/kh02_movie_bridge.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/FScriptArray.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02cutscene_detail;
    using Json = nlohmann::json;

    namespace
    {
        constexpr int maximum_matinee_array_entries = 256;
        constexpr int maximum_subtitle_keys = 512;
        constexpr int subtitle_key_record_bytes = 24;
        constexpr int subtitle_text_record_bytes = 64;
        constexpr int dialogue_subtitle_usage = 0;
        constexpr float movie_eof_hold_seconds = 0.5f;
        constexpr float subtitle_clock_tolerance_seconds = 0.08f;

    } // namespace

    static std::wstring subtitle_string(FProperty* property, const void* owner)
    {
        if (!property || property->GetClass().GetName() != L"StrProperty" ||
            property->GetElementSize() != sizeof(FString))
        {
            throw std::runtime_error("kh02_subtitle_string_schema");
        }
        const auto* value = property->ContainerPtrToValuePtr<FString>(owner);
        return value->GetCharArray() ? value->GetCharArray() : L"";
    }

    void Kh02EndingSubtitles::close()
    {
        if (ending_subtitle_index >= 0)
        {
            ReflectedCall close(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                                L"TresUI_CloseSubtitle");
            close.run();
            ending_subtitle_index = -1;
        }
    }

    void Kh02EndingSubtitles::prepare(UObject* matinee, Json& ending_presentation)
    {
        bool matinee_audio = false;
        for (const auto& movie : kh02movie_metadata::stories)
        {
            if (ending_full_movie_logical == to_utf8(movie.logical))
            {
                matinee_audio = movie.matinee_audio;
                break;
            }
        }
        for (const auto& weak : ending_movie_disabled_tracks)
        {
            if (auto* track = weak.Get(); is_uobject_valid_fast(track))
            {
                set_flag(track, L"bDisableTrack", false);
            }
        }
        ending_movie_disabled_tracks.clear();
        close();
        ending_subtitle_matinee = {};
        ending_subtitle_started = false;
        ending_matinee_held = false;
        ending_subtitle_keys.clear();
        ending_subtitle_namespace.clear();
        ReflectedCall language(kh02_require_loaded_object(L"/Script/Engine.Default__KismetInternationalizationLibrary"),
                               L"GetCurrentLanguage");
        language.run();
        const auto selected_language = subtitle_string(language.param(L"ReturnValue"), language.parameters.data());
        destroy_property_value_dynamic(
            language.param(L"ReturnValue"),
            language.param(L"ReturnValue")->ContainerPtrToValuePtr<void>(language.parameters.data()));
        auto* data = try_read_reflected_object_property_value(matinee, L"MatineeData");
        auto* groups_property = is_uobject_valid_fast(data) ? data->GetPropertyByNameInChain(L"InterpGroups") : nullptr;
        if (!groups_property || !groups_property->IsA<FArrayProperty>())
        {
            throw std::runtime_error("kh02_subtitle_groups_schema");
        }
        const auto* groups = groups_property->ContainerPtrToValuePtr<TArray<UObject*>>(data);
        if (groups->Num() < 1 || groups->Num() > maximum_matinee_array_entries)
        {
            throw std::runtime_error("kh02_subtitle_groups_count");
        }
        for (auto* group : *groups)
        {
            auto* tracks_property =
                is_uobject_valid_fast(group) ? group->GetPropertyByNameInChain(L"InterpTracks") : nullptr;
            if (!tracks_property || !tracks_property->IsA<FArrayProperty>())
            {
                continue;
            }
            const auto* tracks = tracks_property->ContainerPtrToValuePtr<TArray<UObject*>>(group);
            if (tracks->Num() < 0 || tracks->Num() > maximum_matinee_array_entries)
            {
                throw std::runtime_error("kh02_subtitle_tracks_count");
            }
            for (auto* track : *tracks)
            {
                const auto track_type = is_uobject_valid_fast(track) ? track->GetClassPrivate()->GetName() : L"";
                const bool full_movie_track = kh02_full_movie_disables_track(track_type, matinee_audio);
                if (full_movie_track && matinee == ending_full_movie.Get() && !flag(track, L"bDisableTrack"))
                {
                    // TimeLapseStart/End and hide_island belong to the
                    // realtime branch. The full movie contains those shots;
                    // firing TimeLapseEnd would cut it off at 38.67 seconds.
                    set_flag(track, L"bDisableTrack", true);
                    ending_movie_disabled_tracks.push_back(make_uobject_weak(track));
                }
                if (!is_uobject_valid_fast(track) || track->GetClassPrivate()->GetName() != L"TresInterpTrackSubtitle")
                {
                    continue;
                }
                const auto culture = subtitle_string(track->GetPropertyByNameInChain(L"m_Culture"), track);
                if (!kh02_subtitle_culture_matches(selected_language, culture))
                {
                    continue;
                }
                if (!ending_subtitle_keys.empty())
                {
                    throw std::runtime_error("kh02_subtitle_duplicate_culture");
                }
                ending_subtitle_namespace = subtitle_string(track->GetPropertyByNameInChain(L"m_TextNamespace"), track);
                auto* keys_property = track->GetPropertyByNameInChain(L"m_Keys");
                if (!keys_property || !keys_property->IsA<FArrayProperty>())
                {
                    throw std::runtime_error("kh02_subtitle_keys_schema");
                }
                auto* inner = static_cast<FArrayProperty*>(keys_property)->GetInner();
                if (!inner || !inner->IsA<FStructProperty>() || inner->GetElementSize() != subtitle_key_record_bytes)
                {
                    throw std::runtime_error("kh02_subtitle_key_schema");
                }
                auto* schema = static_cast<FStructProperty*>(inner)->GetStruct();
                auto* time_property = find_struct_field_by_name(schema, L"m_Time");
                auto* length_property = find_struct_field_by_name(schema, L"m_Length");
                auto* label_property = find_struct_field_by_name(schema, L"m_TextLabel");
                if (!time_property || !length_property || time_property->GetClass().GetName() != L"FloatProperty" ||
                    length_property->GetClass().GetName() != L"FloatProperty")
                {
                    throw std::runtime_error("kh02_subtitle_key_time_schema");
                }
                const auto* keys = keys_property->ContainerPtrToValuePtr<FScriptArray>(track);
                if (keys->Num() < 0 || keys->Num() > maximum_subtitle_keys)
                {
                    throw std::runtime_error("kh02_subtitle_keys_count");
                }
                for (int index = 0; index < keys->Num(); ++index)
                {
                    const auto* key = static_cast<const std::byte*>(keys->GetData()) + index * inner->GetElementSize();
                    ending_subtitle_keys.push_back({*time_property->ContainerPtrToValuePtr<float>(key),
                                                    *length_property->ContainerPtrToValuePtr<float>(key),
                                                    subtitle_string(label_property, key)});
                }
            }
        }
        ending_subtitle_matinee = make_uobject_weak(matinee);
        ending_presentation["subtitle_keys"] = ending_subtitle_keys.size();
        ending_presentation["subtitle_language"] = to_utf8(selected_language);
    }

    void Kh02EndingSubtitles::tick(UObject* world, Kh02MovieBridge& movie_bridge,
                                   const LocalizedMessage& localized_message, Json& ending_presentation)
    {
        auto* matinee = ending_subtitle_matinee.Get();
        if (!is_uobject_valid_fast(matinee) || kh02_actor_world(matinee) != world)
        {
            close();
            ending_subtitle_matinee = {};
            return;
        }
        if (!flag(matinee, L"bIsPlaying"))
        {
            if (ending_subtitle_started)
            {
                close();
                ending_subtitle_matinee = {};
            }
            return;
        }
        ending_subtitle_started = true;
        const auto movie_position = movie_bridge.presentation_seconds();
        const auto position = movie_position.value_or(real(matinee, L"InterpPosition"));
        if (matinee == ending_full_movie.Get() && movie_position)
        {
            auto* data = try_read_reflected_object_property_value(matinee, L"MatineeData");
            const auto hold_position = real(data, L"InterpLength") - movie_eof_hold_seconds;
            const auto target_position = std::min(position, hold_position);
            if (std::abs(real(matinee, L"InterpPosition") - target_position) > subtitle_clock_tolerance_seconds)
            {
                ReflectedCall synchronize(matinee, L"SetPosition");
                synchronize.number(L"NewPosition", target_position);
                synchronize.boolean(L"bJump", true);
                synchronize.run();
            }
            // Native Finished calls StopVideo. Hold that callback until the
            // decoder has presented and released its actual final frame.
            if (position >= hold_position && !flag(matinee, L"bPaused"))
            {
                ReflectedCall pause(matinee, L"Pause");
                pause.run();
                ending_matinee_held = true;
                ending_presentation["matinee_held_for_movie_eof"] = true;
            }
        }
        else if (ending_matinee_held && movie_bridge.completed_movie(ending_full_movie_logical))
        {
            if (kh02_held_matinee_action(ending_matinee_held, true, flag(matinee, L"bPaused"), true, false) ==
                Kh02HeldMatineeAction::Resume)
            {
                ReflectedCall resume(matinee, L"Pause");
                resume.run();
            }
            ending_matinee_held = false;
            ending_presentation["matinee_released_after_movie_eof"] = true;
        }
        const int selected = kh02_subtitle_at(ending_subtitle_keys, position);
        if (selected == ending_subtitle_index)
        {
            return;
        }
        close();
        if (selected < 0)
        {
            return;
        }
        ReflectedCall open(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                           L"TresUI_OpenSubtitle");
        auto* text_property = open.param(L"Text");
        if (!text_property || !text_property->IsA<FStructProperty>() ||
            text_property->GetElementSize() != subtitle_text_record_bytes)
        {
            throw std::runtime_error("kh02_subtitle_text_schema");
        }
        auto* schema = static_cast<FStructProperty*>(text_property)->GetStruct();
        auto* text = text_property->ContainerPtrToValuePtr<void>(open.parameters.data());
        const auto borrow = [&](const wchar_t* name, const std::wstring& value)
        {
            auto* property = find_struct_field_by_name(schema, name);
            if (!property || property->GetClass().GetName() != L"StrProperty" ||
                property->GetElementSize() != sizeof(FString))
            {
                throw std::runtime_error("kh02_subtitle_text_field");
            }
            struct StringView
            {
                const wchar_t* data;
                int count;
                int capacity;
            };
            const StringView view{value.c_str(), static_cast<int>(value.size() + 1),
                                  static_cast<int>(value.size() + 1)};
            std::memcpy(property->ContainerPtrToValuePtr<void>(text), &view, sizeof(view));
        };
        borrow(L"Namespace", ending_subtitle_namespace);
        borrow(L"Key", ending_subtitle_keys[selected].label);
        std::wstring localized_text;
        const auto translated = localized_message(ending_subtitle_namespace.c_str(),
                                                  ending_subtitle_keys[selected].label.c_str(), &localized_text);
        if (!translated["found"].get<bool>())
        {
            throw std::runtime_error("kh02_subtitle_text_not_loaded");
        }
        borrow(L"LocalizedText", localized_text);
        open.integer(L"SubtitleUsage", dialogue_subtitle_usage);
        open.run();
        ending_subtitle_index = selected;
        ending_presentation["subtitle_key"] = to_utf8(ending_subtitle_keys[selected].label);
        ending_presentation["subtitle_position"] = position;
    }

    void Kh02EndingSubtitles::select_full_movie(UObject* matinee, std::string logical)
    {
        ending_full_movie = make_uobject_weak(matinee);
        ending_full_movie_logical = std::move(logical);
    }

    bool Kh02EndingSubtitles::needs_frame() const
    {
        return is_uobject_valid_fast(ending_subtitle_matinee.Get());
    }

    void Kh02EndingSubtitles::retire_presentation() noexcept
    {
        restore_resource([&] { close(); });
        restore_resource(
            [&]
            {
                auto* matinee = ending_subtitle_matinee.Get();
                if (!is_uobject_valid_fast(matinee) ||
                    kh02_held_matinee_action(ending_matinee_held, flag(matinee, L"bIsPlaying"),
                                             flag(matinee, L"bPaused"), false, true) != Kh02HeldMatineeAction::Retire)
                {
                    return;
                }
                // Interpolation.cpp: Stop terminates playback/group state;
                // only natural StepInterp completion emits Finished. Clear
                // our owned playback first so Stop also skips OnStop, which
                // may otherwise call back into the retired source graph.
                ReflectedCall stop(matinee, L"Stop");
                if (stop.function->GetParmsSize() != 0)
                {
                    throw std::runtime_error("kh02_cancel_matinee_stop_signature");
                }
                set_flag(matinee, L"bIsPlaying", false);
                stop.run();
            });
    }

    void Kh02EndingSubtitles::restore_tracks() noexcept
    {
        for (const auto& weak : ending_movie_disabled_tracks)
        {
            restore_resource(
                [&]
                {
                    if (auto* track = weak.Get(); is_uobject_valid_fast(track))
                    {
                        set_flag(track, L"bDisableTrack", false);
                    }
                });
        }
    }

    void Kh02EndingSubtitles::reset()
    {
        ending_movie_disabled_tracks.clear();
        ending_subtitle_matinee = {};
        ending_subtitle_keys.clear();
        ending_subtitle_namespace.clear();
        ending_subtitle_index = -1;
        ending_subtitle_started = false;
        ending_matinee_held = false;
        ending_full_movie = {};
        ending_full_movie_logical.clear();
    }
} // namespace kh3ap
