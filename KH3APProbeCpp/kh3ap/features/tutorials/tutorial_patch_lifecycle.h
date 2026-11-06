#pragma once

#include <string_view>

namespace kh3ap
{
    // A context reset starts a new function generation. Missing functions stay
    // pending; a bytecode result normally ends attempts for that generation.
    class TutorialPatchLifecycle
    {
    public:
        [[nodiscard]] auto should_attempt(bool enabled, bool matching_context) const noexcept
            -> bool
        {
            return enabled && matching_context && status_ == Status::Pending;
        }

        [[nodiscard]] auto should_attempt(bool enabled, std::wstring_view context_hint,
                                          std::wstring_view context_prefix) const noexcept -> bool
        {
            return should_attempt(enabled,
                                  context_hint.find(context_prefix) != std::wstring_view::npos);
        }

        [[nodiscard]] auto note_unavailable() noexcept -> bool
        {
            const bool first_unavailable = !unavailable_logged_;
            unavailable_logged_ = true;
            return first_unavailable;
        }

        auto record_result(bool accepted) noexcept -> void
        {
            status_ = accepted ? Status::Applied : Status::Rejected;
        }

        [[nodiscard]] auto applied() const noexcept -> bool
        {
            return status_ == Status::Applied;
        }

        auto reset() noexcept -> void
        {
            *this = {};
        }

    private:
        enum class Status
        {
            Pending,
            Applied,
            Rejected,
        };

        Status status_{Status::Pending};
        bool unavailable_logged_{false};
    };
} // namespace kh3ap
