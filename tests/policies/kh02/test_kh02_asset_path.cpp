#include "kh3ap/features/kh02/transport/kh02_asset_path.h"
#include <cassert>
#include <string>
#include <unordered_map>

int main()
{
    using namespace kh02asset;
    using namespace std::literals;
    const std::wstring canonical = L"/content/zaps/dw/level.umap";
    const auto variant = L"\\CONTENT\\Zaps\\DW\\Level.UMAP"sv;
    assert(AssetPathEqual{}(canonical, variant));
    assert(AssetPathHash{}(canonical) == AssetPathHash{}(variant));
    assert(!AssetPathEqual{}(canonical, canonical + L"x"));
    assert(!AssetPathEqual{}(canonical, L"/content/zaps/dw/other.umap"));
    assert(content_path_suffix(L"../../../KINGDOM HEARTS III/Content/Zaps/dw/level.umap") ==
           L"/Content/Zaps/dw/level.umap");
    assert(content_path_suffix(L"C:\\Content\\Game\\CONTENT\\Zaps\\DW\\Level.UMAP") == variant);
    assert(content_path_suffix(L"C:\\Users\\\u00e9\\Game\\Content\\Zaps\\DW\\Level.UMAP") ==
           L"\\Content\\Zaps\\DW\\Level.UMAP");
    assert(content_path_suffix(L"/content/") == L"/content/");
    assert(content_path_suffix(L"content/x.umap").empty());
    assert(content_path_suffix(L"/contention/x.umap").empty());
    assert(content_path_suffix(L"short").empty());
    assert(content_path_suffix(L"").empty());
    assert(!AssetPathEqual{}(L"/content/../x", L"/content/x"));
    assert(!AssetPathEqual{}(L"/content//x", L"/content/x"));

    std::unordered_map<std::wstring, int, AssetPathHash, AssetPathEqual> files;
    files.emplace(canonical, 7);
    const auto found = files.find(variant);
    assert(found != files.end() && found->second == 7);
    assert(files.find(L"/content/native/unmapped.uasset"sv) == files.end());
    assert(!files.emplace(std::wstring(variant), 8).second);
}
