#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <set>
#include <cstdint>
#include <string>
#include <vector>

namespace kh3ap
{
    // Asset acquisition only: row capture, restoration and scaling remain in EnemyStats.
    // Restore changed rows before releasing the roots acquired by this lease.
    class Kh02EnemyStatAssets
    {
      public:
        Kh02EnemyStatAssets() = default;
        ~Kh02EnemyStatAssets();
        Kh02EnemyStatAssets(const Kh02EnemyStatAssets&) = delete;
        auto operator=(const Kh02EnemyStatAssets&) -> Kh02EnemyStatAssets& = delete;
        auto prepare(RC::Unreal::UObject* context) -> bool;
        void release();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        auto path_lookups() const -> uint64_t
        {
            return asset_path_lookups;
        }

        auto cache_hits() const -> uint64_t
        {
            return asset_cache_hits;
        }
#endif

        auto base_table() const -> RC::Unreal::UObject*
        {
            return base_status.Get();
        }

        auto native_tide_table() const -> RC::Unreal::UObject*
        {
            return native_tide.Get();
        }

        auto tables() const -> const std::vector<RC::Unreal::FWeakObjectPtr>&
        {
            return loaded_tables;
        }

      private:
        auto load(RC::Unreal::UObject* context, const wchar_t* path, int uuid, RC::Unreal::FWeakObjectPtr& cached)
            -> RC::Unreal::UObject*;
        std::vector<RC::Unreal::FWeakObjectPtr> roots;
        std::vector<RC::Unreal::FWeakObjectPtr> loaded_tables;
        std::set<std::wstring> requested;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        uint64_t asset_path_lookups{};
        uint64_t asset_cache_hits{};
#endif
        RC::Unreal::FWeakObjectPtr base_status, native_tide;
    };
} // namespace kh3ap
