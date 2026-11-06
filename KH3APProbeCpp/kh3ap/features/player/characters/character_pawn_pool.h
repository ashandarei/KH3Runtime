#pragma once
#include "kh3ap/core/performance_build.h"
#include <memory>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include <nlohmann/json_fwd.hpp>
#endif

namespace RC::Unreal
{
    class UObject;
}
namespace kh3ap::character_pawn_pool
{
    // A synchronous factory transaction. Only its outgoing objects may be
    // retained, and only the two verified factory spawn sites may reuse actors.
    class Transaction
    {
    public:
        Transaction(RC::Unreal::UObject* outgoing, int target);
        ~Transaction();
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;
        void finish();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
    void clear();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    bool set_enabled(bool enabled);
    nlohmann::json snapshot();
#endif
} // namespace kh3ap::character_pawn_pool
