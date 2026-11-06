#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
namespace RC::Unreal
{
    class UObject;
}
namespace kh3ap::keyblade_options
{
    using RC::Unreal::UObject;
    auto resolve_keyblade_name_fallback(uint64_t raw_weapon_id) -> std::optional<std::wstring>;
    auto resolve_keyblade_name_via_blueprint_enum_utility(uint64_t raw_weapon_id,
                                                          std::wstring* diagnostics)
        -> std::optional<std::wstring>;
    auto try_read_equipped_weapon_id(UObject* sora, std::wstring_view function_path,
                                     std::wstring* failure_details, std::wstring* diagnostics)
        -> std::optional<uint64_t>;
} // namespace kh3ap::keyblade_options
