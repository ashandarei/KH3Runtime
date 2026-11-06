#include "kh02_runtime_config.h"
#define NOMINMAX
#include <Windows.h>

namespace kh3ap
{
    auto kh02_read_runtime_config(const wchar_t* filename, std::uintmax_t limit, bool optional, const char* image_error,
                                  const char* size_error) -> std::optional<nlohmann::json>
    {
        constexpr int maximum_windows_path_characters = 32'768;
        wchar_t image[maximum_windows_path_characters]{};
        if (!GetModuleFileNameW(nullptr, image, maximum_windows_path_characters))
        {
            throw std::runtime_error(image_error);
        }
        const auto file = std::filesystem::path(image).parent_path() / filename;
        if (optional && !std::filesystem::exists(file))
        {
            return std::nullopt;
        }
        return kh02_read_json_file(file, limit, size_error);
    }
} // namespace kh3ap
