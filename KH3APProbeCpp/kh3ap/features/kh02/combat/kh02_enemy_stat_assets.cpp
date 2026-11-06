#include "kh02_enemy_stat_assets.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr int base_status_request_id = 0x4b483490;
        constexpr int imported_table_request_begin = 0x4b483491;
        constexpr int native_tide_request_id = 0x4b4834b0;
        constexpr const wchar_t* paths[] = {
            L"/Game/Zlueprints/Enemy/e_ex001/DataTable/e_ex001_BaseDataTable.e_ex001_BaseDataTable",
            L"/Game/Zlueprints/Enemy/e_ex001_s0/DataTable/e_ex001_BaseDataTable_s0.e_ex001_BaseDataTable_s0",
            L"/Game/Zlueprints/Enemy/e_ex003/DataTable/e_ex003_BaseDataTable.e_ex003_BaseDataTable",
            L"/Game/Zlueprints/Enemy/e_ex081/DataTable/e_ex081_BaseDataTable.e_ex081_BaseDataTable",
            L"/Game/Zlueprints/Enemy/e_dw402/DataTable/e_dw402_BaseDataTable.e_dw402_BaseDataTable",
            L"/Game/Zlueprints/Enemy/e_dw402_s0/DataTable/e_dw402_BaseDataTable_s0.e_dw402_BaseDataTable_s0",
            L"/Game/Zlueprints/Enemy/e_dw401/DataTable/e_dw401_BaseDataTable.e_dw401_BaseDataTable",
            L"/Game/Zlueprints/Enemy/e_ex004/DataTable/e_ex004_BaseDataTable.e_ex004_BaseDataTable",
            L"/Game/Zlueprints/Enemy/e_ex016/DataTable/e_ex016_BaseDataTable.e_ex016_BaseDataTable",
            L"/Game/Zlueprints/Enemy/e_dw405/DataTable/e_dw405_BaseDataTable.e_dw405_BaseDataTable",
            L"/Game/Zlueprints/Enemy/e_dw405_s1/DataTable/e_dw405_BaseDataTable_s1.e_dw405_BaseDataTable_s1",
            L"/Game/Zlueprints/Enemy/e_dw405_s100/DataTable/e_dw405_BaseDataTable_s100.e_dw405_BaseDataTable_s100",
            L"/Game/Zlueprints/Enemy/e_dw402_s100/DataTable/e_dw402_BaseDataTable_s100.e_dw402_BaseDataTable_s100",
            L"/Game/Zlueprints/Enemy/e_dw407_s100/DataTable/e_dw407_BaseDataTable_s100.e_dw407_BaseDataTable_s100",
            L"/Game/Zlueprints/Enemy/e_ex016_s100/DataTable/e_ex016_BaseDataTable_s100.e_ex016_BaseDataTable_s100",
            L"/Game/Zlueprints/Enemy/e_dw407/DataTable/e_dw407_BaseDataTable.e_dw407_BaseDataTable",
            L"/Game/Zlueprints/Enemy/e_dw407a_s0/DataTable/e_dw407a_BaseDataTable.e_dw407a_BaseDataTable",
            L"/Game/Zlueprints/Enemy/e_dw407b/DataTable/e_dw407b_BaseDataTable.e_dw407b_BaseDataTable"};

    } // namespace

    Kh02EnemyStatAssets::~Kh02EnemyStatAssets()
    {
        release();
    }

    auto Kh02EnemyStatAssets::load(UObject* context, const wchar_t* path, int uuid, FWeakObjectPtr& cached) -> UObject*
    {
        auto* o = cached.Get();
        if (!is_uobject_valid_fast(o))
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            ++asset_path_lookups;
#endif
            o = find_loaded_runtime_asset(path);
            if (is_uobject_valid_fast(o))
            {
                cached = make_uobject_weak(o);
                requested.erase(path);
            }
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        else
        {
            ++asset_cache_hits;
        }
#endif
        if (is_uobject_valid_fast(o))
        {
            if (!o->HasAnyInternalFlags(EInternalObjectFlags::RootSet))
            {
                o->SetRootSet();
                roots.push_back(make_uobject_weak(o));
            }
            return o;
        }
        if (requested.insert(path).second)
        {
            std::wstring why;
            if (!request_runtime_asset_load(context, path, &why, uuid))
            {
                throw std::runtime_error("kh02_stats_asset_request:" + to_utf8(why));
            }
        }
        return nullptr;
    }

    auto Kh02EnemyStatAssets::prepare(UObject* context) -> bool
    {
        const bool base_loaded = load(context, L"/Game/Load/Common/EnemyBaseStatus.EnemyBaseStatus",
                                      base_status_request_id, base_status) != nullptr;
        const bool tide_loaded =
            load(context, L"/Game/DataTable/Enemy/Base/e_dw401_BaseDataTable_s1.e_dw401_BaseDataTable_s1",
                 native_tide_request_id, native_tide) != nullptr;
        bool loaded = base_loaded && tide_loaded;
        // Preserve successful slots while other async requests are unfinished.
        // Weak identities are validated before reuse; no row pointers are cached.
        if (loaded_tables.empty())
        {
            loaded_tables.resize(std::size(paths));
        }
        for (int index = 0; index < std::size(paths); ++index)
        {
            auto* table = load(context, paths[index], imported_table_request_begin + index, loaded_tables[index]);
            loaded &= table != nullptr;
        }
        return loaded;
    }

    void Kh02EnemyStatAssets::release()
    {
        for (const auto& root : roots)
        {
            if (auto* object = root.Get(); is_uobject_valid_fast(object))
            {
                object->ClearRootSet();
            }
        }
        roots.clear();
        loaded_tables.clear();
        requested.clear();
        base_status = {};
        native_tide = {};
    }
} // namespace kh3ap
