#pragma once
#include <filesystem>
#include <optional>
namespace RC::Unreal
{
    class UObject;
}
namespace kh3ap
{
    struct RuntimeKeybladeStyleState;
    struct RuntimeKeybladeStyleSanityDeps;
} // namespace kh3ap

namespace kh3ap::keyblade_options
{
    auto load_persisted_styles(RuntimeKeybladeStyleState& state,
                               const std::optional<std::filesystem::path>& bridge_dir) -> void;
    auto save_persisted_styles(RuntimeKeybladeStyleState& state,
                               const std::optional<std::filesystem::path>& bridge_dir) -> void;
} // namespace kh3ap::keyblade_options
