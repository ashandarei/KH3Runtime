#include "kh02_event_aqua.h"
#include "kh02_event_aqua_materials.h"
#include "kh3ap/features/kh02/runtime/kh02_entry_assets.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/AActor.hpp>
#include <Unreal/UClass.hpp>

#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

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

        constexpr auto aqua_class = L"/Game/Zlueprints/EventPawn/ex/p_ex002_0_Event.p_ex002_0_Event_C";
        constexpr int aqua_class_request_id = 0x4b483440;
        constexpr int playable_asset_request_base = 0x4b483441;
        constexpr int always_spawn_collision_handling = 1;

    } // namespace

    bool Kh02EventAqua::prepare(UObject* pawn)
    {
        if (!is_uobject_valid_fast(event_actor.Get()))
        {
            auto* event_class = find_loaded_runtime_asset_class(aqua_class);
            if (!event_class)
            {
                if (!class_requested)
                {
                    std::wstring why;
                    if (!request_runtime_asset_class_load(pawn, aqua_class, &why, aqua_class_request_id))
                    {
                        throw std::runtime_error("kh02_story_aqua_request:" + to_utf8(why));
                    }
                    class_requested = true;
                }
                return false;
            }
            ReflectedCall begin(kh02_require_loaded_object(L"/Script/Engine.Default__GameplayStatics"),
                                L"BeginDeferredActorSpawnFromClass");
            begin.object(L"WorldContextObject", pawn);
            begin.object(L"ActorClass", event_class);
            begin.integer(L"CollisionHandlingOverride", always_spawn_collision_handling);
            begin.spawn_transform();
            begin.run();
            auto* actor = static_cast<AActor*>(begin.result());
            if (!is_uobject_valid_fast(actor))
            {
                throw std::runtime_error("kh02_story_spawn_begin");
            }
            // Resolve by reflection: this game's SpawnTransform is aligned
            // to offset 16; UE4SS's generic wrapper mispacks Finish's field.
            ReflectedCall finish(kh02_require_loaded_object(L"/Script/Engine.Default__GameplayStatics"),
                                 L"FinishSpawningActor");
            finish.object(L"Actor", actor);
            finish.spawn_transform();
            finish.run();
            auto* finished = finish.result();
            if (is_uobject_valid_fast(finished))
            {
                actor = static_cast<AActor*>(finished);
            }
            if (!is_uobject_valid_fast(actor) ||
                !is_uobject_valid_fast(try_read_reflected_object_property_value(actor, L"MyMesh")))
            {
                throw std::runtime_error("kh02_story_spawn_finish");
            }
            event_actor = make_uobject_weak(actor);
            if (event_actor.Get() != actor)
            {
                throw std::runtime_error("kh02_story_aqua_weak");
            }
            kh02_adapt_event_aqua_eyes(actor);
        }
        return true;
    }

    auto Kh02EventAqua::preload(UObject* pawn, Kh02EntryAssets& assets) -> bool
    {
        auto* event_class = find_loaded_runtime_asset_class(aqua_class);
        if (!event_class)
        {
            if (!class_requested)
            {
                std::wstring why;
                if (!request_runtime_asset_class_load(pawn, aqua_class, &why, aqua_class_request_id))
                {
                    throw std::runtime_error("kh02_entry_aqua_preload:" + to_utf8(why));
                }
                class_requested = true;
            }
        }
        else
        {
            assets.retain(event_class);
        }

        // Story Aqua's gameplay pawn is the KH3 player-factory class, separate
        // from the imported event pawn. Load its weapon dependencies while the
        // chapter's entry screen is already up, before the first R2 press.
        struct PlayableAsset
        {
            const wchar_t* path;
            bool is_class;
        };

        constexpr std::array playable_assets{
            PlayableAsset{L"/Game/Blueprints/Player/p_ex002/p_ex002_Pawn.p_ex002_Pawn_C", true},
            PlayableAsset{L"/Game/Blueprints/Player/p_ex002/w_aq010_Wep.w_aq010_Wep_C", true},
            PlayableAsset{L"/Game/Blueprints/Player/p_ex002/w_aq010_Equip.w_aq010_Equip", false}};
        bool playable_ready = true;
        for (size_t asset_index = 0; asset_index < playable_assets.size(); ++asset_index)
        {
            const auto& playable_asset = playable_assets[asset_index];
            auto* loaded = playable_asset.is_class
                               ? static_cast<UObject*>(find_loaded_runtime_asset_class(playable_asset.path))
                               : find_loaded_runtime_asset(playable_asset.path);
            if (is_uobject_valid_fast(loaded))
            {
                assets.retain(loaded);
                continue;
            }
            playable_ready = false;
            if (!playable_assets_requested[asset_index])
            {
                std::wstring why;
                const int request_id = playable_asset_request_base + static_cast<int>(asset_index);
                const bool requested =
                    playable_asset.is_class
                        ? request_runtime_asset_class_load(pawn, playable_asset.path, &why, request_id)
                        : request_runtime_asset_load(pawn, playable_asset.path, &why, request_id);
                if (!requested)
                {
                    throw std::runtime_error("kh02_entry_playable_aqua_preload:" + to_utf8(why));
                }
                playable_assets_requested[asset_index] = true;
            }
        }
        return event_class != nullptr && playable_ready;
    }

} // namespace kh3ap
