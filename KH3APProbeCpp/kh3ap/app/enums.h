#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace kh3ap
{
    struct RuntimeEnumPrewarmResult
    {
        size_t victory_bonus_count{};
        size_t ability_count{};
        bool victory_bonus_reflected{};
        bool ability_reflected{};
    };

    class RuntimeEnumResolver
    {
    public:
        explicit RuntimeEnumResolver(std::filesystem::path shared_enums_relative_path);

        auto ensure_initialized() -> void;
        auto prewarm_reflected_values() -> RuntimeEnumPrewarmResult;
        auto resolve(std::wstring_view runtime_token) -> std::optional<uint64_t>;
        auto format_ability(uint64_t enum_value) -> std::wstring;

    private:
        enum class ActiveEnumBlock
        {
            None,
            VictoryBonus,
            Ability,
        };

        auto load() -> void;

        std::filesystem::path m_shared_enums_relative_path;
        std::once_flag m_init_once{};
        std::unordered_map<std::wstring, uint64_t> m_vbonus_values{};
        std::unordered_map<std::wstring, uint64_t> m_ability_values{};
        bool m_vbonus_reflection_loaded{};
        bool m_ability_reflection_loaded{};
    };

    auto configure_runtime_enum_resolver(std::filesystem::path shared_enums_relative_path) -> void;
    auto maybe_initialize_runtime_enum_value_maps() -> RuntimeEnumPrewarmResult;
    auto try_resolve_runtime_enum_value(std::wstring_view runtime_token) -> std::optional<uint64_t>;
    auto try_format_runtime_ability_enum_value(uint64_t enum_value) -> std::wstring;
}
