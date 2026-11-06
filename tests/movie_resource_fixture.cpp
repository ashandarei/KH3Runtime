// Portable decoder tests only. Windows tests and the DLL link the real RCDATA.
#include "kh3ap/core/ui/embedded_movie_resource.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

namespace kh3ap {
auto load_embedded_movie_resource(const wchar_t* name, uint32_t expected_size) noexcept
    -> std::span<const uint8_t> {
    try {
        struct Fixture { std::wstring_view name; std::vector<uint8_t> bytes; };
        static const auto fixtures = [] {
            const auto read = [](const char* filename) {
                std::ifstream file(std::filesystem::path{KH3AP_TEST_MOVIE_RESOURCE_DIR}/filename, std::ios::binary);
                return std::vector<uint8_t>{std::istreambuf_iterator<char>{file}, {}};
            };
            return std::array{
                Fixture{L"KH3AP_MAP_CHEST_MOVIE", read("map_chest_movie.swf")},
                Fixture{L"KH3AP_SHOP_PATCH_EGS", read("shop_movie_patch_egs.bin")},
                Fixture{L"KH3AP_SHOP_PATCH_STEAM", read("shop_movie_patch_steam.bin")},
                Fixture{L"KH3AP_INGREDIENT_JOURNAL_PATCH", read("ingredient_journal_patch.bin")},
            };
        }();
        for (const auto& item : fixtures)
            if (item.name == name && expected_size && item.bytes.size() == expected_size) return item.bytes;
    } catch (...) {}
    return {};
}
}
