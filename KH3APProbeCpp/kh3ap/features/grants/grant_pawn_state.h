#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimeGrantPawnState
    {
    public:
        static constexpr std::chrono::milliseconds stability_delay{2500};
        // Runtime callers capture FWeakObjectPtr here. Keeping the resolver at
        // this boundary lets the state remain independent of engine headers.
        using ControllerResolver = std::function<RC::Unreal::UObject*()>;

        auto reset() -> void;
        auto update_controller(
                ControllerResolver resolver,
                std::wstring_view name) -> void;
        auto clear_candidate() -> void;
        auto update_candidate(
                RC::Unreal::UObject* object,
                std::wstring_view name,
                std::chrono::steady_clock::time_point now,
                ControllerResolver resolver = {}) -> void;
        [[nodiscard]] auto controller_object() const -> RC::Unreal::UObject*;
        [[nodiscard]] auto controller_name() const -> const std::wstring&;
        [[nodiscard]] auto object() const -> RC::Unreal::UObject*;
        // The name is retained for diagnostics only. Candidate identity and
        // stability are based on the validated UObject pointer.
        [[nodiscard]] auto name() const -> const std::wstring&;
        // Discovery validates the candidate; runtime weak resolvers also revoke
        // it when a cinematic replaces the pawn without a map transition.
        [[nodiscard]] auto candidate_trusted() const -> bool;
        [[nodiscard]] auto first_seen_at() const -> std::chrono::steady_clock::time_point;
        [[nodiscard]] auto stable_for(std::chrono::steady_clock::time_point now) const -> std::chrono::steady_clock::duration;

    private:
        ControllerResolver controller_resolver_{};
        std::wstring controller_name_{};
        RC::Unreal::UObject* object_{nullptr};
        ControllerResolver object_resolver_{};
        std::wstring name_{};
        bool candidate_trusted_{false};
        std::chrono::steady_clock::time_point first_seen_at_{};
    };
}
