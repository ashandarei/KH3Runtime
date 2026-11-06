#pragma once
#include <filesystem>
#include <string_view>

namespace kh3ap
{
    // Disk only: publishing a committed checkpoint is the caller's decision.
    auto write_checkpoint_file_atomic_verified(const std::filesystem::path& path,
                                               std::string_view content) -> bool;
} // namespace kh3ap
