#pragma once
namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    void notify_master_ascension_created(RC::Unreal::UObject* actor);
    void process_pending_master_ascension_centers();
    namespace enemy_remap_detail
    {
        void install_master_xehanort_center_support();
        void prepare_master_xehanort_center(RC::Unreal::UObject* pawn);
        void prepare_master_xehanort_action(RC::Unreal::UObject* pawn);
        void initialize_master_xehanort_center(RC::Unreal::UObject* pawn,
                                               RC::Unreal::UObject* entity);
    } // namespace enemy_remap_detail
} // namespace kh3ap
