#pragma once
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace kh3ap
{
    inline constexpr std::uintmax_t kh02_entry_config_limit_bytes = 4'096;
    inline constexpr std::uintmax_t kh02_checkpoint_config_limit_bytes = 8'192;

    inline auto kh02_read_json_file(const std::filesystem::path& file, std::uintmax_t limit, const char* size_error)
        -> nlohmann::json
    {
        if (std::filesystem::file_size(file) > limit)
        {
            throw std::runtime_error(size_error);
        }
        std::ifstream input(file);
        return nlohmann::json::parse(input);
    }

    auto kh02_read_runtime_config(const wchar_t* filename, std::uintmax_t limit, bool optional, const char* image_error,
                                  const char* size_error) -> std::optional<nlohmann::json>;
} // namespace kh3ap
