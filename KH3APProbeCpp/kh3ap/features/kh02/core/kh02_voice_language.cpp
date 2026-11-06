#include "kh02_voice_language.h"
#include "kh3ap/features/kh02/core/kh02_profile.h"
#include "kh02_reflected_call.h"
#include "kh3ap/features/kh02/transport/kh02_remote_files.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/UFunction.hpp>
#include <stdexcept>
#include <cstring>
#define NOMINMAX
#include <Windows.h>

namespace kh3ap
{
    auto parse_kh02_voice_language(std::wstring_view language) -> std::optional<Kh02VoiceLanguage>
    {
        if (language == L"en" || language.starts_with(L"en-"))
        {
            return Kh02VoiceLanguage::english;
        }
        if (language == L"ja" || language.starts_with(L"ja-"))
        {
            return Kh02VoiceLanguage::japanese;
        }
        return std::nullopt;
    }

    auto kh02_voice_language_code(Kh02VoiceLanguage language) -> const char*
    {
        return language == Kh02VoiceLanguage::japanese ? "ja" : "en";
    }

    auto current_kh02_voice_language() -> Kh02VoiceLanguage
    {
        Kh02ReflectedCall query(L"/Script/TresGame.Default__TresGameBlueprintLibrary", L"TresGetVoiceLanguage",
                                "kh02_voice_language", 32);
        auto* returned = query.param(L"ReturnValue");
        if (!returned || returned->GetClass().GetName() != L"StrProperty" || returned->GetElementSize() != 16 ||
            query.function->GetParmsSize() != 16)
        {
            throw std::runtime_error("kh02_voice_language_layout");
        }
        query.run();
        auto* value = returned->ContainerPtrToValuePtr<RC::Unreal::FString>(query.parameters.data());
        const std::wstring language = value->GetCharArray() ? value->GetCharArray() : L"";
        destroy_property_value_dynamic(returned, value);
        const auto selected = parse_kh02_voice_language(language);
        if (!selected)
        {
            throw std::runtime_error("kh02_voice_language_unsupported");
        }
        return *selected;
    }

    auto kh02_voice_language_fixture(std::wstring_view requested) -> nlohmann::json
    {
        const auto before = current_kh02_voice_language();
        if (requested != L"inspect")
        {
            const auto selected = parse_kh02_voice_language(requested);
            if (!selected || nlohmann::json::parse(kh02_remote_files_status()).at("enabled").get<bool>())
            {
                throw std::runtime_error("kh02_voice_fixture_requires_fresh_title");
            }
            // The native internationalization setter owns the active voice
            // culture. This title-only fixture never saves the settings object.
            auto* image = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
            using kh02_native::Site;
            if (!kh02_native::verify_sites({Site::voice_setter, Site::voice_field, Site::voice_instance_getter}))
            {
                throw std::runtime_error("kh02_voice_fixture_profile");
            }
            auto* internationalization = *reinterpret_cast<void**>(image + kh02_native::rva(Site::voice_instance));
            const RC::Unreal::FString language(*selected == Kh02VoiceLanguage::japanese ? L"ja" : L"en");
            using SetVoiceLanguage = bool (*)(void*, const RC::Unreal::FString&);
            if (!internationalization ||
                !reinterpret_cast<SetVoiceLanguage>(image + kh02_native::rva(Site::voice_setter))(internationalization,
                                                                                                  language))
            {
                throw std::runtime_error("kh02_voice_fixture_setter");
            }
            if (current_kh02_voice_language() != *selected)
            {
                throw std::runtime_error("kh02_voice_fixture_readback");
            }
        }
        return {{"before", kh02_voice_language_code(before)},
                {"voice_language", kh02_voice_language_code(current_kh02_voice_language())},
                {"saved_to_config", false},
                {"source", "TresGetVoiceLanguage"}};
    }
} // namespace kh3ap
