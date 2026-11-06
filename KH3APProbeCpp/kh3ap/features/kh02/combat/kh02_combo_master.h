#pragma once
#include "kh02_combo_master_visit.h"
#include "kh3ap/features/kh02/core/kh02_context_refresh.h"
#include <chrono>
#include <string>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02ComboMaster
    {
      public:
        // A native context event starts a refresh. Once settled, quiet frames
        // do not resolve loading state, the save menu, the pawn or AP receipts.
        template <class Ready, class ResolvePawn>
        void tick(uint64_t context_revision, std::chrono::steady_clock::time_point now, Ready&& ready,
                  ResolvePawn&& resolve_pawn)
        {
            refresh.observe(context_revision);
            if (!refresh.due(now))
            {
                return;
            }
            const bool settled = ready() && update(resolve_pawn());
            refresh.complete(settled, now);
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        auto snapshot(RC::Unreal::UObject* pawn) const -> std::string;
#endif
      private:
        auto update(RC::Unreal::UObject* pawn) -> bool;
        Kh02ContextRefresh refresh;
        Kh02ComboMasterVisit visit;
        std::wstring failure;
        std::string phase{"idle"};
    };
} // namespace kh3ap
