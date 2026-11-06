#include "kh3ap/app/enums.h"

#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/utilities.h"

#include <Unreal/UEnum.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <memory>
#include <mutex>
#include <regex>
#include <sstream>

namespace kh3ap
{
    namespace
    {
        auto runtime_enum_resolver_mutex() -> std::mutex&
        {
            static std::mutex mutex{};
            return mutex;
        }

        auto runtime_enum_resolver_instance() -> std::unique_ptr<RuntimeEnumResolver>&
        {
            static std::unique_ptr<RuntimeEnumResolver> resolver{};
            return resolver;
        }

        auto runtime_enum_resolver() -> RuntimeEnumResolver&
        {
            auto& resolver = runtime_enum_resolver_instance();
            if (!resolver)
            {
                resolver = std::make_unique<RuntimeEnumResolver>(std::filesystem::path{});
            }
            return *resolver;
        }

        auto try_resolve_reflected_enum_value(std::wstring_view runtime_token)
                -> std::optional<uint64_t>
        {
            std::wstring_view enum_prefix{};
            std::wstring_view enum_path{};
            if (runtime_token.starts_with(L"ETresVictoryBonusKind::"))
            {
                enum_prefix = L"ETresVictoryBonusKind::";
                enum_path = L"/Script/TresGame.ETresVictoryBonusKind";
            }
            else if (runtime_token.starts_with(L"ETresAbilityKind::"))
            {
                enum_prefix = L"ETresAbilityKind::";
                enum_path = L"/Script/TresGame.ETresAbilityKind";
            }
            else
            {
                return std::nullopt;
            }

            RC::Unreal::UEnum* const enum_object =
                    RC::Unreal::UObjectGlobals::StaticFindObject<RC::Unreal::UEnum*>(
                            nullptr,
                            nullptr,
                            std::wstring{enum_path});
            if (!enum_object || !kh3ap::is_uobject_valid_fast(enum_object))
            {
                return std::nullopt;
            }

            const std::wstring_view member_name = runtime_token.substr(enum_prefix.size());
            for (const auto& [name, value] : enum_object->ForEachName())
            {
                if (value < 0)
                {
                    continue;
                }
                const std::wstring reflected_name = name.ToString();
                if (reflected_name == runtime_token || reflected_name == member_name)
                {
                    return static_cast<uint64_t>(value);
                }
                const size_t separator = reflected_name.rfind(L"::");
                if (separator != std::wstring::npos &&
                    std::wstring_view{reflected_name}.substr(separator + 2) == member_name)
                {
                    return static_cast<uint64_t>(value);
                }
            }
            return std::nullopt;
        }

        auto append_reflected_enum_values(
                std::wstring_view enum_path,
                std::wstring_view enum_prefix,
                std::unordered_map<std::wstring, uint64_t>& values) -> bool
        {
            RC::Unreal::UEnum* const enum_object =
                    RC::Unreal::UObjectGlobals::StaticFindObject<RC::Unreal::UEnum*>(
                            nullptr,
                            nullptr,
                            std::wstring{enum_path});
            if (!enum_object || !kh3ap::is_uobject_valid_fast(enum_object))
            {
                return false;
            }

            for (const auto& [name, value] : enum_object->ForEachName())
            {
                if (value < 0)
                {
                    continue;
                }
                const std::wstring reflected_name = name.ToString();
                const size_t separator = reflected_name.rfind(L"::");
                const std::wstring_view member_name = separator == std::wstring::npos
                        ? std::wstring_view{reflected_name}
                        : std::wstring_view{reflected_name}.substr(separator + 2);
                if (member_name.empty())
                {
                    continue;
                }
                values.insert_or_assign(
                        std::wstring{enum_prefix} + std::wstring{member_name},
                        static_cast<uint64_t>(value));
            }
            return true;
        }
    }

    RuntimeEnumResolver::RuntimeEnumResolver(std::filesystem::path shared_enums_relative_path)
        : m_shared_enums_relative_path(std::move(shared_enums_relative_path))
    {
    }

    auto RuntimeEnumResolver::ensure_initialized() -> void
    {
        std::call_once(m_init_once, [this]() {
            load();
        });
    }

    auto RuntimeEnumResolver::load() -> void
    {
        const auto module_directory = try_get_current_module_directory();
        if (!module_directory.has_value())
        {
            return;
        }

        const auto enum_path = (*module_directory / m_shared_enums_relative_path).lexically_normal();
        const auto enum_text = read_text_file(enum_path);
        if (!enum_text.has_value())
        {
            return;
        }

        ActiveEnumBlock active_block = ActiveEnumBlock::None;
        std::istringstream input(*enum_text);
        std::string line{};
        const std::regex entry_pattern(R"(^\s*([A-Za-z0-9_]+)\s*=\s*([0-9]+),\s*$)");
        while (std::getline(input, line))
        {
            if (line.find("---@enum ETresVictoryBonusKind") != std::string::npos)
            {
                active_block = ActiveEnumBlock::VictoryBonus;
                continue;
            }
            if (line.find("---@enum ETresAbilityKind") != std::string::npos)
            {
                active_block = ActiveEnumBlock::Ability;
                continue;
            }
            if (active_block == ActiveEnumBlock::None)
            {
                continue;
            }
            if (line.find("local ETres") != std::string::npos)
            {
                continue;
            }
            if (line.find('}') != std::string::npos)
            {
                active_block = ActiveEnumBlock::None;
                continue;
            }

            std::smatch match{};
            if (!std::regex_match(line, match, entry_pattern) || match.size() < 3)
            {
                continue;
            }

            const std::string enum_member_text = match[1].str();
            const std::wstring enum_member{enum_member_text.begin(), enum_member_text.end()};
            const uint64_t enum_value = static_cast<uint64_t>(std::stoull(match[2].str()));
            if (active_block == ActiveEnumBlock::VictoryBonus)
            {
                m_vbonus_values.emplace(std::wstring{L"ETresVictoryBonusKind::"} + enum_member, enum_value);
            }
            else if (active_block == ActiveEnumBlock::Ability)
            {
                m_ability_values.emplace(std::wstring{L"ETresAbilityKind::"} + enum_member, enum_value);
            }
        }
    }

    auto RuntimeEnumResolver::prewarm_reflected_values() -> RuntimeEnumPrewarmResult
    {
        ensure_initialized();
        if (!m_vbonus_reflection_loaded)
        {
            m_vbonus_reflection_loaded = append_reflected_enum_values(
                    L"/Script/TresGame.ETresVictoryBonusKind",
                    L"ETresVictoryBonusKind::",
                    m_vbonus_values);
        }
        if (!m_ability_reflection_loaded)
        {
            m_ability_reflection_loaded = append_reflected_enum_values(
                    L"/Script/TresGame.ETresAbilityKind",
                    L"ETresAbilityKind::",
                    m_ability_values);
        }
        return {
                .victory_bonus_count = m_vbonus_values.size(),
                .ability_count = m_ability_values.size(),
                .victory_bonus_reflected = m_vbonus_reflection_loaded,
                .ability_reflected = m_ability_reflection_loaded,
        };
    }

    auto RuntimeEnumResolver::resolve(std::wstring_view runtime_token) -> std::optional<uint64_t>
    {
        ensure_initialized();

        if (runtime_token.starts_with(L"ETresVictoryBonusKind::"))
        {
            if (const auto found = m_vbonus_values.find(std::wstring{runtime_token}); found != m_vbonus_values.end())
            {
                return found->second;
            }
        }
        else if (runtime_token.starts_with(L"ETresAbilityKind::"))
        {
            if (const auto found = m_ability_values.find(std::wstring{runtime_token}); found != m_ability_values.end())
            {
                return found->second;
            }
        }

        const auto reflected_value = try_resolve_reflected_enum_value(runtime_token);
        if (reflected_value.has_value())
        {
            if (runtime_token.starts_with(L"ETresVictoryBonusKind::"))
            {
                m_vbonus_values.emplace(std::wstring{runtime_token}, *reflected_value);
            }
            else if (runtime_token.starts_with(L"ETresAbilityKind::"))
            {
                m_ability_values.emplace(std::wstring{runtime_token}, *reflected_value);
            }
            return reflected_value;
        }
        return std::nullopt;
    }

    auto RuntimeEnumResolver::format_ability(uint64_t enum_value) -> std::wstring
    {
        ensure_initialized();
        for (const auto& [name, value] : m_ability_values)
        {
            if (value == enum_value)
            {
                return name;
            }
        }
        return {};
    }

    auto configure_runtime_enum_resolver(std::filesystem::path shared_enums_relative_path) -> void
    {
        std::scoped_lock lock{runtime_enum_resolver_mutex()};
        auto& resolver = runtime_enum_resolver_instance();
        if (!resolver)
        {
            resolver = std::make_unique<RuntimeEnumResolver>(std::move(shared_enums_relative_path));
        }
    }

    auto maybe_initialize_runtime_enum_value_maps() -> RuntimeEnumPrewarmResult
    {
        std::scoped_lock lock{runtime_enum_resolver_mutex()};
        return runtime_enum_resolver().prewarm_reflected_values();
    }

    auto try_resolve_runtime_enum_value(std::wstring_view runtime_token) -> std::optional<uint64_t>
    {
        std::scoped_lock lock{runtime_enum_resolver_mutex()};
        return runtime_enum_resolver().resolve(runtime_token);
    }

    auto try_format_runtime_ability_enum_value(uint64_t enum_value) -> std::wstring
    {
        std::scoped_lock lock{runtime_enum_resolver_mutex()};
        return runtime_enum_resolver().format_ability(enum_value);
    }

}
