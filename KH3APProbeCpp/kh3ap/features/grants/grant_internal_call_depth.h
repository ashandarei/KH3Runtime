#pragma once

namespace kh3ap
{
    class RuntimeGrantInternalCallDepth
    {
    public:
        class Scope
        {
        public:
            Scope();
            Scope(const Scope&) = delete;
            auto operator=(const Scope&) -> Scope& = delete;
            ~Scope();
        };

        [[nodiscard]] static auto is_active() -> bool;

    private:
        static auto enter() -> void;
        static auto leave() -> void;

        static thread_local int depth_;
    };
}
