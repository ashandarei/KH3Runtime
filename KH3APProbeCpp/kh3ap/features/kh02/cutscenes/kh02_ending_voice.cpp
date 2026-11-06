#include "kh02_ending_voice.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <stdexcept>
#include <algorithm>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/Property/FObjectProperty.hpp>
#include <span>

namespace
{
    constexpr int native_voice_line_digits = 3;
    constexpr int mickey_native_voice_lines[]{1, 2, 3};
    constexpr int aqua_native_voice_lines[]{4, 6, 7, 8, 9, 10};
} // namespace

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;

    namespace
    {
        bool flag(UObject* object, const wchar_t* property_name)
        {
            auto* property = is_uobject_valid_fast(object) ? object->GetPropertyByNameInChain(property_name) : nullptr;
            if (!property || !property->IsA<FBoolProperty>())
            {
                throw std::runtime_error("kh02_story_bool");
            }
            return static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(object);
        }

        void set_flag(UObject* object, const wchar_t* property_name, bool value)
        {
            if (!is_uobject_valid_fast(object) ||
                !write_bool_function_param(object->GetPropertyByNameInChain(property_name), object, value))
            {
                throw std::runtime_error("kh02_story_set_flag");
            }
        }

        std::string path(UObject* object)
        {
            return is_uobject_valid_fast(object) ? to_utf8(object->GetPathName()) : "";
        }

    } // namespace

    void Kh02EndingVoice::prepare(UObject* matinee, Json& ending_presentation)
    {
        const auto scene_path = matinee->GetPathName();
        const bool mickey_scene = scene_path.starts_with(L"/Game/Zutscene/Localization/en/dw/cutscene_dw53_dw033.");
        const bool aqua_scene = scene_path.starts_with(L"/Game/Zutscene/Localization/en/dw/cutscene_dw54_dw034.");
        if (!mickey_scene && !aqua_scene)
        {
            return;
        }

        auto* attenuation = ending_voice_attenuation.Get();
        if (!is_uobject_valid_fast(attenuation))
        {
            auto* attenuation_class =
                static_cast<UClass*>(kh02_require_loaded_object(L"/Script/Engine.SoundAttenuation"));
            FStaticConstructObjectParameters parameters(attenuation_class, attenuation_class->GetOuterPrivate());
            parameters.SetFlags = static_cast<EObjectFlags>(RF_Public | RF_Transient);
            attenuation = UObjectGlobals::StaticConstructObject(parameters);
            if (!is_uobject_valid_fast(attenuation))
            {
                throw std::runtime_error("kh02_ending_voice_attenuation");
            }
            auto* property = attenuation->GetPropertyByNameInChain(L"Attenuation");
            if (!property || !property->IsA<FStructProperty>())
            {
                throw std::runtime_error("kh02_ending_voice_attenuation_struct");
            }
            auto* schema = static_cast<FStructProperty*>(property)->GetStruct();
            auto* settings = property->ContainerPtrToValuePtr<void>(attenuation);
            if (!write_bool_function_param(find_struct_field_by_name(schema, L"bAttenuate"), settings, false) ||
                !write_bool_function_param(find_struct_field_by_name(schema, L"bSpatialize"), settings, false))
            {
                throw std::runtime_error("kh02_ending_voice_attenuation_flags");
            }
            attenuation->SetRootSet();
            ending_voice_attenuation = make_uobject_weak(attenuation);
        }
        ending_presentation["voice_attenuation"] = path(attenuation);
        ending_presentation["voices"] = Json::array();
        const auto lines = mickey_scene ? std::span<const int>(mickey_native_voice_lines)
                                        : std::span<const int>(aqua_native_voice_lines);
        for (const int line : lines)
        {
            const auto number = std::to_wstring(line);
            const auto name = std::wstring(mickey_scene ? L"dw033" : L"dw034") +
                              std::wstring(native_voice_line_digits - number.size(), L'0') + number +
                              (mickey_scene ? L"mm0" : L"aq0");
            const auto package = std::wstring(L"/Game/Zound/VOICE/Localization/en/Event/dw/") + name;
            auto* sound = find_loaded_runtime_asset(package + L"." + name);
            if (!is_uobject_valid_fast(sound) || sound->GetClassPrivate()->GetName() != L"SQEXSEADSound")
            {
                throw std::runtime_error("kh02_ending_voice_asset");
            }
            auto* attenuation_property = sound->GetPropertyByNameInChain(L"AttenuationSettings");
            if (!attenuation_property || !attenuation_property->IsA<FObjectProperty>() ||
                attenuation_property->GetSize() != sizeof(UObject*))
            {
                throw std::runtime_error("kh02_ending_voice_attenuation_property");
            }
            const auto previous = std::find_if(voice_overrides.begin(), voice_overrides.end(),
                                               [sound](const auto& entry) { return entry.sound.Get() == sound; });
            if (previous == voice_overrides.end())
            {
                voice_overrides.push_back(
                    {make_uobject_weak(sound),
                     make_uobject_weak(*attenuation_property->ContainerPtrToValuePtr<UObject*>(sound)),
                     flag(sound, L"bIgnoreAudioVolumeAttenuation")});
            }
            *attenuation_property->ContainerPtrToValuePtr<UObject*>(sound) = attenuation;
            // Keep the authored gain. Cinematic dialogue must not fade with
            // the host pawn's distance or a source world's audio volume.
            set_flag(sound, L"bIgnoreAudioVolumeAttenuation", true);
            ending_presentation["voices"].push_back(path(sound));
        }
    }

    void Kh02EndingVoice::restore() noexcept
    {
        for (const auto& entry : voice_overrides)
        {
            try
            {
                if (auto* sound = entry.sound.Get(); is_uobject_valid_fast(sound))
                {
                    auto* property = sound->GetPropertyByNameInChain(L"AttenuationSettings");
                    if (property && property->IsA<FObjectProperty>() && property->GetSize() == sizeof(UObject*))
                    {
                        *property->ContainerPtrToValuePtr<UObject*>(sound) = entry.attenuation.Get();
                    }
                    set_flag(sound, L"bIgnoreAudioVolumeAttenuation", entry.ignored_audio_volume);
                }
            }
            catch (...)
            {
            }
        }

        voice_overrides.clear();
    }

    void Kh02EndingVoice::release_root()
    {
        if (auto* attenuation = ending_voice_attenuation.Get(); is_uobject_valid_fast(attenuation))
        {
            attenuation->ClearRootSet();
        }
        ending_voice_attenuation = {};
    }

} // namespace kh3ap
