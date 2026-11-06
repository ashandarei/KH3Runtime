#include "kh02_compatibility_enums.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <cstring>
#include <stdexcept>
#include <utility>
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh02_aliases.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_ids.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_enum.h"
#include <Unreal/UEnum.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/TArray.hpp>
#include <iterator>

namespace kh3ap
{
    using namespace RC::Unreal;

    void kh02_register_compatibility_enums(Kh02NativeAliases& aliases)
    {
        auto* enum_library = kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary");
        auto* enum_outer = enum_library->GetClassPrivate()->GetOuterPrivate();
        if (!is_uobject_valid_fast(enum_outer) || enum_outer->GetPathName() != L"/Script/TresGame")
        {
            throw std::runtime_error("kh02_challenge_enum_outer");
        }
        FStaticConstructObjectParameters enum_args(UEnum::StaticClass(), enum_outer);
        enum_args.Name = FName(L"ETresChallengeState", FNAME_Add);
        enum_args.SetFlags = static_cast<EObjectFlags>(RF_Public | RF_Transient);
        auto* challenge_enum = static_cast<UEnum*>(UObjectGlobals::StaticConstructObject<UEnum*>(enum_args));
        if (!is_uobject_valid_fast(challenge_enum))
        {
            throw std::runtime_error("kh02_challenge_enum_construct");
        }
        const wchar_t* states[]{L"ETresChallengeState::Wait", L"ETresChallengeState::Progress",
                                L"ETresChallengeState::Clear", L"ETresChallengeState::Error",
                                L"ETresChallengeState::ETresChallengeState_MAX"};
        for (int i = 0; i < static_cast<int>(std::size(states)); ++i)
        {
            if (challenge_enum->InsertIntoNames({FName(states[i], FNAME_Add), i}, i) != i)
            {
                throw std::runtime_error("kh02_challenge_enum_value");
            }
        }
        challenge_enum->SetRootSet();
        aliases.track_descriptor(challenge_enum);
        enum_args.Name = FName(L"ETresChallengeItem", FNAME_Add);
        auto* item_enum = static_cast<UEnum*>(UObjectGlobals::StaticConstructObject<UEnum*>(enum_args));
        if (!is_uobject_valid_fast(item_enum))
        {
            throw std::runtime_error("kh02_challenge_item_enum");
        }
        for (int i = 0; i < kh02_challenge_id::enum_entry_count; ++i)
        {
            if (item_enum->InsertIntoNames({FName(kh02_challenge_item_names[i], FNAME_Add), i}, i) != i)
            {
                throw std::runtime_error("kh02_challenge_item_value");
            }
        }
        item_enum->SetRootSet();
        aliases.track_descriptor(item_enum);
        // The old serialized data-set values are unscoped. Preserve every KH3
        // enumerator/value and admit the three audited legacy spellings.
        auto* data_enum =
            UObjectGlobals::StaticFindObject<UEnum*>(nullptr, nullptr, L"/Script/TresGame.ETresChrDataTableSetType");
        if (!is_uobject_valid_fast(data_enum))
        {
            throw std::runtime_error("kh02_data_enum_missing");
        }
        const wchar_t* legacy[]{L"CDTS_TYPE_PLAYER", L"CDTS_TYPE_ENEMY", L"CDTS_TYPE_GIMMICK"};
        for (int i = 0; i < static_cast<int>(std::size(legacy)); ++i)
        {
            bool found = false;
            for (const auto& v : data_enum->ForEachName())
            {
                if (v.Key.ToString() == legacy[i])
                {
                    if (v.Value != i)
                    {
                        throw std::runtime_error("kh02_data_enum_value");
                    }
                    found = true;
                }
            }
            if (!found && data_enum->InsertIntoNames({FName(legacy[i], FNAME_Add), i}, data_enum->NumEnums() - 1) < 0)
            {
                throw std::runtime_error("kh02_data_enum_alias");
            }
        }
    }

} // namespace kh3ap
