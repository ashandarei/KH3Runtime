#include "kh02_localization.h"
#include "kh02_profile.h"
#include "kh3ap/core/performance_build.h"
#include "kh3ap/core/reflection/reflected_function_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/FString.hpp>
#include <cstring>
#include <stdexcept>
#define NOMINMAX
#include <Windows.h>

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
        constexpr uintptr_t localization_manager_getter = 0x31dc370;
        constexpr uintptr_t localization_reload_callback = 0x31dcca0;
        constexpr uint8_t localization_manager_signature[]{0x48, 0x83, 0xec, 0x38, 0x48, 0xc7, 0x44, 0x24, 0x20,
                                                           0xfe, 0xff, 0xff, 0xff, 0x48, 0x8b, 0x05, 0x7c, 0xa1,
                                                           0xb8, 0x06, 0x48, 0x85, 0xc0, 0x75, 0x40};
        constexpr uint8_t localization_reload_signature[]{0x40, 0x57, 0x48, 0x83, 0xec, 0x50, 0x48, 0xc7, 0x44, 0x24,
                                                          0x30, 0xfe, 0xff, 0xff, 0xff, 0x48, 0x89, 0x5c, 0x24, 0x60,
                                                          0x48, 0x89, 0x74, 0x24, 0x68, 0x48, 0x8b, 0xf1, 0x80, 0x39,
                                                          0x00, 0x74, 0x6f, 0xe8, 0x9a, 0x06, 0xfe, 0xff};
        constexpr auto expected_chapter_text = "Find the gears and rebuild the path to the castle!";

        struct ReflectedCall : ReflectedFunctionCall
        {
            ReflectedCall(UObject* owner, const wchar_t* method)
                : ReflectedFunctionCall(owner, method, "kh02_story", reflected_parameter_limit_bytes)
            {
            }
        };

        auto refresh_current_localizations() -> bool
        {
            if (kh02_native::build() != kh02_native::Build::Egs)
            {
                return false;
            }
            auto* image = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
            auto* getter = image + localization_manager_getter;
            auto* reload = image + localization_reload_callback;
            if (std::memcmp(getter, localization_manager_signature, sizeof(localization_manager_signature)) != 0 ||
                std::memcmp(reload, localization_reload_signature, sizeof(localization_reload_signature)) != 0)
            {
                return false;
            }
            using GetLocalizationManager = void* (*)();
            using ReloadLocalizations = void (*)(void*);
            auto* manager = reinterpret_cast<GetLocalizationManager>(getter)();
            if (!manager)
            {
                return false;
            }
            // FTextLocalizationManager::OnCultureChanged uses the current language
            // and refreshes its resources without changing internationalization state.
            reinterpret_cast<ReloadLocalizations>(reload)(manager);
            return true;
        }

        auto current_language(UObject* library) -> std::wstring
        {
            ReflectedCall get(library, L"GetCurrentLanguage");
            auto* returned = get.param(L"ReturnValue");
            if (!returned || returned->GetClass().GetName() != L"StrProperty" ||
                returned->GetElementSize() != native_string_bytes ||
                get.function->GetParmsSize() != native_string_bytes)
            {
                throw std::runtime_error("kh02_localization_language_layout");
            }
            get.run();
            auto* value = returned->ContainerPtrToValuePtr<FString>(get.parameters.data());
            const std::wstring language = value->GetCharArray() ? value->GetCharArray() : L"";
            destroy_property_value_dynamic(returned, value);
            return language;
        }
    } // namespace

    auto Kh02Localization::lookup(const wchar_t* text_namespace, const wchar_t* text_key, std::wstring* localized_text)
        -> Json
    {
        ReflectedCall find(require_loaded_object(L"/Script/Engine.Default__KismetTextLibrary"),
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
        auto* library = require_loaded_object(L"/Script/Engine.Default__KismetInternationalizationLibrary");
        const auto original = current_language(library);
        if (original != L"en" && !original.starts_with(L"en-"))
        {
            throw std::runtime_error("kh02_localization_requires_english_test");
        }
        const auto before = lookup();
        proof = {{"language", to_utf8(original)}, {"before", before}, {"saved_to_config", false}};
        bool loaded = true;
        if (before["found"] == true && before["text"] == expected_chapter_text)
        {
            proof["load_method"] = "already_loaded";
        }
        else if (refresh_current_localizations())
        {
            proof["load_method"] = "native_refresh";
        }
        else
        {
            const auto set = [&](const std::wstring& culture)
            {
                ReflectedCall change_language(library, L"SetCurrentLanguage");
                change_language.string(L"Culture", culture);
                change_language.boolean(L"SaveToConfig", false);
                change_language.run();
                return change_language.boolean_result();
            };
            proof["load_method"] = "language_reload";
            const bool alternate = set(original == L"en-US" ? L"en-GB" : L"en-US");
            const bool restored = set(original);
            proof["alternate_accepted"] = alternate;
            loaded = alternate && restored;
        }
        const auto language_after = current_language(library);
        proof["language_after"] = to_utf8(language_after);
        proof["restored"] = language_after == original;
        proof["after"] = lookup();
        ready = loaded && language_after == original && proof["after"]["found"] == true &&
                proof["after"]["text"] == expected_chapter_text;
        if (!ready)
        {
            throw std::runtime_error("kh02_localization_resource_not_loaded");
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        // Round-trip one key from each of the menu and subtitle resources as well as the objective.
        proof["challenge_menu"] = lookup(L"kh02_menu.camp_challenge", L"text_001");
        proof["story_subtitle"] = lookup(L"kh02_event_dw.dw033", L"a0020");
#endif
    }

    auto Kh02Localization::snapshot() const -> const Json&
    {
        return proof;
    }
} // namespace kh3ap
