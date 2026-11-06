#pragma once
#include <filesystem>
#include <string_view>

namespace kh3ap
{
    // Experimental EGS optimization, compiled out of player builds. The
    // diagnostic bridge enables it only after an explicit benchmark request.
    auto install_native_frame_wait(std::wstring_view build_stamp) -> void;
    auto uninstall_native_frame_wait() -> void;
    auto observe_native_frame_tick() noexcept -> void;
    auto process_native_frame_wait_request(const std::filesystem::path& bridge) -> void;
}
