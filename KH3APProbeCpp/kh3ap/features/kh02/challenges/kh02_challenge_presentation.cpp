#include "kh02_challenge_presentation.h"
#include "kh02_challenge_text.h"
#include "kh02_challenge_defs.h"
#include "kh02_challenge_ids.h"
#include "kh02_challenge_policy.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FText.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr std::size_t native_text_bytes = 24;
        constexpr int challenge_key_digits = 3;
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;

        struct ReflectedCall : Kh02ReflectedCall
        {
            ReflectedCall(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "kh02_story", reflected_parameter_limit_bytes)
            {
            }
        };
    } // namespace

    std::wstring kh02_challenge_description(int number)
    {
        try
        {
            ReflectedCall find(kh02_require_loaded_object(L"/Script/Engine.Default__KismetTextLibrary"),
                               L"FindTextInLocalizationTable");
            find.string(L"Namespace", L"kh02_menu.camp_challenge");
            const auto digits = std::to_wstring(number);
            find.string(L"Key", L"text_" + std::wstring(challenge_key_digits - digits.size(), L'0') + digits);
            auto* property = find.param(L"OutText");
            if (!property || property->GetClass().GetName() != L"TextProperty" ||
                property->GetElementSize() != native_text_bytes)
            {
                throw std::runtime_error("kh02_challenge_text_layout");
            }
            find.run();
            auto* text = property->ContainerPtrToValuePtr<FText>(find.parameters.data());
            auto value = text->ToString();
            destroy_property_value_dynamic(property, text);
            value =
                kh02_format_challenge_text(std::move(value), number, kh02_challenge_description_numbers[number - 1]);
            return value.empty() ? std::wstring(kh02_challenges[number - 1].title) : value;
        }
        catch (...)
        {
            return L"Original objective description is unavailable.";
        }
    }

    auto kh02_challenge_progress(int number, bool active, const std::map<int, int>& counts) -> std::wstring
    {
        const int id = number - kh02_challenge_id::kill_kurokosagi;
        const int target = kh02_tracking::goal(id);
        const auto count = counts.find(id);
        return kh02_format_challenge_progress(active, count == counts.end() ? 0 : count->second, target);
    }

    auto kh02_challenge_help(int number, bool active, const std::map<int, int>& counts) -> std::wstring
    {
        const auto progress = kh02_challenge_progress(number, active, counts);
        return kh02_challenge_description(number) + (progress.empty() ? L"" : L"  [" + progress + L"]");
    }

    auto kh02_challenge_rows(const std::map<int, int>& states, const std::map<int, int>& counts)
        -> std::vector<Kh02ChallengeRow>
    {
        std::vector<Kh02ChallengeRow> rows;
        rows.reserve(kh02_challenge_id::count);
        for (int number = 1; number <= kh02_challenge_id::count; ++number)
        {
            const auto progress = kh02_challenge_progress(number, true, counts);
            const auto state = states.find(number - 1);
            std::wstring title = kh02_challenges[number - 1].title;
            const auto prefix = title.find(L". ");
            if (prefix != std::wstring::npos)
            {
                title.erase(0, prefix + 2);
            }
            rows.push_back(
                {title, progress, kh02_challenge_description(number), state != states.end() ? state->second : 0});
        }

        return rows;
    }
} // namespace kh3ap
