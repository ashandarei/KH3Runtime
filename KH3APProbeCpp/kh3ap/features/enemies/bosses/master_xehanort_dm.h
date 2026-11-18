#pragma once

namespace RC::Unreal { class UObject; }

namespace kh3ap
{
    void install_master_xehanort_dm_cleanup();
    void observe_master_xehanort_dm_actor(RC::Unreal::UObject* actor);
    void notify_master_xehanort_battle_ended(RC::Unreal::UObject* player);
    void process_pending_master_xehanort_dm_cleanup();
}
