#include "kh02_localization.h"
#include "kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/FString.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;

    namespace
    {
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;
        constexpr std::size_t native_text_bytes = 24;
        constexpr std::size_t native_string_bytes = 16;
        constexpr std::size_t find_text_parameters_bytes = 57;

        struct ReflectedCall : Kh02ReflectedCall
        {
            ReflectedCall(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "kh02_story", reflected_parameter_limit_bytes)
            {
            }
        };
    } // namespace

    auto Kh02Localization::lookup(const wchar_t* text_namespace, const wchar_t* text_key, std::wstring* localized_text)
        -> Json
    {
        ReflectedCall find(kh02_require_loaded_object(L"/Script/Engine.Default__KismetTextLibrary"),
                           L"FindTextInLocalizationTable");
        find.string(L"Namespace", text_namespace);
        find.string(L"Key", text_key);
        auto* text_property = find.param(L"OutText");
        if (!text_property || text_property->GetClass().GetName() != L"TextProperty" ||
            text_property->GetElementSize() != native_text_bytes ||
            find.function->GetParmsSize() != find_text_parameters_bytes)
        {
            throw std::runtime_error("kh02_localization_text_layout");
        }
        auto* output_text = text_property->ContainerPtrToValuePtr<FText>(find.parameters.data());
        find.run();
        const bool found = find.boolean_result();
        const auto text = output_text->ToString();
        if (localized_text)
        {
            *localized_text = text;
        }
        destroy_property_value_dynamic(text_property, output_text);
        return Json{{"found", found}, {"text", to_utf8(text)}};
    }

    void Kh02Localization::prepare()
    {
        if (ready)
        {
            return;
        }
        auto* library = kh02_require_loaded_object(L"/Script/Engine.Default__KismetInternationalizationLibrary");
        ReflectedCall get(library, L"GetCurrentLanguage");
        auto* returned = get.param(L"ReturnValue");
        if (!returned || returned->GetClass().GetName() != L"StrProperty" ||
            returned->GetElementSize() != native_string_bytes || get.function->GetParmsSize() != native_string_bytes)
        {
            throw std::runtime_error("kh02_localization_language_layout");
        }
        get.run();
        auto* value = returned->ContainerPtrToValuePtr<FString>(get.parameters.data());
        const std::wstring original = value->GetCharArray() ? value->GetCharArray() : L"";
        destroy_property_value_dynamic(returned, value);
        if (original != L"en" && !original.starts_with(L"en-"))
        {
            throw std::runtime_error("kh02_localization_requires_english_test");
        }
        proof = {{"language", to_utf8(original)}, {"before", lookup()}, {"saved_to_config", false}};
        auto set = [&](const std::wstring& culture)
        {
            ReflectedCall change_language(library, L"SetCurrentLanguage");
            change_language.string(L"Culture", culture);
            change_language.boolean(L"SaveToConfig", false);
            change_language.run();
            return change_language.boolean_result();
        };
        // The engine reloads its Game target on a language change. Empty pak
        // markers provide discovery; every actual locres byte comes from the
        // active RAM provider. Restore the user's language immediately.
        const bool alternate = set(original == L"en-US" ? L"en-GB" : L"en-US");
        const bool restored = set(original);
        proof["alternate_accepted"] = alternate;
        proof["restored"] = restored;
        proof["after"] = lookup();
        ready = alternate && restored && proof["after"]["found"].get<bool>() &&
                proof["after"]["text"] == "Find the gears and rebuild the path to the castle!";
        if (!ready)
        {
            throw std::runtime_error("kh02_localization_resource_not_loaded");
        }
    }

    auto Kh02Localization::snapshot() const -> const Json&
    {
        return proof;
    }
} // namespace kh3ap
