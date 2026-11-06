#include "kh02_entry_assets.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    void Kh02EntryAssets::retain(UObject* asset)
    {
        if (!is_uobject_valid_fast(asset))
        {
            throw std::runtime_error("kh02_entry_asset_invalid");
        }
        if (!asset->HasAnyInternalFlags(EInternalObjectFlags::RootSet))
        {
            asset->SetRootSet();
            roots.push_back(make_uobject_weak(asset));
        }
    }

    void Kh02EntryAssets::release()
    {
        for (auto& weak : roots)
        {
            if (auto* asset = weak.Get(); is_uobject_valid_fast(asset))
            {
                asset->ClearRootSet();
            }
        }
        roots.clear();
    }

    auto Kh02EntryAssets::paths() const -> nlohmann::json
    {
        auto paths = nlohmann::json::array();
        for (const auto& weak : roots)
        {
            if (auto* asset = weak.Get(); is_uobject_valid_fast(asset))
            {
                paths.push_back(to_utf8(asset->GetPathName()));
            }
        }
        return paths;
    }
} // namespace kh3ap
