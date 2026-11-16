#include "character_chest_voice.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/player_animation_support.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <array>
#include <stdexcept>

namespace kh3ap::character_chest_voice
{
    using namespace RC::Unreal;
    namespace
    {
        FWeakObjectPtr owner, animation_instance, chest_animation, chest_sound;

        UObject* object(UObject* value, const wchar_t* field)
        {
            return is_uobject_valid_fast(value)
                       ? try_read_reflected_object_property_value(value, field)
                       : nullptr;
        }
    } // namespace

    const wchar_t* asset_path(PlayerId player)
    {
        // Use the game's authored references, including its localization routing.
        // The ja package paths resolve to English audio when English is selected.
        switch (player)
        {
        case PlayerId::Riku: // Perfect!
            return L"/Game/Sound/VOICE/Localization/ja/battle/PC/Riku/bt1020460rk0.bt1020460rk0";
        case PlayerId::Aqua: // Okay!
            return L"/Game/Sound/VOICE/Localization/ja/battle/PC/Aqua/bt001044aq0.bt001044aq0";
        case PlayerId::Kairi: // Needed that.
            return L"/Game/Sound/VOICE/Localization/ja/battle/NPC/ex/bt2140240kr0.bt2140240kr0";
        case PlayerId::Roxas: // Yeah! (Thinking of You's acknowledgement, not an attack grunt.)
            return L"/Game/Sound/VOICE/Localization/ja/battle/FNPC_Link/bt4100260rs0.bt4100260rs0";
        default:
            return nullptr;
        }
    }

    bool prepare(UObject* pawn, UObject* instance, UObject* large_chest_animation)
    {
        auto* player = is_uobject_valid_fast(pawn)
                           ? pawn->GetPropertyByNameInChain(L"m_PlayerUniqueID")
                           : nullptr;
        if (!player || player->GetElementSize() != sizeof(uint8_t))
        {
            throw std::runtime_error("character_chest_voice_player_layout");
        }
        auto* path =
            asset_path(static_cast<PlayerId>(*player->ContainerPtrToValuePtr<uint8_t>(pawn)));
        if (!path)
        {
            return true;
        }
        auto* sound = find_loaded_runtime_asset(path);
        if (!sound || !is_uobject_valid_fast(instance) ||
            !is_uobject_valid_fast(large_chest_animation))
        {
            return false;
        }
        if (!player_animation_support::initialize_voice_notify())
        {
            throw std::runtime_error("character_chest_voice_hook_unavailable");
        }
        owner = make_uobject_weak(pawn);
        animation_instance = make_uobject_weak(instance);
        chest_animation = make_uobject_weak(large_chest_animation);
        chest_sound = make_uobject_weak(sound);
        return true;
    }

    UObject* replacement_voice(UObject* notify, UObject* mesh, UObject* animation)
    {
        auto* pawn = owner.Get();
        auto* sound = chest_sound.Get();
        auto* instance = animation_instance.Get();
        if (!pawn || !sound || !instance || !is_uobject_valid_fast(notify) ||
            !is_uobject_valid_fast(animation) || animation != chest_animation.Get() ||
            notify->GetOuterPrivate() != animation ||
            notify->GetClassPrivate()->GetName() != L"TresAnimNotify_PlayVoice" ||
            character_swap::current_hero() != pawn || !character_swap::owns_swap_pawn(pawn) ||
            object(pawn, L"MyMesh") != mesh || object(mesh, L"AnimScriptInstance") != instance ||
            object(object(pawn, L"Controller"), L"Pawn") != pawn)
        {
            return nullptr;
        }
        auto* voice_field = notify->GetPropertyByNameInChain(L"m_VoiceAsset");
        if (!voice_field || voice_field->GetClass().GetName() != L"ObjectProperty" ||
            voice_field->GetElementSize() != sizeof(UObject*))
        {
            return nullptr;
        }
        // A private, short-lived notifier keeps Sora's shared asset untouched.
        // The caller immediately executes it on this game-thread notification;
        // no raw UObject is retained across frames. GC reclaims it afterwards.
        FStaticConstructObjectParameters parameters(notify->GetClassPrivate(), pawn);
        parameters.SetFlags = RF_Transient;
        auto* replacement = UObjectGlobals::StaticConstructObject(parameters);
        if (!is_uobject_valid_fast(replacement))
        {
            return nullptr;
        }
        constexpr std::array presentation_fields{L"m_VolumeMultiplier", L"m_PitchMultiplier",
                                                 L"m_SocketName", L"m_bFollow"};
        for (auto* name : presentation_fields)
        {
            auto* field = notify->GetPropertyByNameInChain(name);
            if (!field)
            {
                return nullptr;
            }
            field->CopyCompleteValue_InContainer(replacement, notify);
        }
        // Default random selection is empty and the voice-group name is None:
        // exactly this clip replaces either of Sora's two chest reactions.
        *voice_field->ContainerPtrToValuePtr<UObject*>(replacement) = sound;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        RC::Output::send<RC::LogLevel::Normal>(
            STR("[KH3APProbeCpp] Alternate large chest voice pawn={} animation={} sound={}\n"),
            pawn->GetName(), animation->GetPathName(), sound->GetPathName());
#endif
        return replacement;
    }
} // namespace kh3ap::character_chest_voice
