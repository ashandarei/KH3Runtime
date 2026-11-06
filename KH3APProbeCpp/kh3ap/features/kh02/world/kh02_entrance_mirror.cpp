#include "kh02_entrance_mirror.h"
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
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/AActor.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <chrono>

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;

    namespace
    {
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;

        struct ReflectedCall : Kh02ReflectedCall
        {
            ReflectedCall(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "kh02_story", reflected_parameter_limit_bytes)
            {
            }
        };

        constexpr int actor_scan_limit = 16'384;
        constexpr auto entrance_mirror_timeout = std::chrono::seconds(5);
        constexpr auto entrance_mirror_poll_interval = std::chrono::milliseconds(100);
        constexpr auto entrance_mirror_retired_window = std::chrono::seconds(4);

        auto flag(UObject* object, const wchar_t* name) -> bool
        {
            auto* property = is_uobject_valid_fast(object) ? object->GetPropertyByNameInChain(name) : nullptr;
            if (!property || !property->IsA<FBoolProperty>())
            {
                throw std::runtime_error("kh02_story_bool");
            }
            return static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(object);
        }

        auto path(UObject* object) -> std::string
        {
            return is_uobject_valid_fast(object) ? to_utf8(object->GetPathName()) : "";
        }

    } // namespace

    bool Kh02EntranceMirror::prepare(UObject* scene, UObject* context, bool allow_retired_duplicate,
                                     Json& encounter_proof)
    {
        if (scene->GetPathName() !=
            L"/Game/Zutscene/Localization/en/dw/cutscene_dw03_dw016.cutscene_dw03_dw016:PersistentLevel.dw016")
        {
            return true;
        }
        UObject* cinematic_mirror = nullptr;
        UObject* gameplay_mirror = nullptr;
        auto* world = kh02_actor_world(scene);
        // Resolve the current world's actors. During streaming replacement,
        // the global object hash can still contain the preceding level's
        // same-named actor or miss its newly registered replacement.
        for (auto* actor : find_live_actors_of_class(context, AActor::StaticClass(), actor_scan_limit))
        {
            if (kh02_actor_world(actor) != world)
            {
                continue;
            }
            if (actor->GetClassPrivate()->GetName() == L"g_dw_MirrorA_Event_C" &&
                actor->GetOuterPrivate() == scene->GetOuterPrivate())
            {
                if (cinematic_mirror && cinematic_mirror != actor)
                {
                    throw std::runtime_error("kh02_within_entrance_mirror_ambiguous");
                }
                cinematic_mirror = actor;
            }
            else if (actor->GetName() == L"g_dw_Mirror_Entrance" &&
                     actor->GetPathName().starts_with(L"/Game/Zevels/dw/dw_03/umap/dw_03_gimmick."))
            {
                if (gameplay_mirror && gameplay_mirror != actor)
                {
                    throw std::runtime_error("kh02_within_entrance_gameplay_mirror_ambiguous");
                }
                gameplay_mirror = actor;
            }
        }
        if (!is_uobject_valid_fast(gameplay_mirror) || kh02_actor_world(gameplay_mirror) != kh02_actor_world(scene) ||
            (!cinematic_mirror && !allow_retired_duplicate))
        {
            return false;
        }
        auto* cinematic_mesh = try_read_reflected_object_property_value(cinematic_mirror, L"MyMesh");
        auto* gameplay_mesh = try_read_reflected_object_property_value(gameplay_mirror, L"MyMesh");
        if (!is_uobject_valid_fast(gameplay_mesh) ||
            !is_uobject_valid_fast(try_read_reflected_object_property_value(gameplay_mesh, L"StaticMesh")) ||
            (cinematic_mirror &&
             (!is_uobject_valid_fast(cinematic_mesh) ||
              !is_uobject_valid_fast(try_read_reflected_object_property_value(cinematic_mesh, L"SkeletalMesh")))))
        {
            return false;
        }
        encounter_proof["entrance_mirror"] = {
            {"scene", path(scene)},
            {"cinematic_mesh", path(cinematic_mesh)},
            {"gameplay_mirror", path(gameplay_mirror)},
            {"cinematic_world", path(kh02_actor_world(cinematic_mirror))},
            {"gameplay_world", path(kh02_actor_world(gameplay_mirror))},
            {"scene_world", path(kh02_actor_world(scene))},
            {"skeletal_mesh", path(try_read_reflected_object_property_value(cinematic_mesh, L"SkeletalMesh"))},
            {"static_mesh", path(try_read_reflected_object_property_value(gameplay_mesh, L"StaticMesh"))}};
        const bool duplicate_present = is_uobject_valid_fast(cinematic_mirror);
        if (kh02_actor_world(gameplay_mirror) != kh02_actor_world(scene) || !is_uobject_valid_fast(gameplay_mesh) ||
            path(try_read_reflected_object_property_value(gameplay_mesh, L"StaticMesh")) !=
                "/Game/Zaps/dw/uasset/Meshes/SM_dw_Pro_MirrorD.SM_dw_Pro_MirrorD" ||
            (duplicate_present &&
             (kh02_actor_world(cinematic_mirror) != kh02_actor_world(scene) || !is_uobject_valid_fast(cinematic_mesh) ||
              path(try_read_reflected_object_property_value(cinematic_mesh, L"SkeletalMesh")) !=
                  "/Game/Zaps/dw/gimmick/g_dw_MirrorA/mdl/g_dw_MirrorA.g_dw_MirrorA")))
        {
            throw std::runtime_error("kh02_within_entrance_mirror_binding");
        }
        ReflectedCall material(gameplay_mesh, L"GetMaterial");
        material.integer(L"ElementIndex", 1);
        material.run();
        if (!is_uobject_valid_fast(material.result()))
        {
            throw std::runtime_error("kh02_within_entrance_mirror_material");
        }
        // DW016 overlays a skeletal duplicate on the already visible entrance
        // mirror. Its opaque source shader renders the glass red in KH3.
        // Keep the gameplay mirror's original surface and frame underneath.
        // Only this duplicate mesh is hidden; its actor, attached effects,
        // Matinee tracks and the other animated mirrors retain their events.
        ReflectedCall show_actor(gameplay_mirror, L"SetActorHiddenInGame");
        show_actor.boolean(L"bNewHidden", false);
        show_actor.run();
        ReflectedCall show_mesh(gameplay_mesh, L"SetVisibility");
        show_mesh.boolean(L"bNewVisibility", true);
        show_mesh.boolean(L"bPropagateToChildren", false);
        show_mesh.run();
        if (duplicate_present)
        {
            ReflectedCall visibility(cinematic_mesh, L"SetVisibility");
            visibility.boolean(L"bNewVisibility", false);
            visibility.boolean(L"bPropagateToChildren", false);
            visibility.run();
        }
        const bool duplicate_visible = duplicate_present && flag(cinematic_mesh, L"bVisible");
        const bool gameplay_visible = !flag(gameplay_mirror, L"bHidden") && flag(gameplay_mesh, L"bVisible");
        if (duplicate_visible || !gameplay_visible)
        {
            throw std::runtime_error("kh02_within_entrance_mirror_visibility");
        }
        encounter_proof["entrance_mirror"] = {{"scene", path(scene)},
                                              {"cinematic_mesh", path(cinematic_mesh)},
                                              {"duplicate_present", duplicate_present},
                                              {"duplicate_visible", duplicate_visible},
                                              {"gameplay_mirror", path(gameplay_mirror)},
                                              {"gameplay_material", path(material.result())},
                                              {"gameplay_visible", gameplay_visible}};
        return true;
    }

    auto Kh02EntranceMirror::tick(UObject* scene, UObject* context, bool playing, Json& encounter_proof)
        -> std::optional<std::string>
    {
        if (playing && ready_scene.Get() != scene)
        {
            const auto now = std::chrono::steady_clock::now();
            if (pending_scene.Get() != scene)
            {
                pending_scene = make_uobject_weak(scene);
                deadline = now + entrance_mirror_timeout;
                next_attempt = now;
            }
            if (now >= next_attempt)
            {
                next_attempt = now + entrance_mirror_poll_interval;
                try
                {
                    const bool allow_retired = now >= deadline - entrance_mirror_retired_window;
                    if (prepare(scene, context, allow_retired, encounter_proof))
                    {
                        ready_scene = make_uobject_weak(scene);
                    }
                    else if (now >= deadline)
                    {
                        throw std::runtime_error("kh02_within_entrance_mirror_not_ready");
                    }
                }
                catch (const std::exception& error)
                {
                    ready_scene = make_uobject_weak(scene);
                    encounter_proof["entrance_mirror"]["error"] = error.what();
                    return error.what();
                }
            }
        }

        return std::nullopt;
    }

    void Kh02EntranceMirror::clear()
    {
        ready_scene = {};
        pending_scene = {};
        deadline = {};
        next_attempt = {};
    }

} // namespace kh3ap
