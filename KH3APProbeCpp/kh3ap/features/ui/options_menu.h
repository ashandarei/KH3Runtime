#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace kh3ap
{
    using RuntimeOptionsAvailability = std::function<bool()>;
    using RuntimeOptionsHelpText = std::function<std::wstring()>;
    using RuntimeOptionsSelectionReader = std::function<std::size_t()>;
    using RuntimeOptionsSelectionWriter = std::function<void(std::size_t)>;

    struct RuntimeOptionsChoice
    {
        std::wstring id{};
        std::wstring label{};
        RuntimeOptionsAvailability available{};
    };

    enum class RuntimeOptionsItemKind
    {
        choice,
        submenu,
        information,
    };

    struct RuntimeOptionsItemDefinition
    {
        RuntimeOptionsItemKind kind{RuntimeOptionsItemKind::choice};
        std::wstring id{};
        std::wstring label{};
        std::wstring value{};
        std::vector<RuntimeOptionsChoice> choices{};
        std::vector<RuntimeOptionsItemDefinition> children{};
        RuntimeOptionsSelectionReader selected_choice{};
        RuntimeOptionsSelectionWriter apply_choice{};
        RuntimeOptionsAvailability available{};
        RuntimeOptionsHelpText help_text{};
        RuntimeOptionsHelpText choice_help_text{};
        RuntimeOptionsHelpText value_text{};
    };

    struct RuntimeOptionsMenuDefinition
    {
        std::wstring id{};
        std::wstring label{};
        std::wstring help_text{};
        std::vector<RuntimeOptionsItemDefinition> items{};
        std::function<void()> on_host_ready{};
    };

    // Builds one page of options. A submenu receives another page builder, so
    // arbitrary nesting does not require changes to the Scaleform host.
    class RuntimeOptionsPageBuilder
    {
    public:
        explicit RuntimeOptionsPageBuilder(
                std::vector<RuntimeOptionsItemDefinition>& items);

        auto toggle(
                std::wstring id,
                std::wstring label,
                std::function<bool()> read,
                std::function<void(bool)> write,
                RuntimeOptionsAvailability available = {},
                RuntimeOptionsHelpText help_text = {},
                RuntimeOptionsHelpText choice_help_text = {})
                -> RuntimeOptionsPageBuilder&;

        auto choice(
                std::wstring id,
                std::wstring label,
                std::vector<RuntimeOptionsChoice> choices,
                RuntimeOptionsSelectionReader selected_choice,
                RuntimeOptionsSelectionWriter apply_choice,
                RuntimeOptionsAvailability available = {},
                RuntimeOptionsHelpText help_text = {},
                RuntimeOptionsHelpText choice_help_text = {})
                -> RuntimeOptionsPageBuilder&;

        auto submenu(
                std::wstring id,
                std::wstring label,
                std::function<void(RuntimeOptionsPageBuilder&)> scaffold,
                RuntimeOptionsAvailability available = {},
                RuntimeOptionsHelpText help_text = {})
                -> RuntimeOptionsPageBuilder&;

        // Adds a display-only row. This is useful for credits and other
        // informational pages that should retain stock focus/help behavior
        // without presenting an editable choice.
        auto information(
                std::wstring id,
                std::wstring label,
                std::wstring value,
                RuntimeOptionsHelpText help_text = {},
                RuntimeOptionsHelpText value_text = {})
                -> RuntimeOptionsPageBuilder&;

    private:
        std::vector<RuntimeOptionsItemDefinition>& items_;
    };

    // Typical use:
    //   RuntimeOptionsMenuBuilder menu{L"my_mod", L"My Options", L"..."};
    //   menu.page().toggle(...).submenu(...);
    //   return std::move(menu).build();
    class RuntimeOptionsMenuBuilder
    {
    public:
        RuntimeOptionsMenuBuilder(
                std::wstring id,
                std::wstring label,
                std::wstring help_text);

        [[nodiscard]] auto page() -> RuntimeOptionsPageBuilder;
        auto on_host_ready(std::function<void()> callback)
                -> RuntimeOptionsMenuBuilder&;
        [[nodiscard]] auto build() && -> RuntimeOptionsMenuDefinition;

    private:
        RuntimeOptionsMenuDefinition definition_{};
    };
}
