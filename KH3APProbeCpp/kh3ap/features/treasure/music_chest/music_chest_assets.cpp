#include "kh3ap/features/treasure/music_chest/music_chest_internal.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include <Unreal/UClass.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace music_chest;

    void RuntimeMusicChestService::Impl::retain_ready_assets()
    {
        // Only unfinished setup probes loading assets. Retain each independent
        // completion so GC cannot unload it while another package is pending.
        if (shared_assets_requested)
        {
            if (!retained_notice)
            {
                if (auto* a = find_loaded_runtime_asset_class(notice_path))
                {
                    a->SetRootSet();
                    retained_notice = a;
                }
            }
            if (!retained_paint)
            {
                if (auto* a = loaded(paint_path))
                {
                    a->SetRootSet();
                    retained_paint = a;
                }
            }
        }
        if (spawn_assets_requested && retained_spawn_assets.size() < 4)
        {
            for (auto path : {spawned_class_path, spawned_mesh_path, spawned_diffuse_path,
                              spawned_animation_path})
            {
                if (retained_spawn_assets.contains(path))
                {
                    continue;
                }
                if (auto* a = loaded(path))
                {
                    a->SetRootSet();
                    retained_spawn_assets.emplace(path, a);
                }
            }
        }
    }

    bool RuntimeMusicChestService::Impl::prepare_spawn_assets(UObject* pawn)
    {
        spawn_assets_requested = true;
        retain_ready_assets();
        spawn_load_state.clear();
        bool ready = true;
        const bool request = Clock::now() >= next_asset_request;
        for (auto path :
             {spawned_class_path, spawned_mesh_path, spawned_diffuse_path, spawned_animation_path})
        {
            if (!retained_spawn_assets.contains(path))
            {
                ready = false;
                spawn_load_state += std::wstring(path) + L" ";
                if (request)
                {
                    const int32_t uuid = 0x4b600000 | (++load_sequence & 0xfffff);
                    std::wstring reason;
                    if (path == spawned_class_path)
                    {
                        request_runtime_asset_class_load(pawn, path, &reason, uuid);
                    }
                    else
                    {
                        request_runtime_asset_load(pawn, path, &reason, uuid);
                    }
                    spawn_load_state += reason + L" ";
                }
            }
        }
        if (request)
        {
            next_asset_request = Clock::now() + music_chest_asset_retry_interval;
        }
        return ready;
    }
} // namespace kh3ap
