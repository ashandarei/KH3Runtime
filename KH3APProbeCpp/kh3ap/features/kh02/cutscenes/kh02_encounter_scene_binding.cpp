#include "kh02_encounter_scene_binding.h"
#include "kh02_cutscene_session.h"
#include "kh02_event_aqua.h"
#include "kh02_event_aqua_materials.h"
#include "kh02_matinee_binding.h"
#include "kh02_opening_followup.h"
#include "kh02_scene_catalog.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/kh02/story/kh02_postboss_handoff.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace kh3ap
{
    using RC::Unreal::UObject;

    void Kh02EncounterSceneBinding::prepare_and_bind(UObject* object, const Services& services)
    {
        try
        {
            services.cutscenes.prepare(object, services.world.Get(), services.pawn.Get());
        }
        catch (const std::exception& error)
        {
            services.error = error.what();
        }
        // Native presentation can re-enter the story. Resolve the borrowed weak
        // world and event pawn again in bind(), just as the original thunk did.
        bind(object, services);
    }

    void Kh02EncounterSceneBinding::bind_aqua(UObject* object, const wchar_t* group, const Services& services)
    {
        kh02_bind_event_aqua(object, services.event_aqua.actor(), group);
        services.cutscenes.track_event_aqua(object, services.event_aqua.actor());
    }

    void Kh02EncounterSceneBinding::bind_and_show(UObject* object, const wchar_t* group, const Services& services)
    {
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;
        if (kh02_actor_world(object) != services.world.Get() || !is_uobject_valid_fast(services.event_aqua.actor()))
        {
            throw std::runtime_error("kh02_encounter_scene_context");
        }
        bind_aqua(object, group, services);
        Kh02ReflectedCall show(services.event_aqua.actor(), L"SetActorHiddenInGame", "kh02_story",
                               reflected_parameter_limit_bytes);
        show.boolean(L"bNewHidden", false);
        show.run();
        services.proof["last_bound_matinee"] =
            is_uobject_valid_fast(object) ? to_utf8(object->GetPathName()) : std::string{};
    }

    void Kh02EncounterSceneBinding::bind(UObject* object, const Services& services)
    {
        if (!is_uobject_valid_fast(object))
        {
            return;
        }
        const auto binding = kh02_scene_binding(object->GetPathName(), object->GetName());
        if (!binding.accepted())
        {
            return;
        }
        try
        {
            bind_and_show(object, binding.group, services);
            if (kh02_scene_package_actor(object->GetPathName(),
                                         L"/Game/Zutscene/Localization/en/dw/cutscene_dw03_dw020") &&
                object->GetName() == L"dw020")
            {
                // DW020 animates a separate reflected Aqua in its second group.
                // It keeps the source NonState eye shaders unless adapted here,
                // independently of the host's main cinematic Aqua.
                auto* reflection = *kh02_matinee_actor_slot(object, L"TresInterpGroup_1");
                if (!is_uobject_valid_fast(reflection) || reflection->GetOuterPrivate() != object->GetOuterPrivate() ||
                    kh02_actor_world(reflection) != services.world.Get() ||
                    reflection->GetName() != L"dw020a_MirrorAqua_p_ex002_0")
                {
                    throw std::runtime_error("kh02_mirror_aqua_eye_context");
                }
                kh02_adapt_mirror_aqua_eyes(reflection);
            }
            if (binding.opening)
            {
                services.opening_followup.bind(
                    object, try_read_reflected_object_property_value(object->GetOuterPrivate(), L"LevelScriptActor"));
            }
            else if (binding.scene)
            {
                services.postboss.bind(object);
            }
        }
        catch (const std::exception& error)
        {
            services.error = error.what();
        }
    }

    void Kh02EncounterSceneBinding::bind_override(UObject* object, bool scene, const Services& services)
    {
        try
        {
            bind_and_show(object, L"TresInterpGroup_0", services);
            if (scene)
            {
                services.postboss.bind(object);
            }
        }
        catch (const std::exception& error)
        {
            services.error = error.what();
        }
    }
} // namespace kh3ap
