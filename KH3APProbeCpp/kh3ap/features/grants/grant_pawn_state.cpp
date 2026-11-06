#include "kh3ap/features/grants/grant_pawn_state.h"

#include <utility>

namespace kh3ap
{
    auto RuntimeGrantPawnState::reset() -> void
    {
        controller_resolver_ = {};
        controller_name_.clear();
        object_ = nullptr;
        object_resolver_ = {};
        name_.clear();
        candidate_trusted_ = false;
        first_seen_at_ = {};
    }

    auto RuntimeGrantPawnState::update_controller(
            ControllerResolver resolver,
            std::wstring_view name) -> void
    {
        controller_resolver_ = std::move(resolver);
        controller_name_ = std::wstring{name};
    }

    auto RuntimeGrantPawnState::clear_candidate() -> void
    {
        object_ = nullptr;
        object_resolver_ = {};
        name_.clear();
        candidate_trusted_ = false;
        first_seen_at_ = {};
    }

    auto RuntimeGrantPawnState::update_candidate(
            RC::Unreal::UObject* object,
            std::wstring_view name,
            std::chrono::steady_clock::time_point now,
            ControllerResolver resolver) -> void
    {
        candidate_trusted_ = object != nullptr;
        const bool same_object = object == this->object();
        object_resolver_ = std::move(resolver);
        if (same_object)
        {
            // The formatted name is diagnostic only. Its text can change as
            // additional UObject details become available, but that must not
            // turn the same validated pawn into a new candidate or restart
            // its stability timer.
            name_ = std::wstring{name};
            return;
        }

        // A replacement pawn cannot inherit the previous character's
        // controller. The next possession/state event supplies the new pair.
        controller_resolver_ = {};
        controller_name_.clear();
        object_ = object;
        name_ = std::wstring{name};
        first_seen_at_ = now;
    }

    auto RuntimeGrantPawnState::controller_object() const -> RC::Unreal::UObject*
    {
        return controller_resolver_ ? controller_resolver_() : nullptr;
    }

    auto RuntimeGrantPawnState::controller_name() const -> const std::wstring&
    {
        return controller_name_;
    }

    auto RuntimeGrantPawnState::object() const -> RC::Unreal::UObject*
    {
        return object_resolver_ ? object_resolver_() : object_;
    }

    auto RuntimeGrantPawnState::name() const -> const std::wstring&
    {
        return name_;
    }

    auto RuntimeGrantPawnState::candidate_trusted() const -> bool
    {
        return candidate_trusted_;
    }

    auto RuntimeGrantPawnState::first_seen_at() const -> std::chrono::steady_clock::time_point
    {
        return first_seen_at_;
    }

    auto RuntimeGrantPawnState::stable_for(std::chrono::steady_clock::time_point now) const
            -> std::chrono::steady_clock::duration
    {
        return first_seen_at_.time_since_epoch().count() == 0 ? std::chrono::steady_clock::duration{} : now - first_seen_at_;
    }
}
