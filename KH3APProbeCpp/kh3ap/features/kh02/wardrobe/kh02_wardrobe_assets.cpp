#include "kh02_wardrobe_assets.h"
#include "kh02_wardrobe_reflection.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/objects/asset_load_helpers.h"

namespace
{
    constexpr int wardrobe_asset_request_base = 0x4b484100;
}

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace kh02wardrobe_detail;

    void Kh02WardrobeAssets::release()
    {
        for (auto& weak : roots)
        {
            if (auto* asset = weak.Get(); is_uobject_valid_fast(asset))
            {
                asset->ClearRootSet();
            }
        }
        roots.clear();
        requested.clear();
    }

    void Kh02WardrobeAssets::retain(UObject* object)
    {
        if (!object->HasAnyInternalFlags(EInternalObjectFlags::RootSet))
        {
            object->SetRootSet();
            roots.push_back(make_uobject_weak(object));
        }
    }

    auto Kh02WardrobeAssets::load(UObject* context, const wchar_t* path) -> UObject*
    {
        auto* loaded = find_loaded_runtime_asset(path);
        if (!loaded)
        {
            if (requested.insert(path).second &&
                !request_runtime_asset_load(context, path, nullptr,
                                            wardrobe_asset_request_base + static_cast<int>(requested.size())))
            {
                throw std::runtime_error("Could not request a wardrobe asset");
            }
            return nullptr;
        }
        retain(loaded);
        return loaded;
    }

} // namespace kh3ap
