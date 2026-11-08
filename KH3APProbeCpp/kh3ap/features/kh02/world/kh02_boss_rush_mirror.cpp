#include "kh02_boss_rush_mirror.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;

    void Kh02BossRushMirror::watch(UObject* actor)
    {
        if (!is_uobject_valid_fast(actor) || actor == mirror.Get() ||
            actor->GetPathName() !=
                L"/Game/Zevels/dw/dw_03/umap/dw_03_challenge.dw_03_challenge:PersistentLevel.g_dw_Mirror_Challenge" ||
            actor->GetClassPrivate()->GetPathName() !=
                L"/Game/Zlueprints/Gimmick/dw/g_dw_Mirror/g_dw_MirrorA2.g_dw_MirrorA2_C")
        {
            return;
        }
        mirror = make_uobject_weak(actor);
        start_requested = false;
    }

    void Kh02BossRushMirror::tick(UObject* world, bool chapter_complete)
    {
        if (!chapter_complete || start_requested)
        {
            return;
        }
        auto* actor = mirror.Get();
        if (!is_uobject_valid_fast(actor) || !is_uobject_valid_fast(world) || kh02_actor_world(actor) != world ||
            world->GetPathName() != L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin")
        {
            return;
        }
        // Use the source's activation sequence without its appearance burst.
        // OnStartGimmick(PlayAnimation=true) spawns vg_dw03_cha_app_par30;
        // revisits should reveal the mirror immediately without that flash.
        // Keep Zodiac queries/objective 43 tied to the actual chest journal.
        constexpr std::size_t parameter_limit_bytes = 64;
        Kh02ReflectedCall start(actor, L"StartGimmick", "kh02_boss_rush_mirror", parameter_limit_bytes);
        start.boolean(L"ActivateGimmick", true);
        start.boolean(L"Reset", true);
        start.boolean(L"PlayAnimation", false);
        start_requested = true;
        start.run();
    }

    void Kh02BossRushMirror::clear_world()
    {
        mirror = {};
        start_requested = false;
    }

    auto Kh02BossRushMirror::snapshot() const -> nlohmann::json
    {
        auto* actor = mirror.Get();
        return {{"actor", is_uobject_valid_fast(actor) ? to_utf8(actor->GetPathName()) : ""},
                {"start_requested", start_requested}};
    }
} // namespace kh3ap
