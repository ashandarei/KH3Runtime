#include "kh3ap/features/ui/options_menu.h"

#include <utility>

namespace kh3ap
{
    namespace
    {
        auto default_available(RuntimeOptionsAvailability available)
                -> RuntimeOptionsAvailability
        {
            return available ? std::move(available) : []() { return true; };
        }
    }

    RuntimeOptionsPageBuilder::RuntimeOptionsPageBuilder(
            std::vector<RuntimeOptionsItemDefinition>& items)
        : items_(items)
    {
    }

    auto RuntimeOptionsPageBuilder::toggle(
            std::wstring id,
            std::wstring label,
            std::function<bool()> read,
            std::function<void(bool)> write,
            RuntimeOptionsAvailability available,
            RuntimeOptionsHelpText help_text,
            RuntimeOptionsHelpText choice_help_text)
            -> RuntimeOptionsPageBuilder&
    {
        return choice(
                std::move(id),
                std::move(label),
                {{L"on", L"ON"}, {L"off", L"OFF"}},
                [read = std::move(read)]() {
                    return read && read() ? std::size_t{0} : std::size_t{1};
                },
                [write = std::move(write)](std::size_t selected) {
                    if (write)
                    {
                        write(selected == 0);
                    }
                },
                std::move(available),
                std::move(help_text),
                std::move(choice_help_text));
    }

    auto RuntimeOptionsPageBuilder::choice(
            std::wstring id,
            std::wstring label,
            std::vector<RuntimeOptionsChoice> choices,
            RuntimeOptionsSelectionReader selected_choice,
            RuntimeOptionsSelectionWriter apply_choice,
            RuntimeOptionsAvailability available,
            RuntimeOptionsHelpText help_text,
            RuntimeOptionsHelpText choice_help_text)
            -> RuntimeOptionsPageBuilder&
    {
        items_.push_back(RuntimeOptionsItemDefinition{
                .kind = RuntimeOptionsItemKind::choice,
                .id = std::move(id),
                .label = std::move(label),
                .choices = std::move(choices),
                .selected_choice = std::move(selected_choice),
                .apply_choice = std::move(apply_choice),
                .available = default_available(std::move(available)),
                .help_text = std::move(help_text),
                .choice_help_text = std::move(choice_help_text),
        });
        return *this;
    }

    auto RuntimeOptionsPageBuilder::submenu(
            std::wstring id,
            std::wstring label,
            std::function<void(RuntimeOptionsPageBuilder&)> scaffold,
            RuntimeOptionsAvailability available,
            RuntimeOptionsHelpText help_text)
            -> RuntimeOptionsPageBuilder&
    {
        RuntimeOptionsItemDefinition submenu{
                .kind = RuntimeOptionsItemKind::submenu,
                .id = std::move(id),
                .label = std::move(label),
                .available = default_available(std::move(available)),
                .help_text = std::move(help_text),
        };
        RuntimeOptionsPageBuilder child_page{submenu.children};
        if (scaffold)
        {
            scaffold(child_page);
        }
        items_.push_back(std::move(submenu));
        return *this;
    }

    auto RuntimeOptionsPageBuilder::information(
            std::wstring id,
            std::wstring label,
            std::wstring value,
            RuntimeOptionsHelpText help_text,
            RuntimeOptionsHelpText value_text)
            -> RuntimeOptionsPageBuilder&
    {
        items_.push_back(RuntimeOptionsItemDefinition{
                .kind = RuntimeOptionsItemKind::information,
                .id = std::move(id),
                .label = std::move(label),
                .value = std::move(value),
                .available = default_available({}),
                .help_text = std::move(help_text),
                .value_text = std::move(value_text),
        });
        return *this;
    }

    RuntimeOptionsMenuBuilder::RuntimeOptionsMenuBuilder(
            std::wstring id,
            std::wstring label,
            std::wstring help_text)
        : definition_{
                  .id = std::move(id),
                  .label = std::move(label),
                  .help_text = std::move(help_text),
          }
    {
    }

    auto RuntimeOptionsMenuBuilder::page() -> RuntimeOptionsPageBuilder
    {
        return RuntimeOptionsPageBuilder{definition_.items};
    }

    auto RuntimeOptionsMenuBuilder::on_host_ready(std::function<void()> callback)
            -> RuntimeOptionsMenuBuilder&
    {
        definition_.on_host_ready = std::move(callback);
        return *this;
    }

    auto RuntimeOptionsMenuBuilder::build() && -> RuntimeOptionsMenuDefinition
    {
        return std::move(definition_);
    }
}
