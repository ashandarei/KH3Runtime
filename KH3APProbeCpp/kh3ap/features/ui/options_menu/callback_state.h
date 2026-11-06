#pragma once

#include "kh3ap/features/ui/options_menu.h"
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace kh3ap::options_menu_detail
{
    enum class CallbackKind { ItemAvailability, ChoiceAvailability, Help, ChoiceHelp, Selection, Value, Apply };

    // Definitions remain stable for the host's lifetime. Keys identify their
    // callbacks without owning the definitions or retaining gameplay objects.
    class CallbackState
    {
    public:
        using FailureReporter = std::function<void(CallbackKind, std::wstring_view, std::string_view)>;

        explicit CallbackState(FailureReporter reporter = {}) : reporter_(std::move(reporter)) {}

        template <typename Value, typename Callback>
        auto read(const void* definition, std::wstring_view label, CallbackKind kind, const Callback& callback)
            -> std::optional<Value>
        {
            const Key key{reinterpret_cast<uintptr_t>(definition), kind};
            try
            {
                auto value = callback();
                failures_.erase(key);
                return value;
            }
            catch (const std::exception& failure)
            {
                report(key, label, failure.what());
            }
            catch (...)
            {
                report(key, label, "unknown_exception");
            }
            return std::nullopt;
        }

        template <typename Callback>
        auto run(const void* definition, std::wstring_view label, CallbackKind kind, const Callback& callback) -> bool
        {
            return read<bool>(definition, label, kind, [&]
            {
                callback();
                return true;
            }).value_or(false);
        }

        auto selection(const RuntimeOptionsItemDefinition& item) -> std::optional<size_t>
        {
            if (item.choices.empty())
            {
                return std::nullopt;
            }
            return read<size_t>(&item, item.label, CallbackKind::Selection, [&]
            {
                const auto selected = item.selected_choice ? item.selected_choice() : size_t{};
                if (selected >= item.choices.size())
                {
                    throw std::out_of_range("selected_choice_out_of_range");
                }
                return selected;
            });
        }

    private:
        using Key = std::pair<uintptr_t, CallbackKind>;

        auto report(const Key& key, std::wstring_view label, std::string_view reason) noexcept -> void
        {
            // Report a failure transition once. A successful callback clears
            // it, allowing a later independent failure to be reported again.
            try
            {
                const auto previous = failures_.find(key);
                if (previous != failures_.end() && previous->second == reason)
                {
                    return;
                }
                failures_[key] = reason;
                if (reporter_)
                {
                    reporter_(key.second, label, reason);
                }
            }
            catch (...)
            {
                // Error reporting must not unwind through the viewport hook.
            }
        }

        FailureReporter reporter_;
        std::map<Key, std::string> failures_;
    };
}
